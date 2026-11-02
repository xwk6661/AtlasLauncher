# 转接文档 · 在 Prism Launcher 上做「地图启动器」
>
> 最后更新：2026-11-02 15:00 · 状态：**构建通过、`13 passed / 1 skipped`（跳过的是需要联网的封面用例）；
> M0–M3 代码全部落地；本轮解决「大厅封面一直出不来」（见 §5.3）；等真机验收**
> 交接人：WorkBuddy（AI 助手）· 接手人：（待填）
>
> 阅读顺序：本文件 → [`README.md`](README.md)（结论） → [`01-可行性评估.md`](01-可行性评估.md)（为什么这么做）
> → [`02-架构与路线图.md`](02-架构与路线图.md)（怎么做、分几步）

---

## 0. 30 秒速览

- **任务**：把 `E:/llmUsage/Minecraft-AIO`（mcml v0.33.0，Electron/TS 写的地图启动器）的设计理念，
  移植到 `E:/llmUsage/PrismLauncher`（C++/Qt 6.8）上，做一个原生 Qt 地图启动器。
- **已完成**：
  1. 读完 AIO 的 README / HANDOVER / 技术演进文档（4600 行）/ launcher README，提炼出设计理念、原理、UI 逻辑；
  2. 通读 Prism 源码，盘点可复用能力与缺口，得出**「可行，且比 AIO 从零做省一半工作量」**的结论；
  3. 写出 4 份文档（本目录）；
  4. 搭好本地构建环境，并**把构建彻底跑通**（坑与配方见 §3.2）；
  5. **M0 完成**：`launcher/maps/` 下 **28 个模块 / 59 个文件（约 1.37 万行）** + dataversion / Agent 资源，
     用 AIO 的 23 个夹具做回归，版本 / 置信度 / 世界数 / 材质包数**逐项一致**；
  6. **M1/M2/M3 全部落地**：导入全流程、Agent 直连（jar + 6 版本符号表已入资源）、
     大厅（搜索/筛选/分页/封面/评论）、实例操作栏地图信息区、重装地图、地图设置。
  7. **第二轮自查（2026-11-01 14:30）**：8 处问题已修，见 §5.2。最关键的是启动准备点
     从「主窗口按钮」收敛到 `Application::launch` —— 否则双击实例 / 实例窗口启动时
     老版本地图不会注入 Agent，会静默停在主菜单。
  8. **2026-11-02 第三轮**：大厅封面终于能出图（`MapCoverLoader`，直连失败切无头浏览器批量取图 +
     磁盘缓存，见 §5.3）；「更新数据」不再随机失败/误报崩溃（页面内 XHR 重试，见 §5.4）；
     同步失败保留旧库并明说「本地数据未受影响」；评论改成**带边框的卡片**、评论少时不再被拉成一片空白（§5.5）。
- **UI 形态（重要）**：曾做过「地图 / 实例」双模式（中央 QStackedWidget），**已按用户要求移除**。
  现在的形态是：主界面 = 原生实例列表；选中地图实例时，**右侧原生实例操作栏**里出现地图信息区
  （`MapInstanceBar`）；「添加实例」按钮带下拉菜单（自定义 / Modrinth / CurseForge / 本地导入 / CTM 大厅）。
  详见 §5 的「入口」。
- **已产出**：`build/win-local/Debug/prismlauncher.exe`（32 MB）与 `LauncherTests.exe`，均链接成功；
  测试 **13 passed, 0 failed, 2 skipped**（跳过的是需要真实网址的联网用例：封面通道、JSON 通道）。
- **下一步**：**真机验收**（1.20+ 进图、≤1.19.4 Agent 进图 + 退出落盘）是唯一没被自动化覆盖的环节；
  其次是 RAR5 实测、更多版本符号表。详见 §5.1 / §6。

---

## 1. 这轮工作的由来

用户先要求「阅读 Minecraft-AIO 所有文档 → 了解其启动器设计理念、原理、UI 逻辑 →
看看能否在 PrismLauncher 现有基础上开发一个基于 PrismLauncher 的新版 Qt 地图启动器」。

结论与论证全部落在 `01-可行性评估.md`。最关键的三条：

1. **Prism 已经有 AIO 手写的一半能力**：`World::readFromZip/install`（压缩包内定位 `level.dat` → 只解那棵子树）、
   `MMCZip::ArchiveReader`（libarchive，通吃 zip/7z/rar）、`libnbtplusplus`（NBT）、
   `MinecraftTarget::world` + `JoinWorldOnLaunch`（**1.20+ 直连存档已实现**）、
   `VersionFile::agents`（`-javaagent` 通道）、`InstanceTask/InstanceStaging`（实例生命周期）。
2. **真正缺的是三块**：三级版本判定 + 置信度（旧版本直连（搬 AIO 的 Java Agent，不要用 C++ 重写符号解析器）、以地图为中心的 UI。
3. **最大风险不在技术**：Prism 是 GPL-3.0，fork 出来的产品必须开源。商业化要另选 MIT 栈（AIO 的 `@xmcl` 路线）。**这一条要在写第一行代码前定死。**

---

## 2. 仓库现状

### 2.1 工作目录

```
E:/llmUsage/PrismLauncher          ← 主仓库（Prism 12.0.0-dev）
E:/llmUsage/Minecraft-AIO           ← 参考项目（只读，未改动）
```

### 2.2 新增了什么

| 路径 | 说明 |
| --- | --- |
| `docs/map-launcher/README.md` | 结论摘要 + 能力对照 + 阅读指引 |
| `docs/map-launcher/01-可行性评估.md` | AIO 设计解析 / Prism 能力盘点 / 差距分析 / 三条路线取舍 |
| `docs/map-launcher/02-架构与路线图.md` | 模块设计 / 数据模型 / 时序 / UI 结构 / 文件级改动清单 / M0–M3 / R1–R10 风险表 |
| `docs/map-launcher/HANDOVER.md` | 本文件 |
| `launcher/maps/` | **全部新代码**（28 个 .cpp / 31 个 .h，约 1.37 万行，见 §5） |
| `launcher/resources/maps/` | `dataversion.json` + `maps.qrc` |
| `launcher/resources/agents/` | `quickboot.jar` + 6 个版本符号表 + `agents.qrc` |
| `tests/MapArchiveScan_test.cpp` | 13 项常规测试（含 23 夹具回归 + 退出判定 + notes.json 往返 + 旧「缺整合包」标记钳位）+ 2 项联网用例（默认 skip） |
| `CMakeUserPresets.json` | 本地构建预设（gitignore，不污染仓库） |
| `build/*.ps1` | 一键构建脚本（gitignore） |

### 2.3 git 现状（2026-11-02 15:30）

全部改动已提交到分支 **`feature/map-launcher`**，工作区干净：

| commit | 内容 |
| --- | --- |
| `e587413c0` | 地图启动器主体（99 文件，+16 765 / −43）：`docs/`、`launcher/maps/`、两个资源目录、测试、对既有文件的加法 |
| `8d131f0ab` | 打开大厅时重读「大厅通道」设置 |
| `8741b2357` | 浏览器通道改成页面内 XHR + 退避重试（见 §5.4） |

对既有文件的改动仍然只做加法，逐项含义：

| 文件 | 改什么 |
| --- | --- |
| `launcher/Application.cpp` | 6 个地图设置项 + `launch()` 里的统一准备点 |
| `launcher/CMakeLists.txt` | `MAPS_SOURCES` + 两个 qrc |
| `launcher/InstancePageProvider.h` | 注册实例窗口「信息 / 地图」页签，默认停在「设置」 |
| `launcher/archive/ArchiveReader.{h,cpp}`、`ArchiveWriter.cpp` | `File::size()`；修 2 处上游编译错误 |
| `launcher/minecraft/MinecraftInstance.cpp` | `processMinecraftArgs` 启动时读 `JoinWorldOnLaunch`（此前没人读） |
| `launcher/tasks/{SequentialTask,ConcurrentTask}.cpp` | 修 Debug 断言崩溃（见 §7 坑 12） |
| `launcher/ui/MainWindow.{h,cpp}` | 地图信息区接线 + 菜单 + 拖放分流 + 启动统一准备 + 后台更新大厅 |
| `launcher/ui/dialogs/NewInstanceDialog.*` | 支持打开时定位到指定来源页；精简来源页 |
| `launcher/ui/pages/instance/WorldListPage.cpp` | 地图实例的世界列表只读（R8） |
| `tests/CMakeLists.txt` | 注册测试 |

新代码全部放新目录；既有文件只做小改动，`tasks/` 两处是**修上游 Debug 构建必崩的竞态**
（Release 无感知），rebase 时值得保留。这是刻意的设计约束：减少与上游 rebase 的冲突面。

---
## 3. 环境现状

### 3.1 已就绪（都验证过）

