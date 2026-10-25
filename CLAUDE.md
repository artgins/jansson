# CLAUDE.md

Guidance for Claude Code (claude.ai/code) when working in this repo.

## ⚠️ First step: read the yunetas CLAUDE.md

**Before doing anything in this repo, read the yunetas SDK's `CLAUDE.md`.**
It lives at `/yuneta/development/yunetas/CLAUDE.md` (standalone clone:
`github.com/artgins/yunetas`, `CLAUDE.md` at the root). It carries the
framework-wide rules that also govern this codebase: a consolidated project
(read in depth, find the invariants, weigh the side effects, justify the
change), no silent errors, and the release and deploy conventions. This
file only adds the jansson-specific layer on top.

## This repo in the yunetas ecosystem

- **jansson is Yuneta's JSON library.** Every json of Yuneta goes through
  it: every message between yunos, every record of timeranger2, every node
  of a treedb, every configuration. It is linked statically into every
  yuno, from `kernel/c/linux-ext-libs` of yunetas.
- **It is upstream jansson (`akheron/jansson`) plus a series of Yuneta's
  commits.** The repo has ONE branch, `master`: the upstream release it
  sits on (tag `v2.15.1` today) and the series on top of it, one commit per
  logical change, linear. `master` is REWRITTEN (force-pushed) by every
  rebase onto a new upstream release and by every fixup, so nobody builds
  from it: each state yunetas pins is an annotated tag
  `<version>-yuneta.<n>` (`2.15.1-yuneta.1`), which keeps that state for
  good, and `TAG_JANSSON` in yunetas' `repos2clone.sh` names it. The series
  of a release is `v<version>..<version>-yuneta.<n>`.
- **Upstream is followed.** On each upstream release the series is rebased
  onto it:

  ```bash
  git fetch https://github.com/akheron/jansson.git tag v2.16.0
  git checkout master
  git rebase --onto v2.16.0 v2.15.1
  git push --force-with-lease origin master
  ```

  then validated (below) and tagged `2.16.0-yuneta.1`. A commit of the
  series that upstream now does is dropped in the rebase. Security fixes of
  upstream are always brought in: if one lands between releases, it is
  cherry-picked onto `master` and a new `-yuneta.<n>` is tagged.
  A fix of the series that upstream would want is offered to upstream.
- **The public API does not change.** `src/jansson.h`, contract in
  `doc/apiref.rst`: no function is added or removed, and none changes its
  contract. What changes is inside. A change that a caller can see goes in
  the commit message and in the yunetas `CHANGELOG.md`: an answer, an error,
  what a custom allocator sees.
- **Style: upstream's code stays as upstream wrote it.** It is never
  reformatted: a reformat would make every rebase conflict on every line it
  touched. The code the series adds or changes braces every block, Yuneta's
  rule. To brace only the lines the series changed:

  ```bash
  git clang-format --style="{BasedOnStyle: LLVM, AlignConsecutiveMacros: true, \
      ColumnLimit: 90, IndentCaseLabels: true, IndentWidth: 4, InsertBraces: true}" \
      v2.15.1
  ```

  `.clang-format` stays upstream's, so `scripts/clang-format-check` keeps
  checking the rest as upstream does. `src/dtoa.c` is David Gay's, vendored
  (`// clang-format off`).
- **`JANSSON_THREAD_SAFE_REFCOUNT=OFF` in Yuneta.** `configure-libs.sh`
  builds jansson with the option OFF: the references are counted with `++`
  and `--`, without atomic instructions, because Yuneta has no threads (one
  yuno per core; a `fork()` is another process). The count is done by
  `json_incref()` / `json_decref()`, which are inline in `jansson.h`, so it
  is decided by the `jansson_config.h` installed in `outputs_ext/include`,
  for the SDK and for every project built against it, and `jansson.h` does
  not define `JANSSON_THREAD_SAFE_REFCOUNT`. If a yuno or a project ever
  shares a json value between threads, it needs the option ON (the default
  of this repo) and a rebuild of the SDK and of every binary; with it OFF,
  such sharing corrupts the counts silently.
- The checkout under `kernel/c/linux-ext-libs/build/jansson` in yunetas is a
  build copy at the pinned tag, not a place to develop.

## Validate a change

Every change, before its commit:

1. `cmake -S . -B build && cmake --build build && (cd build && ctest)`:
   jansson's own tests. Also with `-DJANSSON_THREAD_SAFE_REFCOUNT=OFF`.
2. **`test/differential/run.sh BASE`**, where `BASE` is the tag yunetas
   pins now (`TAG_JANSSON`). The working tree must answer exactly as
   `BASE`: the same results, the same errors with their line, column and
   position, and the same bytes of every dump. It also runs everything
   under AddressSanitizer and UndefinedBehaviorSanitizer. Add `-a` when the
   change must not move a single allocation. When the change reaches code
   the differential programs do not cover, add the case first (see
   `test/differential/README.md`). After a rebase, run it against the
   upstream release too: the differences must be only the series' own
   fixes.
3. For a change made for speed: measure it in Yuneta, with `callgrind` on
   the benchmarks of `performance/c/` (`perf_ievent`,
   `test_topic_pkey_integer`, ...) and an alternated A/B, as
   `performance/audit/` does, and the live memory of a large treedb when
   the change moves allocations. A change that is not faster does not go
   in.

## Ship a change

1. Commit on `master` (a fixup of the commit it belongs to, when it
   corrects one, and then `master` is force-pushed), in English: what
   changes, why, how it was checked, and what a caller can see. Tag
   `<version>-yuneta.<n>` (annotated, never moved) and push `master` and
   the tag.
2. In yunetas, `kernel/c/linux-ext-libs/`:
   - `repos2clone.sh`: `TAG_JANSSON` moves to the new tag;
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
changes made for speed. It is not touched for now (the user, 2026-10-24).
