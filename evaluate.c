/*
 * evaluate.c — a recursive-descent parser and evaluator for the expression
 * grammar LexiGone accepts.
 *
 * What this replaces
 * ------------------
 * evaluate() used to write a C source file containing the caller's expression,
 * compile it with `system("gcc ...")`, run the result through popen(), and
 * scan the printed number back. Every call cost a compiler invocation —
 * plotting a function at 500 points meant 500 of them — and the library
 * silently required gcc to be installed and on PATH on the end user's machine,
 * which the README did not mention.
 *
 * It also meant the safety of the whole thing rested on one function:
 * validate_expression() had to be airtight, because anything that got past it
 * was compiled and executed. It was a good allowlist and it held, but it is
 * not a position worth defending. Division and multiplication were both
 * permitted characters, so putting them next to each other opened a C comment
 * that never closed, and the generated file failed with a baffling error.
 * Caret and percent were accepted by validation and then rejected by the
 * compiler, because in C they mean xor and integer modulo and neither works on
 * a double — so the one notation everybody reaches for, x^2, produced
 * "Compilation failed" and no explanation.
 *
 * A parser has none of those problems, because it does not know how to run a
 * command and cannot be persuaded to learn. An expression that is not an
 * expression simply fails to parse, and says where.
 *
 * The grammar
 * -----------
 *   expression := term (('+' | '-') term)*
 *   term       := unary (('*' | '/' | '%') unary)*
 *   unary      := ('+' | '-') unary | power
 *   power      := primary ('^' unary)?          right-associative
 *   primary    := number
 *               | name '(' expression (',' expression)* ')'
 *               | name
 *               | '(' expression ')'
 *
 * '^' is exponentiation and '%' is fmod, which is what someone writing
 * mathematics means by them. Under the old implementation both were accepted
 * by validation and then rejected by the C compiler with a message about
 * invalid operands.
 */

#include "evaluate.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* MSVC does not define these without _USE_MATH_DEFINES, and relying on a
   platform header for the value of pi is not worth the portability question. */
#ifndef LEXI_PI
#define LEXI_PI 3.14159265358979323846
#define LEXI_E  2.71828182845904523536
#endif

/* Deep nesting is a stack-overflow risk in a recursive-descent parser, and
   "((((((..." is trivial to write. This is far more nesting than any real
   expression has. */
#define LEXI_MAX_DEPTH 64

/* ------------------------------------------------------------------ */
/* Names                                                              */
/* ------------------------------------------------------------------ */

typedef double (*Unary)(double);
typedef double (*Binary)(double, double);

typedef struct {
    const char *name;
    int arity;          /* 0 constant, 1 or 2 arguments */
    double value;       /* arity 0 */
    Unary one;          /* arity 1 */
    Binary two;         /* arity 2 */
} Name;

/* fmod and pow are used for the % and ^ operators as well as by name. */
static double lexi_pow(double a, double b) { return pow(a, b); }
static double lexi_fmod(double a, double b) { return fmod(a, b); }

/*
 * The same list the old allowlist held, which was a good list, now doing real
 * work rather than gatekeeping a compiler.
 *
 * asinh, acosh, atanh, exp2, log2, log1p, cbrt, hypot, round, trunc,
 * remainder, copysign, fdim, fmax and fmin are all C99, so they are available
 * wherever this builds.
 */
