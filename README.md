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
- **Java 按版本自动选**：1.16.5 及更早 → 8、1.17 → 16、1.18–1.20.4 → 17、1.20.5+ → 21、26.x → 21；
  只在版本对得上的运行时里挑（固定路径 → Mojang 托管运行时 → 本机已装 → 全局），
  导入时与每次启动前各核对一次；玩家手选过的实例绝不改写，判不出就什么都不做。
- **退出即关**：≤1.19.4 由 Agent 确认区块落盘后自己退；≥1.20 由启动器读游戏日志（离开存档 → 落盘完成）关窗口。
- **下载会自己找路**：断点续传、空闲 / 龟速两道看门狗、镜像 ↔ 官方互备、8 个连接池 + 大文件 16 段并发；
  每版素材清单按 `assetIndex.sha1` 复用，只有上游重发过索引才重下（启动不再每次刷 450 KB）。
- **大厅、模组预设、地图库、资源迁移**：CTM Maps 整库本地化（2287 张，离线可搜可看，评论优先读缓存）；
  模组按实例的 MC 版本与加载器现查 Modrinth / CurseForge 并自动补依赖；地图库独立成「本地库」页，
  按**系列 → 地图**两层展示（`Mapwreck Series` → `[MW7] Subterranean`），一张图一行、导入时再选语言与
  版本，右键可重命名 / 归类 / 关联站上条目 / 删除；大厅里能直接看出"这张图我本地有没有"并一键导入；
  导入时可选九项游戏设置；实例名里的 `[RC3]` 在网格里显示成 `Ragecraft 3` 徽章；
  `assets` / `libraries` / `java` 可整体搬到别的盘。

## 现状

个人分支，准备公开发布（版本线独立为 **0.1.0**，不再沿用上游的 12.0.0）。
Windows 上**以 Release 便携版为真实运行环境**（`tools/package-windows.ps1`，含 Qt 与 VC++ 运行库，
18.8 MB；本机装在 `E:/Program File/AtlasLaucher`，实例 / 地图库 / 账号都在那里）；`build/win-local/Debug`
只做 UI 调试，靠 `--dir` 复用同一份数据。构建通过，**33 个套件 / 448 项测试全绿**。

真机已验证：1.20+ 点卡片进图与退出即关、Agent 进图与安全退出（1.8.9 / 1.9.4 / 1.12 Forge / 1.16.5 / 1.19.3）、
分片下载与续传 / 看门狗、素材索引命中不重下、本地地图库（含 11-22 晚的系列行：期号补 1、组内「├/└」折线、
聚合列写「~」）、资源库跨盘迁移。

**没在真实窗口验证的**：地图库的**三级右键、删除拒绝、列宽记忆**，11-17 的重命名 / 站上关联菜单与
详情面板滚动条样式，导入对话框的九项设置、资源包挂载、大厅新失败页；**11-22 的大厅 ↔ 本地库**
（「已入库」标记、详情页「从本地库导入」、评论离线降级）只到"界面看得到"；**11-22 的两轮审查整改**
（图标回填、并发与生命周期、扫描/迁移异步化、封面缓存清理入口）只到"编译 + 单测 + 装机"，
并发压测与退出行为也没真机跑过。便携包已装到本机并冒烟
（起窗口、13 个实例 = 10 张地图 + 3 个整合、中文、账号），**干净机器仍未验**。

> 进度、还剩什么、每个数字只看 [04-进度与待办.md](docs/atlas/04-进度与待办.md)；
> 每项功能验到哪一档（编译 / 单测 / 真实产物 / 真机）只看 [05-验证记录.md](docs/atlas/05-验证记录.md)。
> 设计看 [01](docs/atlas/01-设计.md)，踩过的坑与已定决策看 [03](docs/atlas/03-决策与坑.md)。
> 别处与本文件都不重复维护状态。

## 下载

公开发布后：**只从本仓库的 GitHub Releases 下载**（`Atlas-<版本>-win64-release.zip`），
解压即用，不需要安装。第三方转载的包请勿使用。

包内自带 `README-Atlas.txt`（首次使用、数据在哪、已知限制、出问题看哪个日志）、
`LICENSE.txt`、`COPYING.md` 与 `THIRD-PARTY-NOTICES.md`。

## 构建

```powershell
# 干净的开发机：三步（克隆 → 生成本机预设 → 配置/构建）
#   详见 BUILDING.md
git clone --recursive <repo>
Copy-Item CMakeUserPresets.json.example CMakeUserPresets.json   # 然后改里面的 Qt / vcpkg / JDK 路径
cmake --preset win-local
cmake --build --preset win-local

# 出便携包 → dist\Atlas-0.1.0-win64-release.zip
cmake --build --preset win-local-release
powershell -File tools\package-windows.ps1 -Config Release

# 本机（作者机器）那套已经调好的脚本仍在 build\（不进仓库）：
#   build\run-configure.ps1 -Action configure / build-debug / build-release
#   一条命令上机：tools\deploy-local.ps1（构建 → 打包 → 覆盖便携根，-DryRun 只体检）

# 运行
#   真机：E:/Program File/AtlasLaucher/atlas.exe（便携根 = 数据根）
#   界面调试：build\run-atlas-debug.cmd（自动带 --dir 指向便携根，不复制数据）

# 测试：全套 33 套件，或单套件（见 02 §5）
ctest --test-dir build/win-local -C Debug
cd build\win-local\Debug; .\LauncherTests.exe MapArchiveScan_test
```

详细步骤：[`BUILDING.md`](BUILDING.md)（从零构建）、[`docs/atlas/02-构建与运行.md`](docs/atlas/02-构建与运行.md)（本机环境与五个坑）。

## 文档

入口是 [`docs/atlas/README.md`](docs/atlas/README.md)：
为什么与怎么设计（01）、怎么跑起来（02）、踩过哪些坑与已定决策（03）、当前到哪了（04）、
验到哪一档（05）、改过什么（06）、符号表覆盖矩阵（agent-matrix）、
全项目审查与整改状态（[审查-2026-11-22](docs/atlas/审查-2026-11-22.md)）。

## 许可与致谢

GPL-3.0-only，全文见 [`LICENSE`](LICENSE)；第三方组件与数据来源见
[`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md)；上游致谢见 [`COPYING.md`](COPYING.md)。

代码来自 Prism Launcher、PolyMC 与 MultiMC，版权归各自贡献者所有。
**Atlas 是非官方产品**：与 Mojang、Microsoft、Prism Launcher、PolyMC、MultiMC 及
ATLauncher 均无关联，也未获其背书；不包含任何 Minecraft 游戏文件。
图标暂用上游素材，之后会替换为 Atlas 自己的。

数据与服务来自这些地方，只按公开的只读接口访问，没有建立正式合作，版权归各站与各作者：
[CTM Maps](https://ctmmaps.com/)（大厅的地图资料与评论）、
[Modrinth](https://modrinth.com/) 与 [CurseForge](https://www.curseforge.com/)（模组预设）、
[optifine.net](https://optifine.net/)（OptiFine）、
[BMCLAPI](https://bmclapi2.bangbang93.com/)（可选下载镜像）、
以及 Mojang 的版本清单与素材。逐项说明见 `THIRD-PARTY-NOTICES.md`。
