#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把地图库目录里的存量压缩包整理成统一命名，并生成库索引 `map-library.json`。

命名格式（用户指定）：`[mc版本号]英文地图名 地图版本号 中文名.ext`

**每一条都标明依据**，不靠猜：
  · mc 版本：优先 level.dat 的 Version.Name；老存档用它自带的文件名方括号，
    再不行用结构旁证（Anvil+BorderSize → 1.8.x，落成 `[1.8.x]`）；
  · 英文名：level.dat 的 LevelName（剥 § 码）或文件名；中文图没有英文名时留空；
  · 地图版本号：文件名里的 v1.2 / V1.4.3 / 9.1.12 这类；
  · 中文名：文件名里的中文、实例 notes.json 的 alias、或 map.json 的 title；
    **没有证据就留空，不硬翻**。

同时做三件事：
  1. 重命名文件（旧名 → 新名记进 `build/map-library-rename.json`，可回滚）；
  2. 写出库索引 `map-library.json`（分类 + 元数据），分类只给有把握的，其余 other；
  3. 修正实例 `map.json` 里指向旧文件名的 `archive.libraryCopy` / `archive.path`。

用法：
  python tools/map_library_migrate.py --dry-run     # 只打印计划
  python tools/map_library_migrate.py --apply       # 真的改名 + 写索引 + 修引用
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from datetime import datetime, timezone

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA_ROOT = os.path.join(REPO, "build", "win-local", "Debug")
LIB_DIR = os.path.join(DATA_ROOT, "maps")
INSTANCES_DIR = os.path.join(DATA_ROOT, "instances")
INDEX_NAME = "map-library.json"

# 分类：rpg / aad / ctm / minigame / other
# 依据：CTM 系列（Ragecraft / Terra Restore / Final Paradox / Divinity's End / Castlemania /
#       Mapwreck / Entropy）按系列归 CTM；纯建筑/民居归 AAD；盘灵古域是 RPG；
#       Crew Craft Pixelmon 是宝可梦 RPG 整合图；其余宁可放 other 让玩家自己拖。
PLAN = [
    # old file, new file, category, mcVersion, english, mapVersion, chinese, note
    ("(存档)经典中世纪民居1.zip",
     "[1.21.8]Classic Medieval House 1 经典中世纪民居1.zip",
     "aad", "1.21.8", "Classic Medieval House 1", "", "经典中世纪民居1", ""),

    ("-= Ragecraft III - The Prophecy =-.zip",
     "[1.8.9]Ragecraft III - The Prophecy 狂乱之境III·预言（英文原版）.zip",
     "ctm", "1.8.9", "Ragecraft III - The Prophecy", "", "狂乱之境III·预言",
     "英文原版；与繁中版同图不同语言"),

    ("-= 狂亂之境 III - 預言 =-.rar",
     "[1.8.9]Ragecraft III - The Prophecy 狂亂之境III·預言（繁中）.rar",
     "ctm", "1.8.9", "Ragecraft III - The Prophecy", "", "狂亂之境III·預言",
     "繁中版；与英文原版同图不同语言"),

    ("Crew Craft Pixelmon 9.1.12 Reforged Final.zip",
     "[1.16.5]Crew Craft Pixelmon 9.1.12 Reforged Final.zip",
     "rpg", "1.16.5", "Crew Craft Pixelmon", "9.1.12 Reforged Final", "", "宝可梦整合图"),

    ("Little Nightmares #8 Tower of Sinner v1.1.zip",
     "[26.2]Little Nightmares #8 - Tower of Sinner v1.1.zip",
     "other", "26.2", "Little Nightmares #8 - Tower of Sinner", "v1.1", "", ""),

    ("PACK_City_of_nations.zip",
     "[1.12.2]City of Nations.zip",
     "aad", "1.12.2", "City of Nations", "", "", "含 2 个世界，导入时只取第一个"),

    ("Terra Restore V1.4.3 (TW).rar",
     "[1.8.1]Terra Restore V1.4.3 大地复苏（繁中）.rar",
     "ctm", "1.8.1", "Terra Restore", "V1.4.3", "大地复苏",
     "繁中版；与 1.8.1 中文版同图不同版本"),

    ("Untold Stories 09 - Castlemania - V1.0.5 - MC1.19.3.zip",
     "[1.19.3]Untold Stories 09 - Castlemania V1.0.5.zip",
     "ctm", "1.19.3", "Untold Stories 09 - Castlemania", "V1.0.5", "", ""),

    ("World+That+Doesn't+Exist.zip",
     "[1.21.11]World That Doesn't Exist.zip",
     "other", "1.21.11", "World That Doesn't Exist", "", "", ""),

    ("[1.16.5]Final_Paradox_v1.1.15.zip",
     "[1.16.5]Final Paradox v1.1.15.zip",
     "ctm", "1.16.5", "Final Paradox", "v1.1.15", "", ""),

    ("[1.21.1]Torre de Piedra v1.2.zip",
     "[1.21.1]Torre de Piedra v1.2 石之恐惧.zip",
     "other", "1.21.1", "Torre de Piedra", "v1.2", "石之恐惧", "中文名取自实例笔记"),

    ("[1.8.1]大地复苏Terra Restore.zip",
     "[1.8.1]Terra Restore 大地复苏.zip",
     "ctm", "1.8.1", "Terra Restore", "", "大地复苏",
     "与 V1.4.3 繁中版同图不同版本"),

    ("[1.8.9]20150712_盤靈古域_地圖檔1.2.3_壓縮檔.rar",
     "[1.8.9]Panilla 1.2.3 盤靈古域.rar",
     "rpg", "1.8.9", "Panilla", "1.2.3", "盤靈古域", "英文名取自 map.json 的 alias"),

    ("[MW7]破碎领域7：地下秘境v1.4汉化地图存档(汉化：v1.0).zip",
     "[1.20.4]Mapwreck VII - Subterranean v1.4 破碎领域7·地下秘境（汉化v1.0）.zip",
     "ctm", "1.20.4", "Mapwreck VII - Subterranean", "v1.4", "破碎领域7·地下秘境",
     "汉化补丁 v1.0"),

    ("[SP03]虚拟协议 03 - 熵 v1.1.zip",
     "[1.12]Entropy v1.1 虚拟协议03·熵.zip",
     "ctm", "1.12", "Entropy", "v1.1", "虚拟协议03·熵",
     "版本以 level.dat 为准（1.12，站点标 1.12.2）"),

    ("[必下][1.19.2] Divinity's End v1.1.6.zip",
     "[1.19.2]Divinity's End v1.1.6.zip",
     "ctm", "1.19.2", "Divinity's End", "v1.1.6", "", ""),

    ("boulevardier-s-folk-house-e2800.zip",
     "[1.20.4]Boulevardier's Folk House.zip",
     "aad", "1.20.4", "Boulevardier's Folk House", "", "", ""),

    ("house byTAO.zip",
     "[1.7.x]House by TAO.zip",
     "aad", "1.7.x", "House by TAO", "", "",
     "Anvil 无 BorderSize：1.7.x 区间，未细分"),

    ("temple world.zip",
     "[1.16.5]Temple World.zip",
     "aad", "1.16.5", "Temple World", "", "", ""),

    ("长眠者的圣维蕾娜修道院.zip",
     "[26.3]长眠者的圣维蕾娜修道院.zip",
     "aad", "26.3", "", "", "长眠者的圣维蕾娜修道院",
     "中文图，没有官方英文名"),
]


