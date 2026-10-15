#ifndef EVALUATE_H
#define EVALUATE_H

#include <stddef.h>

/*
 * LexiGone — evaluate a mathematical expression at a value of x.
 *
 * The expression is parsed and evaluated directly. It used to be written into
 * a C source file, compiled with gcc through system(), run through popen(),
 * and the printed number scanned back — per call. See README.md for why that
 * went; the short version is that it made every evaluation cost a compiler
 * invocation, required gcc on the end user's machine at runtime, and meant the
 * safety of the library rested entirely on a validating allowlist being
 * airtight.
 *
 * There is nothing to be airtight about now. An expression that is not a
 * well-formed expression does not parse, and a parser cannot be talked into
 * running a command because it does not know how to run one.
 */

typedef struct {
    const char *expression;
    const char *x_value;
} Function;

/** What went wrong, or LEXI_OK. */
typedef enum {
    LEXI_OK = 0,
    LEXI_ERR_NULL_INPUT,
    LEXI_ERR_EMPTY,
    LEXI_ERR_TOO_LONG,
    LEXI_ERR_BAD_X_VALUE,
    LEXI_ERR_SYNTAX,
    LEXI_ERR_UNKNOWN_NAME,
    LEXI_ERR_WRONG_ARITY,
    LEXI_ERR_TRAILING_INPUT,
    LEXI_ERR_TOO_DEEP
} LexiStatus;

/** Where and why parsing failed. `offset` is a byte index into the expression. */
typedef struct {
    LexiStatus status;
    size_t offset;
    char message[160];
} LexiError;

/** Longest expression accepted, in bytes. */
#define LEXI_MAX_EXPRESSION 1024

/**
 * Evaluate `func.expression` with x set to `func.x_value`.
 *
 * Returns the result, or 0.0 on error — which is why evaluate_checked exists.
 * 0.0 is a perfectly ordinary answer (sin(0), x-x, 0*anything), so this
 * signature cannot tell a caller whether the number it received means
 * anything. It is kept because it is the published interface.
 *
 * Errors are reported on stderr, as before.
 */
double evaluate(Function func);

/**
 * The same, but says whether it worked.
 *
 * Writes nothing to stderr. Pass NULL for `error` if you only want the status
 * through the return value.
 *
 * @return LEXI_OK, or the reason it failed. `*out` is set only on LEXI_OK.
 */
LexiStatus evaluate_checked(Function func, double *out, LexiError *error);

/**
 * Evaluate at a numeric x, without the string round trip.
 *
 * This is the one to use when plotting: parsing "3.14159" back out of a string
 * for every point is work with no purpose.
 */
LexiStatus evaluate_at(const char *expression, double x, double *out, LexiError *error);

/** A human-readable name for a status, for messages and tests. */
const char *lexi_status_name(LexiStatus status);

/**
 * The names this library understands, as a NULL-terminated array.
 *
 * Exposed so a caller can show them, and so the documentation cannot drift
 * from the implementation.
 */
const char *const *lexi_known_names(void);

#endif
