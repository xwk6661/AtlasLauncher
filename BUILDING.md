# 从源码构建 Atlas

面向**第一次克隆这个仓库的人**。本机那套已经调好的脚本在 `build/`（不进仓库），
这里写的是只依赖仓库内文件、能在一台干净开发机上复现的路径。

> 本机（作者机器）的特殊环境、五个坑的细节与沙箱配方，见
> [`docs/atlas/02-构建与运行.md`](docs/atlas/02-构建与运行.md)。本文只保证「别人能编出来」。

---

## 1. 需要什么

| 依赖 | 版本 | 说明 |
| --- | --- | --- |
| Windows | 10 1809+ / 11 x64 | 目前只支持 Windows x64 |
| Visual Studio 2022 | 17.8+，含 **MSVC v143** 与 **Windows 10/11 SDK** | 只装 Build Tools 也可以 |
| CMake | ≥ 3.28 | 预设用到 `version: 8` 的字段 |
| Ninja | 任意近期版本 | 生成器是 `Ninja Multi-Config` |
| Qt | **6.8.x `msvc2022_64`** | 必须是 msvc2022 构建；需要 Core / Gui / Widgets / Network / NetworkAuth / OpenGL / Svg / Xml / Concurrent |
| vcpkg | 跟随子模块（推荐）或自备 | 依赖清单在 `vcpkg.json`，工具链走 `cmake/vcpkg` 子模块 |
| JDK 8 | 任意 8u 版本 | 只用来编译 `jars/`（JavaCheck / NewLaunch），运行期不需要 |

Qt 的 `bin` 目录必须能被 CMake 找到（`CMAKE_PREFIX_PATH`），否则 configure 会在
`find_package(Qt6)` 处停下。

## 2. 三步构建

```powershell
git clone --recursive https://github.com/<you>/<repo>.git
cd <repo>

# 1) 生成本机预设：把示例复制成 CMakeUserPresets.json，改掉里面的四个路径
Copy-Item CMakeUserPresets.json.example CMakeUserPresets.json
notepad CMakeUserPresets.json     # Qt / vcpkg / VS / JDK 的绝对路径

# 2) 配置（首次会编译 vcpkg 依赖，10–30 分钟）
cmake --preset win-local

# 3) 构建（Debug；Release 用 win-local-release）
cmake --build --preset win-local
```

产物：

```
build/win-local/Debug/atlas.exe           启动器
build/win-local/Debug/LauncherTests.exe   测试
```

## 3. 跑测试

```powershell
# 全部 33 个套件（约 20–30 秒）
ctest --test-dir build/win-local -C Debug

# 单个套件
cd build\win-local\Debug
.\LauncherTests.exe MapArchiveScan_test
```

基线是 **33/33**。若出现整套 `0xc0000135`，是 Qt 的 DLL 不在 `PATH` 里，
把 `<Qt>/bin` 加进去即可（测试是 GUI-less 的，但仍然链接 Qt6Core/Gui）。

## 4. 出便携包

```powershell
powershell -File tools\package-windows.ps1 -Config Release
# → dist\Atlas-<version>-win64-release.zip
```

脚本做的事：`cmake --install` 的 Runtime / bundle / portable 三个组件 → 补 `jars\`
（缺 `JavaCheck.jar` 时启动器会拒绝启动）→ 补 VC++ 运行库 → 补许可证、第三方声明、
包内说明与中文翻译 → 压缩。任何一步缺文件都会直接失败，不会静默出一个坏包。

环境变量可覆盖工具位置：`ATLAS_CMAKE`、`ATLAS_QT_BIN`。

## 5. 这个仓库特有的坑（先看这几条）

1. **VS 没注册 Windows SDK**（部分精简安装会出现）：`vcvars` 之后 `INCLUDE`/`LIB` 里
   没有 SDK 路径、`PATH` 里没有 `rc.exe`，而 vcpkg 构建端口时用的是干净环境。
   解法是 overlay triplet 里透传 `RC LIB INCLUDE PATH` —— 本机的
   `build/env-toolchain.ps1` 就是这么干的，`tools/build/env-toolchain.example.ps1`
   是可以照着改的版本。
2. **`PATH` / `RC` / `LIB` / `INCLUDE` 参与 vcpkg 的 ABI 哈希**：带不带 Developer
   Shell 会让哈希不同，结果是所有依赖重编一遍。固定成一套值，别来回切。
3. **并行度**：一次挂 5–8 个文件时，杀毒扫新写出的 `.obj` 会造成随机
   `C1083 Permission denied`。`$env:CMAKE_BUILD_PARALLEL_LEVEL = "2"` 基本不再撞。
4. **`.ui` 头文件的 AUTOUIC 路径规则**：在别的目录 `include` 一个 `.ui` 生成的头会编译中止
   （见 `docs/atlas/03-决策与坑.md` §2.9）。
5. **改了 `CACHE STRING` 的默认值不会生效**：`CMakeCache.txt` 里已经存了旧值，
   要 `cmake --preset win-local -U<变量名>` 清一遍。改 `Launcher_*_URL` 这类变量时最容易踩
   （表现为"代码改了，编出来的还是旧地址"）。
6. **不要给 `BaseInstance` 加虚函数**：vtable 槽位会整体后移，增量构建里只要有一个
   TU 用旧头文件就会在无关处崩溃（`docs/atlas/03-决策与坑.md` §5.6）。

## 6. 改了 Java Agent 之后

`quickboot.jar` / `introskip.jar` 是**提交进仓库的构建产物**，源码在
`tools/quickboot-agent/` 与 `tools/introskip-agent/`。改完要重新出 jar，否则改动不会生效
（`agents.qrc` 在构建期嵌进二进制，所以出完 jar 还要重新 configure）。步骤见
[`docs/atlas/02-构建与运行.md` §5.1](docs/atlas/02-构建与运行.md)，两个要点：

- ASM 必须 ≥ 9，且 visitor 的 `api` 要一起抬到 `Opcodes.ASM9`；
- Windows 上必须 `javac -encoding UTF-8`（JDK 8 在中文系统默认 GBK，会把中文注释判成非法字符）。

符号表复核：`python tools/agent_symbol_audit.py`（对真实 client jar 逐槽位核对，结论写进
`docs/atlas/agent-matrix.md`）。

## 7. 提交前

```powershell
ctest --test-dir build/win-local -C Debug        # 33/33
git status                                        # 构建产物在 build/ 与 dist/，已被忽略
```

`dist/` 与 `build/` 都在 `.gitignore` 里。**不要提交 zip、exe、`.qm` 之外的二进制**。
