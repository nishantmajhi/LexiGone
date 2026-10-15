#!/bin/sh
# Builds and runs the evaluator tests. Run from the repository root:
#   sh tests/build.sh
set -e
cc -std=c11 -Wall -Wextra -o tests/evaluate_test tests/evaluate_test.c evaluate.c -lm
exec ./tests/evaluate_test
