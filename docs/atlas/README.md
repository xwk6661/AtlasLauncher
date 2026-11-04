# Atlas · 文档索引

**Atlas** 是一个以地图为中心的 Minecraft 启动器（12.0.0-dev，C++/Qt 6.8，基于 Prism Launcher）：
把 [`Minecraft-AIO`](E:/llmUsage/Minecraft-AIO)（mcml v0.33.0，Electron + TypeScript）的
「以地图为中心」搬到了原生 Qt 上 —— **点地图卡片直接进图，版本由程序推断**。

> **状态（2026-11-04 16:35）**：构建通过 · 测试 `39 passed / 0 failed / 2 skipped` ·
> M0 完成，M1–M4 代码落地 · 真机已过「1.20+ 进图」与「连开两次都自动关游戏」·
> **代码无阻塞项**，现实阻碍是网络（官方资源域在部分线路被断连，见 05 §8）· 分支 `feature/map-launcher`

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
| [`06-变更记录.md`](06-变更记录.md) | **改过什么**：时间线 + 问题总表（#1–#53）+ 六个专题 | 想查某个行为是哪来的 |

阅读顺序：**README → 05 → 03 → 04**；要动设计读 02；要讲清"为什么不做 X"读 01 与 04。

**维护约定**（避免同一件事写在两处、改一处就漂移）：

| 只写在这里 | 内容 |
| --- | --- |
| [05](05-进度与遗留.md) | 一切状态：里程碑、还剩什么、未验证、文件清单 |
| [04](04-踩坑与决策.md) §5 | 一切决策与理由（02 只放指针） |
| [06](06-变更记录.md) | 改过什么、为什么（不写"现在还剩什么"） |
| [02](02-架构与路线图.md) | 分层、数据结构、两条主流程、里程碑的**验收定义** |
| [03](03-环境与构建.md) | 工具链、构建/测试命令、仓库外改动 |
| [01](01-可行性评估.md) | 立项时的论证（已实现的结论就地加批注，不改写历史） |

---

## 30 秒速览

**已经能做的**

- 导入地图（zip / rar / 7z）→ 免解压嗅探 → 三级版本判定（含置信度与证据）→ 建**物理隔离**的实例；
- 启动即进图：≥1.20 走 `--quickPlaySingleplayer` + `introskip`（跳过「实验性设置」确认页，不动世界数据）；
  ≤1.19.4 走 `-javaagent`（6 个版本符号表已随包）；
- 退出即关：≤1.19.4 由 Agent 确认落盘后自己退；**1.20+ 由启动器读游戏日志**（离开存档 → 全部落盘）
  后关闭游戏窗口，回写 `map.json` 统计（真机连开两次均通过）。
- 大厅：CTM Maps **整库本地化**（1000 张地图 / 2243 条详情 / 1000 条评论），搜索、排序、翻页、
  详情、评论全部本地算；封面走直连 → 浏览器通道，命中后磁盘缓存；
- 下载源：设置 → API 顶部可切 BMCLAPI 镜像（资源 / 库 / 版本 json / 运行时清单），镜像直连不走代理，
  旁边有连通性测试；默认仍是官方源；
- 模组预设：内置性能/实用三套 + 可自建，按实例的 MC 版本与加载器现查 Modrinth / CurseForge，
  逐条带说明、可勾选，required 依赖自动补齐；缺加载器先引导安装；编辑器里能直接搜索添加；
- 实例「信息」页三页签：简介（封面 + 基本信息卡）、评论（卡片流）、我的（三张卡片）；
- 设置页：全局「地图」「资源」两页，实例「设置 → 地图」页签（可覆盖全局）；
  资源页能把 `assets / libraries / java` **真的迁移**到任意盘（同盘瞬间完成），换启动器不用重下。

**明确不做的**

- 启动器内下载（Cloudflare 只认可见浏览器）；
- 自研 ECH / QUIC（Qt 在 Windows 走 Schannel，直接借本机 Chromium）；
- 「一个高版本核心通吃」（1.13 / 1.20.5 两次语义断裂，DFU 不翻译命令方块）。

---

## 代码地图

