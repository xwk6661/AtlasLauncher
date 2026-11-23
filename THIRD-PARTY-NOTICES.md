# 第三方组件与许可

Atlas 以 **GPL-3.0-only** 发布（全文见 `LICENSE`）。它派生自 Prism Launcher，
后者派生自 PolyMC 与 MultiMC；这些项目的版权归各自贡献者所有，`COPYING.md` 保留了
上游的致谢与许可说明。

> 下表按各项目**公开**的许可证整理，仅作告知。若与上游仓库的声明不一致，以上游为准。

## 随二进制分发

| 组件 | 版本 / 位置 | 许可证 | 在 Atlas 里的用途 |
| --- | --- | --- | --- |
| Qt 6（Core / Gui / Widgets / Network / NetworkAuth / OpenGL / Svg / Xml / Concurrent） | 6.8.x，`Qt6*.dll`（**动态链接**） | LGPL-3.0（另有 GPL-3.0 可选） | 界面、网络、并发。用户可自行替换这些 DLL |
| libarchive | vcpkg，静态链接进 `atlas.exe` | BSD-2-Clause | 读取 zip / 7z / rar 归档 |
| zlib | vcpkg，静态链接 | zlib | 压缩流 |
| bzip2 / lz4 / liblzma / lzo / zstd | vcpkg，经 libarchive 使用 | BSD-like / BSD-2 / 0BSD / GPL-2.0-or-later / BSD-3 | 归档解码器的可选后端 |
| cmark | vcpkg，静态链接 | BSD-2-Clause | 大厅详情里的 Markdown |
| tomlplusplus | vcpkg，静态链接 | MIT | 读取 TOML 配置 |
| libqrencode | vcpkg，静态链接 | LGPL-2.1 | 账号二次验证二维码 |
| libnbtplusplus | `libraries/libnbtplusplus`（子模块） | MIT | 读取 `level.dat` |
| qdcss / rainbow / murmur2 / LocalPeer | `libraries/` | MIT | 上游 Prism 的界面与工具组件 |
| Eclipse Temurin / Mojang 运行时 | **不随包**，由启动器按需下载 | 各自许可证 | 运行游戏 |

## 随 Agent 分发（`launcher/resources/agents/`）

| 组件 | 许可证 | 说明 |
| --- | --- | --- |
| ASM 9.7.1（core / tree / commons） | BSD-3-Clause | `quickboot.jar` 与 `introskip.jar` 内嵌，用于运行期改写 Minecraft 字节码 |
| `quickboot.jar` / `introskip.jar` | GPL-3.0-only | 本仓库源码在 `tools/quickboot-agent/`、`tools/introskip-agent/` |

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
