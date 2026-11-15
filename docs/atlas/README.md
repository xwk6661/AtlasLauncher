# Atlas · 文档索引

**Atlas** 是一个以地图为中心的 Minecraft 启动器（12.0.0-dev，C++/Qt 6.8，基于 Prism Launcher）：
把 [`Minecraft-AIO`](E:/llmUsage/Minecraft-AIO)（mcml v0.33.0，Electron + TypeScript）的
「以地图为中心」搬到了原生 Qt 上 —— **点地图卡片直接进图，版本由程序推断**。

> **状态（2026-11-15 13:30，以 [05](05-进度与遗留.md) 为准）**：构建通过 · 测试 `459 passed / 0 failed / 14 skipped`
> （全套 `ctest` 34/34 于 12:30 / 13:05 两次复跑；用例级 = 本机 00:42 实测的 454 / 14 加 §79 新增 5 项 ——
> 本机当下**没有**符号链接权限，34 个套件 / 473 项；开了权限是 `466 / 7`，差的 7 项就是符号链接用例）·
> M0、M2、M5 完成，M1 / M3 / M4 代码落地但仍有真机验收缺口，M6 主链路完成但**已隐藏** ·
> 真机已过「1.20+ 进图与自动关游戏」「≤1.19.4 Agent 进图与安全退出（1.8.9 / 1.12 Forge / 1.16.5）」
> 「大文件分片 + 续传 / 看门狗」「本地离线优先启动」「本地地图库」「资源库换目录（修复后同日复验：
> 不重下、不 `ClassNotFoundException`）」，真机反馈 4 条全部闭环（[05 §8](05-进度与遗留.md)）·
> **11-14 之后的地图库界面改动（按图折叠 / 右键改归属 / 导入页重排 / 资源包挂载 / 重装换版本 /
> 子行半高与三级右键菜单 / 列宽记忆 / 标签翻译成系列名）与实例网格的系列标签只过了编译与全套测试，
> 没在真实窗口点过**，待一次窗口复看（[05 §3](05-进度与遗留.md) 第 3 条）·
> 导入时写 `options.txt` 的语言与键位（[06 §55](06-变更记录.md)）·
> 汉化（M6）链路完整、7 张真实地图与参考实现对表通过，但**2026-11-12 起界面整体隐藏**（[06 §63](06-变更记录.md)、[07](07-一键汉化方案.md)）·
> 「地图」相关页面已统一到一套排版令牌（[06 §66](06-变更记录.md)），资源页/导入对话框的叙述性文案已收口（§67、§68）·
> Agent 符号表 60 张，逐槽位对真实 client jar 校验（[`agent-matrix.md`](agent-matrix.md)）· 分支 `feature/map-launcher`

---

## 文档地图

| 文件 | 回答什么问题 | 什么时候看 |
| --- | --- | --- |
| **README.md**（本文） | 索引、状态摘要、代码与术语地图 | 第一次进来 |
| [`01-可行性评估.md`](01-可行性评估.md) | **为什么可行**：AIO 的设计理念与原理、Prism 已有能力与缺口、三条路线取舍 | 要论证方向、或想理解代码为何这么分 |
| [`02-架构与路线图.md`](02-架构与路线图.md) | **怎么设计**：模块职责、磁盘布局与数据模型、导入/启动两条主流程、里程碑定义、风险登记 | 改设计之前 |
| [`03-环境与构建.md`](03-环境与构建.md) | **怎么跑起来**：工具链版本、五个环境坑与配方、构建/测试命令、仓库外动过什么 | 换机器、构建报错 |
| [`04-踩坑与决策.md`](04-踩坑与决策.md) | **哪些弯路走过、哪些决定不许推翻** | 想改既有行为之前 |
| [`05-进度与遗留.md`](05-进度与遗留.md) | **现在到哪了**：里程碑状态、文件级清单、还剩什么、哪些没验证 | 接手第一小时 |
| [`06-变更记录.md`](06-变更记录.md) | **改过什么**：一页结论（十条教训 / 时间线 / 主题索引）+ 逐节档案 §2–§79（含 #1–#187 的全部结论、11-13 的 UI/文档收口、11-15 的地图库两轮排版与系列标签） | 想查某个行为是哪来的 |
| [`agent-matrix.md`](agent-matrix.md) | **哪些版本能自动进图**：60 张符号表逐张对着真实 client jar 的复核结果 + 覆盖缺口与原因（脚本生成，不要手改） | 怀疑某个版本停在主菜单时 |
| [`07-一键汉化方案.md`](07-一键汉化方案.md) | **地图汉化能不能做、怎么做、做到哪了**（⏸ 功能已于 2026-11-12 隐藏，文档留档）：交接包能力盘点、7 个真实实例的实测数字、三档能力线、实际模块清单、实施记录（含踩过的坑）、备份替换流程、还剩什么 | 要动汉化、或要论证"为什么不做全自动机翻" |