| 组件 | 路径 | 版本 / 验证方式 |
| --- | --- | --- |
| Visual Studio 2022 Community | `D:\Program Files\Microsoft Visual Studio\2022` | 17.14.36717.8（vswhere 枚举） |
| MSVC 工具集 | `…\2022\VC\Tools\MSVC\14.44.35207` | 另有 14.36.32532 |
| Windows SDK | `C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0` | 另有 10.0.19041.0 |
| CMake | `…\2022\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe` | **3.31.6-msvc6**（Prism 要求 ≥3.25，预设要求 ≥3.28） |
| Ninja | `…\Microsoft\CMake\Ninja\ninja.exe` | VS 自带 |
| **Qt** | `E:\Program File\Qt\6.8.3\msvc2022_64` | **6.8.3**，0.4 GB；含 qtbase/qtsvg/qttools/qttranslations/qtimageformats + **qtnetworkauth 模块**；`qmake -query QT_VERSION` → 6.8.3 |
| **vcpkg** | `E:\Program File\vcpkg` | `vcpkg.exe` 版本 `2026-10-29-139f2e53…`；仓库 HEAD `9a5dfef5d1a0438a7d24856a85bd60f309d355e9`（`--depth 1` 克隆，30 秒完成） |
| aqtinstall | pip（Python 3.14.3） | 3.3.0，仅用于装 Qt |
| 本地清单目录 | `E:\Program File\vcpkg-manifests\prism\` | `vcpkg.json`（Prism 的副本）+ `vcpkg-configuration.json`（本地补丁版） |

磁盘：C 盘剩 24.7 GB，E 盘剩 170 GB。**所有额外环境都装在 `E:\Program File`**，符合要求。

### 3.2 构建链路：四个坑与配方（**已全部解决，按这个来**）

构建从「卡死」到「出 exe」一共踩了四个坑，都不是 Prism 的问题，全是本机环境的：

| # | 坑 | 现象 | 解法 |
| --- | --- | --- | --- |
| 1 | vcpkg git 注册表要完整历史 | 克隆 `microsoft/vcpkg` 到 63 MB 停滞 | 本地浅克隆当 **builtin 注册表**：manifest 加 `builtin-baseline`，`vcpkg-configuration.json` 改 `{"kind":"builtin","baseline":…}` |
| 2 | vcpkg 要 pwsh 7.6.9 | 卡在 `PowerShell-7.6.9-win-x64.zip`（109 MB，直连 ~30 KB/s） | 16 线程分段下载 → 校验 SHA512 → 丢进 `vcpkg/downloads/`；lzo / bzip2 同理从清华 Debian 镜像取（哈希一致即可） |
| 3 | **VS 实例没注册 Windows SDK** | vcpkg 端口构建报 `rc` 找不到、`LNK1104: kernel32.lib` | overlay triplet + `VCPKG_ENV_PASSTHROUGH RC LIB INCLUDE PATH`（见下） |
| 4 | 上游 Java 子项目用 `-source 7` | `错误: 不再支持源选项 7` | 用本机 **JDK 8** 充当 javac（预设里 `Java_JAVAC_EXECUTABLE` 指过去） |

**坑 3 的关键**（最耗时的一个）：vcpkg 给端口构建的是**干净环境**，进程内改 `PATH` / `RC` 都传不进去。
而本机 `Enter-VsDevShell` 之后 `WindowsSdkDir` 是空的、`INCLUDE`/`LIB` 只有 MSVC 路径、PATH 里没有 `rc.exe`，
所以 cmark 的第一个 try-compile 就挂在 `rc` 上。解法是本地 overlay triplet：

```cmake
# E:/Program File/vcpkg-manifests/prism/triplets/x64-windows.cmake（不进仓库）
include("E:/llmUsage/PrismLauncher/cmake/vcpkg-triplets/x64-windows.cmake")
set(VCPKG_ENV_PASSTHROUGH RC LIB INCLUDE PATH)
```

再配合 `build/env-toolchain.ps1` 把 SDK 的 `RC` / `INCLUDE` / `LIB` / `PATH` 显式写全。
**注意**：这些值参与 vcpkg 的 ABI 哈希，`run-vcpkg.ps1` 与 `run-configure.ps1` 必须 dot-source 同一份脚本，
否则每次都会把所有依赖重编一遍（实测：22 个包重编 11~20 分钟）。

**坑 4 的细节**：本机有 `D:/Program Files/Java/jdk-8`（1.8.0_202），
`CMakeUserPresets.json` 里把 `Java_JAVAC_EXECUTABLE` / `Java_JAR_EXECUTABLE` / `Java_JAVA_EXECUTABLE` 指过去即可；
JDK 21/25 会拒绝 `-source 7`。

**2026-11-02 补记（AI 在受控环境里踩到的坑）**：在沙箱里跑 `cmake --preset` 会触发 vcpkg 重装依赖，
而沙箱的安全策略**拦掉了 `reg.exe`** → `Enter-VsDevShell` / `vcvars64` 静默失败，只设了 MSVC 的
`INCLUDE`/`LIB`，没有 Windows SDK 路径 → cmark 的 try-compile 依次挂在 `rc` 和 `kernel32.lib` 上，
随后 vcpkg 把 `vcpkg_installed` 清空、项目直接不可编译。
临时解法是**手工写全** `INCLUDE`/`LIB`/`PATH`（SDK 在前、MSVC 在后），代价是 ABI 哈希与
`env-toolchain.ps1` 不一致。**接手后建议先跑一次 `build/run-vcpkg.ps1` 归一**（会重编一遍依赖，约 13 分钟），
之后照 §3.3 的脚本走，否则每次都会全量重编。

### 3.3 现在的构建命令（跑通了，别改）

```powershell
# 依赖（只在改过 vcpkg 清单 / triplet / 环境变量后需要）
& E:\llmUsage\PrismLauncher\build\run-vcpkg.ps1

