/*
 * Examples.
 *
 *   gcc -std=c11 example.c evaluate.c -o example -lm
 *   ./example
 */

#include <math.h>
#include <stdio.h>

#include "evaluate.h"

static void show(const char *expression, const char *x_value) {
    Function func = { expression, x_value };
    double result = 0.0;
    LexiError error;

    if (evaluate_checked(func, &result, &error) != LEXI_OK) {
        printf("  %-24s at x=%-6s  rejected: %s\n", expression, x_value, error.message);
        return;
    }

    printf("  %-24s at x=%-6s  = %g\n", expression, x_value, result);
}

int main(void) {
    printf("Evaluating\n");
    show("x*x + 2*x + 1", "1.5");
    show("sin(x) + cos(x)", "3.14159");
    show("exp(x) * log(x + 1)", "2.0");
    show("sqrt(x*x + 1) / (1 + x*x)", "5.0");

    /* Under the old implementation these were accepted by validation and then
       rejected by the C compiler, because in C '^' is bitwise xor and '%' is
       integer modulo, and neither applies to a double. The caller saw
       "Compilation failed" and nothing else. */
    printf("\nOperators that used to fail\n");
    show("x^2", "3");
    show("x^0.5", "16");
    show("x % 3", "7.5");

    /* Errors say what is wrong and where, rather than returning 0.0 and
       leaving the caller to guess. */
    printf("\nRejections\n");
    show("foo(x)", "1");
    show("sin(1, 2)", "1");
    show("(x + 1", "1");
    show("x", "not a number");

    /*
     * Sampling a function.
     *
     * This is the loop the old implementation could not run: it wrote a C
     * file, invoked gcc, executed the result and deleted both — per point.
     * Five hundred points meant five hundred compiler invocations, and gcc had
     * to be installed on the end user's machine.
     */
    printf("\nSampling sin(x)/x over 500 points\n");

    const char *expression = "sin(x)/x";
    double peak = -1e308;
    double peak_at = 0.0;

    for (int i = 1; i <= 500; i++) {
        double x = i * (4.0 * 3.14159265358979323846) / 500.0;
        double y = 0.0;

        /* evaluate_at takes the x as a double, so there is no reason to format
           it into a string and parse it back for every point. */
        if (evaluate_at(expression, x, &y, NULL) != LEXI_OK) {
            printf("  evaluation failed at x=%g\n", x);
            return 1;
        }

        if (y > peak) { peak = y; peak_at = x; }
    }

    printf("  highest value %g, at x = %g\n", peak, peak_at);

    /* Zero is a perfectly ordinary answer, so a caller that needs to know
       whether something worked has to ask. evaluate() cannot say. */
    printf("\nWhy evaluate_checked exists\n");

    Function real_zero = { "sin(0)", "0" };
    Function nonsense  = { "sin(0",  "0" };

    printf("  evaluate(\"sin(0)\") = %g\n", evaluate(real_zero));
    printf("  evaluate(\"sin(0\")  = %g   <- the same number, a different meaning\n",
           evaluate(nonsense));

    double checked = 0.0;
    LexiError error;
    printf("  evaluate_checked on the second one: %s\n",
           lexi_status_name(evaluate_checked(nonsense, &checked, &error)));

    return 0;
}
