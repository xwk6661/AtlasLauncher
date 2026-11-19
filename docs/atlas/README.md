# Atlas · 文档索引

**Atlas** 是一个以地图为中心的 Minecraft 启动器（12.0.0，C++/Qt 6.8，基于 Prism Launcher）：
把 AIO 版（`Minecraft-AIO`，独立仓库）的「点地图进图」搬到原生 Qt 上 ——
**点一张地图卡片，直接落进它的世界；版本由程序从存档推断。**

> 现在能用到什么程度：状态与待办看 [04](04-进度与待办.md)，每项功能验到哪一档看 [05](05-验证记录.md)。
> 一句话：构建与 33 个套件全绿；1.20+ 与 1.8.9 / 1.12 Forge / 1.16.5 / 1.19.3 已真机跑通；
> 真机与数据都在 Release 便携版 `E://Program File\AtlasLaucher`，Debug 只做界面；
> 地图库与导入对话框的近期界面改动还没在真实窗口点过。分支 `feature/map-launcher`。

---

## 文档地图

| 文件 | 只回答 | 什么时候看 |
| --- | --- | --- |
| **README.md**（本文） | 索引、30 秒速览、代码地图、术语 | 第一次进来 |
| [`01-设计.md`](01-设计.md) | **为什么**这么做、**怎么设计**：三条硬约束、与 Prism 的关系、路线取舍、数据模型、模块职责、两条主流程、下载栈、大厅通道、地图库模型、里程碑验收、风险 | 改设计之前 |
| [`02-构建与运行.md`](02-构建与运行.md) | **怎么跑起来**：工具链、五个环境坑、沙箱配方、测试命令与基线、资源库、打包与**数据迁移** | 换机器、构建报错、搬家 |
| [`03-决策与坑.md`](03-决策与坑.md) | **哪些弯路走过、哪些决定不许推翻** | 想改既有行为之前 |
| [`04-进度与待办.md`](04-进度与待办.md) | **现在到哪了**：里程碑、已完成、P0–P2 待办、数据档案、接手第一小时 | 接手第一小时 |
| [`05-验证记录.md`](05-验证记录.md) | **验到哪一档**：L0–L3 定义、功能 × 档位 × 证据矩阵、未验证清单、环境问题 | 想写"已完成"之前 |
| [`06-变更记录.md`](06-变更记录.md) | **改过什么、为什么**（§2–§97 档案，编号可被代码与提交引用） | 查某个行为是哪来的 |
| [`agent-matrix.md`](agent-matrix.md) | **哪些版本能自动进图**：60 张符号表逐张对真实 client jar 的复核结果 | 怀疑某版本停在主菜单时 |
| [`审查清单-2026-11-17.md`](审查清单-2026-11-17.md) | **一次审查的过程留档**（19 项）；结论已并入 [06 §89](06-变更记录.md) | 想追某条修复是怎么来的 |

阅读顺序：**README → 04 → 02 → 03**；要动设计读 01；要讲清"为什么不做 X"读 01 与 03。

**维护约定**（同一件事只写一处，避免改一处就漂移）：

| 只写在这里 | 内容 |
| --- | --- |
| [04](04-进度与待办.md) | 一切状态与**具体数字**：里程碑、已完成、待办、实例/库/缓存/产物/测试基线 |
| [05](05-验证记录.md) | 验证档位、证据、未验证清单（04 只引用，不重复） |
| [03](03-决策与坑.md) | 一切决策与理由、踩过的坑 |
| [01](01-设计.md) | 分层、数据结构、两条主流程、里程碑的**验收定义** |
| [02](02-构建与运行.md) | 工具链、构建/测试/打包命令、仓库外改动 |
| [06](06-变更记录.md) | 改过什么、为什么；**不写"现在还剩什么"** |

---

## 30 秒速览

**已经能做的**