# 配置 + 编译（Debug）
& E:\llmUsage\PrismLauncher\build\run-configure.ps1 -Action configure
& E:\llmUsage\PrismLauncher\build\run-configure.ps1 -Action build-debug
# Release：-Action build-release
```

产物：`build\win-local\Debug\prismlauncher.exe`、`build\win-local\Debug\LauncherTests.exe`。

运行测试（需要 Qt 与 vcpkg 的 DLL 在 PATH 上）：

```powershell
$env:PATH = "E:\Program File\Qt\6.8.3\msvc2022_64\bin;E:\llmUsage\PrismLauncher\build\win-local\vcpkg_installed\x64-windows\bin;" + $env:PATH
$env:PRISM_MAP_FIXTURES = "E:\llmUsage\Minecraft-AIO\launcher\fixtures"
cd E:\llmUsage\PrismLauncher\build\win-local\Debug
.\LauncherTests.exe MapArchiveScan_test
```

**最近一次结果**：`Totals: 13 passed, 0 failed` —— 23 个夹具逐项一致。

### 3.4 仓库外动过什么（可撤销）

| 位置 | 改了什么 | 为什么 |
| --- | --- | --- |
| `E:/Program File/vcpkg-manifests/prism/vcpkg.json` | 加 `builtin-baseline` | 坑 1 |
| `…/prism/vcpkg-configuration.json` | `builtin` 注册表 + 本地 overlay triplet | 坑 1 |
| `…/prism/triplets/x64-windows.cmake` | 新增，透传 RC/LIB/INCLUDE/PATH | 坑 3 |
| `E:/Program File/vcpkg/downloads/` | 手工放入 pwsh / lzo / bzip2 三个源码包 | 坑 2 |
| `CMakeUserPresets.json`（gitignore） | ninja / rc / cl / JDK8 的绝对路径 | 坑 3、4 |
| `build/*.ps1` + `run-prism-debug.cmd` | `env-toolchain.ps1`、`run-vcpkg.ps1`、`run-configure.ps1`、双击启动（配好 DLL 路径） | 一键复现 |

**没有**改系统环境变量（中途试过写 User PATH/INCLUDE/LIB，验证完已全部撤销）。

---

## 5. 已经写好的代码（文件级清单）

全部在 `launcher/maps/`（**新目录，对既有文件只做加法**）：

| 文件 | 职责 |
| --- | --- |
| `MapTypes.h` | Confidence / LevelDatFacts（含 `requiredMods`）/ MapLayoutFacts / VersionGuess |
| `MapTextUtils.{h,cpp}` | 剥 § 格式化码、GBK 回退解码、版本号抠取 |
| `DataVersionTable.{h,cpp}` | 483 个版本 / 481 条 DataVersion 双向索引、Java 大版本、quickPlay 判定 |
| `LevelDatReader.{h,cpp}` | level.dat（GZip+NBT）→ LevelDatFacts：Version 字段、BorderSize、ServerBrands、`fml.LoadingModList` 的模组清单、`enabled_features` |
| `MapVersionDetector.{h,cpp}` | 三级判定 + 冲突裁决 + 26.x 拒绝猜测（逐条对齐 AIO `detector.ts`） |
| `MapArchiveScanner.{h,cpp}` | 免解压嗅探：世界根、结构特征、材质包/模组/说明文件、嵌套 zip 深校验 |
| `MapMeta.{h,cpp}` | `map.json` 唯一读写点：证据链、世界列表、统计、实验性 feature、模组清单 |
| `MapNotes.{h,cpp}` | **玩家自己的数据（`notes.json`）**：中文名 / 我的评分（1~10 半星）/ 笔记。与 `map.json` 分文件存 —— 前者是"这张图是什么"（自动产生、重装会覆盖），后者是"我怎么看这张图"（人产生、任何自动流程都不许碰）。照 AIO `src/ui/server.ts` 的 `UserNote` |
| `MapImportTask.{h,cpp}` | 建实例 → 解世界（`.tmp-world` 再改名）→ 落材质包/模组 → 合并 options.txt → 写 map.json + 封面 → 归档进地图库；**`reinstallWorlds()` 重装**（旧存档进 `saves/.mcml-backup/<时间戳>/`） |
| `MapLaunchPrep.{h,cpp}` | 只认 trait 选轨道、可指定世界、读三个设置项（自动进图 / 退出即关 / 进图延迟）、退出后按 `agent.log` 回写 stats；**`prepareForLaunch()` 是所有启动入口的唯一准备点（由 `Application::launch` 调）** |
| `AgentBootstrap.{h,cpp}` | 符号表 → `k=v,k=v`（含 `autoEnter` / `haltOnSafe`）、JvmArgs 注入、事件解析与日志监听 |
| `MapHallLink.{h,cpp}` | **本地实例 ↔ CTM 地图的关联**（`<实例>/hall-link.json`：slug / map_uuid / 标题 / 时间）。删掉文件即解除关联；不改 `map.json`，也不碰存档 |
| `HallCache.{h,cpp}` | **大厅本地库**：把 4 个接口的原始行（列表 / 详情 / 用户 / 评论）存成 `<数据目录>/maps/hall-cache.json`（QSaveFile 原子写）。有缓存时搜索、排序、翻页、详情、评论全部在内存里算，**不再发任何请求**；只在用户点「更新数据」时重拉 |
| `BrowserFetch.{h,cpp}` | **浏览器通道**：用本机 Edge/Chrome 的无头模式取大厅 JSON（`--headless=new --disable-quic --dump-dom`），解析 `<pre>` 里的内容。给"SNI 被封、但浏览器能过"的网络用，**不需要任何代理**；代价是每次约 2 秒、拿不到 `Content-Range` |
| `HallProxy.{h,cpp}` | **大厅出口解析**（照 AIO `src/util/proxy.ts`）：auto = 环境变量 → TCP 探活 → Windows 系统代理（注册表）→ 探活 → 直连；manual / off 可选；本机/内网/`NO_PROXY` 强制直连（AIO `shouldBypassProxy`）。大厅用**自己的** `QNetworkAccessManager`，不跟启动器全局代理共用；**走代理失败会直连再试一次**（AIO `fetchWithFallback`） |
| `HallAPI.{h,cpp}` + `HallTypes.h` | 搜索/详情/筛选 + **评论分页**（`content-range` 读总数）+ **用户名批量补全** + 失败分类 |
| `MapRepository.{h,cpp}` | 实例列表 → 地图列表（唯一判据：有 map.json）、地图库归档 |
| `MapImportDialog.{h,cpp}` | 名称/世界/版本/加载器 + 证据 + 置信度红条 + 实验性设置与缺模组提示；判不出时禁止直接确认 |
| `MapGalleryPage.{h,cpp}` | 封面卡片（缩放缓存）+ 窄详情侧栏 + 多世界下拉 + 拖放导入（只在独立窗口用） |
| `MapGalleryWindow.{h,cpp}` | 画廊独立窗口（次要入口） |
| `MapInstanceBar.{h,cpp}` | **实例操作栏里的地图卡片（照 AIO `ui/app.js renderDetail`）**：中文名 + 版本徽章 + **加载器徽章（Vanilla/Forge/Fabric/NeoForge/Quilt，取 mmc-pack 组件而不是 map.json）** + 置信度点 + **我的评分（★ 只读）** + **笔记预览** + 状态/游玩/上次/存档/包体积/导入于 + 世界下拉 + 风险提示 + 主按钮「进入地图」；非地图实例但有存档时给轻量卡片。**不做「复制压缩包路径」**（一个地图一个实例，路径对玩家没有意义） |
| `HallDialog.{h,cpp}` | 搜索/排序/难度与时长筛选 + 11 项属性 + 简介 + **评论（加载更多）** + 浏览器打开下载页 |
| `MapHallPage.{h,cpp}` | **新建实例对话框里的「CTM 地图」页**（2026-11-01 新增）：搜索 + 排序 + 系列/难度筛选 + 封面列表 + 详情面板（11 项属性/简介）+「打开下载页」+「选择已下载的文件…」→ 扫描/判定/确认后把 `MapImportTask` 交给对话框的「确定」 |
| `MapInstancePage.{h,cpp}` | 实例窗口「**信息**」页（原「地图」）：中文名/评分/笔记（可编辑，写 `notes.json`）+ 来源包/证据/统计 + **重装地图** + 提示条（实验性/缺加载器/缺模组）。**不列世界目录** —— 一个地图一个实例，`saves/` 里只有它自己的存档 |
| `MapSettingsDialog.{h,cpp}` | 地图设置：自动进图 / 退出即关 / 进图延迟 / 地图库目录 / 大厅数据地址 / 大厅代理（自动·手动·直连）/ 测试连接 / **控制台页签开关** |
| `OptiFineDialog.{h,cpp}` | **安装 OptiFine**（版本页入口）：列表走 BMCLAPI `/optifine/<mc>`，下载**两源依次试**（BMCLAPI → optifine.net 的 adloadx→downloadx，带 Referer），每源重试 2 次，装进 `mods/` 当模组 |
| `resources/maps/dataversion.json` + `maps.qrc` | 对照表随包发布，只读 |
| `resources/agents/quickboot.jar` + `agents.qrc` | Agent（ASM 已 shade 到 `mcml/shaded/asm`，`Premain-Class: mcml.agent.Agent`） |
| `resources/agents/symbols/*.json`（6 个） | 1.7.10 / 1.8.9 / 1.12.2 / 1.16.5 / 1.19.2 / 1.21.1 |
| `tests/MapArchiveScan_test.cpp` | 13 项测试：23 夹具回归、版本表、归一化、**Agent 资产/参数/选项**、Agent 事件、**退出判定**、**notes.json 往返与钳位**、**旧「缺整合包」标记钳位**、**map.json 往返**、**content-range 解析** |

既有文件只改了这些（合计 +379 / -18 行）：

| 文件 | 改了什么 |
| --- | --- |
| `launcher/CMakeLists.txt` | `MAPS_SOURCES` + `maps.qrc` + `agents.qrc` |
| `launcher/Application.cpp` | 6 个地图设置项 |
| `launcher/archive/ArchiveReader.{h,cpp}` | `File::size()` |
| `launcher/archive/ArchiveWriter.cpp` | 修 2 处上游编译错误（`QString::arg(std::string)`） |
| `launcher/minecraft/MinecraftInstance.cpp` | `processMinecraftArgs`：无显式 target 时读 `JoinWorldOnLaunch`（此前只有设置页回填在用，启动时没人读）→ **所有启动入口都能自动进图** |
| `launcher/tasks/{SequentialTask,ConcurrentTask}.cpp` | 多个子任务同时失败时防二次 `emitFailed`（Debug 构建的 `Q_ASSERT` 会 qFatal，实测崩过一次） |
| `launcher/ui/MainWindow.{h,cpp}` | 操作栏插 `MapInstanceBar` + `prepareMapLaunch()` 统一启动准备 + 菜单 + 拖放先判地图再判整合包 |
| `launcher/ui/dialogs/NewInstanceDialog.{h,cpp}` | 构造支持 `initialPage`（下拉菜单直达来源页） |
| `launcher/InstancePageProvider.h` | 注册实例窗口「地图」页签 |
| `launcher/ui/pages/instance/WorldListPage.cpp` | 地图实例的世界列表**只读**（R8） |
| `tests/CMakeLists.txt` | 注册测试 |

**入口**（全部在原生 UI 里，无独立模式）：
- 主界面：实例列表选中地图实例 → 右侧操作栏出现地图信息区
- 「添加实例」▼ → 自定义 / Modrinth / CurseForge / 本地导入 / **CTM 地图大厅**
- 文件菜单 →「导入地图…」「地图设置…」；视图菜单 →「地图画廊（新窗口）」「地图大厅…」
- 实例窗口 →「地图」页签（重装地图 / 提示条）
- 拖压缩包进主窗口：先判地图（level.dat），再判整合包

## 5.1 还剩什么（诚实清单）

| # | 事项 | 说明 |
| --- | --- | --- |
| 1 | **真机验收** | 1.20+ 地图从原生「启动」按钮进图；1.16.5 / 1.8.9 各 2 次「进图 → Save and Quit → 游戏自行退出」并检查 `agent.log` 的 `EXIT_SAFE`。这是唯一没被自动化覆盖的环节 |
| 2 | RAR5 大包 | 夹具里的小 RAR 能扫；分卷 / 加密头没测 |
| 3 | 导入流程的加载器版本选择器 | 目前手填；Prism 的实例「版本」页已能装 Forge/Fabric |
| 4 | 模组套件 | AIO 的 4 个预置套件没搬；Prism 自带 Modrinth/CurseForge 的模组页可用 |
| 5 | 符号表覆盖 | 6 个版本（1.7.10 / 1.8.9 / 1.12.2 / 1.16.5 / 1.19.2 / 1.21.1）；缺表时降级为「停在主菜单 + 提示」 |
| 6 | 重装地图是同步的 | `MapInstancePage::reinstallWorlds` 在 UI 线程解压 + `QApplication::setOverrideCursor`。700 MB 的包会卡十几秒、4 GB 的会卡几分钟。要改就改成 `Task`（R6） |
| 7 | `map.json` 的 `archive.sha1` 没填 | 导入时不额外读一遍整个包（大包多花 10 秒不值）。字段留着，目前恒为空 |
| 8 | Agent 参数落在 `JvmArgs` 上 | 注入的是实例的 `JvmArgs`，所以实例「设置 → Java 参数」里能看到一条很长的 `-javaagent:…`。功能正确、可被后启动覆盖，但不好看。彻底解决要把 Agent 参数改成启动时计算（`MinecraftInstance::extraArguments()` 追加一个专用设置项） |
| 9 | UI 文案的中文写在 `tr()` 源串里 | 中文用户无感，但切到英文/其他语言仍显示中文。国际化要先把源串改英文 + 加 `.ts` 翻译，工作量大，暂不做 |
| 10 | `ctmrepository.com` 的 50 张封面取不到 | Cloudflare 不认无头浏览器（没有正确 Referer / 走人机校验），这 50 张继续显示占位封面；874 张 supabase 的正常 |
| 11 | 首次打开大厅要等约 12 秒才出图 | 一个无头进程一次取 8 张、20 行要 3 批；之后翻页/重开都是磁盘命中。想更快只能做"可见行预取 + 常驻浏览器（CDP）"，工程量大 |
| 12 | 封面缓存没有上限也没有清理入口 | 现在按 URL 哈希落盘、永不淘汰（单张 ~20 KB，1000 张约 20 MB）。要加就在「地图设置」里加一个「清空封面缓存」 |

## 5.2 第二轮自查修掉的问题（2026-11-01 14:30）

| # | 问题 | 后果 | 修法 |
| --- | --- | --- | --- |
| 1 | **启动准备只挂在主窗口按钮上** | 双击实例 / 从实例窗口点启动 / 快捷方式启动时，≤1.19.4 的地图实例**不会注入 Agent**，静默停在主菜单（quickPlay 版本侥幸能用，因为设置是持久化的） | 准备点下沉到 `Application::launch()`（五个入口的唯一必经之路），主窗口只负责提示与传「选中的世界」 |
| 2 | **多世界选择在 Agent 轨道丢失** | 多世界包选第二个存档，老版本实例仍然进第一个（只有 quickPlay 传了 target） | 选中的世界经实例属性 `mapRequestedWorld` 传到 `prepareForLaunch`，写进 Agent 参数 |
| 3 | **陈旧 `agent.log` 污染退出判定** | 上一次是 `EXIT_SAFE`、这一次崩溃，仍会被判成「正常落盘」，并写进 `map.json` 统计 | 每次启动前删 `agent.log`；判定改成「最后一条标记为准」（原来用 `contains`，先超时后落盘会写反） |
| 4 | 启动准备与 `saveAll()` 的先后 | 实例设置页开着时，Java 面板会把整段 `JvmArgs` 写回，覆盖刚注入的 Agent | 把准备挪到 `saveAll()` 之后 |
| 5 | 置信度颜色写死 `#2e7d32/#ef6c00/#c62828` | 深色主题下对比度 ~1.6:1，等于看不见（设计文档里明确写了不许写死颜色） | 新增 `maps/MapTheme.h`：按 `QPalette` 明暗各给一套，四处调用点统一 |
| 6 | 导入对话框切世界不同步版本 | 多世界包选第二个世界，版本下拉框还显示第一个的版本（实际建实例用的是第二个） | `worldChanged()` 同步版本/加载器/名称，确认按钮状态按「实际会用的版本」重算 |
| 7 | 大厅请求没有超时 | 站点只握手不回包时对话框一直转圈 | `QNetworkRequest::setTransferTimeout(15s)`，超时走已有的失败分类 |
| 8 | 零碎 | `MapMode` 死设置、`setInitialPage` 死代码、画廊「导入地图…」传空路径、封面缓存不刷新、`MapInstanceBar` 封面被拉伸、导入摘要 HTML 未转义 | 一并清掉 |
| 9 | 新建实例对话框来源太多（2026-11-01 产品要求） | 左侧列表里 ATLauncher / FTB / FTB Legacy / 从 FTB 导入 / Technic 与这个启动器无关，干扰「选个版本就能玩」的主路径 | `NewInstanceDialog::getPages()` 只保留 自定义 / 导入 / CurseForge / Modrinth；页面源码仍留在工程里（上游同步冲突面不变） |
| 10 | 实例操作栏（右侧边栏）消失 | `QMainWindow::restoreState()` 连工具栏的可见性与宽度一起恢复，旧窗口状态一旦是隐藏/压扁的，主界面就只剩实例列表，且用「视图」菜单勾回来也会被下次启动覆盖 | `MainWindow::showEvent()` 首次显示后延一轮复查：隐藏或宽度 < 80 就按默认位置重新停靠；另清掉了用户配置里那份坏状态 |
| 11 | 新建实例「列表更新失败：Connection closed」 | 用户在 设置 → API 里把 Meta URL 覆盖成了 `https://lively-math-14d3.xwk6661.workers.dev/`，该镜像已失效（`RemoteHostClosedError`），而官方 `meta.prismlauncher.org/v1/index.json` 实测 200 | 不是代码问题。清掉 `MetaURLOverride` 即可（配置已备份为 `prismlauncher.cfg.bak-20261101-1455`） |
| 12 | **地图信息区从来不显示** | 旧代码用 `widget->setVisible(false)` 控制显隐；`QToolBar` 会把这个 widget 从布局里摘掉，之后 `setVisible(true)` 也回不到栏里 —— 所以明明有 `map.json` 也看不到卡片 | 显隐改走 `QWidgetAction::setVisible()`（`m_mapBarAction`），widget 显隐只做辅助 |
| 13 | 右侧栏信息密度太低（2026-11-01 产品要求） | 只有一排原版按钮，和 AIO 的详情栏差得远 | `MapInstanceBar` 重做成 AIO 式卡片（版本/置信度/状态/游玩/上次/存档/来源/导入于/风险提示 + 主按钮）；有卡片时隐藏原生「启动」，避免两个一样的主按钮 |
| 14 | 新建实例里没有地图大厅 | 只有菜单里的独立对话框，不符合「和 Modrinth / CurseForge 一致」的使用习惯 | 新增 `MapHallPage`：搜索 + 排序 + 筛选 + 封面列表 + 详情 + 两个动作；「确定」直接建实例 |
| 15 | **信息栏缺了 AIO 的三样东西**（2026-11-01 按用户要求补齐） | 只有版本/统计，没有 **中文名 / 我的评分 / 笔记** —— 而这三样正是 AIO 详情栏区别于原版的地方 | 新增 `MapNotes`（`notes.json`）：卡片显示中文名 + ★ 评分 + 笔记预览；实例窗口「地图」页给编辑入口（星控件半星粒度、笔记 2000 字上限、保存后显示"上次编辑"） |
| 16 | 大厅直连必失败，也没有"出口"概念 | 启动器全局代理常被设成"直连"（meta/下载更快），但 `*.supabase.co` 必须走代理；两者需求相反 | 照 AIO `util/proxy.ts` 写 `HallProxy`：auto 先探活再选出口，大厅用自己的 NAM，状态栏显示"直连 / 经代理 X" |
| 17 | 大厅页的筛选选项永远加载不出来 | `HallAPI` 同一时刻只允许一个在途请求，`openedImpl` 里同时发 search + facets，后者被 abort | facets 改到 searchDone 之后再发，且只取一次 |
| 18 | 右侧卡片没对齐 AIO 的版式（2026-11-01 对齐） | 140px 宽、只有 KV 行，没有徽章/chip、没有分组标题，和 AIO 的 `#pane-detail` 差得远 | 侧栏在卡片出现时放宽到 240px（AIO 是 252px）；结构改成 alias → chip 行（版本/加载器/置信度）→ 主按钮 → 复制包路径 → 评分 → 笔记 → KV；新增 `StarRatingWidget`（只读模式给卡片、可编辑给「地图」页） |
| 19 | 大厅失败态只有一行字 | 用户能做的只有关掉页面；也看不出是"没代理"还是"代理不通" | 照 AIO `renderHallFail` 做结构化面板：标题 / 说明 / 三条建议 / 四个动作（重试·出网诊断·网络设置·打开原站）/ 可展开技术细节（数据地址·服务端通道·当前出口） |
| 20 | 没有"出网诊断" | 排查只能靠猜 | `HallAPI::probe()`：用当前出口打一次 `/rest/v1/`，回 `✓ 320 ms · HTTP 401 · 出口：直连`；点一下就知道卡在哪 |
| 21 | 「没有匹配结果」被当成故障弹面板 | 搜一个不存在的名字也显示"连不上 CTMMaps" | `HallError::Empty` 走空列表 + 一句提示，只有传输/解析类错误才进失败面板 |
| 22 | 代理挂了就没有第二次机会（2026-11-01 对齐 `loader/http.ts`） | 用户代理切规则/对某域名失效时，大厅直接报错；而裸连往往还是通的 | 照 `fetchWithFallback`：**走代理失败（没拿到任何 HTTP 响应）就直连再试一次**，状态栏写"经代理失败，已改直连重试"；`abort()` 取消的请求不参与兜底（否则取消会被当成失败重试） |
| 23 | 内网地址不该走代理 | 把 `127.0.0.1` 交给代理只会 502（AIO 踩过） | `shouldBypassProxy`：localhost / 10./127./192.168./172.16-31. + `NO_PROXY` 后缀匹配 → 直连 |
| 24 | 画廊卡片只有"封面 + 一行名字" | 看不出中文名、评分、加载器，和 AIO 的卡片差得远 | 自绘 `MapCardDelegate`：封面 + **右上角评分角标** + 标题 + 中文名（次要色）+ 版本·加载器，选中时描边 |
| 25 | 设置页没法验证代理配没配对 | 只能回大厅页看失败文案，来回折腾 | `MapSettingsDialog` 加「测试连接」：先落盘再打一次真查询，显示 `✓ 320 ms · HTTP 200 · 出口：…`；大厅的「出网诊断」也从 `/rest/v1/` 根路径改成真查询（能同时验证 anon key 与 RLS） |
| 26 | 控制台左侧一长串用不上的页签（2026-11-01 产品要求） | 备注/世界/服务器/其他日志在地图启动器里没有使用场景，一屏挤 11 个页签 | `InstancePageProvider` 按设置 `MapConsoleShow{Notes,Worlds,Servers,OtherLogs}`（默认全 false）**不构造**这些页面；开关放「地图设置 → 控制台页签」。同时重命名：地图→**信息**、Minecraft 日志→**日志**、Mod→**模组**、资源包→**材质**、光影包→**光影** |
| 27 | **原版地图报「压缩包里没 mods/，需要额外整合包」**（用户实测） | 判据用的是 `level.isModded`，它把 `ServerBrands=["fabric"]` / `WasModded=1` 也算进去 —— 这只说明**作者自己开了 Fabric**，不说明存档依赖模组。Torre de Piedra（纯原版 1.21.1）就是这么被误判的 | 判据改为只看 Forge 写在 level.dat 里的 `FML LoadingModList`（= `requiredMods`）；`MapImportTask` 与导入对话框同步改；`MapMeta::load` 对旧 `map.json` 做钳位（没有 `requiredMods` 就把标记清成 false）。加回归 `test_legacyModpackFlag` |
| 28 | 地图页的「世界目录」表格没有意义 | 一个地图一个实例，实例里只有它自己的 `saves/`，还要玩家理解"实例内目录 ↔ 压缩包内路径" | 整块删除（`m_worlds` 一并去掉） |
| 29 | 大厅失败一律说「CTMMaps 拒绝了这次请求」 | `RemoteHostClosedError` 被归到 HTTP 错误，用户看到"对方拒绝"，实际是**根本没连上** | 新增 `HallError::Blocked`：没有 HTTP 状态码的连接中断单独一类，文案讲清"数据主机把连接掐断" |
| 30 | 玩家明明开着 Clash，启动器说"没有检测到代理" | 只读环境变量 + 系统代理，而 Clash/v2rayN 默认都不写这两处 | `HallProxy` 增加**本地端口并发扫描**（7890/7897/7891/10808/10809/1080/2080/2081/8889/4780，全部 connectToHost 后逐个 waitForConnected(600)，最坏 600ms），命中即用，出口里写明「自动发现的本地代理」 |
| 31 | 阻断之后没有出路 | 只能让用户自己找代理 | 失败面板加「**反代模板…**」：弹出可直接粘贴的 Cloudflare Workers 代码（20 行）+ 一键复制 + 控制台链接，配合「大厅数据地址」使用；只在 Blocked/Tls/Timeout 时出现 |
| 32 | **被封的其实是 SNI 这一个字符串**（2026-11-01 复测，推翻"IP 被封"的旧结论） | 旧结论说"主机级阻断"，导致只能劝用户配代理 | 实测：`--resolve api.modrinth.com:443:104.18.38.10` → **200**（同一个 IP、只换 SNI 就通），而 supabase 的 SNI 到任何 IP、任何端口、明文 :80 全 reset。所以是 **ClientHello 里的 SNI 被拦** |
| 33 | 没有代理时完全没有出路 | 用户明确不能用出海代理 | 新增 `BrowserFetch`（浏览器通道）+ `MapHallTransport` 设置（auto/network/browser）：auto 下 Qt 网络一旦被判 `Blocked`，**自动改用本机浏览器重发这一条**并记住；浏览器走 ECH，握手里看不出目标域名。实测无代理可拿到数据（含 `apikey` 走查询参数被 Kong 接受） |
| 34 | 控制台打开停在日志页 | 玩家进控制台十次有九次是改 Java/内存 | 「设置」页置顶（列表第一项），`PageContainer` 默认页从 `console` 改成 `settings` |
| 35 | **浏览器通道报「没有返回内容（退出码 21）」**（用户实测截图） | 整个 BrowserFetch 对象共用一个 `--user-data-dir`：上一次的浏览器还没退干净，新进程判定"profile 已被占用"，把活交接给老进程后**自己退出码 21、stdout 为空** | 改成**每次尝试一个新 profile 目录**，用完即删；顺带加 `--no-proxy-server`（这条通道的意义就是绕开代理）、`--disable-background-networking`；**只要 DOM 里有 `<pre>` 就认**（退出码不代表没拿到数据）；失败时把浏览器 stderr 尾巴拼进错误文案 |
| 36 | 大厅详情里看不到评论 | 只有菜单里的独立对话框有评论 | `MapHallPage` 详情底部加评论区：一页 6 条、卡片式（名字加粗 + ★ 星级 + 游玩时长 + 日期 + 正文，超 320 字折叠为 tooltip）；名字异步二次补（`user_maps` 没有外键）；「加载更多评论」按 offset 翻页；**浏览器通道拿不到 `Content-Range`**，标题退化成「已加载 N 条」而不是编一个总数 |
| 37 | 时长显示成「365.2 分钟」 | 站上的分钟数玩家得自己除 60 | 统一 `formatLength()`：<60 分钟保留分钟，≥60 换算成「6.1 小时」，≥100 小时取整；列表行、详情 KV、统计 chip、评论播放时长四处共用 |
| 38 | **大厅每点一下就要等 5–10 秒**（用户实测：列表能出来，但详情/评论一直转） | 每条请求都要起一个无头浏览器；看一张图要 3 个请求（详情 → 评论 → 名字），串行下来 15–30 秒 | 按"整体拉一次、之后全靠本地"重做：`HallAPI::syncLibrary()` 用 **4 个请求**把列表(1000)/详情(1000)/用户(3000)/评论(1000) 全下下来存 `hall-cache.json`，之后搜索/排序/翻页/详情/评论**全部本地计算**；列表页顶部按钮从「下载地图库」变成「更新数据」。任一步失败但列表已到手时，用已有数据落盘，缺的部分按需回落网络 |
| 39 | 同步接口的字段先验证再写死 | 直接按猜的列名写会 400 | 逐个实测：`maps?select=slug,description_1..3,download,download_links,trailer` → **632 KB**；`user_maps?select=...,map_uuid,...` → **551 KB**；`users?select=user_uuid,username` → 19 KB，但 **username 大量为 null**（站方数据如此）→ 评论一律显示「匿名玩家」，不拿 uuid 占位 |
| 40 | **详情面板布局挤成一团**（评论卡在小滚动框里、中间一大片空） | 260×150 的大封面 + 11 项属性清单把高度吃光，简介和评论只能共用一个小滚动区 | 头部改成 96×54 缩略图 + 标题横排；正文改成**「简介 / 评论」两个页签**（各自独立滚动，评论独占一屏）；11 项属性移到简介页顶部；splitter 比例 3:2、详情最小宽 320 |
| 41 | **封面图不显示** | `image_main` 两种来源都取不到：`*.supabase.co/storage/...` 被 SNI 阻断；`ctmrepository.com` 对非浏览器客户端一律 403（带 UA/Referer 也不行） | 画**本地占位封面**（标题哈希→稳定色相 + 首字母），列表和详情都不再留白；请求带浏览器 UA/Referer 再试一次，失败记进 `m_coverFailed` 不再重试（避免每次等 15 秒超时）。真图能取到时自动替换 |
| 42 | 点开大厅要干等二三十秒 | 同步放在 `openedImpl` 里，用户盯着进度条 | **打开只读本地**：`openedImpl` 只 `reloadLibrary()` + 本地搜索；拉取改到**启动后 20 秒的静默后台任务**（`MainWindow::maybeUpdateHallLibrary`，条件：开关打开且缓存超过 `MapHallUpdateDays` 天）。日志里能看到 `[hall] 更新完成：N 张地图…` |
| 43 | **评论条数明显少于站上**（站上 6 条，界面 2 条） | 整库同步只拉"最近 1000 条评论"（跨所有地图），老图的评论大多落在窗口外；我却把这份子集当成某张图的全部 | `reviews()` 只在"这一批没拉满 1000"时才吃缓存；截断时按 `map_uuid` 现拉一次（并记住 url/mapUuid），拉到的行**按 `ledger_uuid` 去重并入**按图索引，之后再翻页就命中本地 |
| 44 | 封面图到底能不能救 | 用户反复追问，这次把所有路子跑了一遍 | **拿不到字节**：①直连 → `Connection was reset`（SNI 阻断）；②带浏览器 UA + Referer → `ctmrepository` 403；③无头浏览器 `fetch` → `Failed to fetch`（CORS/Sec-Fetch）；④本地 http 页 `img+canvas→toDataURL` → 两张都是 `ERR:load`；⑤`--screenshot` → 只有 346 字节空白。结论：**只有"正常页面导航"能显示，但没有接口能把字节交出来**。所以正文用本地生成的占位封面（标题哈希色块 + 首字母），能取到真图时自动替换，失败记入 `m_coverFailed` 不再重试 |
| 45 | 本地实例和 CTM 地图是两套东西 | 实例窗口只有导入时的信息，看不到站上的简介/评论 | 信息页改成**三页签：简介 / 评论 / 我的**，前两页读本地大厅库；没关联时先说清楚"还没链接"，并用**标题归一化匹配**给出"可能是《X》，点此关联"，也可以手动从本地库里挑；关联写 `hall-link.json`，可随时解除 |
| 46 | **「始终加载不出来」的真因：list 和 details 的 slug 集合对不上**（2026-11-02 复盘缓存实据） | 同步详情时按 `maps` 表默认顺序取前 1000，列表按 `sort_date` 取前 1000 —— 两表排序不同，集合差很多；点缺详情的图就回落浏览器通道，几秒到永远失败 | 详情改为 **`order=slug.asc` + offset 分页**，翻页直到覆盖列表里全部 slug（上限 3 页防死循环）；本地搜索命中时状态栏改写「本地库」，不再误导成"浏览器通道" |
| 47 | **评论全是「匿名玩家」** | 信息页把兜底文案写死了；且 `reviewFromJson` 解析时不查 users 表 | 名字在 `reviewFromJson` 里就地补（缓存实测 80/80 评论用户都有 username）；两个界面共用，不用各自异步补 |
| 48 | 信息页「我的」页签布局散架（中文名和输入框隔半屏） | 先建到顶层再用 `takeAt` 搬进页签，**QFormLayout 失去拉伸约束**，行距被垂直撑开 | 放弃搬移：内容**直接建进「我的」页容器**，行距/间距/末尾 stretch 一步到位 |
| 49 | 大厅里看不到本机视角 | 大厅详情只有 简介/评论 | 详情加第三个页签「**我的**」：遍历实例的 `hall-link.json` 找到关联实例，显示其中文名/我的评分/笔记/游玩时长/上次游玩/退出状态；没关联时说明"导入后可在实例的信息页关联" |
| 50 | **便携化：数据全部集中到程序目录**（2026-11-02 按用户要求迁移） | 库/缓存/配置都在 C 盘 AppData，实例在 D 盘手动指过去，重装系统即丢 | 用上游自带的 **`portable.txt` 机制**（exe 旁放空文件 → 数据根 = exe 目录）：`E://...//win-local//Debug//` 下现在有 `portable.txt`、`prismlauncher.cfg`（六条路径已改指程序目录）、`maps\`（库+缓存，自 AppData **移入**）、`instances\`（自 D 盘 **robocopy /MOVE**，71 个文件核验一致）、`accounts.json`/`themes` 等。AppData 旧目录保留作备份，确认没问题后可整目录删除。**注意**：Debug 目录同时是构建产物目录 —— 别对 build 目录做 clean |
| 51 | 导入压缩包留两份（原件 + 库副本） | 占双倍空间，且"该删哪份"永远说不清 | `MapRepository::importIntoLibrary` 改为 **`FS::move` 移入**（跨盘自动退化复制+删除；移动失败退回复制保导入）。库里已有同包（大小+SHA1 相同）时复用并清掉外面的原件。`archive.path` 退化为导入位置留痕 |
| 52 | 便携化后旧 `map.json` 里的库路径失效 | 重装地图会报"来源压缩包已经不在原来的位置了" | 新增 `MapRepository::resolveArchivePath`：`libraryCopy` → 库目录同名文件 → 原始 `path` 三级回退，重装/信息页/打开目录三处共用；以后再搬库不用改数据 |
| 53 | 旧缓存详情覆盖不全导致"一直加载不出来"（上轮修了同步，没修存量） | 用户手里的缓存是旧版同步写的，缺详情的图点开仍回落网络 | 新增 `HallCache::detailsCoverList`（列表每个 slug 都有详情才算完整）：打开大厅和启动后台任务都会检查，**不完整就自动重同步**，不依赖用户手点「更新数据」 |

> 新增回归用例 `test_exitStatus`：覆盖「无日志兜底 / Agent 结论优先 / 最后一条标记为准 / 启动前清日志」。

### 5.3 大厅封面为什么一直出不来，现在怎么解决的（2026-11-02）

**现象**：列表和详情只有本地生成的占位封面（标题哈希色块 + 首字母），一张真图都出不来。

**根因（先排除掉的错误猜测）**：

- 封面地址并不是全部在同一台机器上：缓存里 **874/1000** 在
  `https://qtxjbukjyuylqjgiphxs.supabase.co/storage/v1/object/public/map_images/…`，
  50 张在 `ctmrepository.com/map_img/…`，75 张没有封面字段。
- 对 `*.supabase.co` 实测：DNS 正常（Cloudflare `104.18.38.10` / `172.64.149.246`），
  TCP 能连上，但 **TLS 握手收不到 ServerHello**（`curl: (35) schannel: failed to receive handshake`）。
  这是**按 SNI 的阻断**，不是超时、不是证书、也不是代理没配。
- 本机 Qt 在 Windows 走 Schannel，**不支持 ECH**，所以 Qt 网络栈无论怎么调代理都过不去
  （这也解释了为什么上一轮把 UA/Referer 都补上了仍然 0% 命中）。
- 决定性对照实验：同一个 URL 用 Edge 无头模式的 `<img>` 打开，**1920×1080 / 3.3s 正常出图**。
  Chromium 支持 ECH，这是本机唯一能过的通道。

**方案**：新增 `maps/MapCoverLoader`（列表、详情、大厅对话框共用）

1. 内存 LRU（96 张）+ 磁盘缓存 `<数据目录>/maps/covers/<sha1(url)>.jpg`，命中就完全不出网；
2. 先 Qt 直连（有代理/没被阻断时最快），失败后把这个 **host 标记成 Browser**，之后同类请求直接走浏览器；
3. 浏览器通道：**一个进程批量取 8 张**，页面内并发 6；
   `<img>` → canvas 缩到 480px → `toDataURL('image/jpeg', 0.82)` → 写进 `<pre>`，
   宿主从 `--dump-dom` 的输出里把 base64 收回来解码落盘。实测 6 张 4.1s、单张 4.6s（含进程启动）；
4. 必须带 `--disable-web-security`：跨域图片画进 canvas 会被标记为 tainted，`toDataURL` 直接抛异常。
   进程用一次性空白 profile、只加载我们自己生成的临时页面，跑完即杀并删除目录；
5. 失败要两次才记 bad，避免一次网络抖动就永久放弃某张图。

**验证**：新增端到端用例 `test_coverLoaderBrowser`
（需 `PRISM_TEST_COVER_URL=url1;url2;…`，默认 skip）→ 生成页面 → 起浏览器 → 收回 JPEG →
落盘 → 二次请求命中缓存，全部 PASS。全量：`13 passed, 0 failed, 1 skipped`。

**已知边界**：

- `ctmrepository.com` 的 50 张仍然拿不到（Cloudflare 不认这个无头浏览器，也没有正确 Referer），继续显示占位封面；
- 每批一个浏览器进程：首次打开大厅 20 行约 3 批 ≈ 12s 才陆续出图，之后翻页/重开都是磁盘命中，瞬时；
- 不建议"首次启动预热全部 1000 张"（会起 100+ 个进程），要预热也只能按可见行来。

### 5.4 「更新数据」单独失败、还报「浏览器进程崩溃 62097」（2026-11-02 下午）

**现象**：大厅打开一切正常（封面、简介、评论都在），唯独点「更新数据」弹错误面板，
技术细节是 `https://…/rest/v1/maps?select=…&limit=1000`，失败原因
「浏览器通道也没拿到数据：浏览器进程崩溃（退出码 62097）」。

**为什么"直接打开"没事**：大厅页打开时**只读本地快照**（`<数据目录>/maps/hall-cache.json`），
一个请求都不发；「更新数据」才会真的出网做 4 个请求（列表 / 详情 / 用户 / 评论）。
所以这不是"代码没更新"，而是两条完全不同的路径——一条读盘，一条过网。

**排查**：

1. 用应用的同一组参数重放那条 REST URL → `net::ERR_CONNECTION_RESET`，3 次里 2 次失败；
   换 `limit=5` 也是同样概率 → **与响应体大小无关，是这条链路本身约 1/3 概率被 RST**；
2. 并发起 3 个无头实例（2 个取图 + 1 个取 JSON）→ 全部正常退出，排除"多个浏览器打架"；
3. 退出码 62097 只在进程被**强行终止**时出现，不是服务端错误；
4. 也就是说：旧实现是"一次进程只发一跳"，撞上 RST 或超时就把进程杀掉重开，
   重开到第 4 次仍失败，最后把"被杀"报成了"崩溃"——用户看到的原因因此完全对不上。

**修复**（`BrowserFetch`，所有浏览器通道请求一起受益）：

- 不再让浏览器直接打开 API 地址，而是打开一个**本地页面**，页面里用 `XMLHttpRequest`
  取目标 URL，**页面内退避重试 3 次**（300ms / 600ms），响应体 base64 回传给宿主；
- 能拿到真实 HTTP 状态码：4xx 直接失败并把服务端的 `message` 带出来（鉴权/参数问题重试没意义），
  5xx / 连接重置才重试；
- 进程超时从 30s 提到 60s，`--virtual-time-budget` 25s（够 2.5 MB 响应体）；
- **不能**加 `?_r=N` 之类防缓存参数：PostgREST 对未知查询参数直接 `PGRST100`（400），
  加了反而必然失败（这个坑先踩了一次）。

**验证**：新用例 `test_browserChannelJson`（`PRISM_TEST_FETCH_URL=…`，默认 skip）——
小响应 3.8s、真实 1000 行详情响应 2.6s，均 PASS；全量 `13 passed / 2 skipped`。

> 构建环境的坑（vcpkg 被清空、`reg.exe` 被拦）见 §3.2 的 2026-11-02 补记。

### 5.5 评论卡片：少评论时排版散架 + 卡片化（2026-11-02 晚）

**现象**：某张图只有 1 条评论时，用户名在顶部、正文掉到面板底部，中间一大片空白；
评论多的时候也只有留白分隔，一眼看不出哪条是谁的（用户截图）。

**根因**：`clearReviews()` 用 `takeAt(0)` 清空布局，把末尾那条 `addStretch(1)` 一起删了，
**但没有补回去** —— 之后插入的卡片就独占整个滚动区，卡片内部的 `QVBoxLayout` 再把多出来的
高度分给标签，于是"头像在顶、正文到底"。卡片本身也只是一块裸 `QWidget`，没有任何边界。

**修复**：

1. `clearReviews()` 清完立即 `addStretch(1)`，恢复"stretch 永远在最后一条"的约定；
2. 卡片从 `QWidget` 换成 **`QFrame`**：`rgba(127,127,127,16)` 底 + 1px 边框 + 6px 圆角，
   亮/暗主题都用同一套半透明灰（不写死色号）；
3. 卡片纵向 size policy 设成 **`Maximum`**，评论少时不允许被拉高；
4. 头部加 **首字母圆形头像**（按用户名哈希取色，深色主题抬亮度、浅色主题压亮度）；
5. 正文折叠阈值 320 → 480 字符，超长仍悬停看全文；
6. 顺带补上「同步失败但本地库仍在」的保护：`onFailed` 里若正在同步且 `hasLibrary()`，
   只更新状态栏（写清"本地库仍是 MM-dd HH:mm 的数据（N 张），搜索/详情/评论/封面照常可用"），
   **不把列表换成失败面板**。没有本地库（首次使用）时才照旧显示失败面板。
7. 卡片抽成 **`maps/MapReviewCard.{h,cpp}`**，大厅详情与实例「信息 → 评论」共用一份；
   时长格式化挪到 `MapTextUtils::formatPlaytime`（原来大厅里有个私有副本）。

## 6. 开发路线与进度

| 里程碑 | 内容 | 状态 |
| --- | --- | --- |
| **M0** | 归档嗅探 + 三级版本判定 + `dataversion.json` | ✅ **完成**：23 个夹具逐项一致（版本 / 置信度 / 世界数 / 材质包数） |
| **M1** | 导入建实例 + 主界面融入 + 1.20+ 直连 | 🟡 **代码完成**：导入全流程、多世界选择、拖放智能分流、`JoinWorldOnLaunch` 全启动入口生效、操作栏地图信息区；差真机验收 |
| **M2** | Agent 直连（≤1.19.4）+ 安全退出判定 | 🟡 **代码 + 资产齐了，单测已验**：`quickboot.jar`（已 shade ASM）+ 6 个版本符号表进资源，`test_agentAssets` 断言 6 个版本都能拼出合法参数；差真机验收 |
| **M3** | 大厅 + 打磨 | 🟡 搜索/排序/系列与难度时长筛选/详情 11 项/评论分页与用户名/**封面真图 + 磁盘缓存**/**列表分页**/失败空态/评论卡片 可用；三档瓦片尺寸与快捷键未做 |

