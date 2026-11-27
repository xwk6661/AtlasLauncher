# 第三方组件与许可

Atlas 以 **GPL-3.0-only** 发布（全文见 `LICENSE`）。它派生自 Prism Launcher，
后者派生自 PolyMC 与 MultiMC；这些项目的版权归各自贡献者所有，`COPYING.md` 保留了
上游的致谢与许可说明。

> 下表按各项目**公开**的许可证整理，仅作告知。若与上游仓库的声明不一致，以上游为准。
> **完整许可文本随包放在 `LICENSES/`**（清单见 `LICENSES/README.md`），本文件只做索引与说明。

## 品牌与图标

`program_info/` 下的 logo、应用图标与品牌素材**不在 GPL-3.0 的覆盖范围内**，按上游的做法以
**CC BY-SA 4.0** 授权（全文见 `program_info/LICENSE`，随安装包发布时文件名为 `LICENSE-branding.txt`）。

| 素材 | 来源 | 许可 |
| --- | --- | --- |
| `org.atlas.Atlas.png`、`org.atlas.Atlas.icon.png`、`atlas.ico`、`atlas.icns`、`org.atlas.Atlas_256.png` | **Atlas 项目图标稿**（2026-11-26 起；六边形瓦片 + 等距立方体 + 罗盘指针）。素材由维护者提供、经 AI 生成工具产出后人工去背与排版（口径见 `program_info/README.md`） | **CC BY-SA 4.0** · © 2026 Atlas Contributors。对 AI 生成部分不主张排他著作权，仅按 CC BY-SA 4.0 对外授权 |
| `program_info/archive/icon-2026-11-25/*.svg`（归档，不参与构建） | **Atlas 自绘**（2026-11-25 版；地图瓦片 + 等距立方体 + 罗盘） | CC BY-SA 4.0 · © 2026 Atlas Contributors |
| `instance_icons.svg`、`org.atlas.Atlas.Social.svg` | Prism Launcher 的原始品牌素材 | CC BY-SA 4.0 · 署名 Prism Launcher Contributors |
| `Atlas.icon/`（macOS Icon Composer 包里的 `block.svg`、`rainbow.svg`） | Prism Launcher 的原始品牌素材 | CC BY-SA 4.0 · 署名 Prism Launcher Contributors |

**界面图标主题**（`launcher/resources/`）**不是同一份许可**，按目录分列（2026-11-26 核对）：

| 目录 | UI 里的名字 | 来源 | 许可 |
| --- | --- | --- | --- |
| `breeze_light/`、`breeze_dark/` | Breeze (Black) / Breeze (White) | KDE Breeze，© 2014 Uri Herrera and others | **LGPL-3.0-or-later** |
| `flat/`、`flat_white/` | Material (Black) / Material (White) | 基于 Google Material Icons；`COPYING.md` 另列 Material Design Icons（© 2014 Austin Andrews） | **Apache-2.0**（Google）/ **OFL-1.1**（Material Design Icons） |
| `multimc/` | Legacy | MultiMC Contributors（`COPYING.md` 的 MultiMC 段） | **Apache-2.0** |
| `pe_blue/`、`pe_colored/`、`pe_dark/`、`pe_light/` | Simple (Blue / Colored / Black / White) | **pexner**；仓库里没有单独的许可文件，随 Prism 整体分发 | **GPL-3.0-only**（随仓库）；署名 pexner |
| `multimc/scalable/launcher.svg` | — | **已换成 Atlas 图标**（2026-11-25） | 同 Atlas 图标 |

- 以上四类对 GPL-3.0-only 的下游（本项目）都是**兼容**的：Apache-2.0 与 GPLv3 单向兼容、
  LGPL-3.0 可并入 GPLv3 作品、OFL 只约束字体本身。需要做的只是**保留各自的许可文本与署名**，
  不改称这些图标是 Atlas 原创，也不把图标集单独再分发/售卖（Batch/pexner 一路尤其如此）。
- `pe_*`（Simple）是唯一**没有独立许可文件**的一套：按"提交进 GPL 仓库即按 GPL 分发"处理；
  若要拿去做闭源/商业发行，应先向 pexner 或 Prism 维护者确认。
