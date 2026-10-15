# LexiGone

Evaluate a mathematical expression, given as text, at a value of x.

```c
#include "evaluate.h"

Function func = { "x^2 + 2*x + 1", "1.5" };
double result = evaluate(func);   /* 6.25 */
```

Two files, no dependencies, no build step, nothing to install.

## How it works

It parses the expression and evaluates it. About 400 lines of
recursive-descent parser in `evaluate.c`.

That is worth stating plainly, because it did not used to. Until September
2026 this library evaluated an expression by **writing a C source file
containing it, compiling that file with `gcc` through `system()`, running the
result through `popen()`, and scanning the printed number back** — per call.

That approach had three problems, and all three are gone:

- **It cost a compiler invocation per evaluation.** Sampling a function at 500
  points to plot it meant 500 compilations. The `example.c` in this repository
  now does exactly that loop; it finishes instantly.
- **It required `gcc` on the end user's machine, at runtime**, which the README
  did not mention. The library was described as cross-platform, which was true
  only where a compiler happened to be installed.
- **Its safety rested entirely on one function.** `validate_expression()` had
  to be airtight, because anything that got past it was compiled and executed.
  It was a good allowlist and it held — but the repository also contained
  `evaluate_with_debug.c`, a complete second copy of the same function
  exporting the same symbol from the same header, with **no validation at
  all**. Its name suggested "the same thing, with logging", which is precisely
  what invites the mistake, and an expression like
  `0); system("whatever"); return sin(0` compiled cleanly through it. That
  file is deleted.

A parser cannot be talked into running a command, because it does not know how
to run one. An expression that is not an expression simply fails to parse, and
says where.

## Grammar

```
expression := term (('+' | '-') term)*
term       := unary (('*' | '/' | '%') unary)*
unary      := ('+' | '-') unary | power
power      := primary ('^' unary)?        right-associative
primary    := number
            | name '(' expression (',' expression)* ')'
            | name
            | '(' expression ')'
```

`^` is exponentiation and `%` is `fmod`, which is what someone writing
mathematics means by them. Under the old implementation both were accepted by
validation and then handed to a C compiler, where they mean bitwise xor and
integer modulo and neither applies to a `double` — so `x^2`, the one notation
everybody reaches for, produced "Compilation failed" and no explanation.

`2^3^2` is 512, not 64. `-2^2` is −4. `2^-1` is 0.5.

## Names

The variable is `x`. Everything else:

| | |
| --- | --- |
| Trigonometric | `sin` `cos` `tan` `asin` `acos` `atan` `atan2` |
| Hyperbolic | `sinh` `cosh` `tanh` `asinh` `acosh` `atanh` |
| Exponential, logarithmic | `exp` `exp2` `log` `log2` `log10` `log1p` |
| Power, root | `pow` `sqrt` `cbrt` `hypot` |
| Rounding | `ceil` `floor` `round` `trunc` `fmod` `remainder` |
| Other | `fabs` `copysign` `fdim` `fmax` `fmin` |
| Constants | `M_PI` `M_E` `M_SQRT2` `M_LN2` `M_LN10` `M_LOG2E` `M_LOG10E` `M_PI_2` `M_PI_4` |

Anything else is rejected by name, with the offset it appeared at.

`lexi_known_names()` returns this list at runtime, so a caller can display it —
and so this table cannot quietly drift from the implementation. The test suite
walks the list and checks that every name in it actually works.

## API

```c
typedef struct {
    const char *expression;   /* "x^2 + 2*x + 1" */
    const char *x_value;      /* "1.5"           */
} Function;

double     evaluate(Function func);
LexiStatus evaluate_checked(Function func, double *out, LexiError *error);
LexiStatus evaluate_at(const char *expression, double x, double *out, LexiError *error);
```

**`evaluate`** is the original interface and behaves as it always did: the
result, or `0.0` with a message on stderr. It is kept for callers that already
use it.

But `0.0` is a perfectly ordinary answer — `sin(0)`, `x-x`, `0*anything` — so
that signature cannot tell you whether the number it gave you means anything.

**`evaluate_checked`** can. It writes nothing to stderr, returns a status, and
fills in a `LexiError` with the reason and a byte offset into the expression.
`*out` is left untouched unless it succeeded.

```c
double result;
LexiError error;

if (evaluate_checked(func, &result, &error) != LEXI_OK) {
    printf("%s at offset %zu\n", error.message, error.offset);
}
```

**`evaluate_at`** takes x as a `double`. Use it when sampling: formatting a
number into a string so the library can parse it back, five hundred times, is
work with no purpose.

Statuses: `LEXI_OK`, `LEXI_ERR_NULL_INPUT`, `LEXI_ERR_EMPTY`,
`LEXI_ERR_TOO_LONG`, `LEXI_ERR_BAD_X_VALUE`, `LEXI_ERR_SYNTAX`,
`LEXI_ERR_UNKNOWN_NAME`, `LEXI_ERR_WRONG_ARITY`, `LEXI_ERR_TRAILING_INPUT`,
`LEXI_ERR_TOO_DEEP`. `lexi_status_name()` turns one into text.

## Using it

Copy `evaluate.c` and `evaluate.h` into your project.

```sh
gcc -std=c11 your_program.c evaluate.c -o your_program -lm
```

`-lm` is needed on Linux and macOS; MinGW and MSVC fold the math library in.

### The examples

```sh
gcc -std=c11 example.c evaluate.c -o example -lm
./example
```

### The tests

```sh
sh tests/build.sh          # or, on Windows:  tests\build.bat
```

116 checks, no framework. They cover the grammar, precedence and
associativity, every published name, the error positions, and the strings that
used to be compiled and run.

There were no tests before, which is unsurprising: testing the old
implementation meant invoking a compiler per assertion.

## What it does not do

- **More than one variable.** `x` only.
- **Assignment, sequencing, comparison, or anything else that is not
  arithmetic.** There is no `=`, no `;`, no `<`.
- **Complain about infinity or NaN.** `1/0` is `inf` and `sqrt(-1)` is `nan`,
  as IEEE 754 says and as the compiled program used to produce. Check with
  `isfinite()` if it matters to you.
- **Accept an expression longer than 1024 bytes**, or nested more than 64
  levels deep. A recursive-descent parser can be made to overflow its stack by
  a string of open parentheses, which is trivial to write.

## Requirements

A C99 compiler. Nothing else, at build time or at run time.

## Licence

Free for personal, non-commercial use. You may use, study and modify this
software for yourself, but you may not use it commercially, and you may not
share or publish it, in its original form or modified. See [LICENSE](LICENSE)
for the full terms.

## Author

**Manish Majhi** — [@nishantmajhi](https://github.com/nishantmajhi)

Thank you to the teachers of Mechi Multiple Campus for believing in me.
