/*
 * Tests for the expression evaluator.
 *
 *   gcc -std=c11 -Wall -Wextra -o tests/evaluate_test.exe tests/evaluate_test.c evaluate.c -lm
 *   tests/evaluate_test.exe
 *
 * No framework, because the library has no dependencies and should not gain
 * one to be testable.
 *
 * There were no tests before, which is not surprising: testing the old
 * implementation meant invoking a compiler per assertion.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../evaluate.h"

static int checks = 0;
static int failures = 0;
static const char *current = "";

static void group(const char *name) {
    current = name;
    printf("\n%s\n", name);
}

static void pass(const char *what) {
    checks++;
    printf("  ok    %s\n", what);
}

static void flunk(const char *what, const char *detail) {
    checks++;
    failures++;
    printf("  FAIL  %s  [%s]\n        %s\n", what, current, detail);
}

/* Doubles are compared with a tolerance; these are transcendental functions. */
static void close_to(const char *expression, double x, double expected, const char *what) {
    double got = 0.0;
    LexiError err;
    LexiStatus status = evaluate_at(expression, x, &got, &err);

    if (status != LEXI_OK) {
        char detail[256];
        snprintf(detail, sizeof(detail), "\"%s\" did not evaluate: %s (%s)",
                 expression, err.message, lexi_status_name(status));
        flunk(what, detail);
        return;
    }

    if (fabs(got - expected) > 1e-9 * (1.0 + fabs(expected))) {
        char detail[256];
        snprintf(detail, sizeof(detail), "\"%s\" at x=%g gave %.17g, expected %.17g",
                 expression, x, got, expected);
        flunk(what, detail);
        return;
    }

    pass(what);
}

static void rejects(const char *expression, LexiStatus expected, const char *what) {
    double got = 0.0;
    LexiError err;
    LexiStatus status = evaluate_at(expression, 1.0, &got, &err);

    if (status == LEXI_OK) {
        char detail[256];
        snprintf(detail, sizeof(detail), "\"%s\" was accepted and gave %.17g", expression, got);
        flunk(what, detail);
        return;
    }

    if (status != expected) {
        char detail[256];
        snprintf(detail, sizeof(detail), "\"%s\" was rejected as \"%s\", expected \"%s\"",
                 expression, lexi_status_name(status), lexi_status_name(expected));
        flunk(what, detail);
        return;
    }

    pass(what);
}

static void ok(int condition, const char *what) {
    if (condition) pass(what); else flunk(what, "condition was false");
}

/* ---------- the reason this file exists ---------- */

static void test_no_shell(void) {
    group("Nothing is compiled or executed");

    /* evaluate_with_debug.c was a second copy of the whole function with no
       validation at all, exporting the same symbol from the same header. It
       fprintf'd the caller's string into a C file, ran system("gcc ..."), and
       popen'd the result. This expression compiled cleanly through it:

           0); system("whatever"); return sin(0

       There is nothing to compile now, so the string is simply not an
       expression: it parses "0", then meets a closing parenthesis that opens
       nothing. */
    rejects("0); system(\"echo owned\"); return sin(0", LEXI_ERR_TRAILING_INPUT,
            "the payload that ran through the unhardened copy does not parse");

    rejects("system(\"echo owned\")", LEXI_ERR_UNKNOWN_NAME, "system is not a name");
    rejects("__asm__(\"nop\")", LEXI_ERR_UNKNOWN_NAME, "nor is anything else");
    rejects("x; printf(\"hi\")", LEXI_ERR_TRAILING_INPUT, "a semicolon does not start a statement");
    rejects("\"string\"", LEXI_ERR_SYNTAX, "there are no strings");
    rejects("#include <stdio.h>", LEXI_ERR_SYNTAX, "there are no directives");

    /* Both were permitted characters, and together they opened a C comment
       that never closed. */
    rejects("1/*x", LEXI_ERR_SYNTAX, "a comment opener is a syntax error, not a comment");
}

/* ---------- arithmetic ---------- */

