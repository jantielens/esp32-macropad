---
description: "Copilot agent guidelines — approval workflow, build verification, and documentation update checklist for significant changes"
applyTo: "**"
---

# Copilot Agent Guidelines

## Before Making Significant Changes

Before implementing significant changes or starting major work, the agent must:

1. **Create a concise summary** of the proposed changes including:
   - What will be changed and why
   - Which files will be affected
   - Expected impact on the project
   - Any potential risks or breaking changes
2. **Ask for user approval and/or feedback** and wait for confirmation
3. **Only proceed with implementation** after receiving user approval

## After Significant Changes

After every significant change, the agent must:

1. **Verify the changes by building** the code:
   - Run `./build.sh` to ensure the code compiles successfully
   - Check for any compilation errors or warnings
   - Only proceed if the build completes without errors

2. **Check if documentation needs updates** by reviewing:
   - `README.md` — Main project documentation
   - `docs/dev/web-portal.md` — Web portal and REST API guide
   - `docs/dev/display-touch-architecture.md` — Display/touch HAL and screen architecture
   - `docs/dev/scripts.md` — Script usage guide
   - `docs/dev/library-management.md` — Library management guide
   - `docs/dev/build-and-release-process.md` — Project branding, build system, and release workflow guide
   - `docs/dev/wsl-development.md` — WSL setup guide
   - `docs/first-time-setup.md` — User first-time setup guide
   - `docs/web-portal-guide.md` — User web portal guide
   - `docs/pad-editor-guide.md` — Pad editor, binding templates, widgets, and real-world examples
   - `.github/copilot-instructions.md` — Project instructions
   - `.github/workflows/build.yml` — CI/CD build pipeline
   - `.github/workflows/release.yml` — CI/CD release pipeline

3. **Update existing documentation** if changes affect documented behavior
4. **Before creating new documentation files**, ask the user first

## Build Verification

Always verify code changes by building:

```bash
./build.sh  # Must complete successfully after code changes
```

If the build fails:

- Review and fix compilation errors
- Check library dependencies in `arduino-libraries.txt`
- Verify Arduino code syntax and ESP32 compatibility

## Examples of Significant Changes

- Adding new scripts or tools
- Modifying build/upload/monitor workflows
- Changing project structure
- Adding new dependencies or requirements
- Updating CI/CD pipeline
- Changing library management approach

## Documentation Update Triggers

- New script added → Update `README.md` script table and `docs/dev/scripts.md`
- Library management changed → Update `docs/dev/library-management.md`
- Build workflow modified → Update `README.md` CI/CD section and `docs/dev/build-and-release-process.md`
- Board configuration system changed → Update `README.md` board configuration section and `docs/dev/build-and-release-process.md`
- Release workflow modified → Update `docs/dev/build-and-release-process.md` and `README.md` release section
- New requirement added → Update `README.md` prerequisites
- REST API endpoint added/changed → Update `docs/dev/web-portal.md` and `README.md` API table
- Web UI feature changed → Update `docs/dev/web-portal.md` features section and `docs/web-portal-guide.md`
- Display/touch driver added/changed → Update `docs/dev/display-touch-architecture.md` driver sections
- Screen management changed → Update `docs/dev/display-touch-architecture.md` screen lifecycle
- New version released → Update `CHANGELOG.md` with changes, update `src/version.h` with new version number
- Release process changed → Update `docs/dev/build-and-release-process.md` with new workflow
