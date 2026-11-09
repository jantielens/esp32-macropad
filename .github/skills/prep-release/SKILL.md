---
name: prep-release
description: "Prepare a release version, curate changelog notes, validate tests, and create or update its PR - Brought to you by jantielens/esp32-macropad"
argument-hint: "[version=X.Y.Z] [base=...]"
disable-model-invocation: true
---

# Prep Release

## Overview

Prepare a release for review, not publication. Confirm the version, curate the
release notes, validate the completed change set, and create or update its pull
request using the current PR guidance.

Do not merge, create or push tags, publish a GitHub Release, or trigger the
release workflow. Do not commit or push without explicit user authorization;
invoking this skill does not bypass existing approval gates.

## Prerequisites

* Use an editing-capable agent with Git, authenticated GitHub access, and the
  project's documented host-test and build prerequisites on Linux or WSL.
* Read the [agent guidelines](../../instructions/agent-guidelines.instructions.md),
  [user documentation contract](../../instructions/user-documentation.instructions.md),
  and relevant portions of the
  [build and release guide](../../../docs/dev/build-and-release-process.md).
* Locate the current pull-request prompt or skill and its referenced instructions.
  Prefer repository-specific guidance and template conventions over generic
  defaults. If the PR workflow is unavailable, report the blocker rather than
  inventing a replacement.

## Quick start

Invoke `/prep-release`, optionally with `version=X.Y.Z` and `base=...`.
Without a version, propose a semantic version from the release changes and ask
the user to confirm it. Without a base, resolve the existing PR's base or the
repository's default branch; ask if the intended release target is ambiguous.

Run the following workflow in order. Resolve approval gates in the conversation,
not through guessed defaults.

## Release preparation

### Establish scope and confirm the version

1. Inspect the active branch, staged and unstaged changes, untracked files,
   remotes, and any existing PR for this branch. Preserve unrelated work; do
   not automatically stash, stage, commit, or discard it.
2. Read [the firmware version](../../../src/version.h) and
   [the changelog](../../../CHANGELOG.md). Fetch the target base and relevant
   release refs before comparing them. Do not update the branch without the
   approvals required by the existing Git and PR guidance.
3. Identify the previous published release in the intended release lineage.
   Compare its source with the proposed release source, including scoped local
   changes, to determine what is new. Separately inspect the complete PR diff
   against its base for the PR description. If the release baseline is
   ambiguous, ask before classifying changes.
4. Propose or verify a stable `X.Y.Z` version against the previous release,
   target base, existing version sections, and tags. Explain the semantic
   version choice, including breaking changes. Ask for confirmation before
   editing unless the user already explicitly approved that exact version.
   Do not assume prerelease suffixes work with the numeric firmware macros.
5. Summarize the intended edits and verification scope. Reuse an appropriate
   release branch or follow the existing branch workflow. Do not mix unrelated
   changes into a release branch without approval.

### Update version and curate release notes

1. Set `VERSION_MAJOR`, `VERSION_MINOR`, and `VERSION_PATCH` to the confirmed
   version only if needed. Do not bump again when rerunning the skill.
2. Move the pending `[Unreleased]` content into a single
   `[X.Y.Z] - YYYY-MM-DD` section using the preparation date. Leave an empty
   `[Unreleased]` section above it. Preserve all earlier published sections.
3. If this version already has an unpublished section, reconcile pending
   entries into that section without duplicating its heading or resetting its
   date unnecessarily. If the version is already published or tagged, stop
   and ask for a different version. If there are no pending entries, distinguish
   an already-prepared release from missing notes; do not invent changes.
4. Deduplicate entries by meaning and consolidate related details into clear,
   end-user-facing descriptions. Preserve useful links and distinct changes.
5. For features first introduced since the previous release, fold intermediate
   changes, fixes, and removals into the description of their final shipped
   behavior. Omit development churn and features added then removed before
   release. Keep genuine fixes to previously released behavior.
6. Preserve breaking changes, security-relevant changes, compatibility limits,
   removals of previously released behavior, and migration warnings. Do not
   hide these while consolidating entries or downgrade a breaking change to
   an ordinary feature.
7. Retain the changelog's existing categories. Within each category, put the
   most important end-user benefits first, followed by smaller improvements
   and developer-facing changes. Keep important upgrade warnings prominent.
8. Cross-check curated claims against the release diff and existing docs.
   Show the proposed release notes and resolve uncertain classifications
   with the user rather than silently deleting meaningful information.

### Reconcile documentation and validate

1. Perform the existing commit-ready documentation reconciliation against the
   completed scoped aggregate change set, including relevant local changes.
   Refresh required metadata and generated snapshots through existing tools.
   Update the smallest affected owners; report explicit scope exclusions.
2. Discover the current test commands and CI checks from
   [the build workflow](../../workflows/build.yml) and subsystem instructions.
   Run the full registered host suite, not only tests related to the version:

   ```bash
   cmake -S . -B build/host-tests
   cmake --build build/host-tests --parallel
   ctest --test-dir build/host-tests --output-on-failure
   ```

3. Run other applicable checks required by CI or the completed changes, including
   documentation and generated-output checks. Do not assume CTest covers every
   CI job. Record exact commands and their outcomes.
4. Apply the firmware build decision and board preference rules in the agent
   guidelines. A firmware version change requires firmware verification.
   Build only the preferred or explicitly selected board locally. Never run
   bare `./build.sh` without explicit all-board build authorization; use PR CI
   for the board matrix.
5. Stop on failed validation. Fix release-preparation errors within scope;
   ask before expanding into unrelated failures. Report missing dependencies,
   blocked checks, and pre-existing failures explicitly. Do not weaken tests
   or claim that skipped or unavailable checks passed.
6. Re-run affected validation after subsequent edits. Review the final scoped
   aggregate changes and confirm version/changelog agreement before requesting
   any needed commit or push approval.

### Create or update the PR and verify CI

1. Use the current pull-request workflow to prepare and create or update the PR.
   Read the [repository PR template](../../pull_request_template.md) and check
   for template alternatives before drafting. Preserve its headings, comments,
   checklist items, and ordering. Follow its version-based title convention
   rather than a conflicting generic title format.
2. Describe the complete PR diff, not just the release-preparation edits or
   the last commit. Supply the confirmed version, curated release highlights,
   breaking changes, migration guidance, and exact validation evidence to
   the existing workflow. Include Markdown changes in the analysis.
3. Honor required branch freshness, commit, push, and creation approvals.
   Reuse an existing open PR with the same head and base instead of creating
   a duplicate. Confirm before changing its base or replacing manually
   maintained content unrelated to this preparation.
4. After the final push, verify that the PR head matches the intended commit
   and inspect all applicable CI results for that exact SHA. Wait for required
   tests and the board matrix to pass before declaring the release ready.
   Missing, pending, skipped, cancelled, or failing applicable checks do not
   establish success. Do not rely on checks from an earlier commit.
5. Finish with the PR link, version, release-note summary, documentation
   impact, validation outcomes, and remaining blockers or manual hardware
   verification. State that merging, tagging, and publication were not done.
   If CI is still running, report preparation as pending, not release-ready.

## Troubleshooting

* If version, tag, branch, or changelog state conflicts, stop before making
  additional edits and ask which release state is intended.
* If GitHub access, PR tooling, or validation is unavailable, retain the
  prepared local changes and report the incomplete step and recovery action.
* On reruns, reuse the confirmed version, unpublished release section, and PR.
  Reassess the current diff and validation evidence; do not assume earlier
  successful checks still cover the current source.

> Brought to you by jantielens/esp32-macropad
