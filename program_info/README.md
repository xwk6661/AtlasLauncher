# Atlas Program Info

Atlas 的品牌与打包元数据（应用名、app id `org.atlas.Atlas`、图标、桌面文件、
AppStream 元数据、Windows 资源、man page、安装器脚本）。这里决定「装出来长什么样」，
与 `launcher/` 里的功能代码无关。

## Atlas 自己的

- 应用名 `Atlas`、app id `org.atlas.Atlas`、可执行文件名 `atlas`
- `atlas.rc.in` / `atlas.manifest.in` / `atlas.6.scd.in` / `win_install.nsi.in`
- `org.atlas.Atlas.{desktop,metainfo,mime}.*`、`org.atlas.Atlas*.svg`
- URL 协议 `atlas://`（与上游的 `prismlauncher://` 分开，两个启动器可共存）

## 沿用的（技术必需，不是耦合）

| 项 | 原因 |
| --- | --- |
| `Launcher_META_URL` = `meta.prismlauncher.org` | Atlas 不自建版本清单服务，改了就没有版本可装 |
| `org.prismlauncher.EntryPoint` | 游戏 jar 里的 Java 主类名，写死在 jar 内 |
| `x-prismlauncher-*`（packwiz） | 包格式规范的字段名 |

完整清单与「为什么不能改」见 `docs/atlas/07-发布清单.md` §7.1。

## 图标

应用图标是 **Atlas 自绘**的（2026-11-25 起，地图瓦片 + 等距立方体 + 罗盘），不再沿用上游素材。
原稿只有一份：`org.atlas.Atlas.svg`；其余都是派生物。

| 文件 | 谁在用 |
| --- | --- |
| `org.atlas.Atlas.svg` | 编进 Qt 资源（`atlas.qrc.in`）当窗口 / 任务栏图标；桌面文件的 `Icon=` |
| `atlas.ico` | `atlas.rc.in` 嵌进 `atlas.exe`（16/24/32/48/64/128/256，小于 256 用 BMP、256 用 PNG） |
| `atlas.icns` | macOS 包（16–1024，来自加了 10% 留白的 `org.atlas.Atlas.bigsur.svg`） |
| `org.atlas.Atlas_256.png` | 商店 / 文档用的 256 px 位图 |
| `org.atlas.Atlas.logo*.svg` | README 顶部的 logo（与应用图标同图，深浅主题都能看） |
| `../launcher/resources/multimc/scalable/launcher.svg` | 主题里的启动器图标（同一份 SVG 的副本） |

改图之后**必须重新生成**二进制：

```bash
python genicons.py      # 需要 Chromium 内核浏览器 + Pillow；不出图就退非零
```

> `genicons.sh` 是上游那份（inkscape + icotool + oxipng），本机没有这些工具，所以有了
> `genicons.py`：用无头浏览器渲染一张 1024 的母图再降采样，避免 Chromium 在小窗口下裁切。
> 生成结果提交进仓库，**构建本身不需要任何渲染工具**。
> 许可与署名见仓库根 `THIRD-PARTY-NOTICES.md` 的「品牌与图标」节。

## 待办

- `instance_icons.svg`（实例占位图）与 `org.atlas.Atlas.Social.svg`（社交预览图）仍是上游素材，
  署名保留在 `THIRD-PARTY-NOTICES.md`；换成自绘素材时一并替换。
