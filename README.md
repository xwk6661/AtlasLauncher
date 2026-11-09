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
- **下载会自己找路，断了接着下**。官方域在弱网/受限网络下常常直接断连，设置里可一键换到
  BMCLAPI 镜像（只影响资源、库、版本 json 与运行时；镜像请求直连，不经过系统代理）。
  断流后写 `.part` **从断点续传**，不用从头；连接彻底不动 20 秒、或者龟速（<15 KB/s）10 秒就被换掉
  （有响应头、之后不再给数据的也算"不动"），分片则按片判、慢片从断点重连；
  镜像与官方互为备胎；并发下载走 8 个连接池（48 条连接）、大文件再切 16 段并发
  （后两项可在设置里调，实测同一个 29 MB 文件 368 KB/s → 2998 KB/s）。
  进度窗口里能直接看到总速度、已收字节和每个并行文件的进度条。
- **一键装模组预设**。内置/自建的模组清单按**这个实例的 MC 版本与加载器**现查
  Modrinth / CurseForge，逐条带说明、可取消勾选，required 依赖自动补齐；缺加载器会先带你装，
  预设里还能直接搜模组加进去。
- **资源可迁移**。`assets` / `libraries` / `java`（含 Mojang 官方运行时）可以整体搬到别的盘
  或指向已有的 `.minecraft`，换启动器不用重新下载。
- **本地地图库**。`添加实例 → 导入 → 地图存档导入` 下半屏就是库：分类（RPG / AAD / CTM / 小游戏 / 其他）、
  拖一下归类、已安装的标绿；库里那份压缩包就是重装用的备份，导入时自动入库。

## 现状

个人分支，Windows 上以 Debug 构建为主。1.20+ 的「点卡片进图」「退出存档即关游戏」、≤1.19.4 的
Agent 自动进图与安全退出（1.8.9 / 1.12 Forge / 1.16.5 已实测），以及「大文件分片下载」「断点续传」
都已在真机跑通；**本地地图库**（分类 / 已安装标识 / 拖拽归类）也已上机验证，还剩 3 个反馈问题在改
（见 [`docs/atlas/05-进度与遗留.md`](docs/atlas/05-进度与遗留.md) §8）。
仍没上过真机的是资源库跨盘迁移与 RAR5 大包。
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