阅读顺序：**README → 05 → 03 → 04**；要动设计读 02；要讲清"为什么不做 X"读 01 与 04。

**维护约定**（避免同一件事写在两处、改一处就漂移）：

| 只写在这里 | 内容 |
| --- | --- |
| [05](05-进度与遗留.md) | 一切状态与**具体数字**：里程碑、还剩什么、未验证、文件清单、测试基线 / 库内包数 / 行数统计（其它文档一律只写"约"或指向这里，避免每次提交都要同步） |
| [04](04-踩坑与决策.md) §5 | 一切决策与理由（02 只放指针） |
| [06](06-变更记录.md) | 改过什么、为什么（不写"现在还剩什么"） |
| [02](02-架构与路线图.md) | 分层、数据结构、两条主流程、里程碑的**验收定义** |
| [03](03-环境与构建.md) | 工具链、构建/测试命令、仓库外改动 |
| [01](01-可行性评估.md) | 立项时的论证（已实现的结论就地加批注，不改写历史） |

---

## 30 秒速览

**已经能做的**

- 导入地图（zip / rar / 7z）→ 免解压嗅探 → 三级版本判定（带置信度与证据）→ 建**物理隔离**的实例；
- 启动即进图：≥1.20 走 `--quickPlaySingleplayer` + `introskip`（跳过「实验性设置」页，不动世界数据）；
  ≤1.19.4 走 `-javaagent`（60 张符号表随包，1.8.9 / 1.12 Forge / 1.16.5 真机通过）；
- 退出即关：≤1.19.4 由 Agent 确认落盘后自退（已攻克 Forge 1.12 的退出拦截），`MapQuitWatcher` 外层兜底；
  ≥1.20 由启动器读日志（离开存档 → 落盘完成）关窗口，并回写 `map.json` 统计；
- 启动优化：组件校验「本地离线优先」，已装实例直接读本地缓存秒开，不再每次拉 `index.json`；
- 大厅：CTM Maps **整库本地化**（**2287 张地图 / 2287 条详情 / 1000 条评论**，= 站点全量，精确数见 [05 §1](05-进度与遗留.md)），
  搜索 / 排序 / 翻页 / 详情 / 评论全本地算，封面有磁盘缓存；
- 下载：默认官方源，可一键切 BMCLAPI（镜像直连、旁边有连通性测试）；**七层兜底** —— 断点续传 ·
  20 秒空闲看门狗 · 龟速看门狗 · 镜像↔官方双向回落 · 8 个 QNAM 连接池 · 大文件 16 段并发；
  实测 `client.jar` 368 KB/s → 2998 KB/s，进度窗口直接显示速度、已收字节与每个文件的分段；
- 模组预设：内置三套 + 自建，按实例版本与加载器现查 Modrinth / CurseForge，逐条带说明、
  required 依赖自动补齐、缺加载器先引导安装；
- 本地地图库：分类（默认 RPG / AAD / CTM / MiniGame / Other，可增删改）、拖行归类、已安装标绿、双击导入；
  **按图分组并默认折叠**（26 个包 = 20 张图 / 16 条大厅关联，其中 2 条已人工确认；6 组语言版本收在组标题下，
  点前头的三角展开；另有 9 张不在 CTM 上，按名字归组）、组标题**粗体 + MC 版本列**、子行 16px 只写
  `v1.0 EN / 简中 / 繁中 / 官中`（常规字重 + `└` 折号）、**三级右键**（组标题=整组操作 / 子行=只能改自己的
  地图版本与语言标签 / 独立图=两者合一）、列宽可拖并**记住**；左栏 92px 等宽（`ALL` + 计数右对齐）；
  导入时可关联 CTM 地图并标语言与来源；汉化若只是一份资源包，
  右键挂到已装实例（不建实例、不动存档）；重装时可换同组的另一个语言版本；
  身份锚在大厅 slug，索引以**文件名**为键，库整体搬走也不失效；
