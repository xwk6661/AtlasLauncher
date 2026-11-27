# 图标留档 · 2026-11-25（矢量版）

> **2026-11-26 晚补记**：图标已改为矢量优先（新原稿 `../../org.atlas.Atlas.svg`），
> 本目录的"恢复成矢量版"步骤**已不适用** —— 现在构建本来就是矢量优先。这里只作 11-25 那版设计的历史留档。

这是 Atlas 上一版应用图标：**地图瓦片 + 等距立方体 + 罗盘**，512 viewBox 的纯 SVG，
2026-11-25 18:20 启用，2026-11-26 换成位图母图（`../../org.atlas.Atlas.png`）后归档。

| 文件 | 原来的用途 |
| --- | --- |
| `org.atlas.Atlas.svg` | 窗口 / 关于页 / 快捷方式图标；`atlas.qrc.in` 编译进 Qt 资源 |
| `org.atlas.Atlas.bigsur.svg` | macOS 用的 1024 画布（瓦片 80%） |
| `org.atlas.Atlas.logo.svg` / `.logo-darkmode.svg` | README 顶部的 logo（深色 / 浅色） |
| `org.atlas.Atlas.Source.svg` / `.logo.source.svg` | 源稿副本 |

**为什么留档**：设计本身还有价值（矢量、任意缩放不糊）；如果以后想回到矢量路线，
这六个文件是完整的起点。当前构建**不再引用**它们。

## 恢复成矢量版的做法

1. 把本目录的六个文件复制回 `program_info/`；
2. `program_info/CMakeLists.txt`：`Launcher_IconFileName` 改回 `${Launcher_AppID}.svg`；
3. `program_info/atlas.qrc.in`：`@Launcher_AppID@.png` 改回 `.svg`；
4. `program_info/genicons.py` 删掉 `org.atlas.Atlas.png`（或改名），脚本会自动退回无头浏览器渲染 SVG；
5. 重新 configure（`buildconfig` 里的图标文件名是配置期生成的）。

> 归档件**不参与构建**，也不需要跟随后续的品牌调整。
