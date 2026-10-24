#!/bin/bash
#
#   Copyright (c) 2026, ArtGins.
#
#   Jansson is free software; you can redistribute it and/or modify
#   it under the terms of the MIT license. See LICENSE for details.
#

#
#   The differential test of jansson: the working tree against a commit
#
#   usage: test/differential/run.sh [options] BASE
#
#   BASE is a commit of this repo (a hash, a tag, HEAD~1): the one the
#   working tree must answer exactly as. Normally the one yunetas pins now
#   (TAG_JANSSON in kernel/c/linux-ext-libs/repos2clone.sh).
#
#   options:
#       -n ITERATIONS   documents per seed of the fuzz (default 20000)
#       -s SEEDS        seeds of the fuzz (default 11)
#       -a              compare what the allocator sees too (the "alloc"
#                       lines of the fuzz): for a change that must not
#                       change a single allocation
#       -k              keep the work directory
#       -j JOBS         jobs of the builds (default: the number of CPUs)
#
#   It builds BASE and the working tree, the working tree also under
#   AddressSanitizer and UndefinedBehaviorSanitizer, runs jansson's own
#   tests on both builds of the working tree, and runs every program of
#   this directory on the three builds: the outputs of BASE and of the
#   working tree must be the same, and the sanitized run must give the
#   same output with no report. The fuzz also runs under a capped
#   allocator in the sanitized build (allocations refused: no error, no
#   leak). Exit 0 when all of it holds, 1 when anything differs or fails.
#
#   The sanitizers need GCC or clang; the library they test does not.
#

set -u

ITERATIONS=20000
SEEDS=11
WITH_ALLOCATIONS=0
KEEP=0
JOBS=$(nproc 2>/dev/null || echo 4)

while getopts "n:s:akj:" opt; do
    case $opt in
        n) ITERATIONS=$OPTARG ;;
        s) SEEDS=$OPTARG ;;
        a) WITH_ALLOCATIONS=1 ;;
        k) KEEP=1 ;;
        j) JOBS=$OPTARG ;;
        *) sed -n '/^#   usage/,/^#   The sanitizers/p' "$0"; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
