#!/usr/bin/env python3
"""提取 maps/ 里所有 tr() / translate() 的**运行期**源串。

为什么不用 lupdate：本机 Qt 6.8 的 lupdate.exe 缺 Qt6Qml.dll（exit 0xC0000135），
而 lrelease 正常。界面文案的提取规则很简单，自己解析比装一个 QML 运行时省事。

要点：
  · 相邻字符串字面量会拼接（`tr("a\n" "b")` 在运行期是 "a\nb"），必须合并；
  · C 转义要还原成真实字符（`\n` → 换行），词表按运行期源串查；
  · 只取 `tr(...)` 与 `QCoreApplication::translate("ctx", "...")` 的第一个参数；
  · 去掉纯 ASCII 串（不需要翻译），保留含中文的。
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]  # repo root
MAPS = ROOT / "launcher" / "maps"

ESCAPES = {
    "n": "\n",
    "t": "\t",
    "r": "\r",
    "0": "\0",
    "\\": "\\",
    '"': '"',
    "'": "'",
}


def decode_literal(body: str) -> str:
    out = []
    i = 0
    while i < len(body):
        ch = body[i]
        if ch != "\\":
            out.append(ch)
            i += 1
            continue
        i += 1
        if i >= len(body):
            break
        code = body[i]
        if code in ESCAPES:
            out.append(ESCAPES[code])
            i += 1
        elif code == "x":
            m = re.match(r"[0-9a-fA-F]{1,2}", body[i + 1:])
            if m:
                out.append(chr(int(m.group(0), 16)))
                i += 1 + len(m.group(0))
            else:
                out.append("x")
                i += 1
        elif code == "u":
            m = re.match(r"[0-9a-fA-F]{4}", body[i + 1:])
            if m:
                out.append(chr(int(m.group(0), 16)))
                i += 1 + len(m.group(0))
            else:
                out.append("u")
                i += 1
        else:
            out.append(code)
            i += 1
    return "".join(out)


def parse_call_args(text: str, start: int):
    """从 `start`（左括号后第一个字符）开始，返回 (参数字符串列表, 右括号下标)。"""
    args, current, depth, in_str, i = [], [], 0, False, start
    while i < len(text):
        ch = text[i]
        if in_str:
            current.append(ch)
            if ch == "\\" and i + 1 < len(text):
                current.append(text[i + 1])
                i += 2
                continue
            if ch == '"':
                in_str = False
            i += 1
            continue
        if ch == '"':
            in_str = True
            current.append(ch)
            i += 1
            continue
        if ch in "([{":
            depth += 1
            current.append(ch)
            i += 1
            continue
        if ch in ")]}":
            if depth == 0:
                args.append("".join(current).strip())
                return args, i
            depth -= 1
            current.append(ch)
            i += 1
            continue
        if ch == "," and depth == 0:
            args.append("".join(current).strip())
            current = []
            i += 1
            continue
        current.append(ch)
        i += 1
    return None, -1


LITERAL = re.compile(r"^((?:\s*\"(?:[^\"\\]|\\.)*\")+)")


def first_literal(arg: str):
    m = LITERAL.match(arg.strip())
    if not m:
        return None
    parts = re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1))
    return decode_literal("".join(parts))


def main():
    strings = []
    seen = set()
    files = sorted(MAPS.glob("*.cpp")) + sorted(MAPS.glob("*.h"))
    for path in files:
        text = path.read_text(encoding="utf-8")
        for m in re.finditer(r"\btr\s*\(", text):
            args, _ = parse_call_args(text, m.end())
            if not args:
                continue
            value = first_literal(args[0])
            if value and re.search(r"[\u4e00-\u9fff]", value) and value not in seen:
                seen.add(value)
                strings.append(value)
        # maps 里的静态函数用 uiText() 包一层 QCoreApplication::translate("maps", …)
        for m in re.finditer(r"\buiText\s*\(", text):
            args, _ = parse_call_args(text, m.end())
            if not args:
                continue
            value = first_literal(args[0])
            if value and re.search(r"[\u4e00-\u9fff]", value) and value not in seen:
                seen.add(value)
                strings.append(value)
        for m in re.finditer(r"translate\s*\(", text):
            args, _ = parse_call_args(text, m.end())
            if not args or len(args) < 2:
                continue
            value = first_literal(args[1])
            if value and re.search(r"[\u4e00-\u9fff]", value) and value not in seen:
                seen.add(value)
                strings.append(value)

    out_dir = ROOT / ".tmp-i18n"
    out_dir.mkdir(exist_ok=True)
    out_json = out_dir / "strings.json"
    out_json.write_text(json.dumps(strings, ensure_ascii=False, indent=1), encoding="utf-8")

    # 带索引的可读清单：换行显示成 ⏎，方便逐条翻译
    lines = []
    for idx, value in enumerate(strings, start=1):
        one_line = value.replace("\n", "\\n")
        lines.append(f"{idx:04d}\t{one_line}")
    (out_dir / "strings.txt").write_text("\n".join(lines), encoding="utf-8")
    print(f"{len(strings)} unique strings -> .tmp-i18n/strings.json")


if __name__ == "__main__":
    sys.exit(main())