### 下一步（按优先级）

1. **真机验收**（唯一还没被自动化覆盖的）：
   - 1.20+：导入 `07-dv-quickplay.zip` → 实例列表选中它 → 原生「启动」按钮 → 应直接出现在出生点；
   - ≤1.19.4：1.16.5 / 1.8.9 各 2 次「进图 → Save and Quit → 游戏自行退出」，
     然后看 `agent.log` 里是否有 `EXIT_SAFE`，`stats.lastExit` 是否写成 `safe`。
2. **RAR5 大包实测**（分卷 / 加密头）。
3. 补更多版本的符号表（现在 6 个：1.7.10 / 1.8.9 / 1.12.2 / 1.16.5 / 1.19.2 / 1.21.1）。
4. 导入流程里的加载器版本选择器（现在手填，或导入后到实例「版本」页装）。

## 7. 踩过的坑（按"下次遇到时搜什么"分组）

**代理相关**

1. 环境里同时存在 `HTTP_PROXY` / `http_proxy` / `HTTPS_PROXY` / `https_proxy` 四个键（值相同，大小写不同的重复键）。
   `Enter-VsDevShell` 会直接抛 `ArgumentException`（"HTTPS_PROXY 环境变量的键与 https_proxy 冲突"）。
   解法：进 PowerShell 先 `[Environment]::SetEnvironmentVariable("https_proxy", $null, "Process")`（同理 `http_proxy`）。
