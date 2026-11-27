#!/usr/bin/env python
# SPDX-License-Identifier: GPL-3.0-only
"""文档一致性校验（只读）。

三件事，都是"人眼看不出来、但一定会腐化"的：

1. 相对链接指向的文件是否存在（`docs/atlas/01-设计.md` 里的 `[x](02-构建与运行.md)`）；
2. 跨文档章节引用是否越界（`06 §133`、`01 §7.5`、`03 §5.4`）；
3. Markdown 表格的列数是否一致（中间插一段文字会把表断成两半，渲染直接散架）。

另外会报 `06` 的重号（历史上 §94/§95 各重过一次）。

用法：
    python tools/docs_check.py            # 全量检查，有问题 exit 1
    python tools/docs_check.py --quiet    # 只输出汇总

不检查：锚点（`#xx`）、外链可达性、行内代码里的 `|`。
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DOCS = ROOT / "docs" / "atlas"

# 参与检查的文件：工程文档 + 历史留档 + 仓库根对外文档
TARGETS = sorted(DOCS.glob("*.md")) + sorted(DOCS.glob("archive/*.md")) + [
    ROOT / "README.md",
    ROOT / "BUILDING.md",
    ROOT / "CONTRIBUTING.md",
    ROOT / "SECURITY.md",
    ROOT / "CHANGELOG.md",
    ROOT / "THIRD-PARTY-NOTICES.md",
    ROOT / "COPYING.md",
    ROOT / "LICENSES" / "README.md",
    ROOT / "LICENSES" / "packages" / "icon-themes.txt",
]

LINK_RE = re.compile(r"(?<!!)\[[^\]]*\]\(([^)]+)\)")
CROSS_REF_RE = re.compile(r"\b(0[0-8])\s*§\s*(\d+(?:\.\d+)?)")
HEADING_NUM_RE = re.compile(r"^#{2,4}\s+(\d+(?:\.\d+)?)[.\s·]")
TABLE_ROW_RE = re.compile(r"^\s*\|")
INLINE_CODE_RE = re.compile(r"`[^`]*`")


def strip_code(text: str) -> str:
    """去掉行内代码，避免把示例里的 `|` 或链接当真的。"""
    return INLINE_CODE_RE.sub("", text)


def read(path: Path) -> list[str]:
    return path.read_text(encoding="utf-8").splitlines()


def check_links(path: Path, lines: list[str]) -> list[str]:
    problems: list[str] = []
    for no, raw in enumerate(lines, 1):
        if raw.lstrip().startswith("```"):
            continue
        for target in LINK_RE.findall(strip_code(raw)):
            target = target.strip()
            if target.startswith(("http://", "https://", "mailto:", "#", "tel:")):
                continue
            # `[int done, int total](...)` 这类是代码签名，不是链接
            if " " in target:
                continue
            target = target.split("#", 1)[0]
            if not target:
                continue
            resolved = (path.parent / target).resolve()
            if not resolved.exists():
                problems.append(f"{path.relative_to(ROOT)}:{no}: 链接不存在 -> {target}")
    return problems


def section_numbers(path: Path, lines: list[str]) -> set[str]:
    found: set[str] = set()
    for raw in lines:
        m = HEADING_NUM_RE.match(raw)
        if m:
            found.add(m.group(1))
    return found


def check_cross_refs(path: Path, lines: list[str], sections: dict[str, set[str]]) -> list[str]:
    problems: list[str] = []
    for no, raw in enumerate(lines, 1):
        if raw.lstrip().startswith("```"):
            continue
        for doc_no, sec in CROSS_REF_RE.findall(strip_code(raw)):
            known = sections.get(doc_no)
            if known is None:
                problems.append(f"{path.relative_to(ROOT)}:{no}: 引用了不存在的文档 {doc_no}")
                continue
            if sec not in known:
                problems.append(f"{path.relative_to(ROOT)}:{no}: {doc_no} §{sec} 没有对应小节")
    return problems


def table_columns(line: str) -> int:
    """按未转义的 | 数单元格。首尾的 | 不算分隔符；行内代码里的 | 不算。"""
    body = strip_code(line).strip()
    cells = re.split(r"(?<!\\)\|", body)
    if cells and cells[0].strip() == "":
        cells = cells[1:]
    if cells and cells[-1].strip() == "":
        cells = cells[:-1]
    return len(cells)


def check_tables(path: Path, lines: list[str]) -> list[str]:
    problems: list[str] = []
    block_start = 0
    block: list[tuple[int, str]] = []

    def flush() -> None:
        if not block:
            return
        # 正常表格的第二行是 |---|---| 分隔行。缺了它 = 表头被空行/段落截断，
        # 或者这是上一张表被打断后剩下的半截（渲染会散架）。
        separator_ok = len(block) >= 2 and re.fullmatch(
            r"\s*\|[\s:|-]+\|\s*", block[1][1]
        )
        if not separator_ok:
            problems.append(
                f"{path.relative_to(ROOT)}:{block[0][0]}: 表格缺少表头分隔行（被空行或段落截断）"
            )
            return
        counts = {table_columns(text) for _, text in block}
        if len(counts) > 1:
            expected = table_columns(block[0][1])
            for no, text in block:
                if table_columns(text) != expected:
                    problems.append(
                        f"{path.relative_to(ROOT)}:{no}: 表格列数 {table_columns(text)}，表头是 {expected}"
                    )

    for no, raw in enumerate(lines, 1):
        if TABLE_ROW_RE.match(raw) and not raw.lstrip().startswith("```"):
            if not block:
                block_start = no
            block.append((no, raw))
        else:
            flush()
            block = []
    flush()
    return problems


def check_duplicate_sections(path: Path, lines: list[str]) -> list[str]:
    # 06 的 §94/§95 在 2026-11-15 各重号一次，代码与文档按"§94 + 日期"引用；
    # 重编号会让 40+ 处引用失真，所以这两个是已知例外，只提示不算错。
    known = {("06", "94"), ("06", "95")}
    doc_no = path.name[:2]
    seen: dict[str, int] = {}
    problems: list[str] = []
    for no, raw in enumerate(lines, 1):
        m = re.match(r"^##\s+(\d+)[.\s]", raw)
        if not m:
            continue
        num = m.group(1)
        if num in seen:
            tag = "已知重号" if (doc_no, num) in known else "重号"
            problems.append(
                f"{path.relative_to(ROOT)}:{no}: §{num} {tag}（上一次在 {seen[num]} 行）"
            )
        else:
            seen[num] = no
    return problems


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--quiet", action="store_true", help="只输出汇总")
    args = ap.parse_args()

    sections: dict[str, set[str]] = {}
    for doc in sorted(DOCS.glob("*.md")):
        m = re.match(r"(0[0-8])-", doc.name)
        if m:
            sections[m.group(1)] = section_numbers(doc, read(doc))

    all_problems: dict[str, list[str]] = {}
    for path in TARGETS:
        if not path.exists():
            continue
        lines = read(path)
        found: list[str] = []
        found += check_links(path, lines)
        # 06 是历史档案：里面引用的 `04 §2.5` 之类是**当时**的编号，
        # 按现在的文件结构校验只会产生假阳性，所以只查它自己的重号。
        if not path.name.startswith("06-"):
            found += check_cross_refs(path, lines, sections)
        found += check_tables(path, lines)
        if path.name.startswith("06-"):
            found += check_duplicate_sections(path, lines)
        if found:
            all_problems[str(path.relative_to(ROOT))] = found

    total = 0
    known = 0
    for name, items in all_problems.items():
        print(f"\n== {name} ({len(items)}) ==")
        for item in items:
            if "已知重号" in item:
                known += 1
            else:
                total += 1
            if not args.quiet:
                print(f"  {item}")

    print(f"\n检查 {len(TARGETS)} 个文件：问题 {total} 处，已知例外 {known} 处。")
    return 0 if total == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