新代码在 `launcher/maps/`（**36 个 .cpp + 40 个 .h，18,019 行**）、`launcher/ResourcePaths`
与 `launcher/net/DownloadSource`（下载源），
对既有文件只做加法：

| 分组 | 文件 |
| --- | --- |
| **识别** | `MapArchiveScanner`（免解压嗅探）· `LevelDatReader`（NBT）· `MapVersionDetector`（三级判定）· `DataVersionTable` · `MapTypes` · `MapTextUtils` |
| **数据** | `MapMeta`（`map.json`）· `MapNotes`（`notes.json`，玩家数据）· `MapHallLink`（实例 ↔ 大厅关联）· `HallCache`（大厅本地库） |
| **导入** | `MapImportTask`（建实例 / 解世界 / 落材质包 / 重装）· `MapRepository`（地图库）· `MapImportDialog` |
| **启动** | `MapLaunchPrep`（选轨道 / 统一准备点）· `AgentBootstrap`（符号表 → `-javaagent`）· `MapWindowFocus`（把前台交给游戏窗口） |
| **大厅** | `HallAPI` · `HallTypes` · `HallProxy` · `BrowserFetch` · `HallDialog` · `MapHallPage` |
| **界面** | `MapInstanceBar`（右侧地图卡片）· `MapInstancePage`（信息页）· `MapGalleryPage/Window` · `MapReviewCard`（评论卡）· `MapInfoCard`（信息卡工厂）· `MapCoverLoader`（封面）· `OptiFineDialog` · `StarRatingWidget` · `MapTheme` |
| **模组** | `ModPresets`（预设数据 + 双平台解析 + 依赖）· `ModPresetDialog`（安装 / 编辑）· `ModSearchDialog`（搜索添加） |
| **设置** | `MapSettingsWidget`（内容体）· `MapPage`（全局「地图」）· `MapResourcePage`（全局「资源」）· `ResourceStore`（迁移）· `launcher/ResourcePaths`（路径解析） |
| **退出** | `MapQuitWatcher`（1.20+ 读日志关游戏） |

资源：`launcher/resources/maps/dataversion.json`（483 个版本）、`launcher/resources/agents/`
（`quickboot.jar` + 6 个符号表）。测试：`tests/MapArchiveScan_test.cpp`（13 项 + 2 项联网用例）。

---

## 快速开始

```powershell
# 构建（Debug）
$repo = "E:\llmUsage\AtlasLauncher"
& "$repo\build\run-configure.ps1" -Action configure
& "$repo\build\run-configure.ps1" -Action build-debug

# 运行（已配好 Qt / vcpkg 的 DLL 路径）
build\run-atlas-debug.cmd

# 测试
$env:PATH = "E:\Program File\Qt\6.8.3\msvc2022_64\bin;$repo\build\win-local\vcpkg_installed\x64-windows\bin;" + $env:PATH
$env:PRISM_MAP_FIXTURES = "E:\llmUsage\Minecraft-AIO\launcher\fixtures"
cd "$repo\build\win-local\Debug"; .\LauncherTests.exe MapArchiveScan_test
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
| **资源库** | `assets` / `libraries` / `java` 三份跨实例共享资源；可整体迁移到别的盘，默认仍在数据目录 |
| **轨道 A / B** | A = ≥1.20 的 `--quickPlaySingleplayer`；B = ≤1.19.4 的 `-javaagent` 注入 |
| **Agent** | `quickboot.jar`，按符号表拼参数，进图后写 `agent.log` 供宿主判定退出是否安全 |
| **浏览器通道** | 借本机 Edge/Chrome 无头模式取数据/取封面。目标站被封的是 SNI，Qt 没有 ECH |
| **大厅本地库** | `<数据目录>/maps/hall-cache.json`，4 个请求拉全量，之后全部本地计算 |
| **便携模式** | exe 旁放 `portable.txt`，数据根 = exe 目录（当前 Debug 目录即如此） |
| **退出看门狗** | `MapQuitWatcher`：1.20+ 没有 Agent，靠日志判断「离开存档 + 落盘完成」再关窗口 |
