# 从源码构建 Atlas

面向**第一次克隆这个仓库的人**：只用仓库内的文件，在一台干净的 Windows 开发机上编出
`atlas.exe` 与 `LauncherTests.exe`。

> 这一份只保证"能编出来"。**排错、打包细节、数据目录、部署脚本、Agent 重编都在
> [`docs/atlas/02-构建与运行.md`](docs/atlas/02-构建与运行.md)** —— 两处不各写一套。

## 1. 需要什么

| 依赖 | 版本 | 说明 |
| --- | --- | --- |
| Windows | 10 1809+ / 11 x64 | 目前只支持 Windows x64 |
| Visual Studio 2022 | 17.8+，含 **MSVC v143** 与 **Windows 10/11 SDK** | 只装 Build Tools 也可以 |
| CMake | ≥ 3.28 | 预设用到 `version: 8` 字段 |
| Ninja | 任意近期版本 | 生成器是 `Ninja Multi-Config` |
| Qt | **6.8.x `msvc2022_64`** | 需要 Core / Gui / Widgets / Network / NetworkAuth / OpenGL / Svg / Xml / Concurrent |
| vcpkg | 跟随子模块（推荐）或自备 | 依赖清单在 `vcpkg.json`，工具链走 `cmake/vcpkg` 子模块 |
| JDK 8 | 任意 8u 版本 | 只用来编译 `jars/`（JavaCheck / NewLaunch），运行期不需要 |

Qt 的 `bin` 目录必须能被 CMake 找到（`CMAKE_PREFIX_PATH`），否则 configure 会在
`find_package(Qt6)` 处停下。

## 2. 三步构建

```powershell
git clone --recursive https://github.com/xwk6661/AtlasLauncher.git
cd AtlasLauncher

# 1) 生成本机预设：把示例复制成 CMakeUserPresets.json，改掉里面的四个路径
Copy-Item CMakeUserPresets.json.example CMakeUserPresets.json
notepad CMakeUserPresets.json     # Qt / vcpkg / VS / JDK 的绝对路径

# 2) 配置（首次会编译 vcpkg 依赖，10–30 分钟）
cmake --preset win-local

# 3) 构建（Debug；Release 用 win-local-release）
cmake --build --preset win-local
```

产物：`build/win-local/Debug/atlas.exe`（启动器）与 `build/win-local/Debug/LauncherTests.exe`（测试）。

## 3. 跑测试

```powershell
# 全套（约 20–35 秒）。Qt 的 bin 必须在 PATH 里，否则整套 0xc0000135
$env:PATH = "<Qt>/bin;$PWD/build/win-local/vcpkg_installed/x64-windows/bin;" + $env:PATH
ctest --test-dir build/win-local -C Debug

# 单套件
cd build\win-local\Debug
.\LauncherTests.exe MapArchiveScan_test
```

基线与"多少项跳过"只写在 [`docs/atlas/04 §1`](docs/atlas/04-进度与待办.md)；
跳过的原因与两种跑法见 [`02 §5`](docs/atlas/02-构建与运行.md)。

## 4. 出便携包

```powershell
powershell -File tools\package-windows.ps1 -Config Release
# → dist\Atlas-<version>-win64-release.zip
```

脚本会补齐 `jars/`、VC++ 运行库、许可证与中文翻译，**任何一步缺文件直接失败**。
细节与"包为什么必须有 `jars/`"见 [`02 §6.3`](docs/atlas/02-构建与运行.md)。

## 5. 编不动的时候

| 症状 | 去哪 |
| --- | --- |
| `rc` 找不到 / `LNK1104: kernel32.lib` / 依赖重编十几分钟 | [`02 §3`](docs/atlas/02-构建与运行.md)（SDK 未注册、vcpkg ABI 哈希） |
| `no Qt platform plugin could be initialized` | [`02 §3`](docs/atlas/02-构建与运行.md) 坑 5 |
| 随机 `C1083: Permission denied` / 卡在某一步不动 | [`03 §2`](docs/atlas/03-决策与坑.md) 第 4 条与第 16 条 |
| 改了 `Launcher_*_URL` 但编出来还是旧地址 | [`03 §2`](docs/atlas/03-决策与坑.md) 第 12 条（`CACHE STRING`） |
| 改了 `.ui` 之后编译中止 | [`03 §2`](docs/atlas/03-决策与坑.md) 第 9 条（AUTOUIC） |
| 改了 `BaseInstance` 后启动即崩 | [`03 §5.6`](docs/atlas/03-决策与坑.md)（vtable 槽位） |
| 改了 Agent（`quickboot.jar` / `introskip.jar`） | [`02 §5.1`](docs/atlas/02-构建与运行.md) |

提交前的基本要求（格式、提交前缀、不提交什么）见
[`docs/atlas/00 §3`](docs/atlas/00-工程约定.md)。
