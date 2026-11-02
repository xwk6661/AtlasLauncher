# 地图启动器 · 文档索引

把 [`Minecraft-AIO`](E:/llmUsage/Minecraft-AIO)（mcml v0.33.0，Electron + TypeScript 写的地图启动器）
的「以地图为中心」搬到 [`PrismLauncher`](E:/llmUsage/PrismLauncher)（12.0.0-dev，C++/Qt 6.8）上：
**点地图卡片直接进图，版本由程序推断**。

> **状态（2026-11-02）**：构建通过 · 测试 `13 passed / 0 failed / 2 skipped` ·
> M0 完成，M1–M3 代码落地、**等真机验收** · 分支 `feature/map-launcher`

---

## 文档地图

| 文件 | 回答什么问题 | 什么时候看 |
| --- | --- | --- |
| **README.md**（本文） | 索引、状态摘要、代码与术语地图 | 第一次进来 |
| [`01-可行性评估.md`](01-可行性评估.md) | **为什么可行**：AIO 的设计理念与原理、Prism 已有能力与缺口、三条路线取舍 | 要论证方向、或想理解代码为何这么分 |
| [`02-架构与路线图.md`](02-架构与路线图.md) | **怎么设计**：模块职责、磁盘布局与数据模型、导入/启动两条主流程、里程碑定义、风险登记 | 改设计之前 |
| [`03-环境与构建.md`](03-环境与构建.md) | **怎么跑起来**：工具链版本、四个环境坑与配方、构建/测试命令、仓库外动过什么 | 换机器、构建报错 |
| [`04-踩坑与决策.md`](04-踩坑与决策.md) | **哪些弯路走过、哪些决定不许推翻** | 想改既有行为之前 |
| [`05-进度与遗留.md`](05-进度与遗留.md) | **现在到哪了**：里程碑状态、文件级清单、还剩什么、哪些没验证 | 接手第一小时 |
| [`06-变更记录.md`](06-变更记录.md) | **改过什么**：问题总表（#1–#53）+ 四轮迭代记录 | 想查某个行为是哪来的 |

阅读顺序：**README → 05 → 03 → 04**；要动设计读 02；要讲清"为什么不做 X"读 01 与 04。

---

## 30 秒速览

**已经能做的**

- 导入地图（zip / rar / 7z）→ 免解压嗅探 → 三级版本判定（含置信度与证据）→ 建**物理隔离**的实例；
- 启动即进图：≥1.20 走 `--quickPlaySingleplayer`；≤1.19.4 走 `-javaagent`（6 个版本符号表已随包）；
- 退出判定：Agent 确认存档落盘后才算 `EXIT_SAFE`，回写 `map.json` 统计；
- 大厅：CTM Maps **整库本地化**（1000 张地图 / 2243 条详情 / 1000 条评论），搜索、排序、翻页、
  详情、评论全部本地算；封面走直连 → 浏览器通道，命中后磁盘缓存；
- 实例「信息」页三页签：简介（封面 + 基本信息卡）、评论（卡片流）、我的（三张卡片）。

**明确不做的**

- 启动器内下载（Cloudflare 只认可见浏览器）；
- 自研 ECH / QUIC（Qt 在 Windows 走 Schannel，直接借本机 Chromium）；
- 「一个高版本核心通吃」（1.13 / 1.20.5 两次语义断裂，DFU 不翻译命令方块）。

---

## 代码地图

新代码全部在 `launcher/maps/`（**28 个 .cpp + 31 个 .h，约 1.4 万行**），对既有文件只做加法：

| 分组 | 文件 |
| --- | --- |
| **识别** | `MapArchiveScanner`（免解压嗅探）· `LevelDatReader`（NBT）· `MapVersionDetector`（三级判定）· `DataVersionTable` · `MapTypes` · `MapTextUtils` |
| **数据** | `MapMeta`（`map.json`）· `MapNotes`（`notes.json`，玩家数据）· `MapHallLink`（实例 ↔ 大厅关联）· `HallCache`（大厅本地库） |
| **导入** | `MapImportTask`（建实例 / 解世界 / 落材质包 / 重装）· `MapRepository`（地图库）· `MapImportDialog` |
| **启动** | `MapLaunchPrep`（选轨道 / 统一准备点）· `AgentBootstrap`（符号表 → `-javaagent`） |
| **大厅** | `HallAPI` · `HallTypes` · `HallProxy` · `BrowserFetch` · `HallDialog` · `MapHallPage` |
| **界面** | `MapInstanceBar`（右侧地图卡片）· `MapInstancePage`（信息页）· `MapGalleryPage/Window` · `MapReviewCard` · `MapCoverLoader` · `MapSettingsWidget`/`MapPage`（设置页）· `OptiFineDialog` · `StarRatingWidget` · `MapTheme` |

资源：`launcher/resources/maps/dataversion.json`（483 个版本）、`launcher/resources/agents/`
（`quickboot.jar` + 6 个符号表）。测试：`tests/MapArchiveScan_test.cpp`（13 项 + 2 项联网用例）。

---

## 快速开始

```powershell
# 构建（Debug）
& E:\llmUsage\PrismLauncher\build\run-configure.ps1 -Action configure
& E:\llmUsage\PrismLauncher\build\run-configure.ps1 -Action build-debug

# 运行（已配好 Qt / vcpkg 的 DLL 路径）
build\run-prism-debug.cmd

# 测试
$env:PATH = "E:\Program File\Qt\6.8.3\msvc2022_64\bin;E:\llmUsage\PrismLauncher\build\win-local\vcpkg_installed\x64-windows\bin;" + $env:PATH
$env:PRISM_MAP_FIXTURES = "E:\llmUsage\Minecraft-AIO\launcher\fixtures"
cd build\win-local\Debug; .\LauncherTests.exe MapArchiveScan_test
```

细节、四个环境坑、沙箱下的临时配方 → [`03-环境与构建.md`](03-环境与构建.md)。

---

## 术语

| 词 | 含义 |
| --- | --- |
| **地图实例** | 一张地图一个 Prism 实例，靠 `map.json` 判定；`saves/` 里只有它自己的存档 |
| **`map.json`** | 自动产生的元数据：来源包、版本证据、世界列表、统计。重装会覆盖 |
| **`notes.json`** | 玩家自己的数据：中文名 / 评分 / 笔记。任何自动流程都不许碰 |
| **`MapWorld`** | 世界目录的固定英文名（quickPlay 与 Agent 都吃字符串，中文路径有历史坑） |
| **轨道 A / B** | A = ≥1.20 的 `--quickPlaySingleplayer`；B = ≤1.19.4 的 `-javaagent` 注入 |
| **Agent** | `quickboot.jar`，按符号表拼参数，进图后写 `agent.log` 供宿主判定退出是否安全 |
| **浏览器通道** | 借本机 Edge/Chrome 无头模式取数据/取封面。目标站被封的是 SNI，Qt 没有 ECH |
| **大厅本地库** | `<数据目录>/maps/hall-cache.json`，4 个请求拉全量，之后全部本地计算 |
| **便携模式** | exe 旁放 `portable.txt`，数据根 = exe 目录（当前 Debug 目录即如此） |
