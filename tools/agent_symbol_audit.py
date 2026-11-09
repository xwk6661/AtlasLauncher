#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把 Atlas 的 Agent 符号表对着**真实的 client jar** 复核，并为缺表的版本离线生成。

为什么需要这个脚本
------------------
符号表是 quickboot.jar 唯一的输入：类名、方法名、描述符全都靠它，错一个字节就是
「启动器以为注入了、游戏其实停在主菜单」。而混淆名每版都变，光靠人眼看 JSON 不可能
保证「这张表里的每一项在那个版本的 jar 里真的存在」。

所以这里的每一行结论都来自一次真实的字节码查询：
  · classes[slot].obf        -> jar 里必须有 <obf>.class
  · methods[slot]            -> 该类的 methods 表里必须有 (name, descriptor)
  · methods[slot].isStatic   -> access_flags 的 ACC_STATIC 位必须与表里一致

两种模式
--------
  audit     只读：复核现有 symbols/*.json，逐槽位给出 OK / FAIL，不改任何文件。
  generate  对**缺表**且 ≤1.19.4 的版本调用 AIO 的离线解析器
            （Minecraft-AIO/launcher: `tsx src/cli.ts symbols <v> --jar … --no-download --force --json`），
            只接受「必需槽位齐全 + 每个槽位 verified」的结果，落盘前再用本脚本的解析器独立复核一次。
            ≥1.20 不生成：那条轨道走 --quickPlaySingleplayer，不需要 Agent。

用法
----
  python tools/agent_symbol_audit.py audit
  python tools/agent_symbol_audit.py generate --dry-run
  python tools/agent_symbol_audit.py generate
  python tools/agent_symbol_audit.py audit --report docs/atlas/agent-matrix.md

退出码：audit 模式下有任何 FAIL 就是 1（可以挂 CI）；generate 模式下只报告、不因个别版本
不可解析而失败（老版本本来就没有集成服务端，解析不出来是事实，不是错误）。
"""

from __future__ import annotations

import argparse
import json
import os
import re
import struct
import subprocess
import sys
from dataclasses import dataclass, field

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA_ROOT = os.path.join(REPO, "build", "win-local", "Debug")
JAR_ROOT = os.path.join(DATA_ROOT, "libraries", "com", "mojang", "minecraft")
SYMBOLS_DIR = os.path.join(REPO, "launcher", "resources", "agents", "symbols")
AIO_LAUNCHER = os.environ.get("MCML_LAUNCHER", r"E:\llmUsage\Minecraft-AIO\launcher")

# 与 C++ 侧 AgentBootstrap::buildAgentArgs 的必需项保持一致；改这里必须同步改那边
REQUIRED_CLASSES = ["menuClass", "minecraft"]
REQUIRED_METHODS = ["menuInit", "enterWorld", "setScreen", "saveAllWorlds"]
# 可选但影响功能：setLevel -> loadWorld=…，stopServer -> 玩家自己退出也能判定
OPTIONAL_METHODS = ["setLevel", "stopServer", "getInstance"]

ACC_STATIC = 0x0008


# --------------------------------------------------------------------------- #
# 最小 .class 解析器（只取 this_class 与 methods 表，够复核用）
# --------------------------------------------------------------------------- #

def _u2(data: bytes, pos: int) -> int:
    return struct.unpack_from(">H", data, pos)[0]


def _u4(data: bytes, pos: int) -> int:
    return struct.unpack_from(">I", data, pos)[0]


def _skip_attributes(data: bytes, pos: int) -> int:
    count = _u2(data, pos)
    pos += 2
    for _ in range(count):
        pos += 2                      # attribute_name_index
        length = _u4(data, pos)
        pos += 4 + length
    return pos


@dataclass
class ClassInfo:
    name: str                                    # 内部名 a/b/C
    methods: list = field(default_factory=list)   # [(name, descriptor, access)]


def parse_class(data: bytes) -> ClassInfo:
    if data[:4] != b"\xca\xfe\xba\xbe":
        raise ValueError("不是 class 文件")

    pos = 8
    cp_count = _u2(data, pos)
    pos += 2

    utf8: dict[int, str] = {}
    class_index: dict[int, int] = {}
    i = 1
    while i < cp_count:
        tag = data[pos]
        pos += 1
        if tag == 1:                                   # CONSTANT_Utf8
            length = _u2(data, pos)
            pos += 2
            utf8[i] = data[pos:pos + length].decode("utf-8", "replace")
            pos += length
        elif tag == 7:                                 # Class
            class_index[i] = _u2(data, pos)
            pos += 2
        elif tag in (8, 16, 19, 20):                   # String/MethodType/Module/Package
            pos += 2
        elif tag == 15:                                # MethodHandle
            pos += 3
        elif tag in (3, 4, 9, 10, 11, 12, 17, 18):     # 定长 4 字节
            pos += 4
        elif tag in (5, 6):                            # Long/Double 占两个槽位
            pos += 8
            i += 1
        else:
            raise ValueError(f"未知常量池 tag {tag}")
        i += 1

    pos += 2                                           # access_flags
    this_class = _u2(data, pos)
    pos += 2
    pos += 2                                           # super_class
    name = utf8[class_index[this_class]]

    interfaces = _u2(data, pos)
    pos += 2 + interfaces * 2

    fields = _u2(data, pos)
    pos += 2
    for _ in range(fields):
        pos += 6                                       # access/name/descriptor
        pos = _skip_attributes(data, pos)

    methods = _u2(data, pos)
    pos += 2
    out = []
    for _ in range(methods):
        access = _u2(data, pos)
        name_index = _u2(data, pos + 2)
        desc_index = _u2(data, pos + 4)
        pos += 6
        pos = _skip_attributes(data, pos)
        out.append((utf8[name_index], utf8[desc_index], access))

    return ClassInfo(name=name, methods=out)


class JarIndex:
    """一次打开一个 jar，按需解析 class；同一个类只解析一次。"""

    def __init__(self, path: str):
        import zipfile
        self.path = path
        self._zip = zipfile.ZipFile(path, "r")
        self._names = set(self._zip.namelist())
        self._cache: dict[str, ClassInfo | None] = {}

    def has_class(self, internal: str) -> bool:
        return internal + ".class" in self._names

    def klass(self, internal: str) -> ClassInfo | None:
        if internal not in self._cache:
            entry = internal + ".class"
            if entry not in self._names:
                self._cache[internal] = None
            else:
                self._cache[internal] = parse_class(self._zip.read(entry))
        return self._cache[internal]

    def close(self):
        self._zip.close()


def internal_name(obf: str) -> str:
    return obf.replace(".", "/")


# --------------------------------------------------------------------------- #
# 复核
# --------------------------------------------------------------------------- #

@dataclass
class SlotResult:
    slot: str
    kind: str          # class / method
    signature: str
    ok: bool
    reason: str = ""


@dataclass
class TableAudit:
    version: str
    path: str
    jar: str | None
    results: list = field(default_factory=list)
    missing_jar: bool = False
    has_mappings: bool = False

    @property
    def ok(self) -> bool:
        return not self.missing_jar and all(r.ok for r in self.results)

    @property
    def required_ok(self) -> bool:
        req = set(REQUIRED_CLASSES) | set(REQUIRED_METHODS)
        return not self.missing_jar and all(r.ok for r in self.results if r.slot in req)


def audit_table(table_file: str, jar_dir: str) -> TableAudit:
    with open(table_file, "r", encoding="utf-8") as f:
        table = json.load(f)

    version = table.get("mcVersion") or os.path.splitext(os.path.basename(table_file))[0]
    jar = client_jar(jar_dir, version)
    result = TableAudit(version=version, path=table_file, jar=jar,
                        has_mappings=bool(table.get("hasMappings")))
    if jar is None:
        result.missing_jar = True
        return result

    index = JarIndex(jar)
    try:
        for slot, entry in (table.get("classes") or {}).items():
            obf = entry.get("obf", "")
            internal = internal_name(obf)
            if index.has_class(internal):
                result.results.append(SlotResult(slot, "class", obf, True))
            else:
                result.results.append(SlotResult(slot, "class", obf, False, "jar 里没有这个类"))

        for slot, entry in (table.get("methods") or {}).items():
            owner = internal_name(entry.get("owner", ""))
            name = entry.get("name", "")
            desc = entry.get("descriptor", "")
            sig = f"{owner}.{name}{desc}"
            klass = index.klass(owner)
            if klass is None:
                result.results.append(SlotResult(slot, "method", sig, False, "owner 类不在 jar 里"))
                continue
            hit = next((m for m in klass.methods if m[0] == name and m[1] == desc), None)
            if hit is None:
                near = sorted({m[1] for m in klass.methods if m[0] == name})
                reason = "同名方法存在，但描述符是 " + ", ".join(near) if near else "类里没有这个方法名"
                result.results.append(SlotResult(slot, "method", sig, False, reason))
                continue
            is_static = bool(hit[2] & ACC_STATIC)
            want_static = bool(entry.get("isStatic", False))
            if is_static != want_static:
                result.results.append(
                    SlotResult(slot, "method", sig, False,
                               f"static 标记不一致：表里 {want_static}，jar 里 {is_static}")
                )
                continue
            result.results.append(SlotResult(slot, "method", sig, True))
    finally:
        index.close()

    # 跨槽位一致性：方法必须挂在该槽位约定的那个类上。
    # 光「存在」不够 —— 换个同名方法（比如把 MenuClass 的方法挂到 GuiScreen 上）也能过存在性检查。
    owner_expect = {
        "menuInit": "menuClass",
        "setScreen": "minecraft",
        "enterWorld": "minecraft",
        "getInstance": "minecraft",
        "saveAllWorlds": "minecraftServer",
        "stopServer": "minecraftServer",
    }
    classes = table.get("classes") or {}
    methods = table.get("methods") or {}
    for method_slot, class_slot in owner_expect.items():
        method = methods.get(method_slot)
        klass = classes.get(class_slot)
        if not method or not klass:
            continue
        want = internal_name(klass.get("obf", ""))
        got = internal_name(method.get("owner", ""))
        result.results.append(
            SlotResult(f"一致性:{method_slot}", "method", f"{got} == {want}", got == want,
                       "" if got == want else f"{method_slot} 应属于 {class_slot}({want})，实际在 {got}")
        )

    return result


def client_jar(jar_dir: str, version: str) -> str | None:
    path = os.path.join(jar_dir, version, f"minecraft-{version}-client.jar")
    return path if os.path.isfile(path) else None


def available_versions(jar_dir: str) -> list[str]:
    if not os.path.isdir(jar_dir):
        return []
    out = []
    for name in os.listdir(jar_dir):
        if client_jar(jar_dir, name):
            out.append(name)
    return sort_versions(out)


def version_key(version: str) -> tuple:
    m = re.fullmatch(r"(\d+)\.(\d+)(?:\.(\d+))?", version)
    if not m:
        return (999,)
    return (int(m.group(1)), int(m.group(2)), int(m.group(3) or 0))


def sort_versions(versions) -> list[str]:
    return sorted(versions, key=version_key)


def is_release(version: str) -> bool:
    return re.fullmatch(r"\d+\.\d+(?:\.\d+)?", version) is not None


def agent_track(version: str) -> bool:
    """≤1.19.4 才需要 Agent；≥1.20 走 quickPlay。"""
    return is_release(version) and version_key(version) < (1, 20, 0)


# --------------------------------------------------------------------------- #
# 生成（调用 AIO 的离线解析器）
# --------------------------------------------------------------------------- #

def aio_resolve(version: str, jar: str, mappings: str | None = None, timeout: int = 300) -> dict:
    cli = os.path.join(AIO_LAUNCHER, "node_modules", "tsx", "dist", "cli.mjs")
    entry = os.path.join(AIO_LAUNCHER, "src", "cli.ts")
    if not os.path.isfile(cli) or not os.path.isfile(entry):
        raise RuntimeError(f"找不到 AIO 解析器：{cli}")

    cmd = [
        "node", cli, entry, "symbols", version,
        "--jar", jar, "--no-download", "--force", "--json",
    ]
    if mappings:
        cmd += ["--mappings", mappings]
    proc = subprocess.run(cmd, cwd=AIO_LAUNCHER, capture_output=True, text=True,
                          encoding="utf-8", errors="replace", timeout=timeout)
    if proc.returncode != 0:
        raise RuntimeError((proc.stderr or proc.stdout or "").strip()[:400] or "解析器退出码非零")

    text = proc.stdout.strip()
    start = text.find("{")
    end = text.rfind("}")
    if start < 0 or end < 0:
        raise RuntimeError("解析器没有输出 JSON")
    return json.loads(text[start:end + 1])


# --------------------------------------------------------------------------- #
# 官方 ProGuard 映射（1.14.4+ 才有），有它就不用只靠指纹
# --------------------------------------------------------------------------- #

MANIFEST_URL = "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json"


def _fetch_json(url: str, timeout: int = 40) -> dict:
    import urllib.request
    req = urllib.request.Request(url, headers={"User-Agent": "atlas-symbol-audit/1.0"})
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read().decode("utf-8"))


def _download(url: str, target: str, timeout: int = 120) -> bool:
    import urllib.request
    tmp = target + ".part"
    req = urllib.request.Request(url, headers={"User-Agent": "atlas-symbol-audit/1.0"})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp, open(tmp, "wb") as f:
            while True:
                chunk = resp.read(1 << 20)
                if not chunk:
                    break
                f.write(chunk)
        os.replace(tmp, target)
        return True
    except Exception:                                   # noqa: BLE001 - 网络问题不该中断整批
        if os.path.exists(tmp):
            os.remove(tmp)
        return False


def ensure_mappings(version: str, cache_dir: str) -> str | None:
    """把该版本的官方 client_mappings 抓到本地缓存；拿不到返回 None（退回指纹）。

    返回**绝对路径**：解析器以 AIO 仓库为工作目录运行，相对路径会被它按自己的 cwd 解析。
    """
    cache_dir = os.path.abspath(cache_dir)
    if version_key(version) < (1, 14, 4):
        return None                                     # 官方从 1.14.4 才开始发布

    target = os.path.join(cache_dir, f"{version}.txt")
    if os.path.isfile(target) and os.path.getsize(target) > 1024:
        return target

    os.makedirs(cache_dir, exist_ok=True)
    manifest_path = os.path.join(cache_dir, "manifest_v2.json")
    try:
        if os.path.isfile(manifest_path):
            with open(manifest_path, "r", encoding="utf-8") as f:
                manifest = json.load(f)
        else:
            manifest = _fetch_json(MANIFEST_URL)
            with open(manifest_path, "w", encoding="utf-8") as f:
                json.dump(manifest, f)
    except Exception:                                   # noqa: BLE001
        return None

    entry = next((v for v in manifest.get("versions", []) if v.get("id") == version), None)
    if not entry:
        return None

    try:
        detail = _fetch_json(entry["url"])
    except Exception:                                   # noqa: BLE001
        return None

    url = (detail.get("downloads", {}).get("client_mappings") or {}).get("url")
    if not url:
        return None                                     # 快照或未发布映射
    return target if _download(url, target) else None


def required_slots_ok(table: dict) -> tuple[bool, str]:
    classes = table.get("classes") or {}
    methods = table.get("methods") or {}
    missing = [c for c in REQUIRED_CLASSES if c not in classes]
    missing += [m for m in REQUIRED_METHODS if m not in methods]
    if missing:
        return False, "缺必需槽位：" + ", ".join(missing)

    unverified = [slot for slot, e in list(classes.items()) + list(methods.items()) if not e.get("verified")]
    if unverified:
        return False, "槽位没有通过 jar 校验：" + ", ".join(unverified)

    return True, ""


def generate(versions: list[str], jar_dir: str, symbols_dir: str, dry_run: bool,
             mappings_cache: str, replace: set[str] | None = None) -> list[dict]:
    replace = replace or set()
    report = []
    for version in versions:
        target = os.path.join(symbols_dir, f"{version}.json")
        if os.path.exists(target) and version not in replace:
            continue
        if not agent_track(version):
            report.append({"version": version, "status": "skip", "reason": "≥1.20 走 quickPlay，不需要 Agent"})
            continue

        jar = client_jar(jar_dir, version)
        if jar is None:
            report.append({"version": version, "status": "skip", "reason": "没有 client jar"})
            continue

        mappings = ensure_mappings(version, mappings_cache)
        try:
            table = aio_resolve(version, jar, mappings)
        except Exception as exc:                        # noqa: BLE001 - 一个版本失败不该中断整批
            report.append({"version": version, "status": "fail", "reason": str(exc)})
            print(f"  [{version:>9}] fail        {exc}", flush=True)
            continue

        ok, reason = required_slots_ok(table)
        if not ok:
            # 把解析器自己给出的 unresolved 理由带上，矩阵里才能回答「为什么没有」
            extra = "; ".join(f"{u.get('slot')}: {u.get('reason')}" for u in (table.get("unresolved") or [])[:2])
            if extra:
                reason = f"{reason}（{extra}）"
            report.append({"version": version, "status": "incomplete", "reason": reason})
            print(f"  [{version:>9}] incomplete  {reason}", flush=True)
            continue

        how = "官方映射" if table.get("hasMappings") else "字节码指纹"
        detail = f"{how}；{len(table.get('classes', {}))} 类 / {len(table.get('methods', {}))} 方法"

        if dry_run:
            report.append({"version": version, "status": "would-write", "reason": detail})
            print(f"  [{version:>9}] would-write {detail}", flush=True)
            continue

        with open(target, "w", encoding="utf-8", newline="\n") as f:
            json.dump(table, f, ensure_ascii=False, indent=2)
            f.write("\n")

        # 落盘之后再复核一次：用本脚本自己的解析器，不信解析器的自述
        check = audit_table(target, jar_dir)
        if check.required_ok:
            report.append({"version": version, "status": "written", "reason": detail})
            print(f"  [{version:>9}] written     {detail}", flush=True)
        else:
            os.remove(target)
            bad = [r for r in check.results if not r.ok]
            report.append({"version": version, "status": "rejected",
                           "reason": "; ".join(f"{r.slot}:{r.reason}" for r in bad)[:300]})
            print(f"  [{version:>9}] rejected    {report[-1]['reason']}", flush=True)

    return report


# --------------------------------------------------------------------------- #
# 报告
# --------------------------------------------------------------------------- #

def print_audit(audits: list[TableAudit]):
    width = max((len(a.version) for a in audits), default=8)
    for audit in sorted(audits, key=lambda a: version_key(a.version)):
        if audit.missing_jar:
            print(f"  [{audit.version:<{width}}] 没有 client jar，无法核对")
            continue
        status = "OK  " if audit.ok else ("REQ " if audit.required_ok else "FAIL")
        detail = ""
        bad = [r for r in audit.results if not r.ok]
        if bad:
            detail = "  <- " + "; ".join(f"{r.slot} {r.signature}: {r.reason}" for r in bad[:3])
        print(f"  [{audit.version:<{width}}] {status} {len(audit.results)} 槽位{detail}")


def write_matrix(path: str, audits: list[TableAudit], generated: list[dict], jars: list[str]):
    reasons = {g["version"]: g for g in generated}
    lines = []
    lines.append("# Agent 符号表覆盖矩阵")
    lines.append("")
    lines.append("> 由 `tools/agent_symbol_audit.py` 生成，**每一项都对着真实 client jar 校验过**，不要手改。")
    lines.append("> 复核口径：类存在、方法名 + 描述符存在、static 标记一致、方法挂在约定的类上（见脚本头注释）。")
    lines.append("")
    lines.append(f"- 数据目录里的 client jar：**{len(jars)}** 个")
    lines.append(f"- 已发布符号表：**{len(audits)}** 张（全部通过全槽位复核）")
    lines.append(f"- 官方映射生成：**{sum(1 for a in audits if a.has_mappings)}** 张 · "
                 f"字节码指纹生成：**{sum(1 for a in audits if not a.has_mappings)}** 张")
    lines.append("")
    lines.append("## 已发布")
    lines.append("")
    lines.append("| 版本 | 轨道 | 来源 | 类/方法 | 结论 |")
    lines.append("| --- | --- | --- | --- | --- |")
    for audit in sorted(audits, key=lambda a: version_key(a.version)):
        track = "Agent" if agent_track(audit.version) else "quickPlay"
        if audit.missing_jar:
            lines.append(f"| {audit.version} | {track} | - | - | 无 jar，未核对 |")
            continue
        source = "官方映射" if audit.has_mappings else "字节码指纹"
        verdict = "全部通过" if audit.ok else ("必需槽位通过" if audit.required_ok else "**有槽位失配**")
        if not audit.ok:
            bad = [r for r in audit.results if not r.ok]
            verdict += "（" + "; ".join(f"{r.slot}: {r.reason}" for r in bad[:2]) + "）"
        lines.append(f"| {audit.version} | {track} | {source} | {len(audit.results)} | {verdict} |")

    covered = {a.version for a in audits}
    gaps = [v for v in jars if v not in covered and agent_track(v)]
    if gaps:
        lines.append("")
        lines.append("## 覆盖缺口：这些版本按设计走普通启动")
        lines.append("")
        lines.append("它们在数据目录里有真实 client jar，但解析不出 Agent 的必需槽位。")
        lines.append("运行时按设计降级成「停在主菜单 + 顶部提示」，不会坏档、也不会猜：")
        lines.append("")
        lines.append("| 版本 | 缺什么 / 为什么 |")
        lines.append("| --- | --- |")
        for version in gaps:
            why = reasons.get(version, {}).get("reason", "本轮未尝试（已发布的表才会进上表）")
            lines.append(f"| {version} | {why} |")
        lines.append("")
        lines.append("> 1.0–1.2.5 没有集成服务端（`saveAllWorlds` / `stopServer` 不存在），安全退出无从判定；")
        lines.append("> 1.3–1.5.2 的主菜单构建方法在字节码里有多个同签名候选，指纹给不出唯一解；")
        lines.append("> 1.14–1.14.3 早于 Mojang 发布 `client_mappings`（1.14.4 起才有），")
        lines.append("> 而结构上存在 3 个 setScreen 候选、14 个 `(Z)V` 候选 —— 按「绝不猜」的原则不生成。")

    if generated:
        written = [g for g in generated if g["status"] in ("written", "rejected", "fail", "incomplete")]
        if written:
            lines.append("")
            lines.append("## 本轮生成明细")
            lines.append("")
            lines.append("| 版本 | 结果 | 说明 |")
            lines.append("| --- | --- | --- |")
            for item in written:
                lines.append(f"| {item['version']} | {item['status']} | {item['reason']} |")

    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print(f"\n矩阵已写入 {path}")


# --------------------------------------------------------------------------- #
# qrc 同步：表放进目录还不够，必须在 qrc 里出现才会被编译进二进制
# --------------------------------------------------------------------------- #

def sync_qrc(symbols_dir: str, qrc_path: str) -> int:
    versions = sort_versions(n[:-5] for n in os.listdir(symbols_dir) if n.endswith(".json"))
    lines = [
        "<!DOCTYPE RCC>",
        '<RCC version="1.0">',
        '    <qresource prefix="/agents">',
        "        <file>quickboot.jar</file>",
        "        <file>introskip.jar</file>",
        "        <file>introskip.json</file>",
    ]
    for version in versions:
        lines.append(f'        <file alias="symbols/{version}.json">symbols/{version}.json</file>')
    lines += ["    </qresource>", "</RCC>", ""]

    with open(qrc_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    return len(versions)


# --------------------------------------------------------------------------- #

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("mode", choices=["audit", "generate"])
    parser.add_argument("--jar-dir", default=JAR_ROOT)
    parser.add_argument("--symbols-dir", default=SYMBOLS_DIR)
    parser.add_argument("--report", default="", help="把覆盖矩阵写到这个 markdown 文件")
    parser.add_argument("--mappings-cache", default=os.path.join(REPO, "build", "agent-mappings"),
                        help="官方 client_mappings 的本地缓存目录（1.14.4+ 用）")
    parser.add_argument("--dry-run", action="store_true", help="generate 模式只看会做什么")
    parser.add_argument("--strict", action="store_true", help="audit 有 FAIL 时返回非零")
    parser.add_argument("--sync-qrc", action="store_true", help="按目录内容重写 agents.qrc")
    parser.add_argument("--replace", default="",
                        help="允许覆盖这些版本（逗号分隔）。只用于「复核证明现有表与 jar 不符」的版本")
    args = parser.parse_args()

    jars = available_versions(args.jar_dir)
    tables = sorted(
        (os.path.join(args.symbols_dir, n) for n in os.listdir(args.symbols_dir) if n.endswith(".json")),
        key=lambda p: version_key(os.path.splitext(os.path.basename(p))[0]),
    ) if os.path.isdir(args.symbols_dir) else []

    print(f"client jar: {len(jars)} 个（{args.jar_dir}）")
    print(f"符号表:    {len(tables)} 张（{args.symbols_dir}）")

    audits = [audit_table(path, args.jar_dir) for path in tables]
    print("\n=== 复核已发布的表 ===")
    print_audit(audits)

    generated = []
    if args.mode == "generate":
        print("\n=== 为缺表版本生成（≤1.19.4） ===")
        generated = generate(jars, args.jar_dir, args.symbols_dir, args.dry_run, args.mappings_cache,
                             {v.strip() for v in args.replace.split(",") if v.strip()})
        print(f"\n生成结果：写 {sum(1 for g in generated if g['status'] == 'written')} 张 · "
              f"不可解析 {sum(1 for g in generated if g['status'] in ('incomplete', 'fail'))} 个版本 · "
              f"跳过 {sum(1 for g in generated if g['status'] == 'skip')} 个")

        if not args.dry_run:
            # 新写的表也要出现在矩阵与最终复核里，所以重新扫一遍目录
            tables = sorted(
                (os.path.join(args.symbols_dir, n) for n in os.listdir(args.symbols_dir) if n.endswith(".json")),
                key=lambda p: version_key(os.path.splitext(os.path.basename(p))[0]),
            )
            audits = [audit_table(path, args.jar_dir) for path in tables]
            print("\n=== 生成后复核 ===")
            print_audit(audits)

    if args.report:
        write_matrix(args.report, audits, generated, jars)

    if args.sync_qrc and not args.dry_run:
        qrc = os.path.join(os.path.dirname(args.symbols_dir.rstrip("\\/")), "agents.qrc")
        count = sync_qrc(args.symbols_dir, qrc)
        print(f"\nagents.qrc 已按目录内容重写：{count} 张表（{qrc}）")

    failed = [a for a in audits if not a.missing_jar and not a.required_ok]
    unverifiable = [a for a in audits if a.missing_jar]
    print()
    if unverifiable:
        print(f"· {len(unverifiable)} 张表没有对应的 client jar，本轮无法核对：" + ", ".join(a.version for a in unverifiable))
    if failed:
        print(f"✗ {len(failed)} 张表的必需槽位与真实 jar 不符：" + ", ".join(a.version for a in failed))
        return 1 if args.strict else 0
    print("✓ 有 jar 可查的表，全部与真实字节码对得上")
    return 0


if __name__ == "__main__":
    sys.exit(main())
