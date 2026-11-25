# Atlas · 文档索引

**Atlas** 是一个以地图为中心的 Minecraft 启动器（0.1.0，C++/Qt 6.8，基于 Prism Launcher）：
把 AIO 版（`Minecraft-AIO`，独立仓库）的「点地图进图」搬到原生 Qt 上 ——
**点一张地图卡片，直接落进它的世界；版本由程序从存档推断。**

> 现在能用到什么程度：状态与待办看 [04](04-进度与待办.md)，每项功能验到哪一档看 [05](05-验证记录.md)。
> 一句话：构建通过，33 个套件 / 448 项全绿（11-24 复跑 20.55 / 22.95 / 25.29 / 32.01 s，11-25 复核 22.95 s）；
> 1.20+ 与 1.8.9 / 1.9.4 / 1.12 Forge / 1.16.5 / 1.19.3 已真机跑通；
> 真机与数据都在 Release 便携版 `E://Program File\AtlasLaucher`（装的是 11-25 18:33 的 0.1.0 包，
> 12,984,832 字节，43 条目、含自绘图标），Debug 只做界面；
> 地图库的系列行（期号 / 折线 / 聚合列）11-22 晚复看通过，删除拒绝与「规范命名后重装」11-23 验过，
> 三级右键 / 列宽记忆 / 右键归属 / 实例网格药丸 / 大厅失败页 / Java 推荐组 11-24 复看通过；
> **仍只到编译**：批量确认页、加载器版本下拉、重命名 / 关联对话框、打开目录选中文件；
> 11-22 的审查整改（并发、异步化）只做过 `ctest`，没做并发压测。
> 仓库 2026-11-25 与上游解绑（不再是 GitHub fork、tag 从 0 开始），默认分支 `main`（发布线），
> 工作分支 `feature/map-launcher`；上游按需 cherry-pick、不追平（[01 §13](01-设计.md)）。
> 发布还差什么看 [07](07-发布清单.md)（现为 **2 条阻断项**：诊断包、更新通道）；发布前该点的四项看 [08](08-真机复看清单.md)。

---

## 文档地图

| 文件 | 只回答 | 什么时候看 |
| --- | --- | --- |
| [仓库根目录](../../README.md) | **面向使用者与贡献者**：`README`（是什么 / 下载 / 许可）、`BUILDING`（从零构建）、`CONTRIBUTING`（提 PR 的验证要求）、`SECURITY`（私密上报）、`CHANGELOG`（版本摘要） | 第一次访问仓库 |
| **README.md**（本文） | 索引、30 秒速览、代码地图、术语 | 第一次进来 |
| [`01-设计.md`](01-设计.md) | **为什么**这么做、**怎么设计**：三条硬约束、与 Prism 的关系、路线取舍、数据模型、模块职责、两条主流程、下载栈、大厅通道、地图库模型、里程碑验收、风险 | 改设计之前 |
| [`02-构建与运行.md`](02-构建与运行.md) | **怎么跑起来**：工具链、五个环境坑、沙箱配方、测试命令与基线、资源库、打包与**数据迁移** | 换机器、构建报错、搬家 |
| [`03-决策与坑.md`](03-决策与坑.md) | **哪些弯路走过、哪些决定不许推翻** | 想改既有行为之前 |
| [`04-进度与待办.md`](04-进度与待办.md) | **现在到哪了**：里程碑、已完成、P0–P2 待办、数据档案、接手第一小时 | 接手第一小时 |
| [`05-验证记录.md`](05-验证记录.md) | **验到哪一档**：L0–L3 定义、功能 × 档位 × 证据矩阵、未验证清单、环境问题 | 想写"已完成"之前 |
| [`06-变更记录.md`](06-变更记录.md) | **改过什么、为什么**（§2–§131 档案，编号可被代码与提交引用；§94/§95 各重号一次，引用带日期） | 查某个行为是哪来的 |
| [`07-发布清单.md`](07-发布清单.md) | **发布就绪与否**：已完成项、阻断项与已决事项、首发动作（tag / 附件 / 说明）与宣发口径 | 想发版、或想知道"还差什么才能发" |
| [`08-真机复看清单.md`](08-真机复看清单.md) | **发布前必须点一遍的四项**：删除拒绝、规范命名后重装、九项 `options.txt`、1.20+ 直连；每项都写了步骤、通过标准与失败长相 | 要去做真机验证（含转交他人）时 |
| [`agent-matrix.md`](agent-matrix.md) | **哪些版本能自动进图**：60 张符号表逐张对真实 client jar 的复核结果 | 怀疑某版本停在主菜单时 |
| [`审查清单-2026-11-17.md`](审查清单-2026-11-17.md) | **一次审查的过程留档**（19 项）；结论已并入 [06 §89](06-变更记录.md) | 想追某条修复是怎么来的 |
| [`审查-2026-11-22.md`](审查-2026-11-22.md) | **全项目静态审查 + 整改状态**：7 个维度的 P0/P1/P2 清单、修改建议、路线图；§8 是逐项状态 | 想知道"还欠什么"或要接着改时 |

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
| `launcher/resources/i18n/atlas_en.json` | 新增界面文案的英文译文；**改了 `tr()` 源串就要同步这里**（`tools/i18n_extract.py` 提取） |

---

## 30 秒速览

**已经能做的**

- 导入地图（zip / rar / 7z）→ 免解压嗅探 → 三级版本判定（带置信度与证据）→ 建**物理隔离**的实例；
  导入时可挑加载器版本（按 MC 版本列本地 meta 的清单，默认推荐项，离线可手填）；
