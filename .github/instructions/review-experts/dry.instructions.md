---
description: "DRY expert — identifies code duplication, magic values, and copy-paste patterns in diffs"
applyTo: "**"
---

# DRY Expert

Review code changes for violations of the Don't Repeat Yourself principle: duplicated logic, magic values, and copy-paste patterns.

## Review Criteria

### Duplicated Logic

* Two or more code blocks (>5 lines) that perform the same operation with minor variations (different variable names, different constant values). These should be extracted into a shared function with parameters.
* Repeated conditional patterns that check the same set of conditions in multiple places.
* Identical error handling blocks across multiple functions that could use a shared handler.
* Repeated initialization or configuration sequences that could be factored into a setup helper.

### Copy-Paste Patterns

* Code blocks that are structurally identical but differ in 1-2 tokens. This is the strongest DRY signal.
* Functions that share >70% of their logic with only the remaining 30% differing. Consider extracting the common part.
* Test cases that repeat setup/teardown boilerplate that could use a fixture or helper.

### Magic Values

* Numeric literals used in more than one location without a named constant. Pin counts, buffer sizes, timeouts, thresholds, port numbers.
* String literals used as keys, identifiers, or format strings in multiple places. NVS keys, MQTT topics, API paths.
* Color values, coordinates, or sizing constants repeated across files.

### Repeated Patterns

* Multiple functions following the same structural template (parse JSON field, validate, store) that could use a data-driven approach or macro.
* Repeated registration patterns (registering handlers, binding schemes, screen types) that could use a table-driven approach.
* Boilerplate guard patterns (null checks, feature flag checks) that could be centralized.

## Severity Guidelines

| Severity | Criteria |
|---|---|
| High | >10 lines duplicated across files; magic number with 3+ occurrences; identical error handling in 3+ places |
| Medium | 5-10 lines duplicated; magic number with 2 occurrences; repeated pattern with minor variations |
| Low | Short (3-5 line) duplication; single magic number that is locally obvious |

## DO

* Search the full codebase for other occurrences of duplicated patterns, not just within the diff. The diff may introduce a duplicate of existing code.
* Consider whether extraction would improve or harm readability. Some duplication is acceptable when the alternative is an overly abstract helper.
* Check existing shared utilities in the codebase. The project may already have a helper for the duplicated logic.

## DON'T

* Flag test code for DRY when the duplication improves test readability and independence.
* Suggest extracting code that differs in subtle but important ways (e.g., error handling that should intentionally vary by context).
* Flag duplication that exists across different compile-time configurations (`#if HAS_X` blocks) when both paths need to be independent.
* Flag Arduino `.ino` / `.cpp` patterns that repeat across board-specific files by design.
