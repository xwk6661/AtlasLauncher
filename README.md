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
- **本地地图库**。`添加实例 → 导入 → 地图存档导入` 下半屏就是库：分类（纯英文 RPG / AAD / CTM / MiniGame / Other，支持动态自定义分类）、
  拖一下归类、已安装的标绿；库里那份压缩包就是重装用的备份，导入时自动入库。
- **导入就按你的习惯来**。新建实例时写好 `options.txt`：语言设成简体中文、Shift 奔跑 / Ctrl 蹲下
  （MC 默认正好相反）、默认全屏进图。只在导入那一次写，之后你在游戏里怎么改都不会被覆盖；
  写法按版本自动切（≤1.12 是 LWJGL2 数字键码 + `zh_CN`，≥1.13 是 `key.keyboard.*` + `zh_cn`）。
  顺带把关掉首屏「无障碍引导」——它会挡住自动进图。
- **地图汉化**。实例「信息 → 汉化」页：先体检（这张图的文字藏在哪、有多少要翻），
  导出待译清单给 Excel 改，或用 OpenAI 兼容接口自动翻。写回**分两条路**：
  文字在资源包语言表里就只写 `resources.zip` 里的 `zh_cn.json`（存档一个字节不动，原包先备份），
  在存档里就写进**存档副本**
  （逐区块结构比对，对不上的区块直接丢弃、保留原文）。确认无误后一键替换本实例存档 ——
  **旧图先打包成 zip 备份**，页面上「恢复原图」随时能挑一份备份解回去（当前存档先挪开，
  解压成功才删），也可以用「重装地图」从库里的归档重来。
  已经含中文的地图不会被当成待译（中文原创图和汉化版都不会白导一遍）。

## 现状

个人分支，Windows 上以 Debug 构建为主。真机跑通了 1.20+ 的「点卡片进图 / 退出即关」、≤1.19.4 的
Agent 自动进图与安全退出（1.8.9 / 1.12 Forge / 1.16.5）、大文件分片与续传 / 看门狗、本地离线优先启动，
以及本地地图库（分类 / 已安装 / 拖拽 / 自定义分类）；真机反馈的 4 条问题已闭环
（见 [`docs/atlas/05-进度与遗留.md`](docs/atlas/05-进度与遗留.md) §8）。还没上真机的是资源库跨盘迁移与 RAR5 大包。

地图汉化的主链路已通（体检 → 导出 → 自动翻译 → 写回副本 → 旧图备份后顶替），
7 张真实地图的体检数字与参考实现逐项对表通过、Torre de Piedra 写回结构差异 0；
但**界面还没在真实窗口里点过**，也**没有真的替换过一份在用的存档**（端到端验证都在临时目录），
真实 LLM 接口同样没跑过。细节见 [07](docs/atlas/07-一键汉化方案.md)。
当前进度、还剩什么、哪些没验证，只看 [05](docs/atlas/05-进度与遗留.md)。

## 构建

```powershell
# 需要 VS2022 + Qt 6.8 + vcpkg；五个环境坑与沙箱配方见文档
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