if [ $# -ne 1 ]; then
    sed -n '/^#   usage/,/^#   The sanitizers/p' "$0"
    exit 2
fi
BASE=$1

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(git -C "$HERE" rev-parse --show-toplevel) || exit 2
BASE_SHA=$(git -C "$ROOT" rev-parse --verify "$BASE^{commit}") || exit 2
WORK=$(mktemp -d "${TMPDIR:-/tmp}/jansson-differential.XXXXXX")
FAILED=0
SANITIZE="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all"
PROGRAMS="strings lexer objects arrays walks dumps"

cleanup() {
    if [ $KEEP -eq 0 ]; then
        rm -rf "$WORK"
    else
        echo "work directory kept: $WORK"
    fi
}
trap cleanup EXIT

fail() {
    echo "FAILED: $*"
    FAILED=1
}

#
#   Builds
#
build() { # name source cflags tests
    local name=$1 src=$2 cflags=$3 tests=$4
    local without_tests=ON
    if [ "$tests" = "tests" ]; then
        without_tests=OFF
    fi
    echo "build $name"
    if ! cmake -S "$src" -B "$WORK/$name" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF \
        -DJANSSON_BUILD_DOCS=OFF \
        -DJANSSON_EXAMPLES=OFF \
        -DJANSSON_WITHOUT_TESTS=$without_tests \
        -DCMAKE_C_FLAGS="$cflags" > "$WORK/$name.cmake.log" 2>&1; then
        cat "$WORK/$name.cmake.log"
        echo "the build of $name could not be configured"
        exit 1
    fi
    if ! cmake --build "$WORK/$name" -j "$JOBS" > "$WORK/$name.build.log" 2>&1; then
        tail -30 "$WORK/$name.build.log"
        echo "the build of $name failed"
        exit 1
    fi
}

mkdir -p "$WORK/base-src"
git -C "$ROOT" archive "$BASE_SHA" | tar -x -C "$WORK/base-src" || exit 1
echo "base:         $BASE ($BASE_SHA)"
echo "working tree: $ROOT ($(git -C "$ROOT" rev-parse --short HEAD)$(git -C "$ROOT" diff --quiet HEAD -- . || echo ', with changes'))"

build base "$WORK/base-src" "" no-tests
build new "$ROOT" "" tests
build sanitized "$ROOT" "$SANITIZE" tests

#
#   jansson's own tests
#
for name in new sanitized; do
    echo "jansson's tests ($name)"
    if ! (cd "$WORK/$name" && ctest -j "$JOBS" > "$WORK/$name.ctest.log" 2>&1); then
        tail -20 "$WORK/$name.ctest.log"
        fail "jansson's tests, $name build"
    fi
done

#
#   The programs, built against each build
#
for name in base new sanitized; do
    flags="-O2"
    if [ "$name" = "sanitized" ]; then
        flags="$SANITIZE"
    fi
    for p in fuzz $PROGRAMS; do
        if ! cc $flags -I"$WORK/$name/include" -o "$WORK/$p.$name" "$HERE/$p.c" \
            "$WORK/$name/lib/libjansson.a" 2> "$WORK/$p.$name.cc.log"; then
            cat "$WORK/$p.$name.cc.log"
            echo "$p could not be built against $name"
            exit 1
        fi
    done
done

compare() { # label file-base file-new
    if ! cmp -s "$2" "$3"; then
        fail "$1: the working tree answers otherwise than $BASE"
        diff "$2" "$3" | head -8
    fi
}

run_sanitized() { # label output command...
    local label=$1 output=$2
    shift 2
    if ! "$@" > "$output" 2> "$output.stderr"; then
        fail "$label: the sanitized build failed"
        tail -20 "$output.stderr"
        return 1
    fi
    if grep -q "runtime error\|ERROR: AddressSanitizer\|ERROR: LeakSanitizer" "$output.stderr"; then
        fail "$label: the sanitizers reported"
        grep -m 5 "runtime error\|ERROR:" "$output.stderr"
        return 1
    fi
    return 0
}

for p in $PROGRAMS; do
    echo "program $p"
    extra=""
    "$WORK/$p.base" > "$WORK/$p.out.base" 2> "$WORK/$p.err.base"
    if [ "$p" = "dumps" ]; then
        extra="--check-callback-rule"
    fi
    if ! "$WORK/$p.new" $extra > "$WORK/$p.out.new" 2> "$WORK/$p.err.new"; then
        fail "$p: $(cat "$WORK/$p.err.new")"
    fi
    compare "$p" "$WORK/$p.out.base" "$WORK/$p.out.new"
    if run_sanitized "$p" "$WORK/$p.out.sanitized" "$WORK/$p.sanitized" $extra; then
        compare "$p (sanitized)" "$WORK/$p.out.base" "$WORK/$p.out.sanitized"
    fi
done

filter() { # the fuzz's output, without what the allocator sees unless -a
    if [ $WITH_ALLOCATIONS -eq 1 ]; then
        cat
    else
        grep -v '^alloc '
    fi
}

echo "fuzz: $SEEDS seeds of $ITERATIONS documents"
for seed in $(seq 1 "$SEEDS"); do
    "$WORK/fuzz.base" "$seed" "$ITERATIONS" | filter > "$WORK/fuzz.$seed.base"
    "$WORK/fuzz.new" "$seed" "$ITERATIONS" | filter > "$WORK/fuzz.$seed.new"
    compare "fuzz seed $seed" "$WORK/fuzz.$seed.base" "$WORK/fuzz.$seed.new"
    rm -f "$WORK/fuzz.$seed.base" "$WORK/fuzz.$seed.new"
done

echo "fuzz under a capped allocator (sanitized)"
for seed in 1 4 5 7 11; do
    cap=$(( (seed * 197) % 2048 + 5 ))
    run_sanitized "fuzz seed $seed cap $cap" "$WORK/fuzz.capped" \
        "$WORK/fuzz.sanitized" "$seed" $((ITERATIONS / 4)) "$cap"
done

if [ $FAILED -eq 0 ]; then
    echo "OK: the working tree answers as $BASE"
    exit 0
fi
echo "the working tree does NOT answer as $BASE"
exit 1
