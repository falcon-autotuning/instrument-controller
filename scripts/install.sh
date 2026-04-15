#!/bin/bash
# ============================================================================
# Falcon Instrument Controller Installer (Bash wrapper)
# Supports: Linux (native), Windows (via pwsh if available)
#
# Usage:
#   Linux:   bash install.sh
#   Windows: bash install.sh (requires Git Bash or WSL)
# ============================================================================

set -e

RELEASE_VERSION="${1:-v1.0.0}"
SHOW_HELP="${2:-}"

if [[ "$SHOW_HELP" == "-h" ]] || [[ "$SHOW_HELP" == "--help" ]]; then
  cat <<EOF
Falcon Instrument Controller Installer

Usage:
  bash install.sh [VERSION]

Arguments:
  VERSION    Release version to install (default: v1.0.0)

Examples:
  bash install.sh
  bash install.sh v1.0.1

Environment Variables:
  FALCON_INSTALL_DIR    Override installation directory

For Windows, use: powershell -ExecutionPolicy Bypass -File install.ps1
EOF
  exit 0
fi

# Detect platform
UNAME_S="$(uname -s)"
IS_WINDOWS=0

case "$UNAME_S" in
MINGW* | MSYS* | CYGWIN*)
  IS_WINDOWS=1
  ;;
Linux)
  IS_WINDOWS=0
  ;;
*)
  echo "❌ Unsupported platform: $UNAME_S"
  exit 1
  ;;
esac

# Set default install directory
if [ -z "$FALCON_INSTALL_DIR" ]; then
  if [ $IS_WINDOWS -eq 1 ]; then
    INSTALL_DIR="C:/falcon"
  else
    INSTALL_DIR="/opt/falcon"
  fi
else
  INSTALL_DIR="$FALCON_INSTALL_DIR"
fi

# Repository info
REPO_OWNER="falcon-autotuning"
REPO_NAME="instrument-controller"
RELEASE_URL="https://github.com/$REPO_OWNER/$REPO_NAME/releases/download/$RELEASE_VERSION"

if [ $IS_WINDOWS -eq 1 ]; then
  PACKAGE_FILE="falcon-instrument-controller-${RELEASE_VERSION}-win64.zip"
else
  PACKAGE_FILE="falcon-instrument-controller-${RELEASE_VERSION}-Linux.tar.gz"
fi

PACKAGE_URL="$RELEASE_URL/$PACKAGE_FILE"

echo "🔧 Falcon Instrument Controller Installer"
echo "=========================================="
echo ""
echo "📍 Installation Directory: $INSTALL_DIR"
echo "📦 Platform: $([ $IS_WINDOWS -eq 1 ] && echo 'Windows (Git Bash)' || echo 'Linux')"
echo "📥 Downloading: $PACKAGE_FILE"
echo ""

# Create install directory
mkdir -p "$INSTALL_DIR"

# Download package
TEMP_FILE="$INSTALL_DIR/installer_temp"

echo "⏳ Downloading from: $PACKAGE_URL"
if ! curl -fSL "$PACKAGE_URL" -o "$TEMP_FILE"; then
  echo "❌ Download failed"
  exit 1
fi

echo "✅ Download complete"

# Extract
echo "📦 Extracting..."
if [ $IS_WINDOWS -eq 1 ]; then
  # Windows: use PowerShell or 7z if available
  if command -v pwsh &>/dev/null; then
    pwsh -Command "Expand-Archive -Path '$TEMP_FILE' -DestinationPath '$INSTALL_DIR' -Force"
  elif command -v 7z &>/dev/null; then
    7z x "$TEMP_FILE" -o"$INSTALL_DIR" -y
  else
    echo "❌ No extraction tool found (requires PowerShell Core or 7-Zip)"
    rm -f "$TEMP_FILE"
    exit 1
  fi
else
  # Linux: use tar
  tar -xzf "$TEMP_FILE" -C "$INSTALL_DIR" --strip-components=1
fi

rm -f "$TEMP_FILE"
echo "✅ Extraction complete"

# Display post-install instructions
echo ""
echo "✅ Installation successful!"
echo "📍 Location: $INSTALL_DIR"
echo ""
echo "📋 Next steps:"
echo ""

if [ $IS_WINDOWS -eq 1 ]; then
  echo "  1. Add to your PATH (PowerShell):"
  echo "     \$env:PATH = \"$INSTALL_DIR\\bin;\" + \$env:PATH"
  echo ""
  echo "  2. Or cmd.exe:"
  echo "     set PATH=$INSTALL_DIR\\bin;%PATH%"
else
  echo "  1. Add to ~/.bashrc or ~/.zshrc:"
  echo "     export PATH=\"$INSTALL_DIR/bin:\$PATH\""
  echo "     export LD_LIBRARY_PATH=\"$INSTALL_DIR/lib:\$LD_LIBRARY_PATH\""
  echo "     export PKG_CONFIG_PATH=\"$INSTALL_DIR/lib/pkgconfig:\$PKG_CONFIG_PATH\""
  echo ""
  echo "  2. Or source the setup script:"
  echo "     source $INSTALL_DIR/setup-falcon-env.sh"
fi

echo ""
echo "📖 For more info, see: $INSTALL_DIR/FALCON_DEPENDENCIES.txt"
