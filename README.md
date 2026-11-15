<p align="center">
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="/program_info/org.atlas.Atlas.logo-darkmode.svg">
  <source media="(prefers-color-scheme: light)" srcset="/program_info/org.atlas.Atlas.logo.svg">
  <img alt="Atlas" src="/program_info/org.atlas.Atlas.logo.svg" width="40%">
</picture>
</p>

<p align="center">
  <b>Atlas</b> —— 以地图为中心的 Minecraft 启动器：点一张地图，直接落进它的世界。<br />
  基于 <a href="https://github.com/PrismLauncher/PrismLauncher">Prism Launcher</a>（其上游是 MultiMC / PolyMC）。
</p>

---

## 它是什么

普通启动器是「先选版本 → 等主菜单 → 挑存档」，Atlas 把顺序反过来：**点一张地图，直接落进它的世界**。

- **一张地图 = 一个实例**：导入压缩包后自动建实例、只解这一个存档、材质包就地装上，实例之间物理隔离。
- **版本由存档推断**：`level.dat` 字段 → DataVersion 表 → 文本旁证三级判定，给出结论、置信度与证据；
  判不出来就拒绝导入，不猜。
- **点开就进图**：≥1.20 走 `--quickPlaySingleplayer`，并用 `introskip` 跳过「实验性设置」确认页；
  ≤1.19.4 走随包的 Java Agent（60 张符号表，覆盖 1.6.1–1.19.4）。
- **退出即关**：≤1.19.4 由 Agent 确认区块落盘后自己退；≥1.20 由启动器读游戏日志（离开存档 → 落盘完成）关窗口。
- **下载会自己找路**：断点续传、空闲 / 龟速两道看门狗、镜像 ↔ 官方互备、8 个连接池 + 大文件 16 段并发；
  实测同一个 29 MB 文件 368 KB/s → 2998 KB/s，进度窗口直接显示速度与每个文件的分段。
- **大厅、模组预设、地图库、资源迁移**：CTM Maps 整库本地化（2287 张，离线可搜可看）；模组按实例的 MC 版本与加载器
  现查 Modrinth / CurseForge 并自动补依赖；**地图库按「图」分组并默认收起**——同一张图的英文原版 / 简中 / 繁中
  收在一个组标题下（粗体 + MC 版本），点三角展开，子行 16px 只写 `v1.0 简中` 这样的「地图版本 + 语言」；
  右键分三级：组标题整组操作、子行只能改自己的地图版本与语言标签；导入时选一次「这是哪张图、哪个语言」，身份锚在站点 slug 上，
  改名换盘都不会散；重装时可换同组的另一个语言版本；汉化若只是一份资源包，右键挂到已装实例（不建实例、不动存档）。
  本地库里那份压缩包就是重装备份；`assets` / `libraries` / `java` 可整体搬到别的盘或指向已有的 `.minecraft`。
- **实例名会说自己在哪一系列**：`[RC3] The Prophecy` 在网格里显示成 `The Prophecy` + 名下一枚 `Ragecraft 3`
  小标签（代码→系列名的表由 CTM 大厅数据学出来，认不出的代码原样显示）。
- **导入即按你的习惯**：新建实例时写好 `options.txt`（简体中文、Shift 奔跑 / Ctrl 蹲下、默认全屏、关掉
  无障碍引导），只写这一次，之后游戏里怎么改都不会被覆盖。
- **实例窗口就是这张图**：打开先落在「地图」页（简介 / 评论 / 我的 / 启动），启动行为与笔记都在这一页；
  右侧卡片另有「模组预设」与「编辑实例」，不用去菜单里找。
- ⏸ **地图汉化（已隐藏）**：链路是完整的（体检 → 导出 → 翻译 → 写回存档副本 → 备份后顶替），
  但字形表（键化图）的图翻不了、写回后没在游戏里验证过实际显示，2026-11-12 起界面入口收起。
  代码与测试全留，`launcher/maps/MapFeatures.h` 一行开关可恢复；细节见 [07](docs/atlas/07-一键汉化方案.md)。

## 现状

个人分支，Windows 上以 Debug 构建为主。真机已验证：1.20+ 点卡片进图与退出即关、≤1.19.4 的 Agent 进图
与安全退出（1.8.9 / 1.12 Forge / 1.16.5）、分片下载与续传 / 看门狗、本地离线优先启动、本地地图库、
资源库跨盘迁移（2026-11-14 迁移后暴露两个 bug，当日修完并复验）。
还没上真机：RAR5 大包、模组预设验收，以及 2026-11-14 之后的地图库界面改动（按图折叠 / 右键改归属 /
导入页重排 / 资源包挂载 / 重装换语言版本 / 子行半高与三级右键菜单 / 列宽记忆）与实例网格的系列标签
（`[US1] Lacuna Sanctum` 显示成「Lacuna Sanctum」+ 名下「Untold Stories 1」徽章）—— 这些只过了编译与全套测试。

**进度、还剩什么、哪些没验证，只看 [05](docs/atlas/05-进度与遗留.md)**；设计看 [02](docs/atlas/02-架构与路线图.md)，
踩过的坑与已定决策看 [04](docs/atlas/04-踩坑与决策.md)。别处与本文件都不重复维护状态。

## 构建

```powershell
# 需要 VS2022 + Qt 6.8 + vcpkg；五个环境坑与沙箱配方见文档
& .\build\run-configure.ps1 -Action configure
& .\build\run-configure.ps1 -Action build-debug

# 运行（已配好 Qt / vcpkg 的 DLL 路径）
.\build\run-atlas-debug.cmd

# 测试：单套件这样跑，全套用 ctest（见 03 §5）
cd build\win-local\Debug; .\LauncherTests.exe MapArchiveScan_test
```

详细步骤：[`docs/atlas/03-环境与构建.md`](docs/atlas/03-环境与构建.md)。

## 文档

入口是 [`docs/atlas/README.md`](docs/atlas/README.md)：
为什么这么做（01）、怎么设计（02）、怎么跑起来（03）、踩过哪些坑与已定决策（04）、
当前到哪了（05）、改过什么（06）、汉化方案留档（07）、符号表覆盖矩阵（agent-matrix）。
**状态与所有具体数字只看 05**，别处都只是引用。

## 许可与致谢

GPL-3.0-only。代码来自 Prism Launcher、PolyMC 与 MultiMC，版权归各自贡献者所有；
Atlas 的改动同样以 GPL-3.0 发布。本项目**未经** Prism Launcher / MultiMC 背书；
图标暂用上游素材，之后会替换为 Atlas 自己的。