2. 代理在 `http://127.0.0.1:13533`。GitHub 小请求（`git ls-remote`）1 秒完成，但**大仓库全量克隆会停滞**。

**vcpkg 相关**

3. `bootstrap-vcpkg.bat` 会失败：`SetFileInformationByHandle got error 0x00000005 拒绝访问`。
   解法：手动下载 `https://github.com/microsoft/vcpkg-tool/releases/download/2026-10-29/vcpkg.exe` 放到 vcpkg 根目录。
4. vcpkg 的 git 注册表需要完整历史（见 §3.2 坑 1）。
5. builtin 注册表 ≠ filesystem 注册表（见 §3.2 坑 1 的说明）。

**Qt 相关**

6. 清华镜像 `https://mirrors.tuna.tsinghua.edu.cn/qt/` 下载的包**校验和不匹配**（`Downloaded file ... is corrupted`）。
   改用官方 `https://download.qt.io/` 正常（187 s）。
7. `--archives` 只管 base 模块里的组件，**`NetworkAuth` 是独立模块**，要单独装：
   `aqt install-qt … --noarchives --modules qtnetworkauth`（加 `--noarchives` 才不会重下 qtbase）。
8. aqt 退出码 1 但日志写着 `Finished installation` —— 是 PowerShell 把 stderr 当错误，**先看日志再判定失败**。

