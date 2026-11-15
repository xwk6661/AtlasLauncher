# Build a portable Windows package (folder + zip) from build/win-local.
#
# Why not CPack: the upstream CMake files already split the install into three
# components - Runtime (exe), bundle (Qt DLLs + windeployqt plugins) and portable
# (portable.txt). Installing all three with windeployqt is what the release CI does,
# so this script just drives that.
#
# Keep this file ASCII-only: Windows PowerShell 5.1 reads BOM-less scripts as ANSI.
#
# Usage (from the repository root):
#   powershell -File tools\package-windows.ps1 -Config Release
#   powershell -File tools\package-windows.ps1 -Config Debug
#
# Optional environment overrides:
#   ATLAS_CMAKE   full path to cmake.exe
#   ATLAS_QT_BIN  Qt bin directory (the one that contains windeployqt.exe)
#
# Prerequisites: a configured build tree at build\win-local and that config already
# built (cmake --build --preset win-local[-release]).
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Release'
)

$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$build = Join-Path $repo 'build\win-local'

# Locate cmake: explicit override, then the VS2022 bundled copy, then PATH.
$cmake = $env:ATLAS_CMAKE
if (-not $cmake) {
    $cmake = 'D:\Program Files\Microsoft Visual Studio\2022\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}
if (-not (Test-Path $cmake)) {
    $cmd = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmd) { $cmake = $cmd.Source }
}
if (-not (Test-Path $cmake)) {
    throw 'cmake.exe not found - set ATLAS_CMAKE to its full path'
}

# Locate Qt: explicit override, then the path this machine uses.
$qtBin = $env:ATLAS_QT_BIN
if (-not $qtBin) { $qtBin = 'E:\Program File\Qt\6.8.3\msvc2022_64\bin' }
if (-not (Test-Path (Join-Path $qtBin 'windeployqt.exe'))) {
    throw "windeployqt.exe not found in '$qtBin' - set ATLAS_QT_BIN to the Qt bin directory"
}

# Same toolchain variables as the build: windeployqt / dumpbin need them, and this
# avoids Enter-VsDevShell (which calls reg.exe and gets blocked in some sandboxes).
# The helper lives in build\ and is untracked; when it is missing we rely on the
# caller's environment instead of failing.
$envScript = Join-Path $repo 'build\env-toolchain.ps1'
if (Test-Path $envScript) {
    . $envScript
}
$env:PATH = "$qtBin;$env:PATH"

$exe = Join-Path $build "$Config\atlas.exe"
if (-not (Test-Path $exe)) {
    $preset = if ($Config -eq 'Release') { 'win-local-release' } else { 'win-local' }
    throw "No $Config build at $exe - run: cmake --build --preset $preset"
}

$version = (Get-Item $exe).VersionInfo.FileVersion   # "12.0.0.0"
$version = ($version -replace '\.0$', '')            # -> "12.0.0"
if (-not $version) { $version = '0.0.0' }
$name = "Atlas-$version-win64-" + $Config.ToLowerInvariant()
$out = Join-Path $repo "dist\$name"
$zip = Join-Path $repo "dist\$name.zip"

if (Test-Path $out) { [System.IO.Directory]::Delete($out, $true) }
if (Test-Path $zip) { [System.IO.File]::Delete($zip) }
New-Item -ItemType Directory -Force -Path $out | Out-Null

# The bundle step (windeployqt) aborts with "Multiple conflicting paths found for
# Qt6Core.dll" when a Qt DLL also sits next to the exe. run-atlas-debug.cmd puts two
# there on purpose (for the elevated atlas_filelink.exe), so clear them first - that
# script recreates them on the next launch.
Get-ChildItem (Join-Path $build $Config) -Filter 'Qt6*.dll' -File |
    ForEach-Object { [System.IO.File]::Delete($_.FullName) }

# Order matters: Runtime (exe) must be installed before bundle (windeployqt looks for
# the exe inside the target directory), and portable.txt comes last.
& $cmake --install $build --config $Config --component Runtime --prefix $out
if ($LASTEXITCODE -ne 0) { throw 'install (Runtime) failed' }
& $cmake --install $build --config $Config --component bundle --prefix $out
if ($LASTEXITCODE -ne 0) { throw 'install (bundle) failed' }
& $cmake --install $build --config $Config --component portable --prefix $out
if ($LASTEXITCODE -ne 0) { throw 'install (portable) failed' }

# Release links the shared VC++ runtime (/MD). windeployqt runs with
# --no-compiler-runtime (upstream expects an installer to add the redist), so a zip
# that must work on a clean machine has to carry the CRT DLLs itself. Only the
# redistributable ones from VC\Redist are copied - never debug_nonredist.
if ($Config -eq 'Release') {
    $vsRoots = @(
        'D:\Program Files\Microsoft Visual Studio\2022',
        'C:\Program Files\Microsoft Visual Studio\2022'
    )
    $crtDir = $null
    foreach ($vsRoot in $vsRoots) {
        if (-not (Test-Path $vsRoot)) { continue }
        $crtDir = Get-ChildItem (Join-Path $vsRoot 'VC\Redist\MSVC') -Directory -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending |
            ForEach-Object { Join-Path $_.FullName 'x64\Microsoft.VC143.CRT' } |
            Where-Object { Test-Path $_ } |
            Select-Object -First 1
        if ($crtDir) { break }
    }
    if (-not $crtDir) {
        Write-Output 'WARNING: VC++ redistributable CRT not found - the package needs the VC++ runtime preinstalled.'
    } else {
        Get-ChildItem $crtDir -Filter '*.dll' -File | ForEach-Object { Copy-Item $_.FullName $out -Force }
    }
}

# The qtlogging.ini install rule hard-codes the configure-time prefix, so it can land
# in <repo>\install\. Copy it from the source tree instead and drop that folder.
Copy-Item (Join-Path $repo 'launcher\qtlogging.ini') $out -Force
$stray = Join-Path $repo 'install'
if (Test-Path $stray) {
    if ((Get-ChildItem $stray -Recurse -File | Measure-Object).Count -le 2) {
        [System.IO.Directory]::Delete($stray, $true)
    }
}

# Debug binaries link the MSVC debug CRT, which is not redistributable: the folder
# runs only on a machine with Visual Studio installed. Release is the shareable one.
if ($Config -eq 'Debug') {
    Write-Output 'NOTE: Debug build - needs MSVC debug runtime (Visual Studio) on the target machine.'
}

Compress-Archive -Path (Join-Path $out '*') -DestinationPath $zip -CompressionLevel Optimal
$size = [math]::Round((Get-Item $zip).Length / 1MB, 1)
Write-Output "folder: $out"
Write-Output "zip   : $zip ($size MB)"