- 实例窗口「地图」页四页签（简介 / 评论 / 我的 / 启动），右侧卡片带「模组预设」「编辑实例」；设置里有全局「地图」「资源」两页，资源页能把
  `assets / libraries / java` **真的迁移**到任意盘，改完立即生效；目标位置已有的库文件按 sha1 认账，不重下；
- ⏸ **地图汉化（已隐藏，代码与测试保留）**：链路完整（体检 → 导出 / 机翻 → 写回副本 → 备份后顶替），
  但字形表（键化图）的图翻不了、写回后没在游戏里验证过显示，2026-11-12 起界面入口收起
  （`MapFeatures.h` 的 `kLocalization = false`），见 [06 §63](06-变更记录.md)。

**明确不做的**

- 启动器内下载（Cloudflare 只认可见浏览器）；
- 自研 ECH / QUIC（Qt 在 Windows 走 Schannel，直接借本机 Chromium）；
- 「一个高版本核心通吃」（1.13 / 1.20.5 两次语义断裂，DFU 不翻译命令方块）。

---

## 代码地图

新代码在 `launcher/maps/`（**49 个 .cpp + 55 个 .h，约 2.88 万行**；逐文件清单见
[05 §4.1](05-进度与遗留.md)）、`launcher/ResourcePaths`
与 `launcher/net/DownloadSource`（换源）；
`launcher/net/{Request,FileSink}` 与 `Application` 为下载能力有较大改动（续传 / 看门狗 / 重试 / 连接池 / 分片），
对既有源码其余部分只做加法：

| 分组 | 文件 |
| --- | --- |
| **识别** | `MapArchiveScanner`（免解压嗅探）· `LevelDatReader`（NBT）· `MapVersionDetector`（三级判定）· `DataVersionTable` · `MapTypes` · `MapTextUtils` |
| **数据** | `MapMeta`（`map.json`）· `MapNotes`（`notes.json`，玩家数据）· `MapHallLink`（实例 ↔ 大厅关联）· `HallCache`（大厅本地库） |
| **导入** | `MapImportTask`（建实例 / 解世界 / 落材质包 / 重装）· `MapRepository`（地图库目录与归档）· `MapImportDialog` |
| **地图库** | `MapLibrary`（`map-library.json`：分类 / 元数据 / 已安装判定 / `hall.slug` / 语言变体 / 地图版本）· `MapVariant`（原版/官中/汉化、整包/资源包）· `HallMatcher`（候选 + 理由）· `MapPackAttach`（资源包挂到已装实例）· `MapLibraryPanel`（92px 等宽分类栏 + 按图分组且默认折叠的列表 + 三级右键 + 子行半高 + 列宽记忆）。位置：**「添加实例 → 导入 → 地图存档导入」整页**，右下角「从外部导入…」 |
| **启动** | `MapLaunchPrep`（选轨道 / 统一准备点）· `AgentBootstrap`（符号表 → `-javaagent`）· `MapWindowFocus`（把前台交给游戏窗口） |
| **大厅** | `HallAPI` · `HallTypes` · `HallProxy` · `BrowserFetch` · `HallDialog` · `MapHallPage` |
| **界面** | `MapInstanceBar`（右侧地图卡片）· `MapInstancePage`（「地图」页）· `MapGalleryPage/Window` · `MapReviewCard`（评论卡）· `MapInfoCard`（信息卡工厂）· `MapCoverLoader`（封面）· `OptiFineDialog` · `StarRatingWidget` · `MapTheme`（颜色）· `MapStyle`（尺度：间距 / 字体层级 / 主次按钮 / 表格）· `SeriesNames`（系列代码 → 站上系列名，`[RC3]` → `Ragecraft 3`） |
| **模组** | `ModPresets`（预设数据 + 双平台解析 + 依赖）· `ModPresetDialog`（安装 / 编辑）· `ModSearchDialog`（搜索添加） |
| **设置** | `MapSettingsWidget`（内容体）· `MapPage`（全局「地图」）· `MapResourcePage`（全局「资源」）· `ResourceStore`（迁移）· `launcher/ResourcePaths`（路径解析） |
| **退出** | `MapQuitWatcher`（1.20+ 读日志关游戏） |
| **汉化**（已隐藏） | `MapL10nProbe`（`.mca` 读 + 剪枝遍历 + 载体白名单 + 散文判定）· `MapL10nTask`（体检任务）· `MapL10nExport`（`strings.json` / `.csv`）· `MapL10nTranslate`（OpenAI 兼容接口）· `MapL10nApply`（写回副本 + 结构比对 + 备份替换）· `MapOptionsTxt`（导入时写语言与键位） |
| **下载** | `net/DownloadMonitor`（旁路观测：整体/单文件速度、活跃文件）+ `ui/widgets/DownloadPanel`（速度与并行明细）· `net/DownloadSource`（换源 + 双向回落）· `net/Request`（失败/停滞重试、空闲与吞吐看门狗、QNAM 连接池、Range 分片）· `net/FileSink`（`.part` 续传 / `.part.parallel` 分段 / 收尾重算校验）· `Application::downloadNetwork()`（4 个 QNAM 的池） |

