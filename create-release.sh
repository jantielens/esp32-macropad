#!/usr/bin/env bash
#
# create-release.sh - Automate release preparation workflow
#
# Usage: ./create-release.sh <version> [release-title]
# Example: ./create-release.sh 0.0.5 "Improved logging and stability"
#
# This script automates the release preparation process:
# 1. Creates a release branch (release/vX.Y.Z)
# 2. Updates src/version.h with new version numbers
# 3. Updates CHANGELOG.md (moves [Unreleased] to [X.Y.Z] with today's date)
# 4. Commits changes
# 5. Pushes branch and provides PR creation URL
#
# After the PR is merged, you can tag and push:
#   git tag -a vX.Y.Z -m "Release vX.Y.Z"
#   git push origin vX.Y.Z

set -euo pipefail

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR"

VERSION="${1:-}"
RELEASE_TITLE="${2:-Release v${VERSION}}"

if [ -z "$VERSION" ]; then
  echo -e "${RED}Error: Version argument required${NC}" >&2
  echo "Usage: $0 <version> [release-title]" >&2
  echo "Example: $0 0.0.5 \"Improved logging and stability\"" >&2
  exit 1
fi

# Validate version format (X.Y.Z or X.Y.Z-suffix)
if ! [[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[a-zA-Z0-9.]+)?$ ]]; then
  echo -e "${RED}Error: Invalid version format${NC}" >&2
  echo "Expected format: X.Y.Z or X.Y.Z-suffix (e.g., 0.0.5 or 1.0.0-beta.1)" >&2
  exit 1
fi

# Parse version components
IFS='.' read -r MAJOR MINOR PATCH_WITH_SUFFIX <<< "$VERSION"
PATCH="${PATCH_WITH_SUFFIX%%-*}"  # Remove suffix if present

echo -e "${BLUE}=== ESP32 Release Preparation ===${NC}"
echo "Version: $VERSION"
echo "Title: $RELEASE_TITLE"
echo ""

# Check if on main branch
CURRENT_BRANCH=$(git branch --show-current)
if [ "$CURRENT_BRANCH" != "main" ]; then
  echo -e "${YELLOW}Warning: Not on main branch (currently on: $CURRENT_BRANCH)${NC}"
  read -p "Continue anyway? (y/N) " -n 1 -r
  echo
  if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "Aborted."
    exit 1
  fi
fi

# Check for uncommitted changes
if ! git diff-index --quiet HEAD --; then
  echo -e "${RED}Error: You have uncommitted changes${NC}" >&2
  echo "Please commit or stash your changes before creating a release." >&2
  exit 1
fi

# Pull latest changes
echo -e "${BLUE}Step 1: Pulling latest changes...${NC}"
git pull origin main

# Create release branch
RELEASE_BRANCH="release/v$VERSION"
echo -e "${BLUE}Step 2: Creating release branch $RELEASE_BRANCH...${NC}"
git checkout -b "$RELEASE_BRANCH"

# Update src/version.h
echo -e "${BLUE}Step 3: Updating src/version.h...${NC}"
VERSION_FILE="$PROJECT_ROOT/src/version.h"

if [ ! -f "$VERSION_FILE" ]; then
  echo -e "${RED}Error: $VERSION_FILE not found${NC}" >&2
  exit 1
fi

# Use sed to update version numbers
sed -i "s/^#define VERSION_MAJOR .*/#define VERSION_MAJOR $MAJOR/" "$VERSION_FILE"
sed -i "s/^#define VERSION_MINOR .*/#define VERSION_MINOR $MINOR/" "$VERSION_FILE"
sed -i "s/^#define VERSION_PATCH .*/#define VERSION_PATCH $PATCH/" "$VERSION_FILE"

echo "Updated version.h:"
grep "^#define VERSION_" "$VERSION_FILE"

# Update CHANGELOG.md
echo -e "${BLUE}Step 4: Updating CHANGELOG.md...${NC}"
CHANGELOG_FILE="$PROJECT_ROOT/CHANGELOG.md"

if [ ! -f "$CHANGELOG_FILE" ]; then
  echo -e "${RED}Error: $CHANGELOG_FILE not found${NC}" >&2
  exit 1
fi

# Get today's date in YYYY-MM-DD format
TODAY=$(date +%Y-%m-%d)

# Replace [Unreleased] with [VERSION] - DATE
sed -i "s/^## \[Unreleased\]/## [$VERSION] - $TODAY/" "$CHANGELOG_FILE"

# Add new [Unreleased] section at the top after "## [Unreleased]" header if it doesn't exist
if ! grep -q "^## \[Unreleased\]" "$CHANGELOG_FILE"; then
  # Find the line number of the first version header
  FIRST_VERSION_LINE=$(grep -n "^## \[$VERSION\]" "$CHANGELOG_FILE" | head -1 | cut -d: -f1)
  
  # Insert new Unreleased section before it
  sed -i "${FIRST_VERSION_LINE}i\\## [Unreleased]\\n\\n### Added\\n\\n### Changed\\n\\n### Fixed\\n\\n---\\n" "$CHANGELOG_FILE"
fi

echo "Updated CHANGELOG.md with version $VERSION - $TODAY"

# Show changes
echo ""
echo -e "${BLUE}Changes to be committed:${NC}"
git diff --stat

echo ""
echo -e "${YELLOW}Review the changes above.${NC}"
read -p "Commit these changes? (y/N) " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
  echo "Aborted. Branch $RELEASE_BRANCH created but not committed."
  echo "You can review changes and commit manually."
  exit 1
fi

# Commit changes
echo -e "${BLUE}Step 5: Committing changes...${NC}"
git add "$VERSION_FILE" "$CHANGELOG_FILE"
git commit -m "chore: bump version to $VERSION"

# Push branch
echo -e "${BLUE}Step 6: Pushing release branch...${NC}"
git push -u origin "$RELEASE_BRANCH"

# Get repository info for PR URL
REPO_URL=$(git config --get remote.origin.url | sed 's/\.git$//')
if [[ "$REPO_URL" == git@github.com:* ]]; then
  REPO_URL=$(echo "$REPO_URL" | sed 's|git@github.com:|https://github.com/|')
fi

# Generate PR URL
PR_URL="${REPO_URL}/compare/main...${RELEASE_BRANCH}?expand=1&title=Release%20v${VERSION}&body=Release%20version%20${VERSION}"

echo ""
echo -e "${GREEN}=== Release branch created successfully! ===${NC}"
echo ""
echo -e "${BLUE}Next steps:${NC}"
echo "1. Create PR: $RELEASE_BRANCH → main"
echo "   Open: $PR_URL"
echo ""
echo "2. After PR is reviewed and merged:"
echo "   git checkout main"
echo "   git pull"
echo "   git tag -a v$VERSION -m \"$RELEASE_TITLE\""
echo "   git push origin v$VERSION"
echo ""
echo "3. GitHub Actions will automatically create the release with binaries"
echo ""