- 导入地图（zip / rar / 7z）→ 免解压嗅探 → 三级版本判定（带置信度与证据）→ 建**物理隔离**的实例；
- 点开就进图：≥1.20 走 `--quickPlaySingleplayer` + `introskip`（跳过「实验性设置」页，不动世界数据）；
  ≤1.19.4 走随包 Java Agent（60 张符号表；1.19+ 走 `WorldOpenFlows.loadLevel`）；
- 退出即关：Agent 确认落盘后自退；≥1.20 由启动器读两句原版日志后关窗；
- Java 按版本自动选：≤1.16.5 → 8、1.17 → 16、1.18–1.20.4 → 17、1.20.5+ → 21、26.x → 21；导入与每次启动前各核一次，
  只改「没手选过」的实例，判不出或没装对版本就什么都不做；实例「启动」页签可看推荐、一键应用；
- 下载自己找路：断点续传、空闲/龟速两道看门狗、镜像↔官方互备、8 连接池 + 大文件 16 段；
  素材清单按 `assetIndex.sha1` 复用，上游重发索引才重下；
- 大厅整库本地化（2287 张，离线可搜可看）；封面缓存；失败页只回答三个问题；
- 本地地图库按「图」分组并默认收起，同一张图的英文原版/简中/繁中收在一个组标题下；
  右键分三级，可**重命名**（只改显示、不拆组）、可**关联到 CTM 地图站条目**（带候选与理由）；
  资源包形态只能挂到已装实例，不建实例、不动存档；
- 模组预设按实例版本与加载器现查 Modrinth / CurseForge 并自动补依赖；
- 导入时按你的习惯写 `options.txt`（九项，按版本能力逐项落地，判不出就跳过）；
- 资源库可整体搬到别的盘，改完立即生效；旧库文件按 sha1 认领，不重下。

**明确不做的**

- 启动器内下载（Cloudflare 只认可见浏览器）；
- 自研 ECH / QUIC（Qt 在 Windows 走 Schannel，直接借本机 Chromium）；
- 「一个高版本核心通吃」（1.13 / 1.20.5 两次语义断裂，DFU 不翻译命令方块）；
- 启动器内的地图机器翻译（2026-11-15 整体移除，见 [06 §83](06-变更记录.md)）。

---

## 代码地图

新代码在 `launcher/maps/`（**45 个 .cpp + 50 个 .h，25,797 行**），另有 `launcher/ResourcePaths`
与 `launcher/net/DownloadSource`；`net/{Request,FileSink}` 与 `Application` 为下载能力有较大改动。
对既有源码其余部分只做加法。逐文件职责见 [01 §6](01-设计.md)，文件级清单与测试数见 [04 §5](04-进度与待办.md)。

| 分组 | 文件 |
| --- | --- |
| **识别** | `MapArchiveScanner`（免解压嗅探）· `LevelDatReader`（NBT）· `MapVersionDetector`（三级判定）· `DataVersionTable` · `MapTypes` · `MapTextUtils` |
| **数据** | `MapMeta`（`map.json`）· `MapNotes`（`notes.json`）· `MapHallLink` · `MapRepository` · `MapDiskCleanup` |
| **导入** | `MapImportTask` + `MapReinstallTask`（建实例 / 解世界 / 落材质包 / 重装；两者同在一个 `MapImportTask.{h,cpp}`）· `MapImportDialog` |
| **地图库** | `MapLibrary` · `MapVariant` · `HallMatcher` · `MapPackAttach` · `MapLibraryPanel` |
| **启动** | `MapLaunchPrep` · `AgentBootstrap` · `JavaRecommend`（按版本选 Java）· `MapWindowFocus` · `MapQuitWatcher` |
| **大厅** | `HallAPI` · `HallTypes` · `HallProxy` · `BrowserFetch` · `HallCache` · `HallDialog` · `MapHallPage` |
| **界面** | `MapInstanceBar` · `MapInstancePage` · `MapGalleryPage/Window` · `MapReviewCard` · `MapInfoCard` · `MapCoverLoader` · `OptiFineDialog` · `StarRatingWidget` · `MapTheme`（色）· `MapStyle`（尺度）· `SeriesNames` |
| **模组** | `ModPresets` · `ModPresetDialog` · `ModSearchDialog` |
| **设置** | `MapSettingsWidget` · `MapPage` · `MapResourcePage` · `ResourceStore` · `launcher/ResourcePaths` |
| **下载** | `net/DownloadMonitor` + `ui/widgets/DownloadPanel` · `net/DownloadSource` · `net/{Request,FileSink}` · `Application::downloadNetwork()` |

