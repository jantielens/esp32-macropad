---
name: sanitycheck
description: Pre-commit expert panel code review — auto-engages all experts, batch triage
agent: Code Review
argument-hint: "[files=...] [state={all|staged|unstaged}]"
---

# Sanity Check

Pre-commit code review driven by the full panel of expert reviewers. Every expert analyzes changes through its specific quality lens. Experts with no findings stay silent. All findings are presented in one table for batch fix/skip decisions.

## Inputs

* ${input:files}: (Optional) Specific files or paths to review. Defaults to all uncommitted changes.
* ${input:state:all}: (Optional) Git state: `staged`, `unstaged`, or `all`.

## Requirements

1. Auto-engage all expert reviewers — no scope selection needed.
2. Present all findings in a single numbered table with a Recommended column.
3. Accept batch selection commands (`fix recommended`, `fix 1,3,5-8`, `skip all`, `details N`).
4. Apply selected fixes directly. Summarize all actions at the end.