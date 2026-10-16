---
description: "Use when writing user guides, examples, or board READMEs, or reconciling documentation before a commit or ready-to-commit handoff. Covers topic ownership, short-page patterns, hardware boundaries, and final checks."
applyTo: "docs/user/**/*.md,docs/samples/**,src/boards/*/README.md"
---

# User Documentation

Apply these patterns when editing documentation and at the
[commit-ready reconciliation](agent-guidelines.instructions.md#commit-ready-documentation-reconciliation).
For implementation tasks, track documentation impact without repeatedly
rewriting narrative guides during development. Runtime metadata, schemas,
fixtures, and their required tests are not deferred documentation.

## Topic ownership

* Start at [the user guide](../../docs/user/README.md) and find the smallest
  existing topic that owns the task. Link to that owner instead of teaching the
  same procedure in multiple guides.
* Keep indexes short and organized by user intent. Add a topic only for a distinct
  task without a suitable owner; follow the existing approval requirement for
  new documentation files. Update its index and relevant incoming links.
* Keep substantial scenarios in `docs/user/examples/`. Link to feature-owned
  syntax instead of repeating its reference.
* Put exact board connections, pinouts, power hazards, revisions, hardware limits,
  and physical recovery controls in `src/boards/<target>/README.md`.
  Shared hardware has one canonical family owner; variants link directly to it
  and describe their differences.
* Use capability-based language in generic guides. Board names belong in board
  READMEs and the [hardware index](../../docs/user/hardware.md), not generic
  feature topics. Evaluate and explain any necessary exception during review
  rather than copying a board-specific subsection into a shared guide.
* Keep implementation details in the appropriate existing developer reference.
  Do not make ordinary configuration depend on reading developer docs, or move
  necessary user-entered syntax there merely to shorten a page.

## Page patterns

* Give user topics YAML `title` and `description` fields; body headings start at
  H2. A frontmatter description does not replace visible introductory prose.
* Orient before instructing: explain what the feature is, why someone would use
  it, and an essential distinction needed to understand the task. Examples lead
  with the intended outcome; indexes establish scope; board references identify
  the hardware. Retain effective openings without filler or a sentence quota.
* Keep prerequisites, actual portal location/labels, steps, Save/Reboot behavior,
  expected result, and relevant recovery or safety advice close together.
  Verify behavior against the current implementation rather than old prose.
* Keep pages focused on a task. Split growing topics by distinct user needs,
  not arbitrary line counts; preserve complete workflows and warnings.
* Document current behavior and actionable limitations. Exclude implementation
  history, milestones, validation diaries, and planned-feature promises.
* Preserve complete meaningful user input: expressions, key sequences, list
  filters, tone patterns, command payloads, automation YAML, recipe catalogs,
  and brew templates. These differ from raw persisted pad/configuration schemas,
  NVS keys, and internal response dumps, which do not belong in user guides.
* Use live action/binding references and MCP tool metadata for exhaustive
  capability-dependent catalogs. Explain concepts and useful examples rather
  than maintaining parallel lists of every key or tool.
* Follow Markdown and terminology instructions, use relative links, and keep
  sample assets under their existing owner rather than copying them into pages.

## Final user-guide checks

At the commit-ready checkpoint, review only affected topics and their links
against the completed change set:

* Confirm conceptual introductions, current controls, save/restart semantics,
  safety warnings, examples, and generic/hardware ownership remain accurate.
* Check changed paths, heading anchors, images, and incoming links. Keep common
  configuration topics reachable within two steps from the user landing page.
* Validate changed examples with existing parsers/checks where available. Use
  focused link/Markdown checks; do not invent a documentation framework or
  source-spelling test solely for a prose edit.
* Reconcile generated documentation snapshots using their existing generators
  and subsystem contracts. Refresh earlier if required by implementation tests;
  never defer runtime metadata or weaken tests/CI to postpone a snapshot update.
* Report what changed, what needed no update, and any explicitly excluded stale
  references or unverified hardware facts. Do not claim hardware validation from
  prose, source inspection, or a build alone.
