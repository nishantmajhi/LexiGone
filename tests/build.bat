@echo off
REM Builds and runs the evaluator tests. Run from the repository root:
REM   tests\build.bat

gcc -std=c11 -Wall -Wextra -o tests\evaluate_test.exe tests\evaluate_test.c evaluate.c -lm
if %ERRORLEVEL% NEQ 0 (
    echo Test build failed.
    exit /b 1
)

tests\evaluate_test.exe
exit /b %ERRORLEVEL%