static void test_arithmetic(void) {
    group("Arithmetic");

    close_to("1", 0, 1.0, "a literal");
    close_to("1.5", 0, 1.5, "a decimal");
    close_to(".5", 0, 0.5, "a leading point");
    close_to("1e3", 0, 1000.0, "scientific notation");
    close_to("1+2", 0, 3.0, "addition");
    close_to("5-3", 0, 2.0, "subtraction");
    close_to("3*4", 0, 12.0, "multiplication");
    close_to("7/2", 0, 3.5, "division, in doubles");
    close_to("  1  +  2  ", 0, 3.0, "whitespace is ignored");
    close_to("1+2+3+4", 0, 10.0, "left-associative addition");
    close_to("10-3-2", 0, 5.0, "left-associative subtraction, not 9");
    close_to("100/10/2", 0, 5.0, "left-associative division, not 20");
}

static void test_precedence(void) {
    group("Precedence and associativity");

    close_to("2+3*4", 0, 14.0, "multiplication before addition");
    close_to("(2+3)*4", 0, 20.0, "parentheses override it");
    close_to("2*3+4*5", 0, 26.0, "both sides");
    close_to("-3+5", 0, 2.0, "leading minus");
    close_to("2*-3", 0, -6.0, "a unary minus after an operator");
    close_to("--3", 0, 3.0, "two of them");
    close_to("+-+3", 0, -3.0, "a mixture");

    /* The old implementation accepted ^ at validation and then handed it to a
       C compiler, where it is bitwise xor and does not apply to doubles. The
       user got "Compilation failed" with no explanation. */
    close_to("2^3", 0, 8.0, "caret is exponentiation");
    close_to("2^3^2", 0, 512.0, "right-associative: 2^(3^2), not (2^3)^2");
    close_to("-2^2", 0, -4.0, "unary minus binds looser than the power");
    close_to("2^-1", 0, 0.5, "a negative exponent");
    close_to("2*3^2", 0, 18.0, "power before multiplication");

    /* And % was integer modulo in C, so it did not apply to doubles either. */
    close_to("7%3", 0, 1.0, "percent is fmod");
    close_to("7.5%2", 0, 1.5, "on a non-integer");
    close_to("-7%3", 0, -1.0, "fmod keeps the sign of the dividend");
}

static void test_variable(void) {
    group("The variable");

    close_to("x", 3.0, 3.0, "on its own");
    close_to("x*x", 4.0, 16.0, "twice");
    close_to("x^2+2*x+1", 3.0, 16.0, "a polynomial");
    close_to("2*x", -1.5, -3.0, "a negative x");
    close_to("x-x", 12345.0, 0.0, "zero is a real answer, not just an error code");

    rejects("x(2)", LEXI_ERR_SYNTAX, "x is a variable, not a function");
    rejects("y", LEXI_ERR_UNKNOWN_NAME, "there is only one variable");
    rejects("X", LEXI_ERR_UNKNOWN_NAME, "and it is lower case");
}

/* ---------- functions ---------- */

static void test_functions(void) {
    group("Functions");

    close_to("sin(0)", 0, 0.0, "sin");
    close_to("cos(0)", 0, 1.0, "cos");
    close_to("sqrt(16)", 0, 4.0, "sqrt");
    close_to("fabs(-3)", 0, 3.0, "fabs");
    close_to("exp(0)", 0, 1.0, "exp");
    close_to("log(M_E)", 0, 1.0, "log of e");
    close_to("log10(1000)", 0, 3.0, "log10");
    close_to("log2(8)", 0, 3.0, "log2");
    close_to("ceil(1.2)", 0, 2.0, "ceil");
    close_to("floor(1.8)", 0, 1.0, "floor");
    close_to("round(1.5)", 0, 2.0, "round");
    close_to("trunc(-1.8)", 0, -1.0, "trunc");
    close_to("cbrt(27)", 0, 3.0, "cbrt");
    close_to("sinh(0)", 0, 0.0, "sinh");
    close_to("atanh(0)", 0, 0.0, "atanh");

    close_to("pow(2,10)", 0, 1024.0, "a two-argument function");
    close_to("pow(2, 10)", 0, 1024.0, "with a space after the comma");
    close_to("hypot(3,4)", 0, 5.0, "hypot");
    close_to("fmax(3,7)", 0, 7.0, "fmax");
    close_to("fmin(3,7)", 0, 3.0, "fmin");
    close_to("atan2(1,1)*4", 0, 3.141592653589793, "atan2");
    close_to("copysign(3,-1)", 0, -3.0, "copysign");

    close_to("sin(x)^2+cos(x)^2", 0.7, 1.0, "an identity, at an arbitrary x");
    close_to("sqrt(pow(x,2))", -5.0, 5.0, "nested calls");
    close_to("pow(x, pow(2, 2))", 2.0, 16.0, "a call as an argument");
}

