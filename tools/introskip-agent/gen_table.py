#!/usr/bin/env python3
"""生成 introskip.json：各版本 WorldOpenFlows.askForBackup 的混淆名、描述符与 loadLevel 位置。

Minecraft 1.20+ 的类/方法名逐版本变化，而「实验性设置确认页」的闸门始终是
WorldOpenFlows.askForBackup(...)：它不加载世界，而是弹确认页；玩家点「我知道我在做什么」
走的就是其中的 loadLevel（Runnable）。这里从 Mojang 官方 client_mappings 里把这个方法抠出来，
产出一张只读表给启动器随包发布。

两种签名都出现过：
  1.20   ~ 1.20.2  askForBackup(Screen parent, String levelId, boolean customized, Runnable loadLevel)
  1.20.3 ~ 1.21.x  askForBackup(LevelStorageAccess access, boolean customized, Runnable loadLevel, Runnable onCancel)

用法：python gen_table.py [输出路径] [版本...]
"""

import json
import os
import re
import sys
import urllib.request

MANIFEST = "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json"
CACHE = os.path.join(os.path.dirname(os.path.abspath(__file__)), ".mappings-cache")

DEFAULT_VERSIONS = [
    "1.20", "1.20.1", "1.20.2", "1.20.3", "1.20.4", "1.20.5", "1.20.6",
    "1.21", "1.21.1", "1.21.2", "1.21.3", "1.21.4", "1.21.5",
]

OWNER = "net.minecraft.client.gui.screens.worldselection.WorldOpenFlows"
HEADER = re.compile(r"^(\S+) -> (\S+):$")
TYPE = re.compile(r"L([\w/$]+);")


def fetch(url, path):
    if os.path.exists(path):
        return path
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with urllib.request.urlopen(url, timeout=120) as response, open(path, "wb") as out:
        out.write(response.read())
    return path


def mappings_for(version, manifest):
    entry = next((v for v in manifest["versions"] if v["id"] == version), None)
    if entry is None:
        return None
    meta_path = os.path.join(CACHE, version + ".json")
    fetch(entry["url"], meta_path)
    with open(meta_path, encoding="utf-8") as handle:
        meta = json.load(handle)
    url = meta["downloads"]["client_mappings"]["url"]
    return fetch(url, os.path.join(CACHE, version + ".txt"))


PRIMITIVES = {
    "boolean": "Z", "byte": "B", "char": "C", "short": "S",
    "int": "I", "long": "J", "float": "F", "double": "D", "void": "V",
}


def split_types(signature):
    """'(net.minecraft...Screen,java.lang.String,boolean,java.lang.Runnable)'
    -> ['net.minecraft...Screen', 'java.lang.String', 'boolean', 'java.lang.Runnable']

    映射里的描述符是「人读的名字」而不是 JVM 描述符，泛型参数里还可能带逗号，所以按 <> 深度切。
    """
    inner = signature[1:signature.rindex(")")]
    if not inner:
        return []
    parts, depth, current = [], 0, ""
    for char in inner:
        if char == "<":
            depth += 1
        elif char == ">":
            depth -= 1
        if char == "," and depth == 0:
            parts.append(current)
            current = ""
        else:
            current += char
    parts.append(current)
    return [part.strip() for part in parts]


def to_descriptor(type_name, class_map):
    if type_name.endswith("[]"):
        return "[" + to_descriptor(type_name[:-2], class_map)
    if type_name in PRIMITIVES:
        return PRIMITIVES[type_name]
    type_name = type_name.split("<")[0]
    obf = class_map.get(type_name)
    if obf is None:
        # Minecraft 自己的类必须在映射里；java.* / com.mojang.* 这些原样转斜杠
        if type_name.startswith("net.minecraft."):
            return None
        obf = type_name.replace(".", "/")
    return "L%s;" % obf


def describe(signature, class_map):
    """返回 (JVM 描述符, loadLevel 的局部变量槽位)；有类型没映射上就返回 (None, None)。"""
    params = split_types(signature)
    descriptors, slots, slot = [], [], 1
    for name in params:
        descriptor = to_descriptor(name, class_map)
        if descriptor is None:
            return None, None
        descriptors.append(descriptor)
        slots.append(slot)
        slot += 2 if descriptor in ("J", "D") else 1

    runnables = [index for index, descriptor in enumerate(descriptors)
                 if descriptor == "Ljava/lang/Runnable;"]
    if not runnables:
        return None, None
    return "(" + "".join(descriptors) + ")V", slots[runnables[0]]


def extract(path):
    with open(path, encoding="utf-8", errors="replace") as handle:
        lines = handle.read().splitlines()

    class_map = {}
    for line in lines:
        header = HEADER.match(line)
        if header:
            class_map[header.group(1)] = header.group(2)

    owner_obf = class_map.get(OWNER)
    if owner_obf is None:
        return None

    in_block = False
    for line in lines:
        header = HEADER.match(line)
        if header:
            in_block = header.group(1) == OWNER
            continue
        if not in_block or "askForBackup(" not in line or " -> " not in line:
            continue

        signature = line.split("askForBackup", 1)[1].split(" -> ")[0]
        method_obf = line.rsplit(" -> ", 1)[1].strip()
        descriptor, index = describe(signature, class_map)
        if descriptor is None:
            return None
        return {"class": owner_obf, "method": method_obf, "desc": descriptor, "index": index}

    return None


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else "introskip.json"
    wanted = sys.argv[2:] or DEFAULT_VERSIONS

    with urllib.request.urlopen(MANIFEST, timeout=60) as response:
        manifest = json.load(response)

    versions = {}
    for version in wanted:
        path = mappings_for(version, manifest)
        if path is None:
            print("%-8s 跳过：版本列表里没有" % version)
            continue
        entry = extract(path)
        if entry is None:
            print("%-8s 跳过：没有 askForBackup 或签名不认识" % version)
            continue
        versions[version] = entry
        print("%-8s %s.%s%s  loadLevel=local%d" % (version, entry["class"], entry["method"], entry["desc"], entry["index"]))

    table = {
        "schema": 1,
        "note": "WorldOpenFlows.askForBackup 的混淆名；把它改写成 loadLevel.run() 即可跳过实验性设置确认页",
        "versions": versions,
    }
    with open(out_path, "w", encoding="utf-8") as handle:
        json.dump(table, handle, ensure_ascii=False, indent=2, sort_keys=True)
        handle.write("\n")
    print("wrote %s (%d versions)" % (out_path, len(versions)))


if __name__ == "__main__":
    main()
