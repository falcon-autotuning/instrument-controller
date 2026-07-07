# =============================================================================
# prepare-release.ps1
# Automates building, packaging, and uploading the release assets to GitHub.
#
# Requirements:
#   - cmake
#   - GitHub CLI (gh) installed and authenticated: run 'gh auth login'
#
# Usage:
#   .\scripts\prepare-release.ps1 <tag-name> [build-preset]
#   Example:
#   .\scripts\prepare-release.ps1 v0.1.1-alpha windows-clang-release
# =============================================================================

param (
    [Parameter(Mandatory=$true)]
    [string]$Tag,
    [string]$Preset = "windows-clang-release"
)

$ErrorActionPreference = "Stop"

# Verify GitHub CLI
if (-not (Get-Command "gh" -ErrorAction SilentlyContinue)) {
    Write-Error "Error: GitHub CLI ('gh') is not installed."
}

# Run auth status check
& gh auth status
if ($LASTEXITCODE -ne 0) {
    Write-Error "Error: gh is not authenticated. Run 'gh auth login'."
}

$RepoRoot = Resolve-Path "$PSScriptRoot\.."
$BuildDir = "$RepoRoot\build"

Write-Host "Starting release preparation for version: $Tag"
Write-Host "--------------------------------------------------"

# 1. Build project
Write-Host "Building project with preset: $Preset..."
Set-Location $RepoRoot
cmake --preset $Preset
cmake --build --preset $Preset

$ActualBuildDir = "$RepoRoot\build\$Preset"
if (-not (Test-Path $ActualBuildDir)) {
    $ActualBuildDir = "$RepoRoot\build"
}

# 2. Run CPack
Write-Host "Running CPack..."
Set-Location $ActualBuildDir
& cpack -G ZIP -C Release -D CPACK_PACKAGE_FILE_NAME="instrument-controller-${Tag}-Windows-AMD64"

$PackageFile = Get-Item "$ActualBuildDir\instrument-controller-${Tag}-Windows-AMD64.zip"

if ($null -eq $PackageFile) {
    Write-Error "Error: CPack zip archive not found."
}

Write-Host "Package generated: $($PackageFile.FullName)"

# 3. Create release and upload asset
Write-Host "Uploading to GitHub releases..."
$ReleaseExists = $true
try {
    & gh release view $Tag 2>$null
} catch {
    $ReleaseExists = $false
}

if (-not $ReleaseExists) {
    Write-Host "Creating new draft release for tag $Tag..."
    & gh release create $Tag --draft --title "Release $Tag" --notes "Pre-release version $Tag"
}

Write-Host "Uploading package asset..."
& gh release upload $Tag $PackageFile.FullName --clobber

Write-Host "--------------------------------------------------"
Write-Host "Release $Tag completed."
& gh release view $Tag --web
