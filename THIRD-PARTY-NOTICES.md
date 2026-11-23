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

## 数据与内容

| 内容 | 来源 | 说明 |
| --- | --- | --- |
| 大厅元数据（地图名 / 简介 / 评论 / 用户名） | [CTM Maps](https://ctmmaps.com/) 的公开接口 | 只缓存在本地 `maps/hall-cache.json`，**不随安装包分发**；版权归原站与各作者 |
| 地图封面 | 同上 | 同上，本地缓存，可在「设置 → 地图」一键清空 |
| `dataversion.json` | 由 Mojang 公开的版本清单整理 | 事实性数据 |
| 符号表（`agents/symbols/`） | 由 Mojang 官方 `client_mappings` 生成 | 事实性数据 |
| 界面翻译（`translations/mmc_zh.qm`） | Prism Launcher 翻译社区（Weblate） | GPL-3.0 |

## Minecraft 相关声明

Atlas **不是** Mojang Studios、Microsoft、Prism Launcher、PolyMC、MultiMC 或 ATLauncher
的官方产品，也未获其背书。Atlas 不包含、不分发任何 Minecraft 游戏文件或素材：
所有版本文件、素材与 Java 运行时都由启动器在玩家本机按需从官方源下载，
使用前需要拥有正版 Minecraft: Java Edition 账号。