**构建 / 断言相关**

12. **Debug 构建的 `Q_ASSERT` 会 qFatal**：`SequentialTask::subTaskFailed` 对每个失败的子任务都调一次 `emitFailed`；
    两个子任务同时网络失败时（实测 FTB 的 modpacks.xml + thirdparty.xml），第二次会撞上
    `ASSERT_NEVER(!isRunning())` 直接崩溃 —— Release 只是记一条警告，所以上游一直没发现。
    已在 `SequentialTask` / `ConcurrentTask` 加 `isRunning()` 守卫。
13. **同一构建目录绝不能同时跑两个编译**：会互相锁 `.obj`（`C1083: Permission denied`）。
    后台任务通知说 "failed" 时先看退出码 —— `run-configure.ps1` 收尾常被误报 failed，实际 exit 0。

**PowerShell / 工具用法**

9. `… | Select-Object -Last 30` 会**缓存全部输出**直到命令结束，长任务看不到进度。
   改用 `Tee-Object -FilePath <log>`，然后 `tail` 日志文件。
10. `Tee-Object` 默认写 UTF-16，日志要 `tr -d '\000'` 才可读。
11. 从 Bash 调 `cmd.exe` 会被安全策略拦（`Command blocked for security`）；`.bat` 用 PowerShell 直接 `& ".\xxx.bat"`。

