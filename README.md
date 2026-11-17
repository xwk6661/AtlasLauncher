<p align="center">
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="program_info/org.atlas.Atlas.logo-darkmode.svg">
  <source media="(prefers-color-scheme: light)" srcset="program_info/org.atlas.Atlas.logo.svg">
  <img alt="Atlas" src="program_info/org.atlas.Atlas.logo.svg" width="40%">
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
  ≤1.19.4 走随包的 Java Agent（60 张符号表；1.13–1.16 走 `loadLevel(String)`，1.19+ 走
  `createWorldOpenFlows().loadLevel(...)`）。1.17 / 1.18 没有这个类，只能停在主菜单。
- **退出即关**：≤1.19.4 由 Agent 确认区块落盘后自己退；≥1.20 由启动器读游戏日志（离开存档 → 落盘完成）关窗口。
- **下载会自己找路**：断点续传、空闲 / 龟速两道看门狗、镜像 ↔ 官方互备、8 个连接池 + 大文件 16 段并发。
- **大厅、模组预设、地图库、资源迁移**：CTM Maps 整库本地化（2287 张，离线可搜可看）；模组按实例的
  MC 版本与加载器现查 Modrinth / CurseForge 并自动补依赖；地图库按「图」分组并默认收起——同一张图的
  英文原版 / 简中 / 繁中收在一个组标题下，右键分三级，可重命名、可关联到 CTM 地图站条目；导入时可选
  九项游戏设置；实例名里的 `[RC3]` 在网格里显示成 `Ragecraft 3` 徽章；`assets` / `libraries` / `java`
  可整体搬到别的盘。

## 现状

个人分支，Windows 上以 Debug 构建为主，Release 可出便携包（`tools/package-windows.ps1`，含 Qt 与 VC++
运行库，18.6 MB）。构建通过，**31 个套件 / 423 项测试全绿**。

真机已验证：1.20+ 点卡片进图与退出即关、Agent 进图与安全退出（1.8.9 / 1.12 Forge / 1.16.5 / 1.19.3）、
分片下载与续传 / 看门狗、本地地图库、资源库跨盘迁移。

**没在真实窗口验证的**：2026-11-14 之后的地图库界面改动（按图折叠 / 三级右键 / 列宽记忆 / 92px 分类栏），
以及 11-17 的重命名 / 站上关联菜单与详情面板滚动条样式、导入对话框的九项设置、资源包挂载、
大厅新失败页；便携包也只在开发机冒烟过。

> 进度、还剩什么、每个数字只看 [04-进度与待办.md](docs/atlas/04-进度与待办.md)；
> 每项功能验到哪一档（编译 / 单测 / 真实产物 / 真机）只看 [05-验证记录.md](docs/atlas/05-验证记录.md)。
> 设计看 [01](docs/atlas/01-设计.md)，踩过的坑与已定决策看 [03](docs/atlas/03-决策与坑.md)。
> 别处与本文件都不重复维护状态。

## 构建

```powershell
# 需要 VS2022 + Qt 6.8 + vcpkg；五个环境坑与沙箱配方见文档
& .\build\run-configure.ps1 -Action configure
& .\build\run-configure.ps1 -Action build-debug

# Release + 便携包（配置一次就够，之后可直接 build-release）
& .\build\run-configure.ps1 -Action build-release
& .\tools\package-windows.ps1 -Config Release    # → dist\Atlas-12.0.0-win64-release.zip

# 运行（已配好 Qt / vcpkg 的 DLL 路径）
.\build\run-atlas-debug.cmd

# 测试：单套件这样跑，全套用 ctest（见 02 §5）
cd build\win-local\Debug; .\LauncherTests.exe MapArchiveScan_test
```

详细步骤：[`docs/atlas/02-构建与运行.md`](docs/atlas/02-构建与运行.md)。

## 文档

入口是 [`docs/atlas/README.md`](docs/atlas/README.md)：
为什么与怎么设计（01）、怎么跑起来（02）、踩过哪些坑与已定决策（03）、当前到哪了（04）、
验到哪一档（05）、改过什么（06）、符号表覆盖矩阵（agent-matrix）。

## 许可与致谢

GPL-3.0-only。代码来自 Prism Launcher、PolyMC 与 MultiMC，版权归各自贡献者所有；
Atlas 的改动同样以 GPL-3.0 发布。本项目**未经** Prism Launcher / MultiMC 背书；
图标暂用上游素材，之后会替换为 Atlas 自己的。