static void test_constants(void) {
    group("Constants");

    close_to("M_PI", 0, 3.141592653589793, "pi");
    close_to("M_E", 0, 2.718281828459045, "e");
    close_to("M_PI_2", 0, 3.141592653589793 / 2, "pi over two");
    close_to("M_SQRT2^2", 0, 2.0, "root two, squared");
    close_to("sin(M_PI_2)", 0, 1.0, "used in a call");

    rejects("M_PI(2)", LEXI_ERR_TRAILING_INPUT, "a constant is not callable");
}

/* ---------- errors ---------- */

static void test_errors(void) {
    group("Rejections");

    rejects("", LEXI_ERR_EMPTY, "an empty expression");
    rejects("1+", LEXI_ERR_SYNTAX, "a trailing operator");
    rejects("*2", LEXI_ERR_SYNTAX, "a leading binary operator");
    rejects("(1+2", LEXI_ERR_SYNTAX, "an unclosed parenthesis");
    rejects("1+2)", LEXI_ERR_TRAILING_INPUT, "an extra closing parenthesis");
    rejects("2 3", LEXI_ERR_TRAILING_INPUT, "two values with nothing between them");
    rejects("sin", LEXI_ERR_SYNTAX, "a function with no arguments at all");
    rejects("sin()", LEXI_ERR_WRONG_ARITY, "a function with empty parentheses");
    rejects("sin(1,2)", LEXI_ERR_WRONG_ARITY, "too many arguments");
    rejects("pow(2)", LEXI_ERR_WRONG_ARITY, "too few");
    rejects("pow(1,2,3)", LEXI_ERR_WRONG_ARITY, "too many");
    rejects("foo(1)", LEXI_ERR_UNKNOWN_NAME, "an unknown function");
    rejects("1 $ 2", LEXI_ERR_TRAILING_INPUT, "an unknown operator");
    rejects("{1}", LEXI_ERR_SYNTAX, "braces");

    double out = 0.0;
    ok(evaluate_at(NULL, 1.0, &out, NULL) == LEXI_ERR_NULL_INPUT, "a NULL expression");
    ok(evaluate_at("1", 1.0, NULL, NULL) == LEXI_ERR_NULL_INPUT, "a NULL output pointer");
    ok(evaluate_at("1", 1.0, &out, NULL) == LEXI_OK, "a NULL error pointer is allowed");
}

static void test_error_positions(void) {
    group("Errors say where");

    double out = 0.0;
    LexiError err;

    evaluate_at("1 + foo(2)", 0, &out, &err);
    ok(err.status == LEXI_ERR_UNKNOWN_NAME, "an unknown name is reported as such");
    ok(err.offset == 4, "and points at the name, not at where the scan stopped");
    ok(strstr(err.message, "foo") != NULL, "and names it");

    evaluate_at("(1 + 2", 0, &out, &err);
    ok(err.status == LEXI_ERR_SYNTAX, "an unclosed parenthesis is a syntax error");
    ok(strstr(err.message, ")") != NULL, "and the message says what was expected");

    evaluate_at("sin(1,2)", 0, &out, &err);
    ok(strstr(err.message, "sin") != NULL, "an arity error names the function");
    ok(strstr(err.message, "1 argument") != NULL, "and says how many it takes");
}

static void test_depth(void) {
    group("Nesting");

    /* A recursive-descent parser can be made to overflow the stack by a string
       of open parentheses, which is trivial to write.

       Deep enough to pass the parser's nesting limit, short enough that the
       length check does not catch it first — otherwise this would pass while
       testing something else entirely. */
    const int levels = 100;
    char deep[LEXI_MAX_EXPRESSION];
    size_t n = 0;

    for (int i = 0; i < levels; i++) deep[n++] = '(';
    deep[n++] = '1';
    for (int i = 0; i < levels; i++) deep[n++] = ')';
    deep[n] = '\0';

    ok(n < LEXI_MAX_EXPRESSION, "the test expression is inside the length limit");
    rejects(deep, LEXI_ERR_TOO_DEEP, "past the nesting limit is refused, not a crash");

    close_to("((((((((((1))))))))))", 0, 1.0, "ten levels is fine");
}

