# The differential test

jansson is a consolidated library and its API is fixed: a change inside it
must leave every answer as it was. The results, the errors with their line,
column and position, and the bytes of every dump must not move. This
directory checks that by comparison. It builds a commit you name (the one
before the change) and the working tree, runs the same programs on both, and
requires the same output.

```bash
test/differential/run.sh BASE        # e.g. the commit yunetas pins now
test/differential/run.sh -a BASE     # also what the allocator sees
```

`BASE` is normally the commit that yunetas pins now (`TAG_JANSSON` in
`kernel/c/linux-ext-libs/repos2clone.sh`). `run.sh` exits 0 when the working
tree answers as `BASE`, and 1 when anything differs or fails. With `-k` it
keeps its work directory (the builds, and the output of every program). The
other options are in the script's header.

## What it runs

1. **Three builds**: `BASE`, the working tree, and the working tree under
   AddressSanitizer and UndefinedBehaviorSanitizer.
2. **jansson's own tests** (`test/`) on both builds of the working tree.
3. **The programs of this directory** on the three builds. The outputs of
   `BASE` and of the working tree must be the same. The sanitized run must
   give that same output, with no report.

| Program | What it covers |
|---|---|
| `fuzz.c` | Random documents, mutated. Each one is loaded by `json_loadb()` and `json_loads()` under five sets of flags, and each value is dumped under five sets of flags. The first bytes of each document go through the UTF-8 check. It runs 11 seeds of 20 000 documents, and five more under a capped allocator in the sanitized build. |
| `strings.c` | A special byte at each place, and two at every pair of places. Values across the size up to which a value shares the block of its string. Random strings checked as UTF-8. |
| `lexer.c` | Integers followed by each of the 256 bytes. Strings with a special byte at each place. Tokens across the growths of the lexer's saved text. |
| `objects.c` | Objects of 200 000 keys (insert, replace, look up, delete, walk, dump with sorted keys). Nested objects with long, escaped and repeated keys. |
| `arrays.c` | Arrays built by random operations across the growths of their table. |
| `walks.c` | The loop check of a dump, a deep copy and a recursive update. Chains of 1 to 2 100 containers, with cycles on both sides of 32 and 2048 levels, and shared containers. |
| `dumps.c` | Every way out of a dump: `json_dumps()`, `json_dump_callback()`, `json_dumpb()`, `json_dumpf()`, `json_dumpfd()`, and dumps that fail in the middle. It also checks the rule of a callback that fails (`--check-callback-rule`). |

Every program prints one line per case with `-v`. Without it, a program
prints only a digest of its lines. When `run.sh` reports a difference, run
the program with `-v` against both builds (in the work directory kept by
`-k`) and compare the two outputs:

```bash
WORK=...                                  # printed by run.sh -k
$WORK/lexer.base -v > base.txt
$WORK/lexer.new -v > new.txt
diff base.txt new.txt | head
```

## What it does not require

- **What a custom allocator sees** (`json_set_alloc_funcs()`). The fuzz
  prints it on lines that start with `alloc`, and `run.sh` leaves those lines
  out unless you pass `-a`. A change that makes fewer allocations, or
  allocations of other sizes, is allowed. Under an allocator that fails, the
  failure can come at another place, so the capped fuzz only checks that
  nothing breaks: no sanitizer report and no leak.
- **The size of the chunks a dump callback gets.** jansson promises only that
  each chunk is whole UTF-8 sequences with no NUL. `dumps.c` checks exactly
  that.

## Adding a case

A change that reaches code these programs do not cover gets a case here
before the change is made. Write the case so that it passes against `BASE`,
then make the change. A program here is plain C on the public API
(`jansson.h`) and `common.h`. The output of a case goes through
`out_text()`, `out_bytes()`, `out_dump()` and `out_error()`, and `out_end()`
ends each case.

The sanitizers need GCC or clang. The library they test does not.
