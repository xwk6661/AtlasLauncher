# 图标留档 · 2026-11-26（矢量 A 字标）

这是 2026-11-26 21:45 短暂用过的一版图标：**六边形底 + 3D 字母 A**（512 viewBox 的纯 SVG，
奶油色 A + 琥珀色描边，青蓝渐变底）。当晚 22:18 按维护者要求换回位图候选
（六边形瓦片 + 等距立方体 + 罗盘指针），本稿归档。

| 文件 | 说明 |
| --- | --- |
| `org.atlas.Atlas.svg` | 当时的唯一原稿；构建时由 `genicons.py` 渲染成 1024 PNG 再生成 ico / icns |

**为什么留档**：矢量稿任意缩放都清晰，如果以后决定用字母标路线，这份是完整起点
（12.8 KB，无外部依赖）。

## 恢复成这一版的做法

1. 把 `org.atlas.Atlas.svg` 复制回 `program_info/`；
2. 删掉（或改名）`program_info/org.atlas.Atlas.png` —— `genicons.py` 会优先渲染 SVG 并回写 PNG；
3. `python program_info/genicons.py`，再重新 configure + 构建（qrc 引用的是 PNG）。

> 只恢复第 1 步而不删 PNG 也可以：脚本是**矢量优先**，有 SVG 就渲染它并覆盖 PNG。
> 归档件本身**不参与构建**。
