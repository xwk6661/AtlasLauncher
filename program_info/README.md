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

## 待办

图标仍是上游素材（GPL 允许改，但不适合当自己的产品标识）。
换自制图标后重跑 `genicons.sh` 重新生成 `atlas.ico` / `atlas.icns`。