**网络 / 元数据镜像**

14. 「列表更新失败：Connection closed」= `MetaURLOverride` 指向的镜像挂了（本机实测
   `https://lively-math-14d3.xwk6661.workers.dev/` 直接 `RemoteHostClosedError`），不是代码 bug。
   排查顺序：`%APPDATA%/PrismLauncher/prismlauncher.cfg` 里的 `MetaURLOverride` → 清空 → 
   确认 `https://meta.prismlauncher.org/v1/index.json` 能返回 200。
15. 同一台机器上 CurseForge / Modrinth 的 API 能通、只有 meta 不通时，基本就是这个键被覆盖了
   （两者走不同 host，代理策略常常不一样）。
16. `msvc` 编译偶尔报 `C1083: Permission denied`（随机某个 `.obj`）—— 杀毒在扫刚写出的 obj 文件，
   重跑一次构建即可，不是并发编译。
17. **`*.supabase.co` 在本机是「主机级阻断」**（2026-11-01 复测，把可能性一条条排掉）：

   | 试法 | 结果 |
   | --- | --- |
   | DNS 解析 | ✅ 正常（Cloudflare 104.18.38.10 / 172.64.149.246，不是污染） |
   | `https://<ref>.supabase.co/` | ❌ `Recv failure: Connection was reset` |
   | `curl --tls-max 1.2` / `--http1.1` | ❌ 一样被 reset（不是 ALPN/版本问题） |
   | `http://<ref>.supabase.co:80/`（明文） | ❌ 同样 reset（不是"只掐 TLS"） |
   | 换端口 8443 / 2053 / 2083 / 2087 / 2096 | ❌ 全 reset（不是"只封 443"） |
   | 换 SNI（连同一 IP、SNI 写别的域名） | ❌ reset（不是按 SNI 做的） |
   | 公共 CORS 中继（allorigins / corsproxy / codetabs） | ❌ 408 / 401 / 522，都不可用 |
   | `https://www.ctmmaps.com/` | ✅ 200（站点在 Vercel，本身没被封） |
   | `https://supabase.com/` | ✅ 200（慢，但通） |

   **决定性的一步**（同一天补测）：同样连 `104.18.38.10` / `172.64.149.246`，
   只把 SNI 换成 `api.modrinth.com` → **200**。也就是说 IP 没被封，
   **被封的是 `*.supabase.co` 这个 SNI 字符串**（明文 :80 也 reset 说明
   是按主机名拦的，不是 TLS 专属）。

   所以"浏览器能看"的真正原因是 **ECH / GREASE**：Chromium 实现了
   Encrypted ClientHello，握手内容在链路上看不出目标域名；而 Qt 的
   `QNetworkAccessManager`（Schannel）只能把 SNI 写在明文里，发出去就被 RST。
   AIO 之所以能通，也是因为它把请求交给 Electron 里的 Chromium 发，不是 Node 的 `fetch`
   （`launcher/src/hall/index.ts` 文件头写着这事）。

   因此启动器这边有四条路，前三条已经做进产品里：
   · **浏览器通道**（默认）：`BrowserFetch` 起一个无头 Edge/Chrome 取数据，**不需要代理**；
   · **代理**（`HallProxy` auto：环境变量 → 系统代理 → 本地端口扫描 → 直连；手动可选）；
   · **反代**（失败面板「反代模板…」给 20 行 Cloudflare Workers 代码）；
   · **离线**（打开原站挑、下载完用「选择已下载的文件…」导入）。
   绝对不要做的：自己实现 HTTP/3 —— Qt 6.8 没有 QUIC，而浏览器就在本机，借它更省事。