- 应用图标自 2026-11-25 起**不再沿用上游素材**；2026-11-26 换成位图母图：
  二进制图标由 `program_info/genicons.py` 从 `org.atlas.Atlas.png` 生成（只需 Pillow），
  仓库里提交生成结果，构建不需要渲染工具。
- CC BY-SA 4.0 **不授予商标权**（§2(b)(2)）；无论素材来源如何，都不构成上游或第三方向本项目的认可或背书（§2(a)(6)）。
- 图标主题的署名与许可对照单独放在 `LICENSES/packages/icon-themes.txt`（含 pexner / Breeze /
  Material / MultiMC 四套），随包分发。
- 图标母图的原始素材（两张 2048 候选）在 `program_info/archive/source-2026-11-26/`，**不随包**。
- 仍沿用上游的还有：`instance_icons.svg`（实例占位图）、`Social.svg`（社交预览图）、
  `Atlas.icon/`（只在 macOS 包用，本项目不发布 macOS 版）与上表的界面图标主题（含主窗口那只黑猫背景）。
  这些保留原署名，替换时同步更新本节。

## 随二进制分发

**完整许可文本随包放在 `LICENSES/`**（安装包里的同名目录），下表逐一指到文件。
构建期工具（ECM、pkgconf、JDK 8）不随包分发，不计入此表。

| 组件 | 版本 / 位置 | 许可证（SPDX） | 许可文本 | 用途 |
| --- | --- | --- | --- | --- |
| Qt 6（Core / Gui / Widgets / Network / NetworkAuth / OpenGL / Svg / Xml / Concurrent + 平台插件） | 6.8.x，`Qt6*.dll`、`plugins/` | **LGPL-3.0-only**（Qt 另有商业与 GPL-3.0 选项，本项目只用 LGPL） | `LICENSES/LGPL-3.0-only.txt`、`LICENSES/packages/qt.txt` | 界面、网络、并发 |
| libarchive | vcpkg，静态链接 | **BSD-2-Clause** | `LICENSES/packages/libarchive.txt` | 读 zip / 7z / rar |
| zlib | vcpkg，静态链接 | **Zlib** | `LICENSES/packages/zlib.txt` | 压缩流 |
| bzip2 | vcpkg，经 libarchive | **bzip2-1.0.6**（BSD 式自定义） | `LICENSES/packages/bzip2.txt` | 归档解码后端 |
| lz4 | vcpkg，经 libarchive | **BSD-2-Clause** | `LICENSES/packages/lz4.txt` | 同上 |
| liblzma（XZ Utils） | vcpkg，经 libarchive | **0BSD** | `LICENSES/packages/liblzma.txt` | 同上 |
| lzo | vcpkg，经 libarchive | **GPL-2.0-or-later** | `LICENSES/packages/lzo.txt`、`LICENSES/GPL-2.0-only.txt` | 同上；按 "or later" 使用，与 GPL-3.0 兼容 |
| zstd | vcpkg，经 libarchive | **BSD-3-Clause**（zstd 另有 GPL-2.0 分支，未采用） | `LICENSES/packages/zstd.txt` | 同上 |
| cmark | vcpkg，静态链接 | **BSD-2-Clause** | `LICENSES/packages/cmark.txt` | 大厅详情 Markdown |
| tomlplusplus | vcpkg，静态链接 | **MIT** | `LICENSES/packages/tomlplusplus.txt` | 读 TOML 配置 |
| libqrencode | vcpkg，静态链接 | **LGPL-2.1-only** | `LICENSES/packages/libqrencode.txt`、`LICENSES/LGPL-2.1-only.txt` | 二次验证二维码 |
| libnbtplusplus | `libraries/libnbtplusplus` 子模块 | **LGPL-3.0-only 或 GPL-3.0**（上游同时给了两份，本项目按 LGPL 使用） | `LICENSES/LGPL-3.0-only.txt` | 读 `level.dat` |
| qdcss | `libraries/qdcss` | **LGPL-3.0-only** | `LICENSES/LGPL-3.0-only.txt` | 上游的样式表组件 |
| rainbow（KGuiAddons） | `libraries/rainbow` | **LGPL-2.0-or-later** | `LICENSES/LGPL-2.1-only.txt`（更高版本条款兼容） | 取色/配色 |
| LocalPeer（Qt Solutions） | `libraries/LocalPeer` | **BSD-3-Clause**（© 2013 Digia Plc） | `LICENSES/BSD-3-Clause.txt` | 单实例 IPC |
| murmur2 | `libraries/murmur2` | **Public Domain**（Austin Appleby；增量修改同样声明为公有领域） | 无需文本，见本节说明 | 哈希 |
| Eclipse Temurin / Mojang 运行时 | **不随包**，按需下载 | 各自许可证 | — | 运行游戏 |

