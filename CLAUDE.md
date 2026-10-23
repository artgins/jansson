# CLAUDE.md

Guidance for Claude Code (claude.ai/code) when working in this repo.

## ⚠️ First step: read the yunetas CLAUDE.md

**Before doing anything in this repo, read the yunetas SDK's `CLAUDE.md`.**
It lives at `/yuneta/development/yunetas/CLAUDE.md` (standalone clone:
`github.com/artgins/yunetas`, `CLAUDE.md` at the root). It carries the
framework-wide rules that also govern this codebase: a consolidated project
(read in depth, find the invariants, weigh the side effects, justify the
change), standard C only, no silent errors, and the release and deploy
conventions. This file only adds the jansson-specific layer on top.

## This repo in the yunetas ecosystem

- **jansson is Yuneta's JSON library**, first class in the SDK like
  `gobj-c`. Every json of Yuneta goes through it: every message between
  yunos, every record of timeranger2, every node of a treedb, every
  configuration. It began as a fork of `akheron/jansson` v2.15.1, and its
  history is kept here (upstream's commits, then Yuneta's). Upstream is no
  longer followed, and nothing here waits for its approval.
- **Consolidated code, and its API is the right one.** The public API
  (`src/jansson.h`, contract in `doc/apiref.rst`) is just what is needed: no
  function is added or removed, and none changes its contract. What changes
  is inside. A change that a caller can see goes in the commit message and
  in the yunetas `CHANGELOG.md`: an answer, an error, what a custom allocator
  sees. A change of documented behaviour is proposed before it is made.
- **Standard C only.** Nothing may depend on the compiler or the processor,
  unless there is a major reason, said in a comment. That rules out
  builtins, `__attribute__`, `__int128`, intrinsics, assumptions about byte
  order or alignment, and an `#ifdef` on a compiler, a CPU or a C library
  for speed. A word read with `memcpy()` and tested in a way that does not
  depend on byte order is standard C (see `jsonp_plain_ascii8()` in
  `src/jansson_private.h`). The portability layer upstream wrote stays: the
  atomics of `hashtable_seed.c`, the Windows paths, and `JANSSON_ATTRS` for
  warnings.
- **Style: the repo's `.clang-format`, which braces every block** (Yuneta's
  always-braces rule, `InsertBraces: true`, since 2026-10-23: one
  mechanical commit that left the library byte-identical). Run
  `scripts/clang-format` after a change; `scripts/clang-format-check` must
  pass. `src/dtoa.c` is David Gay's, vendored, and stays as he wrote it
  (`// clang-format off`).
- **Single line on `master`.** Work in a clone of
  `github.com/artgins/jansson`. The checkout under
  `kernel/c/linux-ext-libs/build/jansson` in yunetas is a build copy at the
  pinned commit, not a place to develop.

## Validate a change

Every change, before its commit:

1. `cmake -S . -B build && cmake --build build && (cd build && ctest)`:
   jansson's own 215 tests.
2. **`test/differential/run.sh BASE`**, where `BASE` is the commit yunetas
   pins now (`TAG_JANSSON`). The working tree must answer exactly as
   `BASE`: the same results, the same errors with their line, column and
   position, and the same bytes of every dump. It also runs everything
   under AddressSanitizer and UndefinedBehaviorSanitizer. Add `-a` when the
   change must not move a single allocation. When the change reaches code
   the differential programs do not cover, add the case first (see
   `test/differential/README.md`).
3. For a change made for speed: measure it in Yuneta, with `callgrind` on
   the benchmarks of `performance/c/` (`perf_ievent`,
   `test_topic_pkey_integer`, ...) and an alternated A/B, as
   `performance/audit/` does. A change that is not faster does not go in.

## Ship a change

1. Commit on `master`. The message is in English and says what changes,
   why, how it was checked, and what a caller can see. Then push.
2. In yunetas, `kernel/c/linux-ext-libs/`:
   - `repos2clone.sh`: `TAG_JANSSON` moves to the new commit;
   - `configure-libs.sh`: bump `VERSION`, with its entry in the header (the
     `yunetas` CLI uses it to tell every node that builds from source to
     rerun `./extrae.sh && ./configure-libs.sh`);
   - `HACKS.md` (the jansson sections) and `README.md`;
   - and a line in the yunetas `CHANGELOG.md`.
3. Rebuild the SDK and every yuno. jansson is a static library, linked into
   every binary.

The ESP32 port has its own copy, in
`kernel/c/root-esp32/components/esp_jansson/jansson` of yunetas. It is
upstream 2.14 with the lexer fix applied by hand, and has none of the
changes made for speed. Whether that copy should become this library is
open.
