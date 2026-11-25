# Atlas —— 构建环境模板（PowerShell）
#
# 作用：把 MSVC 与 Windows SDK 的 RC / INCLUDE / LIB / PATH 显式写进当前进程。
# 为什么需要：vcpkg 构建端口时用的是**干净环境**，进程内改 PATH 传不进去；而部分机器
# （精简安装的 VS、受限环境、沙箱）跑 vcvars64 / Enter-VsDevShell 之后 WindowsSdkDir 是空的，
# 表现为 configure 阶段 `rc` 找不到、链接报 `LNK1104: kernel32.lib`。
#
# 用法：
#   1) 复制成 tools\build\env-toolchain.ps1（该文件不进版本库）
#   2) 按本机情况改下面「需要改的三行」
#   3) 在同一个 PowerShell 会话里 dot-source 它，然后跑 cmake：
#        . .\tools\build\env-toolchain.ps1
#        cmake --preset win-local
#        cmake --build --preset win-local
#
# 注意：RC / LIB / INCLUDE / PATH 都参与 vcpkg 的 ABI 哈希。换一套值 = 所有依赖重编一遍，
# 所以固定用这一个脚本，别在「带不带 Developer Shell」之间来回切。

$ErrorActionPreference = 'Stop'

# ------------------------- 需要改的三行 -------------------------
$VsRoot  = 'C:\Program Files\Microsoft Visual Studio\2022\Community'
$SdkRoot = 'C:\Program Files (x86)\Windows Kits\10'
$SdkVer  = '10.0.26100.0'
# ---------------------------------------------------------------

function Assert-Path([string]$Path, [string]$What) {
    if (-not (Test-Path $Path)) {
        throw "找不到 $What：$Path（请修改本脚本顶部的路径）"
    }
}

Assert-Path $VsRoot  'Visual Studio'
Assert-Path $SdkRoot 'Windows SDK'

# MSVC 工具集：取版本号最大的那个（机器上常有多个）
$msvcRoot = Join-Path $VsRoot 'VC\Tools\MSVC'
Assert-Path $msvcRoot 'MSVC 工具集目录'
$msvc = Get-ChildItem $msvcRoot -Directory |
        Sort-Object { [version]$_.Name } -Descending |
        Select-Object -First 1
if (-not $msvc) { throw "在 $msvcRoot 下没有找到 MSVC 工具集" }

$hostDir = Join-Path $msvc.FullName 'bin\Hostx64\x64'
Assert-Path $hostDir 'Hostx64\x64 工具目录'

$rc = Join-Path $SdkRoot "bin\$SdkVer\x64\rc.exe"
Assert-Path $rc 'rc.exe'

$env:RC      = $rc
$env:INCLUDE = @(
    (Join-Path $SdkRoot "Include\$SdkVer\ucrt"),
    (Join-Path $SdkRoot "Include\$SdkVer\um"),
    (Join-Path $SdkRoot "Include\$SdkVer\shared"),
    (Join-Path $SdkRoot "Include\$SdkVer\winrt"),
    (Join-Path $msvc.FullName 'include')
) -join ';'
$env:LIB = @(
    (Join-Path $SdkRoot "Lib\$SdkVer\ucrt\x64"),
    (Join-Path $SdkRoot "Lib\$SdkVer\um\x64"),
    (Join-Path $msvc.FullName 'lib\x64')
) -join ';'

# PATH 从注册表重建，而不是在现有 PATH 后面拼接：
# 拼接会让 ABI 哈希随当前会话变化，导致依赖被无谓重编。
$machinePath = [Environment]::GetEnvironmentVariable('PATH', 'Machine')
$userPath    = [Environment]::GetEnvironmentVariable('PATH', 'User')
$env:PATH = @(
    (Join-Path $SdkRoot "bin\$SdkVer\x64"),
    $hostDir,
    $machinePath,
    $userPath
) -join ';'

Write-Host "Atlas toolchain ready" -ForegroundColor Green
Write-Host "  MSVC     : $($msvc.Name)"
Write-Host "  SDK      : $SdkVer"
Write-Host "  rc.exe   : $env:RC"
Write-Host "  Qt       : 请确认 CMAKE_PREFIX_PATH 指向 Qt 6.8 msvc2022_64"
