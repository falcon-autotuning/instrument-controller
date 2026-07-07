#!/usr/bin/env bash
# =============================================================================
# prepare-release.sh
# Automates building, packaging, and uploading the release assets to GitHub.
#
# Requirements:
#   - git, cmake, make
#   - GitHub CLI (gh) installed and authenticated: run 'gh auth login'
#
# Usage:
#   ./scripts/prepare-release.sh <tag-name> [build-preset]
#   Example:
#   ./scripts/prepare-release.sh v0.1.1-alpha linux-clang-release
# =============================================================================

set -euo pipefail

TAG="${1:-}"
PRESET="${2:-linux-clang-release}"

if [[ -z "$TAG" ]]; then
    echo "Error: Release tag version is required."
    echo "Usage: $0 <tag-name> [build-preset]"
    exit 1
fi

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$REPO_ROOT/build"

# Verify GitHub CLI is installed and authenticated
if ! command -v gh &> /dev/null; then
    echo "Error: GitHub CLI ('gh') is not installed."
    exit 1
fi

if ! gh auth status &> /dev/null; then
    echo "Error: gh is not authenticated. Run 'gh auth login'."
    exit 1
fi

echo "Starting release preparation for version: $TAG"
echo "--------------------------------------------------"

# 1. Clean build directory and run CMake configure/build
echo "Building project with preset: $PRESET..."
cd "$REPO_ROOT"
cmake --preset "$PRESET"
cmake --build --preset "$PRESET"

# CPack requires running inside the configured build directory
# We extract the build directory path from CMakePreset or look for it
ACTUAL_BUILD_DIR="$REPO_ROOT/build/$PRESET"
if [[ ! -d "$ACTUAL_BUILD_DIR" ]]; then
    ACTUAL_BUILD_DIR="$REPO_ROOT/build"
fi

# 2. Package with CPack
echo "Running CPack..."
cd "$ACTUAL_BUILD_DIR"
cpack -G TGZ -C Release -D CPACK_PACKAGE_FILE_NAME="instrument-controller-${TAG}-Linux-x86_64"

# Locate the package
PACKAGE_FILE="instrument-controller-${TAG}-Linux-x86_64.tar.gz"

if [[ -z "$PACKAGE_FILE" || ! -f "$PACKAGE_FILE" ]]; then
    echo "Error: CPack archive not found."
    exit 1
fi

echo "Package generated: $PACKAGE_FILE"

# 3. Create release and upload asset
echo "Uploading to GitHub releases..."
if ! gh release view "$TAG" &>/dev/null; then
    echo "Creating new draft release for tag $TAG..."
    gh release create "$TAG" --draft --title "Release $TAG" --notes "Pre-release version $TAG"
fi

echo "Uploading package asset..."
gh release upload "$TAG" "$PACKAGE_FILE" --clobber

echo "--------------------------------------------------"
echo "Release $TAG completed."
echo "Release URL: $(gh release view "$TAG" --web)"
