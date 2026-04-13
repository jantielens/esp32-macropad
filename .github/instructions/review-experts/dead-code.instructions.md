---
description: "Dead code detection expert — identifies unused code, unreachable branches, and stale artifacts in diffs"
applyTo: "**"
---

# Dead Code Expert

Review code changes for dead code: unused definitions, unreachable paths, and stale artifacts that should be removed.

## Review Criteria

### Unused Definitions

* Functions, methods, or static helpers that are defined but never called after the change.
* Variables declared but never read. Pay attention to variables that were used before the diff but whose usage was removed.
* `#include` or `#import` directives for headers no longer needed after the change.
* Struct fields or enum values added in this diff that have no consumers.
* Type aliases or `typedef` definitions with zero references.

### Unreachable Code

* Code paths after unconditional `return`, `break`, `continue`, or `goto` statements.
* `#ifdef` / `#if` branches that can never be true given the project's board configurations and compile-time flags. Cross-reference with `board_config.h` and `board_overrides.h` files.
* `else` branches that are logically impossible given preceding conditions.
* Switch-case fall-through paths that are unreachable.

### Stale Artifacts

* Commented-out code blocks (more than 2 lines). Single-line commented code used as documentation is acceptable.
* Debug print statements (`Serial.print`, `printf`, `LOG_D`) that appear to be leftover debugging rather than intentional diagnostics.
* Temporary workaround code marked with TODO/FIXME that the current change should have resolved.

## Severity Guidelines

| Severity | Criteria |
|---|---|
| High | Unused function >20 lines, dead #ifdef branch with significant code |
| Medium | Unused variable, unnecessary include, small unused helper |
| Low | Single commented-out line, minor stale artifact |

## DO

* Read the full file context to verify that a function truly has no callers. Grep for the function name across the codebase.
* Check both `.cpp` and `.h` files — a declaration in a header without a definition is also dead code.
* Consider conditional compilation: a function may appear unused in one board config but used in another.

## DON'T

* Flag `extern` declarations that serve as forward references for other translation units.
* Flag functions guarded by `HAS_*` feature flags unless you can confirm no board enables that flag.
* Flag intentionally empty function bodies (stubs, weak symbols, interface defaults).
* Flag logging or diagnostic code that follows the project's logging guidelines.
