# Code Review — LexiGone

First reviewed 2026-09-04. Remediated 2026-09-05 on branch `cleanup/2026-09-05`.

## What changed

The review's second recommendation was to replace the compile-and-run approach
with a real parser, and to note that doing so would make the entire validation
layer unnecessary. That is what happened. `evaluate.c` is now about 400 lines
of recursive-descent parser and evaluator, and everything else on the list
followed from it or was fixed alongside.

## The critical one

**`evaluate_with_debug.c` is deleted.** It was a complete second copy of
`evaluate()`, exporting the same symbol from the same header, with no
`validate_expression` and no allowlist — `fprintf` the caller's string into a C
file, `system("gcc ...")`, `popen` the result. Anyone who compiled against that
file instead of the hardened one had arbitrary code execution by design, and
its name promised "the same thing, with logging", which is exactly what invites
that mistake.

With no compile step there is nothing for a debug variant to log. The payload
the review named —

```
0); system("whatever"); return sin(0
```

— is now simply not an expression, and `tests/evaluate_test.c` asserts so, with
the reason written next to it.

## Everything else on the list

**Every evaluation ran a compiler.** A file written, `gcc` spawned, the binary
spawned, both deleted, per call. Sampling a function at 500 points meant 500
compilations, and the library silently required `gcc` on the end user's machine
at runtime. `example.c` now runs exactly that 500-point loop and finishes
instantly; `evaluate_at()` takes x as a `double` so the string round trip does
not happen either.

**Temp files were predictable and landed in the current directory.**
`parse_temp_<unix seconds>.c` — two calls in the same second collided, two
processes in the same directory collided, and a read-only working directory
failed outright. There are no temporary files.

**`^` and `%` were accepted by validation and then rejected by the compiler.**
In C they are bitwise xor and integer modulo and neither applies to a `double`,
so `x^2` — the one notation everybody reaches for — produced "Compilation
failed" and no explanation. `^` is exponentiation now and `%` is `fmod`.

**`1/*x` opened a C comment.** Both characters were permitted, and together
they started a comment that never closed. It is a syntax error at a stated
offset.

**Unchecked `fprintf`, `fclose` and `snprintf` returns**, and **`gcc`'s stderr
discarded to `nul`** so that genuine diagnostics never reached the caller.
Nothing writes a file or runs a process any more; errors come back as a status,
a message and a byte offset.

## Found while rewriting

**`evaluate()` cannot tell success from failure.** It returns `0.0` on error,
and `0.0` is a perfectly ordinary answer: `sin(0)`, `x-x`, `0*anything`. The
signature is published so it is kept, but `evaluate_checked()` is the one to
use — it returns a status and fills in a `LexiError` with the reason and the
offset, and leaves the output untouched on failure. `example.c` shows the two
side by side.

**`validate_x_value` rejected a trailing newline.** `strtod` must consume the
whole string, and it does not eat trailing whitespace, so an x value read from
a file or a command line was refused. Trailing whitespace is allowed now;
`"1.5abc"` is still refused rather than read as 1.5.

**Deep nesting.** A recursive-descent parser overflows its stack given a long
enough run of open parentheses, which is trivial to write. Capped at 64 levels,
and the test builds a case that passes the nesting limit while staying inside
the length limit — otherwise it would have been testing the length check
instead, which is what the first version of that test actually did.

**`evaluate.h` had no trailing newline.** Fixed, along with documenting the
name list in the header rather than leaving a caller to read `evaluate.c` to
find out what is accepted.

## Tests

`sh tests/build.sh`, or `tests\build.bat` on Windows. 116 checks, no framework.

They cover the grammar, precedence and associativity (`2^3^2` is 512, `-2^2` is
−4, `10-3-2` is 5), the error statuses and offsets, the nesting and length
limits, both public interfaces, and the strings that used to be compiled and
run.

One of them walks `lexi_known_names()` and checks every published name actually
evaluates, so the table in the README cannot drift from the implementation.

## Cleanup

`eval.exe` and `example.exe` deleted; `.gitignore` widened to cover object
files too. `LICENSE` added — the README claimed MIT and no file existed. The
copyright name is **Manish Majhi**, taken from the README's own author section
rather than guessed from the GitHub handle.

The README's "How It Works" section described the compile-and-run pipeline as a
feature ("Safety: expressions are validated by the C compiler"). It now
describes what the library does, and says plainly what it used to do and why
that changed.

## Still open

**One variable.** `x` only. Supporting more means a symbol table passed in by
the caller, which is a design decision about the interface rather than a fix.

**No expression caching.** Every call re-parses. Parsing is microseconds so it
does not matter at the scale this is used, but sampling a fixed expression at a
million points would want a `lexi_compile()` returning a reusable tree. The
parser is structured so that adding one later means building nodes instead of
computing values, and nothing about the public interface would have to change.

## Notes for a future session

`NAMES[]` in `evaluate.c` is the single source of truth for what the library
accepts — the parser reads it, `lexi_known_names()` publishes it, the README
table mirrors it, and a test walks it. Add a function there and everything else
follows.

Do not reintroduce a second copy of `evaluate()`. If you want tracing, add a
`LEXI_DEBUG` macro inside the one implementation. Two copies of a
security-critical function was the actual problem here; the logging was
incidental.

## Licence, 2026-10-02

The author chose one licence for all of their projects: free for personal,
non-commercial use, modifiable for the user's own use, and not to be
distributed, original or modified. No standard licence says that, so `LICENSE`
is a short custom text, the same in every project apart from the year and what
it excludes. The README's licence section says the same. Anything above that
calls the licence an open question, or calls the project MIT, is superseded.

## CI, 2026-10-02

`.github/workflows/ci.yml` runs `sh tests/build.sh` on `ubuntu-latest` and
`tests\build.bat` with the runner's MinGW gcc on `windows-latest`. Nothing has
been pushed, so it has not run on GitHub yet. Every job was run before
committing, from a fresh clone of this branch, the way a runner would run it:
Linux jobs in a throwaway Ubuntu 24.04 (the image `ubuntu-latest` uses),
Windows jobs here, each with only the tools the job sets up. 116 on each;
MSYS2's gcc 16 stood in for the runner's gcc 15, both defaulting to C23.
