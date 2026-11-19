# Build, package and deploy the portable build to a local install directory.
#
# Why a separate script: the daily loop here is always the same three steps
# (build-release -> package -> copy over the install), and doing it by hand
# invites two mistakes - forgetting to re-run rcc after touching
# launcher/resources/agents/*, and running robocopy with /PURGE (which would
# delete instances/, maps/ and accounts.json).
#
# This script never deletes anything: robocopy runs with /E only, so extra files
# in the install (data, configs, instances) are left untouched.
#
# Keep this file ASCII-only: Windows PowerShell 5.1 reads BOM-less scripts as ANSI.
#
# Usage (from the repository root):
#   powershell -File tools\deploy-local.ps1
#   powershell -File tools\deploy-local.ps1 -SkipBuild -SkipPackage   # deploy only
#   powershell -File tools\deploy-local.ps1 -Config Debug -InstallDir "D:\Atlas"
#
# Optional environment overrides:
#   ATLAS_INSTALL_DIR  default install directory (same as -InstallDir)
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Release',

    # Portable root of the installed launcher (the directory that holds atlas.exe).
    [string]$InstallDir = '',

    # Skip the cmake build (use the exe already in build\win-local).
    [switch]$SkipBuild,

    # Skip packaging (use the newest dist\Atlas-*-win64-*-release folder).
    [switch]$SkipPackage,

    # Print what would happen without building, packaging or copying.
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

if (-not $InstallDir) {
    if ($env:ATLAS_INSTALL_DIR) { $InstallDir = $env:ATLAS_INSTALL_DIR }
    else { $InstallDir = 'E:\Program File\AtlasLaucher' }
}
$InstallDir = [IO.Path]::GetFullPath($InstallDir)

function Write-Step($text) { Write-Output "==> $text" }

# ---------------------------------------------------------------------------
# 1) Preflight: target directory, running launcher, data kept in place
# ---------------------------------------------------------------------------
Write-Step "target: $InstallDir"

if (-not (Test-Path $InstallDir)) {
    throw "install directory not found: $InstallDir (pass -InstallDir or create it first)"
}
$targetExe = Join-Path $InstallDir 'atlas.exe'
if (-not (Test-Path $targetExe)) {
    throw "no atlas.exe in '$InstallDir' - this does not look like a portable install"
}

# Replacing a running exe fails with a sharing violation halfway through the copy,
# so stop early with a readable message instead.
$running = Get-Process atlas, atlas_filelink -ErrorAction SilentlyContinue
if ($running) {
    $names = ($running | ForEach-Object { "$($_.Name) ($($_.Id))" }) -join ', '
    throw "launcher is running: $names - close it and run this script again"
}

# portable.txt decides whether the install directory is also the data root.
# Without it the launcher writes instances/maps under %APPDATA%\Atlas, and a
# "deploy" would silently update the exe but not the data the user is looking at.
$portable = Join-Path $InstallDir 'portable.txt'
if (-not (Test-Path $portable)) {
    Write-Output "WARNING: no portable.txt in $InstallDir - data root may be %APPDATA%\Atlas"
}

$instances = Join-Path $InstallDir 'instances'
if (Test-Path $instances) {
    $count = (Get-ChildItem $instances -Directory -ErrorAction SilentlyContinue | Measure-Object).Count
    Write-Output "data: $count instance(s) will be preserved (robocopy /E, never /PURGE)"
}

if ($DryRun) {
    Write-Step 'dry run: stopping before build/package/copy'
    return
}

# ---------------------------------------------------------------------------
# 2) Build (Release by default; rcc re-embeds resources/agents/* on its own)
# ---------------------------------------------------------------------------
$preset = if ($Config -eq 'Release') { 'win-local-release' } else { 'win-local' }
$action = if ($Config -eq 'Release') { 'build-release' } else { 'build-debug' }
$builtExe = Join-Path $repo "build\win-local\$Config\atlas.exe"

if (-not $SkipBuild) {
    Write-Step "build: $preset"
    & (Join-Path $repo 'build\run-configure.ps1') -Action $action
    if ($LASTEXITCODE -ne 0) { throw "build failed (exit $LASTEXITCODE) - see build\build-$($action).log" }
}
else {
    Write-Step 'build: skipped'
}

if (-not (Test-Path $builtExe)) {
    throw "no $Config build at $builtExe - drop -SkipBuild or build it first"
}
$buildTime = (Get-Item $builtExe).LastWriteTime
Write-Output ("built exe: {0:N0} bytes, {1:yyyy-MM-dd HH:mm:ss}" -f (Get-Item $builtExe).Length, $buildTime)

# ---------------------------------------------------------------------------
# 3) Package (folder + zip under dist\)
# ---------------------------------------------------------------------------
if (-not $SkipPackage) {
    Write-Step "package: $Config"
    # package-windows.ps1 uses ErrorActionPreference=Stop and throws, so a failure
    # surfaces as an exception here; its trailing native calls may leave a stale
    # $LASTEXITCODE, so do not test that.
    & (Join-Path $repo 'tools\package-windows.ps1') -Config $Config
}
else {
    Write-Step 'package: skipped'
}

# The package folder name carries the launcher version; pick the newest one so a
# version bump does not require editing this script.
$suffix = if ($Config -eq 'Release') { '-win64-release' } else { '-win64-debug' }
$candidates = Get-ChildItem (Join-Path $repo 'dist') -Directory -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -like "Atlas-*$suffix" } |
    Sort-Object LastWriteTime -Descending
if (-not $candidates) {
    throw "no package folder under $repo\dist - run without -SkipPackage first"
}
$packageDir = $candidates[0].FullName
if ($candidates.Count -gt 1) {
    Write-Output "note: $($candidates.Count) package folders found, using the newest: $(Split-Path $packageDir -Leaf)"
}
$packageExe = Join-Path $packageDir 'atlas.exe'
if (-not (Test-Path $packageExe)) { throw "package is missing atlas.exe: $packageDir" }

# The packaged exe must be the one we just built - catching this here is cheaper
# than debugging an install that silently kept last week's binary.
$builtHash = (Get-FileHash $builtExe -Algorithm SHA256).Hash
$packageHash = (Get-FileHash $packageExe -Algorithm SHA256).Hash
if ($builtHash -ne $packageHash) {
    throw "package atlas.exe does not match the $Config build - re-run packaging"
}

# ---------------------------------------------------------------------------
# 4) Deploy: /E only. No /PURGE, no /MIR - data must survive.
# ---------------------------------------------------------------------------
Write-Step "deploy: $(Split-Path $packageDir -Leaf) -> $InstallDir"
robocopy $packageDir $InstallDir /E /NFL /NDL /NJH /NJS /NP | Out-Null
$rc = $LASTEXITCODE
# robocopy: 0 = nothing to do, 1-7 = files copied, >=8 = failure
if ($rc -ge 8) { throw "robocopy failed (exit $rc)" }

$installedHash = (Get-FileHash $targetExe -Algorithm SHA256).Hash
if ($installedHash -ne $packageHash) {
    throw "deployed atlas.exe does not match the package (copy was partial)"
}

$installed = Get-Item $targetExe
Write-Step ("done: {0} ({1:N0} bytes, {2:yyyy-MM-dd HH:mm:ss})" -f $targetExe, $installed.Length, $installed.LastWriteTime)
Write-Output "verify: launch an instance, then check logs\Atlas-0.log for 'Map launch plan:'"
if ($Config -eq 'Release') {
    Write-Output 'note: Release package carries the VC++ runtime; Debug needs Visual Studio installed'
}