static const Name NAMES[] = {
    /* trigonometric */
    { "sin",       1, 0, sin,   NULL },
    { "cos",       1, 0, cos,   NULL },
    { "tan",       1, 0, tan,   NULL },
    { "asin",      1, 0, asin,  NULL },
    { "acos",      1, 0, acos,  NULL },
    { "atan",      1, 0, atan,  NULL },
    { "atan2",     2, 0, NULL,  atan2 },

    /* hyperbolic */
    { "sinh",      1, 0, sinh,  NULL },
    { "cosh",      1, 0, cosh,  NULL },
    { "tanh",      1, 0, tanh,  NULL },
    { "asinh",     1, 0, asinh, NULL },
    { "acosh",     1, 0, acosh, NULL },
    { "atanh",     1, 0, atanh, NULL },

    /* exponential and logarithmic */
    { "exp",       1, 0, exp,   NULL },
    { "exp2",      1, 0, exp2,  NULL },
    { "log",       1, 0, log,   NULL },
    { "log2",      1, 0, log2,  NULL },
    { "log10",     1, 0, log10, NULL },
    { "log1p",     1, 0, log1p, NULL },

    /* power and root */
    { "pow",       2, 0, NULL,  lexi_pow },
    { "sqrt",      1, 0, sqrt,  NULL },
    { "cbrt",      1, 0, cbrt,  NULL },
    { "hypot",     2, 0, NULL,  hypot },

    /* rounding */
    { "ceil",      1, 0, ceil,  NULL },
    { "floor",     1, 0, floor, NULL },
    { "round",     1, 0, round, NULL },
    { "trunc",     1, 0, trunc, NULL },
    { "fmod",      2, 0, NULL,  lexi_fmod },
    { "remainder", 2, 0, NULL,  remainder },

    /* miscellaneous */
    { "fabs",      1, 0, fabs,  NULL },
    { "copysign",  2, 0, NULL,  copysign },
    { "fdim",      2, 0, NULL,  fdim },
    { "fmax",      2, 0, NULL,  fmax },
    { "fmin",      2, 0, NULL,  fmin },

    /* constants */
    { "M_PI",      0, LEXI_PI,          NULL, NULL },
    { "M_E",       0, LEXI_E,           NULL, NULL },
    { "M_SQRT2",   0, 1.41421356237309504880, NULL, NULL },
    { "M_LN2",     0, 0.693147180559945309417, NULL, NULL },
    { "M_LN10",    0, 2.30258509299404568402, NULL, NULL },
    { "M_LOG2E",   0, 1.44269504088896340736, NULL, NULL },
    { "M_LOG10E",  0, 0.434294481903251827651, NULL, NULL },
    { "M_PI_2",    0, LEXI_PI / 2.0,    NULL, NULL },
    { "M_PI_4",    0, LEXI_PI / 4.0,    NULL, NULL },

    { NULL, 0, 0, NULL, NULL }
};

static const Name *find_name(const char *text, size_t length) {
    for (int i = 0; NAMES[i].name != NULL; i++) {
        if (strlen(NAMES[i].name) == length &&
            strncmp(NAMES[i].name, text, length) == 0) {
            return &NAMES[i];
        }
    }
    return NULL;
}

const char *const *lexi_known_names(void) {
    /* Built once, in the order NAMES declares, plus "x" first — a caller
       listing what is accepted wants the variable in the list too. */
    static const char *cache[sizeof(NAMES) / sizeof(NAMES[0]) + 1];
    static int built = 0;

    if (!built) {
        size_t n = 0;
        cache[n++] = "x";
        for (int i = 0; NAMES[i].name != NULL; i++) cache[n++] = NAMES[i].name;
        cache[n] = NULL;
        built = 1;
    }

    return cache;
}

const char *lexi_status_name(LexiStatus status) {
    switch (status) {
        case LEXI_OK:                return "ok";
        case LEXI_ERR_NULL_INPUT:    return "null input";
        case LEXI_ERR_EMPTY:         return "empty expression";
        case LEXI_ERR_TOO_LONG:      return "expression too long";
        case LEXI_ERR_BAD_X_VALUE:   return "x value is not a number";
        case LEXI_ERR_SYNTAX:        return "syntax error";
        case LEXI_ERR_UNKNOWN_NAME:  return "unknown name";
        case LEXI_ERR_WRONG_ARITY:   return "wrong number of arguments";
        case LEXI_ERR_TRAILING_INPUT:return "unexpected trailing input";
        case LEXI_ERR_TOO_DEEP:      return "expression nested too deeply";
    }
    return "unknown status";
}

/* ------------------------------------------------------------------ */
/* Parser                                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *start;
    const char *p;
    double x;
    int depth;
    LexiStatus status;
    size_t error_offset;
    char message[160];
} Parser;

static void fail(Parser *ps, LexiStatus status, const char *fmt, ...) {
    /* First failure wins: later ones are consequences of it and point at
       positions that will confuse whoever reads the message. */
    if (ps->status != LEXI_OK) return;

    ps->status = status;
    ps->error_offset = (size_t)(ps->p - ps->start);

    va_list args;
    va_start(args, fmt);
    vsnprintf(ps->message, sizeof(ps->message), fmt, args);
    va_end(args);
}

static void skip_space(Parser *ps) {
    while (*ps->p == ' ' || *ps->p == '\t') ps->p++;
}

static double parse_expression(Parser *ps);