资源：`launcher/resources/maps/dataversion.json`（483 版本）· `series.json`（系列代码 → 站上系列名）·
`launcher/resources/agents/`（`quickboot.jar` + `introskip.jar` + `introskip.json` + 60 张符号表）。

测试：33 个套件 / 443 项；地图相关 `MapArchiveScan`、`MapOptionsTxt`、`MapVariant`、`MapPackAttach`、`JavaRecommend`、
`MapSeriesNames`、`ModPresets`、`DownloadSource`、`RequestRetry`、`AssetIndexCache`（明细见 [02 §5](02-构建与运行.md)）。

资源库与迁移：库坐标与「四种换库」看 [02 §6.1–6.2](02-构建与运行.md)，打包之后怎么把现有数据搬进便携根看
[02 §6.4](02-构建与运行.md)（`tools/migrate-data.ps1`）。

---

## 快速开始

```powershell
$repo = "E:\llmUsage\AtlasLauncher"
& "$repo\build\run-configure.ps1" -Action configure
& "$repo\build\run-configure.ps1" -Action build-debug

# 界面调试：Debug 版靠 --dir 指向真实便携根，数据不复制
build\run-atlas-debug.cmd
# 真机跑图：E:\Program File\AtlasLaucher\atlas.exe

# 测试（单套件；全套走 ctest）
cd "$repo\build\win-local\Debug"; .\LauncherTests.exe MapArchiveScan_test
```

四个环境坑、沙箱配方与打包步骤 → [`02-构建与运行.md`](02-构建与运行.md)。

---

## 术语

| 词 | 含义 |
| --- | --- |
| **地图实例** | 一张地图一个 Prism 实例，靠 `map.json` 判定；`saves/` 里只有它自己的存档 |
| **`map.json`** | 自动产生的元数据：来源包、版本证据、世界列表、统计、语言变体。重装会覆盖 |
| **`notes.json`** | 玩家自己的数据：中文名 / 评分 / 笔记。任何自动流程都不许碰 |
| **`MapWorld`** | 世界目录的固定英文名（quickPlay 与 Agent 都吃字符串，中文路径有历史坑） |
| **资源库** | `assets` / `libraries` / `java` 三份跨实例共享资源；可整体迁移，改完立即生效 |
| **轨道 A / B** | A = ≥1.20 的 `--quickPlaySingleplayer`；B = ≤1.19.4 的 `-javaagent` 注入 |
| **Agent** | `quickboot.jar`，按符号表拼参数，进图后写 `agent.log` 供宿主判定退出是否安全 |
| **introskip** | `introskip.jar`，把 `WorldOpenFlows.askForBackup` 改成直接 `loadLevel`，跳过「实验性设置」页 |
| **浏览器通道** | 借本机 Edge/Chrome 无头模式取数据/取封面。目标站被封的是 SNI，Qt 没有 ECH |
| **大厅本地库** | `<数据目录>/maps/hall-cache.json`，整库拉全量，之后全部本地计算 |
| **便携模式** | exe 旁放 `portable.txt`，数据根 = exe 目录（当前真实根：`E://Program File\AtlasLaucher`；Debug 只留 `portable.txt` 当 UI 沙箱） |
| **系列标签** | 实例名前缀 `[US1]` / `[RC3]`；网格里显示成名字下方药丸，文本由 `SeriesNames` 翻译 |
| **验证档位** | L0 编译 / L1 单测 / L2 真实产物 / L3 真机（[05 §1](05-验证记录.md)） |