def now_iso() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def load_index(path: str) -> dict:
    if os.path.isfile(path):
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    return {"schema": 1, "entries": []}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--apply", action="store_true")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()
    apply_changes = args.apply and not args.dry_run

    if not os.path.isdir(LIB_DIR):
        print(f"库目录不存在：{LIB_DIR}")
        return 1

    existing = {n for n in os.listdir(LIB_DIR) if os.path.isfile(os.path.join(LIB_DIR, n))}
    planned_old = {p[0] for p in PLAN}
    planned_new = {p[1] for p in PLAN}
    archive_suffixes = {".zip", ".rar", ".7z", ".tar", ".gz", ".mcworld"}

    # 已经改过名的文件（新名）不能被当成「计划外的新文件」，否则每次跑都会重复登记一遍
    unknown = sorted(
        n for n in existing - planned_old - planned_new
        if os.path.splitext(n)[1].lower() in archive_suffixes
    )
    missing = sorted(planned_old - existing)

    print(f"库目录：{LIB_DIR}")
    print(f"计划处理 {len(PLAN)} 个包；目录里另有 {len(unknown)} 个未列入计划：{unknown}")
    if missing:
        print(f"⚠ 计划里这些文件在目录里不存在：{missing}")

    index = load_index(os.path.join(LIB_DIR, INDEX_NAME))
    by_file = {e["file"]: e for e in index.get("entries", [])}

    rename_log = []
    entries = []
    for old, new, category, mc, english, mapver, chinese, note in PLAN:
        src = os.path.join(LIB_DIR, old)
        dst = os.path.join(LIB_DIR, new)
        src_exists = os.path.isfile(src)
        dst_exists = os.path.isfile(dst)

        # 幂等：上一次已经改过名了（源不在、目标在），按计划登记即可
        if not src_exists and not dst_exists:
            print(f"  [skip] 源文件不在：{old}")
            continue
        if src_exists and dst_exists and os.path.abspath(src) != os.path.abspath(dst):
            print(f"  [skip] 目标已存在，拒绝覆盖：{new}")
            continue

        size = os.path.getsize(dst if dst_exists else src)
        entry = {
            "file": new,
            "category": category,
            "mcVersion": mc,
            "englishName": english,
            "mapVersion": mapver,
            "chineseName": chinese,
            "note": note,
            "bytes": size,
            "addedAt": by_file.get(new, {}).get("addedAt") or now_iso(),
        }
        # 老索引里已有这条（按旧文件名登记过）就沿用它的分类，避免覆盖玩家改过的分类
        old_entry = by_file.get(old)
        if old_entry and old_entry.get("category"):
            entry["category"] = old_entry["category"]
        entries.append(entry)

        rename_log.append({"old": old, "new": new})
        print(f"  {old}\n    → {new}  [{category} / {mc}]")

        if apply_changes and src_exists and os.path.abspath(src) != os.path.abspath(dst):
            os.replace(src, dst)

    # 目录里没进计划的包（玩家后加的）保留原样，归 other，不丢
    for name in unknown:
        if name in by_file:
            entries.append(by_file[name])
        else:
            entries.append({
                "file": name, "category": "other", "mcVersion": "", "englishName": "",
                "mapVersion": "", "chineseName": "", "note": "未整理（后加入）",
                "bytes": os.path.getsize(os.path.join(LIB_DIR, name)), "addedAt": now_iso(),
            })

    if apply_changes:
        # 以文件名为键去重（上一版索引里可能残留同名条目）
        dedup = {}
        for entry in entries:
            dedup.setdefault(entry["file"], entry)
        entries = list(dedup.values())

        index = {"schema": 1, "updatedAt": now_iso(), "entries": entries}
        with open(os.path.join(LIB_DIR, INDEX_NAME), "w", encoding="utf-8", newline="\n") as f:
            json.dump(index, f, ensure_ascii=False, indent=2)
            f.write("\n")
        print(f"\n索引已写入 {os.path.join(LIB_DIR, INDEX_NAME)}（{len(entries)} 条）")

        log_path = os.path.join(REPO, "build", "map-library-rename.json")
        with open(log_path, "w", encoding="utf-8", newline="\n") as f:
            json.dump({"at": now_iso(), "renames": rename_log}, f, ensure_ascii=False, indent=2)
            f.write("\n")
        print(f"改名记录：{log_path}")

        # 修正实例引用：旧文件名 → 新文件名
        mapping = {r["old"]: r["new"] for r in rename_log}
        library_files = {n for n in os.listdir(LIB_DIR) if os.path.isfile(os.path.join(LIB_DIR, n))}
        fixed = 0
        if os.path.isdir(INSTANCES_DIR):
            for instance in sorted(os.listdir(INSTANCES_DIR)):
                meta_path = os.path.join(INSTANCES_DIR, instance, "map.json")
                if not os.path.isfile(meta_path):
                    continue
                with open(meta_path, encoding="utf-8") as f:
                    meta = json.load(f)
                archive = meta.get("archive") or {}
                changed = False
                for key in ("libraryCopy", "path"):
                    value = archive.get(key) or ""
                    base = os.path.basename(value.replace("\\", "/"))
                    if not base:
                        continue
                    if base in mapping:
                        # 旧名刚被改过：直接指向新文件
                        archive[key] = os.path.join(LIB_DIR, mapping[base]).replace("\\", "/")
                        changed = True
                    elif key == "libraryCopy" and base in library_files and not os.path.isfile(value):
                        # 老路径已失效（换过数据目录/盘符），但库里正好有同名文件 → 重新指过去
                        archive[key] = os.path.join(LIB_DIR, base).replace("\\", "/")
                        changed = True
                if changed:
                    meta["archive"] = archive
                    with open(meta_path, "w", encoding="utf-8", newline="\n") as f:
                        json.dump(meta, f, ensure_ascii=False, indent=4)
                        f.write("\n")
                    fixed += 1
                    print(f"  实例引用已更新：{instance}")
        print(f"共更新 {fixed} 个实例的 map.json")

    # 实例里引用但库里没有的备份，单独列出来（这是「直接拖入安装」遗留下来的缺口）
    orphans = []
    if os.path.isdir(INSTANCES_DIR):
        for instance in sorted(os.listdir(INSTANCES_DIR)):
            meta_path = os.path.join(INSTANCES_DIR, instance, "map.json")
            if not os.path.isfile(meta_path):
                continue
            with open(meta_path, encoding="utf-8") as f:
                meta = json.load(f)
            archive = meta.get("archive") or {}
            candidates = [archive.get("libraryCopy") or "", archive.get("path") or ""]
            if not any(c and os.path.isfile(c) for c in candidates):
                orphans.append((instance, candidates))
    if orphans:
        print("\n⚠ 这些实例的备份压缩包找不到（重装会缺源）：")
        for instance, cands in orphans:
            print(f"  - {instance}: {cands}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
