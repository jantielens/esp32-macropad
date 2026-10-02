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

Use explicit active-agent information when available; do not infer the selected
agent solely from tool visibility. If the agent identity is unavailable, ask
the user to confirm Review Panel is selected. Accept that confirmation for
this invocation and remain repository-read-only regardless of visible tools,
with only the protocol's temporary-evidence capture exception.

Run the [Review Panel protocol](../../agents/review-panel.agent.md) in this
conversation. Read that agent definition before starting; it owns the expert
registry, finding format, batch triage, and fix handoff rules. Do not invoke
the Review Panel agent as a one-shot subagent: the user must be able to reply
with `fix recommended`, `skip all`, or `details N` in the same conversation.

1. Accept optional `files=...` and `state=all|staged|unstaged` arguments.
   Default to the protocol's `all` scope if omitted. Pass them through to
   Phase 1 without inventing a separate review process. Do not infer `state`
   from terminal history, previous staging commands, or an empty index, and
   do not switch an explicitly selected state when its scope is empty.
2. Follow Phases 1-4 of the protocol, including evidence transport, pre-dispatch
   validation, bounded batches, coverage receipts, and an `Expert Reviewer`
   invocation for every expert-by-batch assignment. Do not invent a separate
   collection or batching process here. If the reviewer is unavailable, stop
   and report that the full panel could not run. Keep findings and their numbers
   in this conversation for subsequent triage commands.
3. Remain repository-read-only. Terminal access is limited to read-only Git
   collection and the protocol's narrow temporary-evidence capture exception.
   Never edit repository files or claim that handed-off fixes were applied.
   Include untracked files according to Phase 1. Reviewers remain read-only;
   artifact transport does not grant them editing or terminal tools.