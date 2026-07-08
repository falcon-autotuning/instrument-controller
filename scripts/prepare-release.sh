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

# Detect OS
if [[ "${OSTYPE:-}" == "msys" || "${OSTYPE:-}" == "cygwin" ]]; then
    IS_WINDOWS=true
    DEFAULT_PRESET="windows-clang-cl-release"
else
    IS_WINDOWS=false
    DEFAULT_PRESET="linux-clang-release"
fi

TAG="${1:-}"
PRESET="${2:-$DEFAULT_PRESET}"

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
ACTUAL_BUILD_DIR="$REPO_ROOT/build/$PRESET"
if [[ ! -d "$ACTUAL_BUILD_DIR" ]]; then
    ACTUAL_BUILD_DIR="$REPO_ROOT/build"
fi

# 2. Package with CPack
echo "Running CPack..."
cd "$ACTUAL_BUILD_DIR"

if [ "$IS_WINDOWS" = true ]; then
    cpack -G ZIP -C Release -D CPACK_PACKAGE_FILE_NAME="instrument-controller-${TAG}-Windows-AMD64"
    PACKAGE_FILE="instrument-controller-${TAG}-Windows-AMD64.zip"
else
    cpack -G TGZ -C Release -D CPACK_PACKAGE_FILE_NAME="instrument-controller-${TAG}-Linux-x86_64"
    PACKAGE_FILE="instrument-controller-${TAG}-Linux-x86_64.tar.gz"
fi

if [[ -z "$PACKAGE_FILE" || ! -f "$PACKAGE_FILE" ]]; then
    echo "Error: CPack archive not found."
    exit 1
fi

echo "Package generated: $PACKAGE_FILE"

# Prepare customized install.sh with current tag version
echo "Preparing customized install.sh..."
CUSTOM_INSTALL="$ACTUAL_BUILD_DIR/install.sh"
cp "$REPO_ROOT/scripts/install.sh" "$CUSTOM_INSTALL"
# Patch default release version in install.sh (replacing v0.1.1-alpha with current tag)
if [[ "$IS_WINDOWS" == true ]]; then
  # On Windows Git Bash, sed -i might need empty string for extension
  sed -i "s/RELEASE_VERSION=\"\${1:-v0.1.1-alpha}\"/RELEASE_VERSION=\"\${1:-${TAG}}\"/g" "$CUSTOM_INSTALL"
else
  sed -i "s/RELEASE_VERSION=\"\${1:-v0.1.1-alpha}\"/RELEASE_VERSION=\"\${1:-${TAG}}\"/g" "$CUSTOM_INSTALL"
fi

# 3. Create release and upload assets
echo "Uploading to GitHub releases..."
if ! gh release view "$TAG" &>/dev/null; then
    echo "Creating new draft release for tag $TAG..."
    gh release create "$TAG" --draft --title "Release $TAG" --notes "Pre-release version $TAG"
fi

echo "Uploading release assets..."
gh release upload "$TAG" "$PACKAGE_FILE" "$CUSTOM_INSTALL" --clobber

echo "--------------------------------------------------"
echo "Release $TAG completed."
echo "Release URL: $(gh release view "$TAG" --web)"
