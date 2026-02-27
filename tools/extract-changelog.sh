#!/usr/bin/env bash
#
# extract-changelog.sh - Extract version-specific section from CHANGELOG.md
#
# Usage: ./extract-changelog.sh <version>
# Example: ./extract-changelog.sh 0.0.5
#
# Extracts the changelog section for the specified version from CHANGELOG.md.
# Assumes Keep a Changelog format with version headers like: ## [0.0.5] - 2025-11-26

set -euo pipefail

VERSION="${1:-}"

if [ -z "$VERSION" ]; then
  echo "Error: Version argument required" >&2
  echo "Usage: $0 <version>" >&2
  echo "Example: $0 0.0.5" >&2
  exit 1
fi

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CHANGELOG="$PROJECT_ROOT/CHANGELOG.md"

if [ ! -f "$CHANGELOG" ]; then
  echo "Error: CHANGELOG.md not found at $CHANGELOG" >&2
  exit 1
fi

# Extract the section for this version
# Matches from ## [VERSION] to the next ## or end of file
awk -v version="$VERSION" '
  # Match the version header
  /^## \[/ {
    # Check if this is our version
    if ($0 ~ "\\[" version "\\]") {
      in_section = 1
      next  # Skip the header line itself
    } else if (in_section) {
      # We hit the next version section, stop
      exit
    }
  }
  
  # Print lines while in our section
  in_section {
    # Stop at the horizontal rule separator
    if ($0 ~ /^---$/) {
      exit
    }
    print
  }
' "$CHANGELOG" | sed '/^[[:space:]]*$/d' | head -n -0

# If no content was extracted, exit with error
if [ "${PIPESTATUS[0]}" -ne 0 ]; then
  exit 1
fi
