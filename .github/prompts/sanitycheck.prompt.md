---
name: sanitycheck
description: Pre-commit sanity check — dead code, comments, KISS, DRY, docs, architecture
---

Review all changes in scope and report findings for each category below.
Scope: if on a feature branch, review the full branch diff against main (committed + uncommitted). If on main, review uncommitted changes only.
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
  - docs/*.md (especially web-portal.md, display-touch-architecture.md, scripts.md)
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

## Output Format

```
### Dead Code
1. [file.cpp#L42] `unusedFunction()` — never called after refactor
2. [file.h#L10] `#include "old_header.h"` — not needed after changes to module X
...

### Comments & Naming
3. [file.cpp#L88] Comment "handles edge case Y" is no longer accurate after logic change
...

### KISS
...

### DRY
...

### Documentation
...

### Architecture & Clean Design
...

### Recommendations
Recommended to fix 2, 3 and 5 because ...
We can skip 1 and 4 for now since ...

```