static double parse_primary(Parser *ps) {
    skip_space(ps);

    if (ps->status != LEXI_OK) return 0.0;

    if (*ps->p == '(') {
        ps->p++;
        double value = parse_expression(ps);
        skip_space(ps);

        if (*ps->p != ')') {
            fail(ps, LEXI_ERR_SYNTAX, "expected ')'");
            return 0.0;
        }

        ps->p++;
        return value;
    }

    if (isdigit((unsigned char)*ps->p) || *ps->p == '.') {
        char *end = NULL;
        errno = 0;
        double value = strtod(ps->p, &end);

        if (end == ps->p) {
            fail(ps, LEXI_ERR_SYNTAX, "expected a number");
            return 0.0;
        }

        /* strtod accepts "1e999" and returns HUGE_VAL with ERANGE. Letting it
           through as infinity is the same answer the old compiled program
           gave, and is more useful than refusing to parse. */
        ps->p = end;
        return value;
    }

    if (isalpha((unsigned char)*ps->p) || *ps->p == '_') {
        const char *name_start = ps->p;
        while (isalnum((unsigned char)*ps->p) || *ps->p == '_') ps->p++;
        size_t length = (size_t)(ps->p - name_start);

        if (length == 1 && name_start[0] == 'x') {
            skip_space(ps);

            /* "x(2)" is a mistake, not an implicit multiplication. Saying so
               is kinder than silently evaluating something else. */
            if (*ps->p == '(') {
                fail(ps, LEXI_ERR_SYNTAX, "'x' is a variable, not a function");
                return 0.0;
            }

            return ps->x;
        }

        const Name *name = find_name(name_start, length);

        if (!name) {
            /* Point at the name, not at where the scan stopped. */
            ps->p = name_start;
            fail(ps, LEXI_ERR_UNKNOWN_NAME, "unknown name '%.*s'", (int)length, name_start);
            return 0.0;
        }

        if (name->arity == 0) return name->value;

        skip_space(ps);

        if (*ps->p != '(') {
            fail(ps, LEXI_ERR_SYNTAX, "'%s' takes %d argument%s, so it needs '('",
                 name->name, name->arity, name->arity == 1 ? "" : "s");
            return 0.0;
        }

        ps->p++;

        double args[2] = { 0.0, 0.0 };
        int count = 0;

        skip_space(ps);

        if (*ps->p != ')') {
            for (;;) {
                double value = parse_expression(ps);
                if (ps->status != LEXI_OK) return 0.0;

                if (count < 2) args[count] = value;
                count++;

                skip_space(ps);

                if (*ps->p == ',') { ps->p++; continue; }
                break;
            }
        }

        if (*ps->p != ')') {
            fail(ps, LEXI_ERR_SYNTAX, "expected ')' to close '%s('", name->name);
            return 0.0;
        }

        ps->p++;

        if (count != name->arity) {
            fail(ps, LEXI_ERR_WRONG_ARITY, "'%s' takes %d argument%s, got %d",
                 name->name, name->arity, name->arity == 1 ? "" : "s", count);
            return 0.0;
        }

        return name->arity == 1 ? name->one(args[0]) : name->two(args[0], args[1]);
    }

    if (*ps->p == '\0') {
        fail(ps, LEXI_ERR_SYNTAX, "expression ends where a value was expected");
    } else {
        fail(ps, LEXI_ERR_SYNTAX, "unexpected character '%c'", *ps->p);
    }

    return 0.0;
}

static double parse_unary(Parser *ps);

/* Right-associative, and binding tighter than unary minus on its left but
   looser on its right: -2^2 is -4, and 2^-1 is 0.5. */
static double parse_power(Parser *ps) {
    double base = parse_primary(ps);
    if (ps->status != LEXI_OK) return 0.0;

    skip_space(ps);

    if (*ps->p == '^') {
        ps->p++;
        double exponent = parse_unary(ps);
        if (ps->status != LEXI_OK) return 0.0;
        return pow(base, exponent);
    }

    return base;
}

static double parse_unary(Parser *ps) {
    skip_space(ps);

    if (*ps->p == '-') { ps->p++; return -parse_unary(ps); }
    if (*ps->p == '+') { ps->p++; return  parse_unary(ps); }

    return parse_power(ps);
}

static double parse_term(Parser *ps) {
    double left = parse_unary(ps);

    for (;;) {
        if (ps->status != LEXI_OK) return 0.0;

        skip_space(ps);

        char op = *ps->p;
        if (op != '*' && op != '/' && op != '%') return left;

        ps->p++;
        double right = parse_unary(ps);
        if (ps->status != LEXI_OK) return 0.0;

        /* Division and fmod by zero produce infinity and NaN rather than an
           error, which is what IEEE 754 says and what the compiled program
           did. A caller that cares should check with isfinite(). */
        if (op == '*') left = left * right;
        else if (op == '/') left = left / right;
        else left = fmod(left, right);
    }
}