> **Qt 的 LGPL 合规**：Qt 以**动态链接**方式使用，DLL 原样随包、未做修改，用户可用兼容构建替换
> （放在 `atlas.exe` 旁的 `Qt6*.dll`）。Atlas 不提供静态 Qt 构建。Qt 自带的第三方组件
> （PCRE2、FreeType、HarfBuzz、libpng 等）的声明在 Qt 安装目录的 `sbom/` 与 qt.io 许可页面。

## 随 Agent 分发（`launcher/resources/agents/`）

| 组件 | 许可证 | 许可文本 | 说明 |
| --- | --- | --- | --- |
| ASM 9.7.1（core / tree / commons / analysis） | **BSD-3-Clause**（© 2000-2011 INRIA, France Telecom） | `LICENSES/packages/asm.txt` | 被 shade 进 `quickboot.jar` 与 `introskip.jar`，用于运行期改写字节码 |
| `quickboot.jar` / `introskip.jar` | **GPL-3.0-only** | `LICENSES/GPL-3.0-only.txt` | 源码在 `tools/quickboot-agent/`、`tools/introskip-agent/` |
| `introskip.json` / `symbols/*.json` | 同仓库 GPL-3.0-only | 同上 | 由 Mojang 公开的 `client_mappings` 推导出的**事实性数据**（类名/方法名/描述符），不含游戏代码 |

## 数据与服务

Atlas 不自建内容服务，只按各站**公开的只读接口**取数据。没有与之建立正式合作，
名称与内容版权归各站及各作者所有；如果站方要求调整，会按对方要求改或移除。

| 内容 | 来源 | 用途 / 说明 |
| --- | --- | --- |
| 地图元数据（地图名 / 简介 / 评论 / 用户名）与封面 | [CTM Maps](https://ctmmaps.com/) | 大厅的「发现地图」。只缓存在本地 `maps/hall-cache.json`，**不随安装包分发**；封面缓存在「设置 → 地图」里可一键清空 |
| 模组元数据与下载 | [Modrinth](https://modrinth.com/) API | 模组预设与模组搜索，按实例的 MC 版本与加载器现查 |
| 模组元数据与下载 | [CurseForge](https://www.curseforge.com/) API | 同上（双平台互为备份） |
| OptiFine 版本列表与安装包 | [optifine.net](https://optifine.net/) | 可选组件；列表经 BMCLAPI 转取，安装包来自官网 |
| 下载镜像 | [BMCLAPI](https://bmclapi2.bangbang93.com/) | 可选的国内下载源（默认仍是官方源），映射表见 `net/DownloadSource` |
| 版本清单 / 素材 / 库 | Mojang（`launchermeta` / `piston-data` / `libraries`） | 由启动器在玩家本机按需下载 |
| 地图封面以外的站点数据 | 无 | 启动器内不下载地图，下载一律交系统浏览器 |
| `dataversion.json`（483 个版本） | 由 Mojang 公开的版本清单整理 | 事实性数据，用于存档版本推断 |
| 符号表（`agents/symbols/`，60 张） | 由 Mojang 官方 `client_mappings` 生成 | 事实性数据，用于低版本自动进图 |
| 界面翻译（`translations/mmc_zh.qm`） | Prism Launcher 翻译社区（Weblate） | GPL-3.0 |

致谢：Atlas 的「点地图进图」这条产品思路与版本判定规则来自同作者的 AIO 版原型
（`Minecraft-AIO`，独立仓库）；低版本进图用的符号表方法论、`quickboot` / `introskip`
两个 Agent 的初版也来自那里。

## Minecraft 相关声明

Atlas **不是** Mojang Studios、Microsoft、Prism Launcher、PolyMC、MultiMC 或 ATLauncher
的官方产品，也未获其背书。Atlas 不包含、不分发任何 Minecraft 游戏文件或素材：
所有版本文件、素材与 Java 运行时都由启动器在玩家本机按需从官方源下载，
使用前需要拥有正版 Minecraft: Java Edition 账号。