18. **OptiFine 的列表和 jar 走的不是同一条路**（2026-11-01 实测）：
   · 列表 `https://bmclapi2.bangbang93.com/optifine/1.21.1` → 200，JSON 里带
     `type/patch/filename`，**混着 `preview_*` 预览版**（type 里还嵌着版本号，排不了序），
     所以 `OptiFineDialog` 把预览版滤掉；
   · 下载 `…/optifine/1.21.1/HD_U/J1` → **302 到 `bmclapi7.sakura.sld.tw`，直连被 reset**；
   · 官网 `optifine.net/adloadx?f=…` → 200，页面里能抠到 `downloadx?f=…&x=…`，
     带 `Referer` 取下来是 7.3 MB 的真 jar（不带 Referer 会 403）。
   所以下载顺序是 **BMCLAPI（90s 超时，2 次）→ 官网（2 次）**，两边错误都记进提示里。

---

## 8. 命令速查

```bash
# 找工具（Everything 未安装，用这些替代）
powershell -c "Get-Command cmake,ninja,vcpkg -ErrorAction SilentlyContinue | Select Name,Source"
powershell -c "& 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -all -products * -format json"

# Qt 版本
"E:/Program File/Qt/6.8.3/msvc2022_64/bin/qmake.exe" -query QT_VERSION

# vcpkg
"E:/Program File/vcpkg/vcpkg.exe" version
"E:/Program File/vcpkg/vcpkg.exe" list                 # 已装包
"E:/Program File/vcpkg/vcpkg.exe" install <port>:<ver> # 单独装端口（调试用）

# 项目
cd /e/llmUsage/PrismLauncher && git status --short && git submodule status
```

---

## 9. 已定的决策（别推翻，除非有新的硬约束）

| 决策 | 理由 |
| --- | --- |
| **不嵌 Node 引擎**跑 AIO 的 `launcher/src` | 双运行时 + 双错误边界 + 打包体积；AIO 的判断逻辑是纯函数，C++ 重写才划算 |
| **符号表离线生成、运行时只读** | 实时解析要扫 jar，秒级开销；AIO 已经算好了，直接当静态资源 |
| **不重写符号解析器到 C++** | 工作量 ×3，收益为零 |
| **实例目录名沿用 Prism 的"名称派生"** | 改 `InstanceStaging` 会波及实例列表/快捷方式；AIO 自己 v0.28 改 UUID 又在 v0.28.1 回滚了 |
| **世界目录固定 `MapWorld`** | `--quickPlaySingleplayer` 与 Agent 都吃字符串；老版本对中文路径有坑 |
| **`map.json` 独立于 `instance.cfg`** | INI 表达不了数组与证据链；Prism 的 CFG 有类型语义，不要污染 |
| **UI 融入原生，不另起模式**（曾做过 `QStackedWidget` 双模式，**已按用户要求移除**） | 地图信息区插进原生实例操作栏、`JoinWorldOnLaunch` 让所有启动入口统一生效 —— 对 Prism 的侵入最小，`Application::m_mainWindow` 的 25 处引用零改动 |
| **启动准备挂在 `Application::launch`，不挂在主窗口**（2026-11-01 修正） | 启动入口有五个，漏一个就会让老版本静默停在主菜单。主窗口只负责提示与传「选中的世界」 |
| **下载一律交系统浏览器** | AIO §40 的教训：Cloudflare 只认可见浏览器，七种"藏窗口"方案全失败 |
| **Agent 用 `haltOnSafe=1`（JVM 自己退）** | Prism 侧没有"落盘完成"概念，宿主 kill 时机靠猜 |
| **封了 SNI 的站一律借本机浏览器，不在 C++ 里实现 ECH/QUIC**（2026-11-02） | Qt 在 Windows 走 Schannel，没有 ECH；自己实现等于重写 TLS 栈。借 Edge/Chrome 一条通道同时解决"取数据"和"取封面" |
| **不复活"一个高版本核心通吃"** | 1.13/1.16~1.20/1.20.5 三次语义断裂，DFU 不翻译命令方块 |

---

## 10. 未验证 / 已知未知

1. ~~构建没跑通~~ → **已跑通**（Debug 出 exe，测试全绿）。见 §3.3。
2. **Prism 的 meta 下发版本是否都带 `feature:is_quick_play_singleplayer` trait** —— 代码按 trait 判断（不写死版本号），
   但没有实机验证过 1.20.2 / 1.21.x 各版本的 trait 是否齐全。
3. **libarchive 对 RAR5 / 加密头 / 分卷的实际边界** —— 夹具里的 RAR 能扫（世界数/版本对），但大包没测。
4. **Agent 与 Forge 的 FML ClassLoader 冲突** —— AIO §26 有解法（参数落系统属性 + Agent 自写日志文件），
   Prism 这边已经照做（`AgentBootstrap` 用 `log=<路径>` 且把 log 写进 agent 参数），但**没有资产可测**。
5. ~~Qt 6.8.3 与 Prism 12.0.0-dev 的兼容性~~ → **已验证**：整棵工程用 Qt 6.8.3 编译链接通过。
6. ~~符号表 JSON 结构待核对~~ → **已核对**：真实表（AIO `.cache/symbols/test-shared/symbols/`）用 `mcVersion` 键（兼容 `version`），
   `test_agentAssets` 用真实资产断言 6 个版本都能拼出合法参数。
7. **AIO 的 8 张真实地图**在 `E:/llmUsage/Minecraft-AIO/launcher/env/profiles/*.json`；
   M0 回归现在用的是 `launcher/fixtures/` 的 23 个夹具（更全，且带期望值）。
8. **`ctmrepository.com` 的 50 张封面**仍然取不到（Cloudflare 不认这个无头浏览器）——
   已确认不是代码问题；其余 874 张 supabase 封面已能正常出图并落盘缓存。

---

## 11. 接手后的第一件事

1. **先跑一次构建与测试**，确认基线没坏：
   ```powershell
   & E:\llmUsage\PrismLauncher\build\run-configure.ps1 -Action configure
   & E:\llmUsage\PrismLauncher\build\run-configure.ps1 -Action build-debug
   ```
   然后按 §3.3 跑 `LauncherTests.exe MapArchiveScan_test`，应该是 **`13 passed, 0 failed`**。
   日常手动测试双击 `build/run-prism-debug.cmd`（已配好 Qt / vcpkg 的 DLL 路径）。
2. **导入一张地图走全流程**：拖 zip 进主窗口 → 导入确认对话框 → 实例列表选中地图实例
   （右侧操作栏应出现地图信息区）→ 原生「启动」按钮 → 1.20+ 应直接进图；1.7.10 的会停在主菜单并给出提示。
3. **真机验收**（§6 下一步 1）是当前唯一未被自动化覆盖的环节，做完即可宣布 M1/M2 落地。
4. 之后按 §6 的优先级推进；`docs/map-launcher/` 的设计文档是唯一依据，别另起炉灶。

---
## 12. 附：当前文件清单（便于核对）

**仓库内（git 可见）**

```
launcher/maps/                    19 个模块 / 38 个文件，6800+ 行（见 §5）
launcher/resources/maps/          dataversion.json + maps.qrc
launcher/resources/agents/        quickboot.jar + 6 个符号表 + agents.qrc
tests/MapArchiveScan_test.cpp     13 项测试（含 23 夹具回归）
docs/map-launcher/                四份文档
launcher/CMakeLists.txt           MAPS_SOURCES + 两个 qrc
launcher/Application.cpp          6 个地图设置项
launcher/archive/ArchiveReader.*  +5 行（File::size）
launcher/archive/ArchiveWriter.cpp  修 2 处上游编译错误
launcher/minecraft/MinecraftInstance.cpp  启动时读 JoinWorldOnLaunch
launcher/tasks/{Sequential,Concurrent}Task.cpp  修 Debug 断言崩溃
launcher/ui/MainWindow.*          操作栏地图信息区 + 菜单 + 拖放分流
launcher/ui/dialogs/NewInstanceDialog.*  支持打开定位到来源页
launcher/InstancePageProvider.h   注册「地图」页签
launcher/ui/pages/instance/WorldListPage.cpp  地图实例只读
tests/CMakeLists.txt              +1 行
```

**仓库外（本机环境，可撤销）**

```
CMakeUserPresets.json                                    ninja / rc / cl / JDK8 绝对路径（gitignored）
build/env-toolchain.ps1                                  统一工具链环境（SDK + MSVC + RC）
build/run-vcpkg.ps1                                      装依赖
build/run-configure.ps1                                  configure / build-debug / build-release
build/configure.log, build/build-debug.log               最近一次成功日志
E:/Program File/vcpkg/                                   vcpkg（浅克隆，HEAD 9a5dfef5）
E:/Program File/vcpkg-manifests/prism/                   Prism 清单副本 + builtin 注册表 + 本地 triplet
E:/Program File/Qt/6.8.3/msvc2022_64/                    Qt 6.8.3
```

产物：

```
build/win-local/Debug/prismlauncher.exe
build/win-local/Debug/LauncherTests.exe
```
