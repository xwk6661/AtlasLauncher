#!/usr/bin/env python3
"""从本地大厅缓存 + 实例名里「学习」系列代码（RC / US / SP）。

实例名是玩家自己标的：`[RC3] The Prophecy`、`[US1] Lacuna Sanctum`、`[SP03] Entropy`。
CTMMaps 的大厅缓存里每张图带 `series` / `series_num`，但**没有**代码。两边一配对，
就能得到 "RC → Ragecraft" 这种映射 —— 这正是 `launcher/resources/maps/series.json` 的内容。

配对规则（保守，宁可漏也不认错）：
  · 把实例名去掉 `[CODE n]` 之后的部分，与大厅行的 `name` 做归一化比较（大小写 / 标点 / 空格）；
  · 名字完全相等 → 采信；否则要求一方包含另一方且长度差不超过 6 个字符；
  · 大厅行必须有 `series`，否则跳过（没有系列名就没什么可映射的）。

用法：
    python tools/ctm_series_codes.py                 # 只打印学到的映射
    python tools/ctm_series_codes.py --write          # 合并进 launcher/resources/maps/series.json
    python tools/ctm_series_codes.py --hall <cache> --instances <dir>
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import unicodedata
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_HALL = ROOT / "build/win-local/Debug/maps/hall-cache.json"
DEFAULT_INSTANCES = ROOT / "build/win-local/Debug/instances"
TARGET = ROOT / "launcher/resources/maps/series.json"

TAG_RE = re.compile(r"^\s*\[([A-Za-z]{1,4})(\d{1,4})\]\s*(.+)$")


def normalize(text: str) -> str:
    text = unicodedata.normalize("NFKC", text).casefold()
    return "".join(ch for ch in text if ch.isalnum())


def load_hall_rows(path: Path) -> list[dict]:
    with path.open(encoding="utf-8") as handle:
        data = json.load(handle)
    return data.get("list", [])


def load_instance_names(path: Path) -> list[str]:
    """实例的**显示名**来自 instance.cfg 的 `name=`，不是目录名。

    目录名是导入时用 `FS::DirNameFromString` 派生的（中文/符号会被改写），
    而玩家标的 `[RC3]` 只在 `name=` 里 —— 用目录名会把 RC 整组漏掉。
    """
    if not path.is_dir():
        return []

    names = []
    for child in sorted(path.iterdir()):
        if not child.is_dir():
            continue
        cfg = child / "instance.cfg"
        display = ""
        if cfg.is_file():
            for line in cfg.read_text(encoding="utf-8", errors="replace").splitlines():
                if line.startswith("name="):
                    display = line[len("name=") :].strip()
                    break
        names.append(display or child.name)
    return names


def match_row(rest: str, rows: list[dict]) -> dict | None:
    target = normalize(rest)
    if not target:
        return None

    best: tuple[int, dict] | None = None
    for row in rows:
        name = normalize(str(row.get("name") or ""))
        if not name or not row.get("series"):
            continue
        if name == target:
            score = 100
        elif target in name or name in target:
            diff = abs(len(name) - len(target))
            if diff > 6:
                continue
            score = 60 - diff
        else:
            continue
        if best is None or score > best[0]:
            best = (score, row)
    return best[1] if best else None


def learned_codes(rows: list[dict], instances: list[str]) -> dict[str, dict]:
    out: dict[str, dict] = {}
    for name in instances:
        match = TAG_RE.match(name)
        if not match:
            continue
        code, number, rest = match.group(1).upper(), match.group(2), match.group(3)
        row = match_row(rest, rows)
        if row is None:
            continue

        series = str(row.get("series") or "").strip()
        if not series:
            continue

        entry = {"name": series}
        # 站上的系列名太长（`Simulation Protocol`）时给一个短写，界面上才放得进 100px 的格子
        if len(series) > 16:
            words = series.split()
            if len(words) > 1:
                entry["short"] = " ".join([words[0][:3] + "."] + words[1:])
        current = out.get(code)
        if current and current["name"] != series:
            print(f"! {code} 冲突：{current['name']} / {series}（保留第一个）", file=sys.stderr)
            continue
        out[code] = entry
        print(f"  {code}{number} → {series} {int(number)}   （{name} ≡ {row.get('name')}）")
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--hall", type=Path, default=DEFAULT_HALL)
    parser.add_argument("--instances", type=Path, default=DEFAULT_INSTANCES)
    parser.add_argument("--write", action="store_true", help="合并写回 series.json")
    args = parser.parse_args()

    rows = load_hall_rows(args.hall)
    instances = load_instance_names(args.instances)
    print(f"大厅 {len(rows)} 张图 · 实例 {len(instances)} 个")

    learned = learned_codes(rows, instances)
    if not learned:
        print("没有学到新的代码（实例名里没有 [CODE n]，或者大厅行没有 series）")

    if not args.write:
        print(json.dumps(learned, ensure_ascii=False, indent=4))
        return 0

    data = {"schema": 1, "note": "", "codes": {}}
    if TARGET.exists():
        data = json.loads(TARGET.read_text(encoding="utf-8"))
    merged = data.setdefault("codes", {})
    added = 0
    for code, entry in learned.items():
        if merged.get(code) != entry:
            merged[code] = entry
            added += 1
    data["note"] = (
        "实例名里的系列代码 → CTMMaps 站上的系列名。由 tools/ctm_series_codes.py 从大厅缓存的 "
        "series 字段学习（2026-11-15），表里没有的代码原样显示，不猜。"
    )
    TARGET.write_text(json.dumps(data, ensure_ascii=False, indent=4) + "\n", encoding="utf-8")
    print(f"写入 {TARGET.relative_to(ROOT)}：新增/更新 {added} 条")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