- 点开就进图：≥1.20 走 `--quickPlaySingleplayer` + `introskip`（跳过「实验性设置」页，不动世界数据）；
  ≤1.19.4 走随包 Java Agent（60 张符号表；1.19+ 走 `WorldOpenFlows.loadLevel`）；
- 退出即关：Agent 确认落盘后自退；≥1.20 由启动器读两句原版日志后关窗；
- Java 按版本自动选：≤1.16.5 → 8、1.17 → 16、1.18–1.20.4 → 17、1.20.5+ → 21、26.x → 21；导入与每次启动前各核一次，
  只改「没手选过」的实例，判不出或没装对版本就什么都不做；实例「启动」页签可看推荐、一键应用；
- 下载自己找路：断点续传、空闲/龟速两道看门狗、镜像↔官方互备、8 连接池 + 大文件 16 段；
  素材清单按 `assetIndex.sha1` 复用，上游重发索引才重下；
- 大厅整库本地化（2287 张，离线可搜可看）；封面缓存（设置里可一键清空）；失败页只回答三个问题；
- 本地地图库按**系列 → 地图**两层：系列行默认收起、聚合四列（**版本与语言写「~」**，分类与已装张数照常）；
  地图行一张图一行
  （`[MW7] Subterranean`），语言与地图版本在导入时再选；右键分三级，可**重命名**（只改显示、不拆组）、
  可**关联到 CTM 地图站条目**（带候选与理由）、可**删除**（有实例在用时整批拒绝）；
  资源包形态只能挂到已装实例，不建实例、不动存档；
- 大厅 ↔ 本地库互通：大厅列表标「已入库」，详情页可直接「从本地库导入」；
- 「本地库」底部「待确认关联 (N)…」把自动匹配、还没人工确认的关联一次列出：证据支持的行默认勾选，
  可批量确认、改归属或解除（只确认不改分组）；
- 界面可切英文：整个 `launcher/` 的 **901 条**中文源串有一份英文词表（显示层，落盘证据保持中文）；
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

新代码在 `launcher/maps/`（**48 个 .cpp + 53 个 .h，28,160 行**），另有 `launcher/ResourcePaths`
与 `launcher/net/DownloadSource`；`net/{Request,FileSink}` 与 `Application` 为下载能力有较大改动。
对既有源码其余部分只做加法。逐文件职责见 [01 §6](01-设计.md)，文件级清单与测试数见 [04 §5](04-进度与待办.md)。

| 分组 | 文件 |
| --- | --- |
| **识别** | `MapArchiveScanner`（免解压嗅探）· `LevelDatReader`（NBT）· `MapVersionDetector`（三级判定）· `DataVersionTable` · `MapTypes` · `MapTextUtils` |
| **数据** | `MapMeta`（`map.json`）· `MapNotes`（`notes.json`）· `MapHallLink` · `MapRepository` · `MapDiskCleanup` |
| **导入** | `MapImportTask` + `MapReinstallTask`（建实例 / 解世界 / 落材质包 / 重装；两者同在一个 `MapImportTask.{h,cpp}`）· `MapImportDialog` |
| **地图库** | `MapLibrary` · `MapVariant` · `HallMatcher` · `MapPackAttach` · `MapLibraryPanel` · `HallLinkReviewDialog`（未确认关联的批量确认页） |
| **启动** | `MapLaunchPrep` · `AgentBootstrap` · `JavaRecommend`（按版本选 Java）· `MapWindowFocus` · `MapQuitWatcher` |
| **大厅** | `HallAPI` · `HallTypes` · `HallProxy` · `BrowserFetch` · `HallCache` · `HallDialog` · `MapHallPage` |
| **界面** | `MapInstanceBar` · `MapInstancePage` · `MapGalleryPage/Window` · `MapReviewCard` · `MapInfoCard` · `MapCoverLoader` · `OptiFineDialog` · `StarRatingWidget` · `MapTheme`（色）· `MapStyle`（尺度）· `SeriesNames` |
| **模组** | `ModPresets` · `ModPresetDialog` · `ModSearchDialog` |
| **设置** | `MapSettingsWidget` · `MapPage` · `MapResourcePage` · `ResourceStore` · `launcher/ResourcePaths` |
| **翻译** | `launcher/translations/AtlasCatalogTranslator`（按源串查表的补充词表，`resources/i18n/atlas_en.json`） |
| **下载** | `net/DownloadMonitor` + `ui/widgets/DownloadPanel` · `net/DownloadSource` · `net/{Request,FileSink}` · `Application::downloadNetwork()` |

资源：`launcher/resources/i18n/atlas_en.json`（901 条界面英文）· `launcher/resources/maps/dataversion.json`（483 版本）· `series.json`（系列代码 → 站上系列名）·
`launcher/resources/agents/`（`quickboot.jar` + `introskip.jar` + `introskip.json` + 60 张符号表）。

测试：33 个套件 / 448 项（11-24 复跑 20.55 / 22.95 / 25.29 / 32.01 秒）；地图相关 `MapArchiveScan`、`MapOptionsTxt`、`MapVariant`、`MapPackAttach`、`MapSeriesNames`、`JavaRecommend`、
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

# 改完代码 → 一条命令上机（构建 → 打包 → 覆盖便携根；/E 不删数据，-DryRun 只体检）
& "$repo\tools\deploy-local.ps1"

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
