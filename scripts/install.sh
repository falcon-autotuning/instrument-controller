#!/usr/bin/env bash
# ============================================================================
# Falcon Instrument Controller Installer
# Supports: Linux (native bash), Windows (Git Bash/WSL/MSYS2)
#
# Usage:
#   curl -fsSL https://github.com/falcon-autotuning/instrument-controller/releases/download/v1.0.0/install.sh | bash
#   Or with custom version:
#   bash install.sh v1.0.1
# ============================================================================

set -euo pipefail

# Configuration
RELEASE_VERSION="${1:-v0.1.1-alpha}"
REPO_OWNER="falcon-autotuning"
REPO_NAME="instrument-controller"
RELEASE_URL="https://github.com/$REPO_OWNER/$REPO_NAME/releases/download/$RELEASE_VERSION"

# Detect platform
detect_platform() {
  local uname_s
  uname_s="$(uname -s 2>/dev/null || echo "Unknown")"

  case "$uname_s" in
  Linux)
    echo "linux"
    ;;
  MINGW* | MSYS* | CYGWIN*)
    echo "windows"
    ;;
  *)
    echo "unsupported"
    ;;
  esac
}

PLATFORM="$(detect_platform)"

if [ "$PLATFORM" = "unsupported" ]; then
  echo "❌ Unsupported platform"
  exit 1
fi

# Platform-specific configuration
if [ "$PLATFORM" = "windows" ]; then
  PACKAGE_FILE="instrument-controller-${RELEASE_VERSION}-Windows-AMD64.zip"
  INSTALL_DIR="${FALCON_INSTALL_DIR:-/c/falcon}"
else
  PACKAGE_FILE="instrument-controller-${RELEASE_VERSION}-Linux-x86_64.tar.gz"
  INSTALL_DIR="${FALCON_INSTALL_DIR:-/opt/falcon}"
fi

PACKAGE_URL="$RELEASE_URL/$PACKAGE_FILE"

# Display info
echo "🔧 Falcon Instrument Controller Installer"
echo "=========================================="
echo ""
echo "📍 Installation Directory: $INSTALL_DIR"
echo "📦 Platform: $([ "$PLATFORM" = "windows" ] && echo "Windows" || echo "Linux")"
echo "📥 Package: $PACKAGE_FILE"
echo ""

# Create install directory
mkdir -p "$INSTALL_DIR" || {
  echo "❌ Failed to create installation directory: $INSTALL_DIR"
  exit 1
}

# Temp workspace
TMP_DIR="$(mktemp -d)" || {
  echo "❌ Failed to create temp directory"
  exit 1
}

cleanup() {
  rm -rf "$TMP_DIR"
}
trap cleanup EXIT

ARCHIVE_FILE="$TMP_DIR/package"

echo "⏳ Downloading..."
if ! curl -fsSL "$PACKAGE_URL" -o "$ARCHIVE_FILE"; then
  echo "❌ Download failed: $PACKAGE_URL"
  exit 1
fi

echo "📦 Extracting..."

if [ "$PLATFORM" = "windows" ]; then
  unzip -q -o "$ARCHIVE_FILE" -d "$TMP_DIR/extract" || {
    echo "❌ Extraction failed"
    exit 1
  }

  # Detect top-level directory
  TOP_DIR="$(find "$TMP_DIR/extract" -mindepth 1 -maxdepth 1 -type d | head -n 1)"

  if [ -n "$TOP_DIR" ]; then
    echo "📁 Flattening extracted directory..."
    cp -r "$TOP_DIR"/. "$INSTALL_DIR"/
  else
    echo "⚠️ No wrapper directory detected, copying directly..."
    cp -r "$TMP_DIR/extract"/. "$INSTALL_DIR"/
  fi

else
  tar --strip-components=1 -xzf "$ARCHIVE_FILE" -C "$INSTALL_DIR" || {
    echo "❌ Extraction failed"
    exit 1
  }
fi

echo "✅ Installation successful!"
echo "📍 Location: $INSTALL_DIR"
echo ""

if [ "$PLATFORM" = "windows" ]; then
  WINDOWS_INSTALL_DIR="$(cygpath -w "$INSTALL_DIR")"
fi

if [ "$PLATFORM" = "windows" ]; then
  echo "📋 Next steps"
  echo ""
  echo "Git Bash / MSYS2:"
  echo "  echo 'export PATH=\"$INSTALL_DIR/bin:\$PATH\"' >> ~/.bashrc"
  echo "  echo 'export PKG_CONFIG_PATH=\"$INSTALL_DIR/lib/pkgconfig:\$PKG_CONFIG_PATH\"' >> ~/.bashrc"
  echo "  source ~/.bashrc"
  echo ""

  echo "PowerShell (persistent user PATH):"
  printf '  [System.Environment]::SetEnvironmentVariable("Path", "%s\\bin;" + $env:Path, [System.EnvironmentVariableTarget]::User)\n' "$WINDOWS_INSTALL_DIR"
  echo ""

  echo "Optional (CMake projects):"
  printf '  [System.Environment]::SetEnvironmentVariable("CMAKE_PREFIX_PATH", "%s\\lib\\cmake;" + $env:CMAKE_PREFIX_PATH, [System.EnvironmentVariableTarget]::User)\n' "$WINDOWS_INSTALL_DIR"

else
  echo "📋 Next steps:"
  echo ""
  echo "Add to ~/.bashrc or ~/.zshrc:"
  echo "  export PATH=\"$INSTALL_DIR/bin:\$PATH\""
  echo "  export LD_LIBRARY_PATH=\"$INSTALL_DIR/lib:\$LD_LIBRARY_PATH\""
  echo "  export PKG_CONFIG_PATH=\"$INSTALL_DIR/lib/pkgconfig:\$PKG_CONFIG_PATH\""
  echo ""
  echo "Reload shell:"
  echo "  source ~/.bashrc"
fi

echo ""
echo "📖 For more info, see:"
echo "   $INSTALL_DIR/FALCON_DEPENDENCIES.txt"
