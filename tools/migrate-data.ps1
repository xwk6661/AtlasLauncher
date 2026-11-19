# Move an existing Atlas portable data directory (instances, map library, settings,
# ledger) to another portable root - typically a freshly unzipped release package.
#
# Why a script: a plain copy of the data directory is not enough.
#   * atlas.cfg stores absolute paths (InstanceDir / IconsDir / SkinsDir / JavaDir /
#     CentralModsDir / DownloadsDir ...). Copied as-is, the launcher keeps reading and
#     writing the OLD directory while the new copy sits unused.
#   * every instances\<map>\map.json stores an absolute archive.libraryCopy path, which
#     is what a reinstall uses when the original archive is gone.
#   * the ledger (metacache) is fine to copy (relative paths + md5), but it has to be
#     copied or libraries/ is re-downloaded in full.
# This script copies a whitelist, rewrites those absolute paths and then checks every
# instance against the shared resource library.
#
# Keep this file ASCII-only: Windows PowerShell 5.1 reads BOM-less scripts as ANSI.
#
# Usage (from the repository root):
#   powershell -File tools\migrate-data.ps1 -Target D:\Games\Atlas -DryRun
#   powershell -File tools\migrate-data.ps1 -Target D:\Games\Atlas
#   powershell -File tools\migrate-data.ps1 -Target D:\Games\Atlas -VerifyOnly
#
# Optional:
#   -Source <dir>      data root to copy from (default: build\win-local\Debug)
#   -IncludeLogs       also copy logs\ (old Atlas-*.log, useful for forensics)
#   -IncludeCache      also copy cache\ and downloads\ (covers, installers)
#   -Force             merge into a target that already has instances\
#   -Move              after a successful check, delete the copied items from the source
#                      (debug data root becomes UI-test only; binaries/dlls stay)
#
# The resource library (assets / libraries / java) is NOT copied: it is shared through
# Settings > Resources (SharedResourceDir). Point the new install at the same directory.
param(
    [string]$Source = '',
    [string]$Target = '',
    [switch]$DryRun,
    [switch]$VerifyOnly,
    [switch]$IncludeLogs,
    [switch]$IncludeCache,
    [switch]$Force,
    [switch]$Move
)

$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $Source) { $Source = Join-Path $repo 'build\win-local\Debug' }

if (-not $Target) { throw 'Missing -Target: the portable root that will hold the data (the folder with atlas.exe)' }
if (-not (Test-Path -LiteralPath $Target -PathType Container)) {
    throw "Target directory does not exist: $Target"
}

$src = (Resolve-Path -LiteralPath $Source).Path
$dst = (Resolve-Path -LiteralPath $Target).Path

function Normalize([string]$p) { return ($p -replace '\\', '/').TrimEnd('/') }

$srcFwd = Normalize $src
$dstFwd = Normalize $dst

