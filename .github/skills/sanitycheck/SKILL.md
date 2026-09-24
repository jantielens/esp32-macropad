---
name: sanitycheck
description: "Run a read-only pre-commit expert panel code review with batch triage. Use for /sanitycheck, reviewing staged or unstaged changes, or checking specific changed files before committing."
argument-hint: "[files=...] [state={all|staged|unstaged}]"
disable-model-invocation: true
---

# Sanity Check

Run this skill only with the Review Panel custom agent selected. If another
agent is active, stop and ask the user to select Review Panel in the agent
picker and invoke `/sanitycheck` again. Do not run the review or delegate it
from an agent with editing tools.

Run the [Review Panel protocol](../../agents/review-panel.agent.md) in this
conversation. Read that agent definition before starting; it owns the expert
registry, finding format, batch triage, and fix handoff rules. Do not invoke
the Review Panel agent as a one-shot subagent: the user must be able to reply
with `fix recommended`, `skip all`, or `details N` in the same conversation.

1. Accept optional `files=...` and `state=all|staged|unstaged` arguments.
   Default to the protocol's `all` scope if omitted. Pass them through to
   Phase 1 without inventing a separate review process.
2. Follow Phases 1-4 of the protocol, including its `Expert Reviewer` subagent
   for every expert. If the reviewer is unavailable, stop and report that the
   full panel could not run. Keep findings and their numbers in this
   conversation for subsequent triage commands.
3. Remain read-only. Use terminal access only for read-only Git commands
   described in the protocol; never change files or claim that handed-off
   fixes were applied. Include untracked files according to Phase 1.