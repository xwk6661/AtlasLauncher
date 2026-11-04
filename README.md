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

普通启动器是「先选版本，再进主菜单，再挑存档」；Atlas 把这个顺序反过来：

- **一张地图 = 一个实例**。导入压缩包后自动建实例、只解这一个存档、材质包就地装上，
  实例之间物理隔离，换图不互相污染。
- **版本由存档推断**。三级判定（level.dat 字段 → DataVersion 表 → 文本旁证）给出结论、
  置信度与证据，冲突时按规则裁决，而不是让玩家自己猜。
- **点开就进图**。≥1.20 走 `--quickPlaySingleplayer`，并用 `introskip` Agent 跳过
  「不支持使用实验性设置的世界」确认页；≤1.19.4 由随包的 Java Agent 跳过主菜单。
- **退出即关**。≤1.19.4 由 Agent 确认区块落盘后自己退；≥1.20 由启动器读游戏日志
  （离开存档 → 全部落盘）后关闭窗口。
- **大厅**。CTM Maps 整库本地化（封面、简介、评论、筛选都在本地算），支持离线浏览。
- **一键装模组预设**。内置/自建的模组清单按**这个实例的 MC 版本与加载器**现查 Modrinth，
  逐条显示解析到的版本、可取消勾选，一次装好；缺加载器会先带你装。
- **资源可迁移**。`assets` / `libraries` / `java`（含 Mojang 官方运行时）可以整体搬到别的盘
  或指向已有的 `.minecraft`，换启动器不用重新下载。

## 现状

个人分支，Windows 上以 Debug 构建为主。1.20+ 的「点卡片进图」和「退出存档即关游戏」已在真机跑通
（连开两次均正常），**目前没有阻塞项**；≤1.19.4 的 Agent 链路与资源迁移尚未上真机。
当前进度、还剩什么、哪些没验证，只看
[`docs/atlas/05-进度与遗留.md`](docs/atlas/05-进度与遗留.md)。

## 构建

```powershell
# 需要 VS2022 + Qt 6.8 + vcpkg；四个环境坑与沙箱配方见文档
& .\build\run-configure.ps1 -Action configure
& .\build\run-configure.ps1 -Action build-debug

# 运行（已配好 Qt / vcpkg 的 DLL 路径）
.\build\run-atlas-debug.cmd

# 测试
cd build\win-local\Debug; .\LauncherTests.exe MapArchiveScan_test
```

详细步骤：[`docs/atlas/03-环境与构建.md`](docs/atlas/03-环境与构建.md)。

## 文档

入口是 [`docs/atlas/README.md`](docs/atlas/README.md)：
为什么这么做（01）、怎么设计（02）、怎么跑起来（03）、踩过哪些坑与已定决策（04）、
当前到哪了（05）、改过什么（06）。

## 许可与致谢

GPL-3.0-only。代码来自 Prism Launcher、PolyMC 与 MultiMC，版权归各自贡献者所有；
Atlas 的改动同样以 GPL-3.0 发布。本项目**未经** Prism Launcher / MultiMC 背书；
图标暂用上游素材，之后会替换为 Atlas 自己的。
