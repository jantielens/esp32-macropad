---
name: sanitycheck
description: Pre-commit sanity check — dead code, comments, KISS, DRY, docs, architecture
---

Review all changes in scope and report findings for each category below. If no scope is provided, review all uncommitted changes in the current branch.
For each category, list concrete findings with file paths and line numbers — or state "No issues found."
Do NOT fix anything — only report.

## 1. Dead Code

- Unreachable code, unused functions, unused variables, unused includes/imports
- Commented-out code blocks that should be removed
- #ifdef branches that can never be true given the current board configs
- Unused struct fields or enum values introduced in this diff

## 2. Comments & Naming

- Comments that are outdated, misleading, or contradict the code they describe
- TODO/FIXME/HACK markers that should be resolved before merging
- Functions or variables whose names no longer match their behavior after this change
- Missing comments only where logic is non-obvious (do NOT flag missing comments on self-documenting code)

## 3. KISS (Keep It Simple)

- Over-engineered abstractions for something used only once
- Unnecessary indirection or wrapper layers
- Complex conditionals that could be simplified
- Premature generalization — code designed for hypothetical future requirements

## 4. DRY (Don't Repeat Yourself)

- Duplicated logic across files or functions that should be extracted
- Copy-pasted code blocks with minor variations
- Magic numbers or strings that appear in multiple places and should be constants
- Repeated patterns that could use a shared helper

## 5. Documentation

- Check if changes affect behavior documented in any of these files:
  - README.md
  - docs/dev/*.md (especially web-portal.md, display-touch-architecture.md, scripts.md)
  - docs/first-time-setup.md, docs/web-portal-guide.md
  - .github/copilot-instructions.md
  - CHANGELOG.md (is the new entry accurate and complete?)
  - src/app/drivers/README.md (if driver changes)
- Flag any doc that describes old behavior contradicted by the diff

## 6. Architecture & Clean Design

- Separation of concerns violations (e.g., UI logic in data layer, network code in display code)
- Thread safety issues (e.g., shared state without mutex, LVGL calls outside LVGL task)
- Resource leaks (e.g., allocated memory not freed, opened files not closed)
- Inconsistent error handling patterns compared to surrounding code
- Opportunities to simplify the dependency graph between modules

## Sample Output Format

### Dead Code
DEAD01. [file.cpp#L42] `unusedFunction()` — never called after refactor
DEAD02. [file.h#L10] `#include "old_header.h"` — not needed after changes to module X
...

### Comments & Naming
COMMENT01. [file.cpp#L88] Comment "handles edge case Y" is no longer accurate after logic change
...

### KISS
KISS01. [file.cpp#L120] `ComplexWrapper` class adds unnecessary indirection for a one-off use case
...

### DRY
DRY01. [file1.cpp#L50] and [file2.cpp#L75] Both contain similar logic for parsing config values — should be extracted to a shared helper function
...

### Documentation
DOC01. [README.md#L200] The "Supported Devices" section needs updating to reflect the new board added in this PR
...

### Architecture & Clean Design
ARCH01. [module1.cpp#L30] Direct calls to `Serial.print()` in the data processing module violate separation of concerns — should use a logging interface instead
...

### Recommendations

Recommended to fix:
- DEAD01: (Super short description & why it should be fixed.)
- KISS01: (Super short description & why it should be fixed.)
...
Recommended to skip:
- KISS03: (Super short description & why it should be skipped.)

Checklist (to copy/paste as a reply):
````
Fix: 
- DEAD01
- DEAD02
- COMMENT01
- KISS01
- DRY01
- DOC01
- ARCH01
````