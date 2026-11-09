#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""只读盘点一个目录里的地图压缩包：版本、存档名、世界数、体积、sha1、重复项。

用途有两个：
  · 地图库整理前先看清「到底有哪些图、哪几张其实是同一张」；
  · 之后新增地图时也能跑一次做体检。

它**不修改任何文件**，只输出 TSV / Markdown 表。

实现要点：
  · 用 7z 列目录（zip / rar / 7z 统一），找所有以 level.dat 结尾的条目；
  · 只把 level.dat 抽到内存（几 KB~几十 KB），不整包解压；
  · level.dat 是 GZip + NBT，这里用一个够用的最小 NBT 解析器读
    Data.LevelName / Data.Version.Name / Data.Version.Id / Data.DataVersion。

用法：
  python tools/map_library_scan.py --dir "E:/.../Debug/maps"
  python tools/map_library_scan.py --dir "..." --json out.json
"""

from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import os
import re
import struct
import subprocess
import sys

SEVEN_ZIP = r"C:\Program Files\7-Zip\7z.exe"

MAP_SUFFIXES = {".zip", ".rar", ".7z", ".tar", ".gz", ".mcworld"}


# --------------------------------------------------------------------------- #
# 最小 NBT 解析（只要 level.dat 顶层几个字段）
# --------------------------------------------------------------------------- #

TAG_END, TAG_BYTE, TAG_SHORT, TAG_INT, TAG_LONG = 0, 1, 2, 3, 4
TAG_FLOAT, TAG_DOUBLE, TAG_BYTE_ARRAY, TAG_STRING = 5, 6, 7, 8
TAG_LIST, TAG_COMPOUND, TAG_INT_ARRAY, TAG_LONG_ARRAY = 9, 10, 11, 12


class Reader:
    def __init__(self, data: bytes):
        self.data = data
        self.pos = 0

    def take(self, n: int) -> bytes:
        out = self.data[self.pos:self.pos + n]
        self.pos += n
        return out

    def u1(self) -> int:
        v = self.data[self.pos]
        self.pos += 1
        return v

    def u2(self) -> int:
        v = struct.unpack_from(">H", self.data, self.pos)[0]
        self.pos += 2
        return v

    def i4(self) -> int:
        v = struct.unpack_from(">i", self.data, self.pos)[0]
        self.pos += 4
        return v

    def i8(self) -> int:
        v = struct.unpack_from(">q", self.data, self.pos)[0]
        self.pos += 8
        return v

    def f4(self) -> float:
        v = struct.unpack_from(">f", self.data, self.pos)[0]
        self.pos += 4
        return v

    def f8(self) -> float:
        v = struct.unpack_from(">d", self.data, self.pos)[0]
        self.pos += 8
        return v

    def string(self) -> str:
        n = self.u2()
        return self.take(n).decode("utf-8", "replace")


def read_payload(r: Reader, tag: int):
    if tag == TAG_BYTE:
        return r.u1()
    if tag == TAG_SHORT:
        return r.u2()
    if tag == TAG_INT:
        return r.i4()
    if tag == TAG_LONG:
        return r.i8()
    if tag == TAG_FLOAT:
        return r.f4()
    if tag == TAG_DOUBLE:
        return r.f8()
    if tag == TAG_BYTE_ARRAY:
        return r.take(r.i4())
    if tag == TAG_STRING:
        return r.string()
    if tag == TAG_LIST:
        item = r.u1()
        count = r.i4()
        return [read_payload(r, item) for _ in range(max(0, min(count, 4096)))]
    if tag == TAG_COMPOUND:
        out = {}
        while True:
            t = r.u1()
            if t == TAG_END:
                return out
            name = r.string()
            out[name] = read_payload(r, t)
    if tag == TAG_INT_ARRAY:
        n = r.i4()
        return [r.i4() for _ in range(max(0, min(n, 4096)))]
    if tag == TAG_LONG_ARRAY:
        n = r.i4()
        return [r.i8() for _ in range(max(0, min(n, 4096)))]
    raise ValueError(f"未知 NBT tag {tag}")


def parse_level_dat(raw: bytes) -> dict:
    if raw[:2] == b"\x1f\x8b":
        raw = gzip.decompress(raw)
    r = Reader(raw)
    tag = r.u1()
    if tag != TAG_COMPOUND:
        raise ValueError("level.dat 顶层不是 compound")
    r.string()                                  # 根名（通常是空串）
    return read_payload(r, TAG_COMPOUND) or {}


# --------------------------------------------------------------------------- #
# 7z 交互
# --------------------------------------------------------------------------- #

def list_entries(archive: str) -> list[str]:
    # -sccUTF-8：rar 里的中文条目名默认按本地代码页输出，会导致后面 -so 取不到
    proc = subprocess.run(
        [SEVEN_ZIP, "l", "-ba", "-slt", "-sccUTF-8", archive],
        capture_output=True, text=True, encoding="utf-8", errors="replace",
    )
    if proc.returncode != 0:
        raise RuntimeError((proc.stderr or proc.stdout).strip()[:200])
    # 注意：带 -ba 时**没有**「压缩包自己」那一行，第一个 Path 就是第一个条目
    return [line[7:] for line in proc.stdout.splitlines() if line.startswith("Path = ")]


def extract_entry(archive: str, entry: str) -> bytes | None:
    # `--` 必须加：地图包里的条目常常以 "-=" 开头，否则 7z 会把它当开关（Command Line Error: Unknown switch）
    proc = subprocess.run(
        [SEVEN_ZIP, "e", "-so", "-sccUTF-8", archive, "--", entry],
        capture_output=True,
    )
    if proc.returncode != 0 or not proc.stdout:
        return None
    return proc.stdout


def sha1_of(path: str) -> str:
    h = hashlib.sha1()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


# --------------------------------------------------------------------------- #

def human(n: int) -> str:
    for unit in ("B", "KB", "MB", "GB"):
        if n < 1024 or unit == "GB":
            return f"{n:.1f} {unit}" if unit != "B" else f"{n} B"
        n /= 1024
    return f"{n:.1f} GB"


def scan_one(path: str) -> dict:
    info = {
        "file": os.path.basename(path),
        "path": path,
        "bytes": os.path.getsize(path),
        "sha1": "",
        "mcVersion": "",
        "versionId": None,
        "dataVersion": None,
        "levelName": "",
        "worlds": 0,
        "roots": [],
        "error": "",
    }
    try:
        entries = list_entries(path)
    except Exception as exc:                    # noqa: BLE001
        info["error"] = f"列目录失败：{exc}"
        return info

    level_dats = [e for e in entries if e.replace("\\", "/").lower().endswith("level.dat")]
    world_roots = sorted({e.replace("\\", "/").rsplit("/", 1)[0] if "/" in e.replace("\\", "/") else ""
                          for e in level_dats})
    info["worlds"] = len(level_dats)
    info["roots"] = world_roots[:6]

    if not level_dats:
        info["error"] = "包里没有 level.dat"
        return info

    # 取最浅的那个当主世界
    primary = sorted(level_dats, key=lambda e: e.replace("\\", "/").count("/"))[0]
    try:
        raw = extract_entry(path, primary)
        if raw is None:
            raise RuntimeError("解不出 level.dat")
        data = parse_level_dat(raw)
        # level.dat 的字段一律包在根 compound 的 Data 里（少数工具会拍平，做一次兼容）
        data = data.get("Data") or data
        info["levelName"] = data.get("LevelName", "") or ""
        version = data.get("Version") or {}
        info["mcVersion"] = version.get("Name", "") or ""
        info["versionId"] = version.get("Id")
        info["dataVersion"] = data.get("DataVersion")
        # 老存档（≤1.8.9）没有 Version / DataVersion，只能看容器格式与 1.8+ 才有的字段
        info["storageVersion"] = data.get("version")
        info["hasBorderSize"] = "BorderSize" in data
        info["lastPlayed"] = data.get("LastPlayed")
        for key in ("fml", "FML", "Forge"):
            if key in data:
                info["modded"] = True
    except Exception as exc:                    # noqa: BLE001
        info["error"] = f"读 level.dat 失败：{exc}"

    info["sha1"] = sha1_of(path)
    return info


def version_hint(row: dict) -> str:
    """没有内嵌版本号时给出保守的结构旁证，不猜具体版本。"""
    storage = row.get("storageVersion")
    if storage == 19133:
        return "≤1.8.9（Anvil）" + ("＋Border" if row.get("hasBorderSize") else "")
    if storage == 19132:
        return "≤1.2（McRegion）"
    return "?"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", required=True)
    ap.add_argument("--json", default="")
    args = ap.parse_args()

    files = sorted(
        os.path.join(args.dir, n) for n in os.listdir(args.dir)
        if os.path.splitext(n)[1].lower() in MAP_SUFFIXES
        and os.path.isfile(os.path.join(args.dir, n))
    )

    rows = [scan_one(f) for f in files]

    print(f"共 {len(rows)} 个地图包\n")
    print("| 文件 | 体积 | 版本 | 存档名 | 世界数 | sha1 前 8 位 | 备注 |")
    print("| --- | --- | --- | --- | --- | --- | --- |")
    for r in rows:
        print("| {file} | {size} | {ver} | {name} | {worlds} | {sha} | {note} |".format(
            file=r["file"], size=human(r["bytes"]),
            ver=r["mcVersion"] or version_hint(r), name=r["levelName"] or "?",
            worlds=r["worlds"], sha=r["sha1"][:8], note=r["error"]))

    # 重复判定：sha1 相同即同一份；否则按 (版本, 存档名, 体积近似) 提示疑似
    seen = {}
    print("\n## 完全相同的包（sha1 一致）\n")
    dup = False
    for r in rows:
        if not r["sha1"]:
            continue
        seen.setdefault(r["sha1"], []).append(r)
    for sha, group in seen.items():
        if len(group) > 1:
            dup = True
            print("- " + " / ".join(g["file"] for g in group))
    if not dup:
        print("（无）")

    print("\n## 疑似同一张图（版本 + 存档名 + 体积相近）\n")
    buckets = {}
    for r in rows:
        if not r["mcVersion"]:
            continue
        key = (r["mcVersion"], re.sub(r"\s+", "", r["levelName"]).lower())
        buckets.setdefault(key, []).append(r)
    suspected = False
    for key, group in buckets.items():
        if len(group) > 1:
            suspected = True
            print(f"- {key[0]} / {key[1] or '(无名)'}：" + " / ".join(
                f"{g['file']}（{human(g['bytes'])}）" for g in group))
    if not suspected:
        print("（无）")

    if args.json:
        with open(args.json, "w", encoding="utf-8", newline="\n") as f:
            json.dump(rows, f, ensure_ascii=False, indent=2)
        print(f"\nJSON 已写入 {args.json}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