static void test_length_limit(void) {
    group("Length");

    char long_expression[LEXI_MAX_EXPRESSION + 64];
    size_t n = 0;

    long_expression[n++] = '1';
    while (n < LEXI_MAX_EXPRESSION + 10) {
        long_expression[n++] = '+';
        long_expression[n++] = '1';
    }
    long_expression[n] = '\0';

    rejects(long_expression, LEXI_ERR_TOO_LONG, "past the limit is refused");
}

/* ---------- the published interface ---------- */

static void test_function_struct(void) {
    group("The Function interface");

    double out = 0.0;
    LexiError err;

    Function f = { "x^2", "4" };
    ok(evaluate_checked(f, &out, &err) == LEXI_OK && fabs(out - 16.0) < 1e-12,
       "expression and x_value as strings");

    Function negative = { "x+1", "-2.5" };
    ok(evaluate_checked(negative, &out, &err) == LEXI_OK && fabs(out + 1.5) < 1e-12,
       "a negative x_value");

    Function spaced = { "x", " 3.5 \n" };
    ok(evaluate_checked(spaced, &out, &err) == LEXI_OK && fabs(out - 3.5) < 1e-12,
       "surrounding whitespace in x_value is allowed");

    Function bad = { "x", "1.5abc" };
    ok(evaluate_checked(bad, &out, &err) == LEXI_ERR_BAD_X_VALUE,
       "an x_value with trailing text is refused, not read as 1.5");

    Function empty_x = { "x", "" };
    ok(evaluate_checked(empty_x, &out, &err) == LEXI_ERR_BAD_X_VALUE, "an empty x_value");

    Function null_x = { "x", NULL };
    ok(evaluate_checked(null_x, &out, &err) == LEXI_ERR_NULL_INPUT, "a NULL x_value");
}

static void test_zero_is_not_an_error(void) {
    group("Zero is an answer");

    /* evaluate() returns 0.0 both for sin(0) and for a rejected expression,
       and cannot tell the caller which happened. That is why
       evaluate_checked() exists; this records the distinction. */
    double out = 0.0;
    LexiError err;

    Function real_zero = { "sin(0)", "0" };
    ok(evaluate_checked(real_zero, &out, &err) == LEXI_OK && out == 0.0,
       "sin(0) is zero and succeeded");

    Function failure = { "nonsense(", "0" };
    out = 12345.0;
    ok(evaluate_checked(failure, &out, &err) != LEXI_OK,
       "a bad expression is reported as a failure");
    ok(out == 12345.0, "and the output is left untouched, not set to zero");
}

static void test_known_names(void) {
    group("The published name list");

    const char *const *names = lexi_known_names();

    ok(names != NULL && names[0] != NULL, "the list is not empty");
    ok(strcmp(names[0], "x") == 0, "the variable comes first");

    int found_sin = 0, found_pi = 0, count = 0;
    for (int i = 0; names[i]; i++) {
        count++;
        if (strcmp(names[i], "sin") == 0) found_pi |= 0, found_sin = 1;
        if (strcmp(names[i], "M_PI") == 0) found_pi = 1;
    }

    ok(found_sin, "it contains sin");
    ok(found_pi, "it contains M_PI");
    ok(count > 40, "and everything else");

    /* Every published name must actually work, or the documentation this
       feeds is a lie. Constants are used bare; functions get the right number
       of arguments by trying one and then two. */
    int all_usable = 1;
    for (int i = 0; names[i]; i++) {
        char expression[64];
        double value = 0.0;

        snprintf(expression, sizeof(expression), "%s", names[i]);
        if (evaluate_at(expression, 0.5, &value, NULL) == LEXI_OK) continue;

        snprintf(expression, sizeof(expression), "%s(0.5)", names[i]);
        if (evaluate_at(expression, 0.5, &value, NULL) == LEXI_OK) continue;

        snprintf(expression, sizeof(expression), "%s(0.5, 0.5)", names[i]);
        if (evaluate_at(expression, 0.5, &value, NULL) == LEXI_OK) continue;

        printf("        unusable: %s\n", names[i]);
        all_usable = 0;
    }

    ok(all_usable, "every published name can actually be used");
}

int main(void) {
    printf("LexiGone — expression evaluator\n");

    test_no_shell();
    test_arithmetic();
    test_precedence();
    test_variable();
    test_functions();
    test_constants();
    test_errors();
    test_error_positions();
    test_depth();
    test_length_limit();
    test_function_struct();
    test_zero_is_not_an_error();
    test_known_names();

    printf("\n%d checks, %d failures\n", checks, failures);

    return failures == 0 ? 0 : 1;
}
