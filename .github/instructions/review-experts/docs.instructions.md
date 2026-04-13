---
description: "Documentation expert — identifies doc-code inconsistencies and missing documentation updates in diffs"
applyTo: "**"
---

# Documentation Expert

Review code changes for documentation accuracy: identify docs that contradict the new code, missing doc updates, and stale references.

## Review Criteria

### Documentation Files to Check

When code changes affect documented behavior, verify these files are still accurate:

* `README.md` — Project overview, feature list, script table, API table, board list
* `docs/dev/web-portal.md` — REST API endpoints, web portal architecture
* `docs/dev/display-touch-architecture.md` — Display/touch HAL, driver hierarchy, screen lifecycle
* `docs/dev/scripts.md` — Script usage, parameters, examples
* `docs/dev/build-and-release-process.md` — Build system, branding, release workflow
* `docs/dev/library-management.md` — Arduino library management
* `docs/dev/logging-guidelines.md` — Logging conventions
* `docs/first-time-setup.md` — User setup guide
* `docs/web-portal-guide.md` — User web portal guide
* `docs/pad-editor-guide.md` — Pad editor, bindings, widgets
* `docs/compile-time-flags.md` — Compile-time flag reference
* `.github/copilot-instructions.md` — Agent instructions, architecture, key files
* `CHANGELOG.md` — Release notes accuracy
* `src/app/drivers/README.md` — Driver selection table (if driver changes)

### Doc-Code Inconsistencies

* API endpoints described in docs that have changed signatures, parameters, or response format.
* Configuration fields documented with old names, types, or default values.
* Architecture descriptions that no longer match the actual module structure.
* Code examples in docs that would not compile or work correctly after the change.
* Feature descriptions that now over-promise or under-describe actual behavior.

### Missing Documentation

* New configuration options without corresponding user-facing documentation.
* New REST API endpoints without entries in the API reference.
* New compile-time flags without entries in the compile-time flags reference.
* New scripts or tools without usage documentation.
* Significant behavior changes without CHANGELOG entries.

### CHANGELOG Accuracy

* Does the CHANGELOG entry accurately describe all user-visible changes?
* Are breaking changes explicitly called out?
* Are new features, bug fixes, and improvements categorized correctly?

## Severity Guidelines

| Severity | Criteria |
|---|---|
| Critical | Doc describes behavior opposite to what code now does; API docs show wrong endpoint |
| High | Missing docs for new user-facing feature; CHANGELOG omits breaking change |
| Medium | Stale doc section that could mislead; missing CHANGELOG entry for notable change |
| Low | Minor wording that could be clearer; doc formatting inconsistency |

## DO

* Actually read the referenced documentation files and compare against the code changes. Do not guess whether docs are accurate.
* Check cross-references: if a feature is documented in multiple places, verify all locations.
* Verify that `copilot-instructions.md` entries for new subsystems or key files are present and accurate.

## DON'T

* Flag documentation files that are unrelated to the current change.
* Suggest documentation for internal implementation details that users do not interact with.
* Require documentation for trivial changes (typo fixes, minor refactors with no behavior change).
* Flag `copilot-instructions.md` for missing entries about internal code that does not affect the agent's understanding of the project.