static double parse_expression(Parser *ps) {
    if (++ps->depth > LEXI_MAX_DEPTH) {
        fail(ps, LEXI_ERR_TOO_DEEP, "nested more than %d levels deep", LEXI_MAX_DEPTH);
        ps->depth--;
        return 0.0;
    }

    double left = parse_term(ps);

    for (;;) {
        if (ps->status != LEXI_OK) break;

        skip_space(ps);

        char op = *ps->p;
        if (op != '+' && op != '-') break;

        ps->p++;
        double right = parse_term(ps);
        if (ps->status != LEXI_OK) break;

        left = (op == '+') ? left + right : left - right;
    }

    ps->depth--;
    return left;
}

/* ------------------------------------------------------------------ */
/* Public interface                                                   */
/* ------------------------------------------------------------------ */

static void report(LexiError *error, LexiStatus status, size_t offset, const char *message) {
    if (!error) return;

    error->status = status;
    error->offset = offset;
    snprintf(error->message, sizeof(error->message), "%s", message);
}

LexiStatus evaluate_at(const char *expression, double x, double *out, LexiError *error) {
    if (error) { error->status = LEXI_OK; error->offset = 0; error->message[0] = '\0'; }

    if (!expression || !out) {
        report(error, LEXI_ERR_NULL_INPUT, 0, "expression or output pointer is NULL");
        return LEXI_ERR_NULL_INPUT;
    }

    size_t length = strlen(expression);

    if (length == 0) {
        report(error, LEXI_ERR_EMPTY, 0, "expression is empty");
        return LEXI_ERR_EMPTY;
    }

    if (length > LEXI_MAX_EXPRESSION) {
        report(error, LEXI_ERR_TOO_LONG, 0, "expression is longer than the limit");
        return LEXI_ERR_TOO_LONG;
    }

    Parser ps;
    ps.start = expression;
    ps.p = expression;
    ps.x = x;
    ps.depth = 0;
    ps.status = LEXI_OK;
    ps.error_offset = 0;
    ps.message[0] = '\0';

    double value = parse_expression(&ps);

    if (ps.status != LEXI_OK) {
        report(error, ps.status, ps.error_offset, ps.message);
        return ps.status;
    }

    skip_space(&ps);

    /* Everything must be consumed. "2 3" and "sin(x))" are mistakes, and
       returning the value of the part that parsed would hide them. */
    if (*ps.p != '\0') {
        char message[160];
        snprintf(message, sizeof(message), "unexpected '%c' after the expression", *ps.p);
        report(error, LEXI_ERR_TRAILING_INPUT, (size_t)(ps.p - ps.start), message);
        return LEXI_ERR_TRAILING_INPUT;
    }

    *out = value;
    return LEXI_OK;
}

LexiStatus evaluate_checked(Function func, double *out, LexiError *error) {
    if (error) { error->status = LEXI_OK; error->offset = 0; error->message[0] = '\0'; }

    if (!func.expression || !func.x_value || !out) {
        report(error, LEXI_ERR_NULL_INPUT, 0, "expression, x_value or output pointer is NULL");
        return LEXI_ERR_NULL_INPUT;
    }

    char *end = NULL;
    errno = 0;
    double x = strtod(func.x_value, &end);

    /* strtod must consume the whole string: "1.5abc" is a mistake, not 1.5.
       Trailing whitespace is allowed, since a value read from a file or a
       command line often carries some. */
    while (end && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) end++;

    if (end == func.x_value || (end && *end != '\0')) {
        char message[160];
        snprintf(message, sizeof(message), "'%s' is not a number", func.x_value);
        report(error, LEXI_ERR_BAD_X_VALUE, 0, message);
        return LEXI_ERR_BAD_X_VALUE;
    }

    return evaluate_at(func.expression, x, out, error);
}

double evaluate(Function func) {
    double result = 0.0;
    LexiError error;

    LexiStatus status = evaluate_checked(func, &result, &error);

    if (status != LEXI_OK) {
        fprintf(stderr, "LexiGone: %s", error.message);

        if (status != LEXI_ERR_BAD_X_VALUE && status != LEXI_ERR_NULL_INPUT && func.expression) {
            fprintf(stderr, "\n  %s\n  %*s^", func.expression, (int)error.offset, "");
        }

        fprintf(stderr, "\n");
        return 0.0;
    }

    return result;
}