# ---- guards ---------------------------------------------------------------
if ($srcFwd -ieq $dstFwd) { throw 'Source and target are the same directory' }
if ($dstFwd.StartsWith($srcFwd + '/', [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Target is inside the source ($dst) - copy would recurse"
}
if ($srcFwd.StartsWith($dstFwd + '/', [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Source is inside the target ($src) - pick the inner data root as -Source"
}

$missingMarkers = @('instances', 'atlas.cfg') | Where-Object { -not (Test-Path -LiteralPath (Join-Path $src $_)) }
if ($missingMarkers.Count -gt 0 -and -not $VerifyOnly) {
    throw "Source does not look like an Atlas data root: $src (missing $($missingMarkers -join ', '))"
}

Write-Output "source : $src"
Write-Output "target : $dst"
if ($DryRun) { Write-Output 'mode   : dry run (nothing is written)' }

# ---- what travels ---------------------------------------------------------
# assets / libraries / java are shared through SharedResourceDir and are NOT copied.
# libraries\ and JavaDir may exist as leftovers in a data root; ignore them on purpose.
$items = @(
    'instances',
    'maps',
    'meta',
    'metacache',
    'accounts.json',
    'atlas.cfg',
    'instgroups.json',
    'mod-presets.json',
    'playtime.cfg',
    'portable.txt',
    'icons',
    'skins',
    'catpacks',
    'centralmods',
    'iconthemes',
    'themes'
)
if ($IncludeLogs) { $items += 'logs' }
if ($IncludeCache) { $items += @('cache', 'downloads') }

if (-not $VerifyOnly) {
    if ((Test-Path -LiteralPath (Join-Path $dst 'instances')) -and -not $Force) {
        throw "Target already has instances\: $dst (pass -Force to merge, or pick an empty root)"
    }
}

# ---- copy -----------------------------------------------------------------
function Get-TreeStats([string]$path) {
    $files = @(Get-ChildItem -LiteralPath $path -Recurse -File -Force -ErrorAction SilentlyContinue)
    $bytes = 0
    foreach ($f in $files) { $bytes += $f.Length }
    return @{ Files = $files.Count; Bytes = $bytes }
}

if (-not $VerifyOnly) {
    foreach ($item in $items) {
        $from = Join-Path $src $item
        if (-not (Test-Path -LiteralPath $from)) { continue }

        $stats = Get-TreeStats $from
        $size = '{0:N1} MiB' -f ($stats.Bytes / 1MB)
        if ($DryRun) {
            Write-Output ("  would copy  {0,-16} {1,6} files  {2,10}" -f $item, $stats.Files, $size)
            continue
        }

        $to = Join-Path $dst $item
        $isDir = Test-Path -LiteralPath $from -PathType Container
        if ($isDir) {
            $null = robocopy $from $to /E /R:1 /W:1 /NFL /NDL /NJH /NJS /NP /FP
            # robocopy: 0-7 = success (bit flags), >=8 = failure
            if ($LASTEXITCODE -ge 8) { throw "robocopy failed for $item (exit $LASTEXITCODE)" }
        }
        else {
            Copy-Item -LiteralPath $from -Destination $to -Force
        }
        Write-Output ("  copied      {0,-16} {1,6} files  {2,10}" -f $item, $stats.Files, $size)
    }
}

# ---- rewrite absolute paths that pointed into the old root -----------------
# Only prefix replacement: never re-serialize JSON (map.json formatting and player data
# must stay byte-identical apart from that one path).
# Pairs are built explicitly (source file -> target file) instead of by string prefix:
# 8.3 aliases (SANPER~1) and long names do not compare equal, and a failed comparison
# used to write into the SOURCE file.
$pairs = @()
$srcInstances = Join-Path $src 'instances'
$dstInstances = Join-Path $dst 'instances'
foreach ($d in @(Get-ChildItem -LiteralPath $srcInstances -Directory -ErrorAction SilentlyContinue)) {
    foreach ($f in @('map.json', 'instance.cfg')) {
        $pairs += @{ From = (Join-Path $d.FullName $f); To = (Join-Path (Join-Path $dstInstances $d.Name) $f) }
    }
}
foreach ($f in @('atlas.cfg', 'mod-presets.json')) {
    $pairs += @{ From = (Join-Path $src $f); To = (Join-Path $dst $f) }
}

$escapedDst = $dstFwd.Replace('$', '$$')
$escapedDstNative = $dst.Replace('$', '$$')
$patterns = @(
    @{ Find = [regex]::Escape($srcFwd); Replace = $escapedDst },
    @{ Find = [regex]::Escape($src); Replace = $escapedDstNative },
    @{ Find = [regex]::Escape($src.Replace('\', '\\')); Replace = ($escapedDstNative.Replace('\', '\\')) }
)

if (-not $VerifyOnly) {
    $rewritten = 0
    foreach ($pair in $pairs) {
        $from = $pair.From
        $to = $pair.To
        # dry run: the target is still empty, so judge the SOURCE file and report the target name
        $readPath = $from
        if (-not $DryRun) { $readPath = $to }
        if (-not (Test-Path -LiteralPath $readPath -PathType Leaf)) { continue }
        $text = [System.IO.File]::ReadAllText($readPath)
        $new = $text
        foreach ($p in $patterns) {
            $new = [regex]::Replace($new, $p.Find, $p.Replace, [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
        }
        if ($new -ne $text) {
            if ($DryRun) {
                Write-Output ("  would rewrite {0}" -f $to)
            }
            else {
                [System.IO.File]::WriteAllText($to, $new)
                Write-Output ("  rewrote     {0}" -f $to)
            }
            $rewritten++
        }
    }
    if ($rewritten -eq 0) { Write-Output '  rewrote      (nothing - no path pointed into the old root)' }
}

if ($DryRun) {
    Write-Output ''
    Write-Output 'Dry run finished. Re-run without -DryRun to apply.'
    return
}

# ---- verify ---------------------------------------------------------------
Write-Output ''
Write-Output '=== check ==='

$cfgPath = Join-Path $dst 'atlas.cfg'
$shared = ''
$instanceDirCfg = ''
if (Test-Path -LiteralPath $cfgPath) {
    foreach ($line in [System.IO.File]::ReadAllLines($cfgPath)) {
        if ($line -match '^SharedResourceDir=(.*)$') {
            $shared = ($Matches[1] -replace '\\\\', '\').Trim()
        }
        if ($line -match '^InstanceDir=(.*)$') {
            $instanceDirCfg = ($Matches[1] -replace '\\\\', '\').Trim()
        }
    }
}

if ($shared) {
    Write-Output "shared library : $shared"
    foreach ($sub in @('assets', 'libraries', 'java')) {
        $p = Join-Path $shared $sub
        if (Test-Path -LiteralPath $p) {
            Write-Output ("                 {0,-10} ok" -f $sub)
        }
        else {
            Write-Output ("                 {0,-10} MISSING" -f $sub)
        }
    }
}
else {
    Write-Output 'shared library : (not set - assets / libraries / java fall back to the data root)'
}

$expectedInstanceDir = $dstFwd + '/instances'
if ($instanceDirCfg -and ((Normalize $instanceDirCfg) -ine $expectedInstanceDir)) {
    Write-Output "WARNING: atlas.cfg InstanceDir still points elsewhere: $instanceDirCfg"
}

$assetsRoot = if ($shared) { Join-Path $shared 'assets' } else { Join-Path $dst 'assets' }

$instanceDirs = @(Get-ChildItem -LiteralPath (Join-Path $dst 'instances') -Directory -ErrorAction SilentlyContinue)
Write-Output ("instances      : {0}" -f $instanceDirs.Count)

if (-not (Test-Path -LiteralPath (Join-Path $dst 'metacache'))) {
    Write-Output 'WARNING: metacache is missing - libraries/ will be re-downloaded in full'
}

$rows = @()
foreach ($inst in $instanceDirs) {
    $pack = Join-Path $inst.FullName 'mmc-pack.json'
    $mc = '?'
    $indexId = '?'
    $missing = '?'
    if (Test-Path -LiteralPath $pack) {
        try {
            $json = [System.IO.File]::ReadAllText($pack) | ConvertFrom-Json
            $mcComponent = $json.components | Where-Object { $_.uid -eq 'net.minecraft' } | Select-Object -First 1
            if ($mcComponent) {
                $mc = $mcComponent.version
                $metaPath = Join-Path (Join-Path $dst 'meta\net.minecraft') ($mc + '.json')
                if (Test-Path -LiteralPath $metaPath) {
                    $meta = [System.IO.File]::ReadAllText($metaPath) | ConvertFrom-Json
                    if ($meta.assetIndex) {
                        $indexId = $meta.assetIndex.id
                        $idxPath = Join-Path (Join-Path $assetsRoot 'indexes') ($indexId + '.json')
                        if (Test-Path -LiteralPath $idxPath) {
                            $idx = [System.IO.File]::ReadAllText($idxPath) | ConvertFrom-Json
                            $n = 0
                            foreach ($prop in $idx.objects.PSObject.Properties) {
                                $h = $prop.Value.hash
                                $objPath = Join-Path (Join-Path (Join-Path $assetsRoot 'objects') $h.Substring(0, 2)) $h
                                if (-not [System.IO.File]::Exists($objPath)) { $n++ }
                            }
                            $missing = [string]$n
                        }
                        else { $missing = 'no index' }
                    }
                }
                else { $missing = 'no meta' }
            }
        }
        catch { $missing = 'parse error' }
    }
    $rows += [pscustomobject]@{ Instance = $inst.Name; MC = $mc; Index = $indexId; MissingObjects = $missing }
}

$rows | Format-Table -AutoSize | Out-String -Width 200 | Write-Output

Write-Output "MissingObjects counts asset objects listed by that version's asset index but absent"
Write-Output 'from the shared library. Non-zero means that version needs a download on first launch.'

# ---- optional: cut (delete the copied items from the source) --------------
# Only the whitelist is touched: atlas.exe / LauncherTests.exe / Qt DLLs / Testing
# artifacts stay behind, so the source root is still a usable UI-test sandbox.
if ($Move -and -not $VerifyOnly) {
    Write-Output ''
    Write-Output '=== move: removing the copied items from the source ==='
    # portable.txt stays: without --dir a bare atlas.exe run must remain a self-contained
    # (empty) sandbox instead of falling back to %APPDATA%.
    $keepOnMove = @('portable.txt')
    $removed = 0
    foreach ($item in $items) {
        if ($keepOnMove -contains $item) {
            Write-Output ("  kept        {0} (keeps the source self-contained)" -f $item)
            continue
        }
        $p = Join-Path $src $item
        if (-not (Test-Path -LiteralPath $p)) { continue }
        try {
            Remove-Item -LiteralPath $p -Recurse -Force -ErrorAction Stop
            Write-Output ("  removed     {0}" -f $p)
            $removed++
        }
        catch {
            Write-Output ("  FAILED      {0} ({1})" -f $p, $_.Exception.Message)
        }
    }
    Write-Output ("  {0} item(s) removed; source now keeps only binaries / logs-moved / build artifacts" -f $removed)
}

Write-Output ''
Write-Output 'Next:'
Write-Output ("  1) run {0}" -f (Join-Path $dst 'atlas.exe'))
Write-Output '  2) Settings > APIs: download source; Settings > Resources: shared library directory'
Write-Output '  3) launch one map; the log should show "Asset index already matches its sha1" when the'
Write-Output '     index is unchanged, and only the objects that are really missing should download.'