资源：`launcher/resources/maps/dataversion.json`（483 个版本）、`launcher/resources/maps/series.json`
（**系列代码 → 站上系列名**，如 `RC` → `Ragecraft`，由 `tools/ctm_series_codes.py` 从大厅缓存学习）、
`launcher/resources/agents/`（`quickboot.jar` + `introskip.jar` + **60 张符号表**，由
`tools/agent_symbol_audit.py` 对着真实 jar 复核）。
测试：34 个套件、473 项。本机（**没有**符号链接权限）`459 passed / 0 failed / 14 skipped`
（= 2026-11-15 00:42 实测的 454 / 14 加 §79 新增的 `MapSeriesNames` 5 项）；
开了权限是 `466 / 7`，差的 7 项就是 `FileSystem_test` 的符号链接用例 ——
两种口径都是 473，明细见 [03 §5](03-环境与构建.md)。地图相关的有 `MapArchiveScan`（17 + 2 项门控）、
`MapOptionsTxt`（32）、`MapVariant`（24 + 1 项门控）、`MapPackAttach`（12）、`MapL10nProbe`（29 + 1 项门控）、`MapL10nTranslate`（7）、
`MapL10nApply`（15 + 3 项门控）、`ModPresets`（14）、`DownloadSource`（12）、`RequestRetry`（23）、
`MapSeriesNames`（5）。

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
| **资源库** | `assets` / `libraries` / `java` 三份跨实例共享资源；可整体迁移到别的盘，默认仍在数据目录；换库立即生效 |
| **轨道 A / B** | A = ≥1.20 的 `--quickPlaySingleplayer`；B = ≤1.19.4 的 `-javaagent` 注入 |
| **Agent** | `quickboot.jar`，按符号表拼参数，进图后写 `agent.log` 供宿主判定退出是否安全 |
| **浏览器通道** | 借本机 Edge/Chrome 无头模式取数据/取封面。目标站被封的是 SNI，Qt 没有 ECH |
| **大厅本地库** | `<数据目录>/maps/hall-cache.json`，4 个请求拉全量，之后全部本地计算 |
| **便携模式** | exe 旁放 `portable.txt`，数据根 = exe 目录（当前 Debug 目录即如此） |
| **退出看门狗** | `MapQuitWatcher`：1.20+ 没有 Agent，靠日志判断「离开存档 + 落盘完成」再关窗口 |
| **系列标签** | 实例名前缀 `[US1]` / `[RC3]` 这类代码。网格里显示成名字下方的一枚小药丸，文本由 `SeriesNames` 翻成站上系列名（`Ragecraft 3`）；**实例名本身不改** |
