# ============================================================================
# Falcon Instrument Controller Installer
# Supports: Windows (native), Linux (via PowerShell Core)
# 
# Usage:
#   Windows: powershell -ExecutionPolicy Bypass -File install.ps1
#   Linux:   pwsh install.ps1
# ============================================================================

param(
    [string]$ReleaseVersion = "v1.0.0",
    [string]$InstallDir = "",
    [switch]$Help
)

if ($Help) {
    Write-Host @"
Falcon Instrument Controller Installer

Usage:
  powershell -ExecutionPolicy Bypass -File install.ps1 [OPTIONS]

Options:
  -ReleaseVersion VERSION  Release version to install (default: v1.0.0)
  -InstallDir PATH         Installation directory (default: platform-specific)
  -Help                    Show this help message

Examples:
  powershell -ExecutionPolicy Bypass -File install.ps1 -ReleaseVersion v1.0.1
  pwsh install.ps1 -InstallDir /opt/falcon
"@
    exit 0
}

# Detect platform
$IsLinux = [System.Runtime.InteropServices.RuntimeInformation]::IsOSPlatform([System.Runtime.InteropServices.OSPlatform]::Linux)
$IsWindows = [System.Runtime.InteropServices.RuntimeInformation]::IsOSPlatform([System.Runtime.InteropServices.OSPlatform]::Windows)

if (-not ($IsWindows -or $IsLinux)) {
    Write-Error "❌ Unsupported platform"
    exit 1
}

# Set default install directory based on platform
if ([string]::IsNullOrEmpty($InstallDir)) {
    if ($IsWindows) {
        $InstallDir = "C:\falcon"
    } else {
        $InstallDir = "/opt/falcon"
    }
}

# Create install directory
if (-not (Test-Path $InstallDir)) {
    Write-Host "📁 Creating directory: $InstallDir"
    New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
}

# Determine package based on platform
$RepoOwner = "falcon-autotuning"
$RepoName = "instrument-controller"
$ReleaseUrl = "https://github.com/$RepoOwner/$RepoName/releases/download/$ReleaseVersion"

if ($IsWindows) {
    $PackageFile = "falcon-instrument-controller-$ReleaseVersion-win64.zip"
    $PackageUrl = "$ReleaseUrl/$PackageFile"
} else {
    $PackageFile = "falcon-instrument-controller-$ReleaseVersion-Linux.tar.gz"
    $PackageUrl = "$ReleaseUrl/$PackageFile"
}

Write-Host "🔧 Falcon Instrument Controller Installer"
Write-Host "=========================================="
Write-Host ""
Write-Host "📍 Installation Directory: $InstallDir"
Write-Host "📦 Platform: $(if ($IsWindows) { 'Windows' } else { 'Linux' })"
Write-Host "📥 Downloading: $PackageFile"
Write-Host ""

# Download package
$TempFile = Join-Path $env:TEMP $PackageFile

try {
    Write-Host "⏳ Downloading from: $PackageUrl"
    
    # Use TLS 1.2+
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
    
    # Download with progress
    $ProgressPreference = 'Continue'
    Invoke-WebRequest -Uri $PackageUrl -OutFile $TempFile -ErrorAction Stop
    
    Write-Host "✅ Download complete"
} catch {
    Write-Error "❌ Download failed: $_"
    exit 1
}

# Extract based on platform
try {
    if ($IsWindows) {
        Write-Host "📦 Extracting ZIP..."
        Expand-Archive -Path $TempFile -DestinationPath $InstallDir -Force
    } else {
        Write-Host "📦 Extracting TAR.GZ..."
        # Use tar command (available on modern Linux)
        tar -xzf $TempFile -C $InstallDir --strip-components=1
    }
    
    Write-Host "✅ Extraction complete"
} catch {
    Write-Error "❌ Extraction failed: $_"
    Remove-Item $TempFile -Force
    exit 1
}

# Cleanup
Remove-Item $TempFile -Force

# Display post-install instructions
Write-Host ""
Write-Host "✅ Installation successful!"
Write-Host "📍 Location: $InstallDir"
Write-Host ""
Write-Host "📋 Next steps:"

if ($IsWindows) {
    Write-Host ""
    Write-Host "  1. Add to your PATH (PowerShell):"
    Write-Host "     `$env:PATH = `"$InstallDir\bin;`" + `$env:PATH"
    Write-Host ""
    Write-Host "  2. Or add CMake prefix (PowerShell):"
    Write-Host "     `$env:CMAKE_PREFIX_PATH = `"$InstallDir\lib\cmake;`" + `$env:CMAKE_PREFIX_PATH"
    Write-Host ""
    Write-Host "  3. For cmd.exe:"
    Write-Host "     set PATH=$InstallDir\bin;%PATH%"
} else {
    Write-Host ""
    Write-Host "  1. Add to ~/.bashrc or ~/.zshrc:"
    Write-Host "     export PATH=`"$InstallDir/bin:`$PATH`""
    Write-Host "     export LD_LIBRARY_PATH=`"$InstallDir/lib:`$LD_LIBRARY_PATH`""
    Write-Host "     export PKG_CONFIG_PATH=`"$InstallDir/lib/pkgconfig:`$PKG_CONFIG_PATH`""
    Write-Host ""
    Write-Host "  2. Or source the setup script:"
    Write-Host "     source $InstallDir/setup-falcon-env.sh"
}

Write-Host ""
Write-Host "📖 For more info, see: $InstallDir/FALCON_DEPENDENCIES.txt"
