# Umbrly Language Specification (draft 0.3)

This document records behavior that is covered by the automated compatibility tests. Features not described here are currently implementation-defined.

## Source and lexical rules

- Source files use UTF-8 and normally have the `.umb` extension.
- Identifiers are ASCII letters, digits and `_`; the first character cannot be a digit.
- `#` and `//` start a line comment.
- Statements are separated by newlines. Blocks end with `END`.
- Variable type is declared by its prefix: `INT_`, `FLOAT_`, `STR_`, `BOOL_`, `ARR_`, or `OBJ_`.

## Values

The value kinds are integer, floating-point, Boolean, string, array, object and `NIL`. Assignment coerces a value to the type required by the destination variable. Plain array assignment copies the outer array; passing an array to a function or builtin shares it.

## Expressions

Arithmetic operators are `+`, `-`, `*`, `/`, `%`. Comparison operators are `==`, `!=`, `<`, `>`, `<=`, `>=`. Boolean operations are `AND`, `OR`, `NOT` and use short-circuit evaluation. Integer division returns an integer only when the result is exact; otherwise it returns a float. Division by zero is a runtime error.

## Control flow

`IF`, `WHILE`, and `FOR` introduce blocks terminated by `END`. `FOR` includes both endpoints and accepts an optional non-zero `STEP`. `BREAK` and `CONTINUE` are valid only inside loops.

## Functions and errors

`FUNC` definitions are registered before top-level execution, allowing forward calls. Parameters use the same prefix-based typing as variables. Recursion is limited to 256 active calls. `RETURN` exits the current function. `TRY`/`CATCH` catches Umbrly runtime errors, not native operating-system failures.

## Arrays and cycle safety

Arrays use zero-based indexing. Out-of-range access is a runtime error. A cyclic array reference is rejected by `PUSH`; string conversion also detects cycles defensively instead of overflowing the native stack.

## Compatibility

Interpreter and native compilation modes must produce the same observable result for specified programs. Any intentional incompatible change requires a language-version update and a migration note.

The compatibility gate is automated by `tests/run_parity_tests.ps1`, which compares standard output and process exit status between interpreter and native modes.

## Execution limits

The interpreter can enforce an instruction budget and a wall-clock deadline. `-safe` uses 5,000,000 instructions and 3,000 milliseconds; `-limit STEPS MS` accepts explicit limits, where zero disables the corresponding limit. Android execution enables the safe limits by default. Exceeding either limit raises a catchable Umbrly runtime error.
