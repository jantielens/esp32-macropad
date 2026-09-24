---
title: AI customization review tracker
description: Temporary inventory and progress tracker for repository AI development customizations.
ms.date: 2026-09-24
---

## Scope and status

Read-only audit completed on 2026-09-24. The first two P1 items are complete. This file is
temporary; remove it after the decisions and work are recorded elsewhere.
Personal and extension-provided customizations are outside the repository inventory.

| Type                  | Repo inventory                                         |
|-----------------------|--------------------------------------------------------|
| Project instructions  | 1 [Copilot instructions](.github/copilot-instructions.md) |
| Scoped instructions   | 9 domain files and 9 review-expert files under [.github/instructions](.github/instructions) |
| Custom agents         | [Review Panel](.github/agents/review-panel.agent.md) and [Expert Reviewer](.github/agents/expert-reviewer.agent.md) |
| Prompt files          | None found                                             |
| Repo-local skills     | 1 [sanitycheck](.github/skills/sanitycheck/SKILL.md)  |
| Repo-local hooks      | None found                                             |
| MCP client config     | None found; the firmware MCP server is a separate product feature |

## Priority work

* [x] P1: Finish migration to the [sanitycheck skill](.github/skills/sanitycheck/SKILL.md).
  The old prompt was removed at the user's request. Verify `/sanitycheck`
  discovery, read-only review, and a follow-up triage command in the selected
  Agent Host. Skill instructions alone cannot restrict tools; select the
  Review Panel agent for a tool-restricted review.
* [x] P1: Remove `applyTo: "**"` from the nine
  [review-expert instructions](.github/instructions/review-experts).
  Have the review workflow explicitly load the relevant lenses; keep universal
  conventions such as terminology automatically attached. Confirm review still
  loads each selected lens and unrelated edits do not load review-only rules.
* [ ] P1: Update the [configuration settings checklist](.github/instructions/adding-config-settings.instructions.md)
  to point at [web_portal_config.cpp](src/app/web_portal_config.cpp),
  [saveFragmentConfig()](src/app/web/portal_fragment_init.js), and
  [portal_config.js](src/app/web/portal_config.js). Account for
  `registerConfigFields()` and the current fragment architecture. Verify the
  instructions against one representative setting's load, save, and UI paths.
* [ ] P2: Refresh the [web portal instructions](.github/instructions/web-portal.instructions.md)
  where they still describe the old page structure. Check each named route,
  fragment, and bundle convention against the current implementation.
* [ ] P2: Test both [review agents](.github/agents/review-panel.agent.md) in the
  Customizations editor. Confirm the top-level reviewer is discovered, tool names
  resolve, and the workflow handles untracked files. Replace self-targeting
  triage handoffs and copy/paste fix handoffs only after testing an explicit,
  user-confirmed transition to an editing agent.
* [ ] P2: Shorten [project-wide instructions](.github/copilot-instructions.md).
  Keep non-obvious architecture rules and build commands; link to maintained
  documentation or on-demand instructions for subsystem details. Test a common
  coding task for missing context before and after the reduction.
* [ ] P3: Consider a repo skill for the multi-step board build or release flow.
  Pilot a hook only for a cheap, deterministic check, using the selected
  harness's hook schema; avoid running a firmware build after every edit.
* [ ] P3: Add a small validation routine: inspect the Customizations editor,
  References, and Agent Debug Logs with representative tasks for each changed
  customization. Record discovery failures and conflicting instructions here.

## Decisions and progress

| Date       | Item | State   | Decision or result                          |
|------------|------|---------|---------------------------------------------|
| 2026-09-24 | All  | Audited | Recommendations recorded; no changes made. |
| 2026-09-24 | P1   | In progress | Skill added; old prompt removed by request. Review agent selection is required, untracked files are included for all/unstaged, and incomplete expert coverage is reported. Agent Host test pending. |
| 2026-09-24 | P1   | In progress | Moved the unchanged private Code Reviewer to the top-level agents directory after a live review reported it unavailable. Retry Agent Host discovery and a review before closing P1. |
| 2026-09-24 | P1   | In progress | Replaced reviewer tool names with `read` and `search`. A direct Code Reviewer smoke test read the architecture instructions successfully; full nine-expert `/sanitycheck` rerun remains pending. |
| 2026-09-24 | P1   | Complete | User confirmed `/sanitycheck` produced full-panel findings and selected a fix for follow-up. |
| 2026-09-24 | P1   | Complete | Removed global `applyTo` from all nine expert lenses. The reviewer read an explicit lens after the change; an unrelated task reported no expert bodies loaded automatically. |
| 2026-09-24 | P1   | Complete | Renamed the parent to Review Panel and its private worker to Expert Reviewer; check discovery under the new names. |

## References

* [VS Code customization overview](https://code.visualstudio.com/docs/agent-customization/overview)
* [Prompt files and Agent Host compatibility](https://code.visualstudio.com/docs/agent-customization/prompt-files)
* [Agent skills](https://code.visualstudio.com/docs/agent-customization/agent-skills)
* [Custom instructions](https://code.visualstudio.com/docs/agent-customization/custom-instructions)
* [Custom agents and handoffs](https://code.visualstudio.com/docs/agent-customization/custom-agents)
* [Harness-specific hooks](https://code.visualstudio.com/docs/agent-customization/hooks)