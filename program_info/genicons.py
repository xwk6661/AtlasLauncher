#!/usr/bin/env python3
"""Generate the binary icon assets from the master artwork.

Primary source (2026-11-26 晚起):
    org.atlas.Atlas.svg   512 viewBox 的矢量原稿；无头浏览器渲染 1024 后
                          **自动裁边 + 留 6% 并写回** org.atlas.Atlas.png（qrc 用的是这份位图）

Fallback:
    org.atlas.Atlas.png   只有位图、没有矢量时才直接用（必须已带透明边距）

Outputs (all committed, so a normal build never needs this script):
    atlas.ico                 Windows multi-size icon (embedded by atlas.rc)
    atlas.icns                macOS icon bundle (from the padded Big Sur artwork)
    org.atlas.Atlas_256.png   store / README 256 px raster
    org.atlas.Atlas.icon.png  窗口 / 关于页 / 快捷方式用的 256：裁掉阴影、按实心边界铺满，
                              16–32 px 下比母图醒目（母图留白 6% + 阴影，缩小后只剩一半）

Requirements: Pillow. The SVG fallback additionally needs a Chromium-based browser
(Edge or Chrome); override it with ATLAS_CHROMIUM=/path/to/msedge.exe.

为什么改成位图母图：新图标是 3D 渲染稿，没有等价的矢量版本；
把 2048 的原图去背、裁到内容、四周留 6% 后存成 1024 RGBA，
比"把 PNG 塞进 SVG 再让 QtSvg 去渲染"可靠（QtSvg 不保证支持 data URI 内嵌位图）。
"""

from __future__ import annotations

import os
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
APP_ID = "org.atlas.Atlas"
MASTER = HERE / f"{APP_ID}.png"
SVG = HERE / f"{APP_ID}.svg"

ICO_SIZES = [16, 24, 32, 48, 64, 128, 256]
# ICNS type codes -> pixel size (all PNG payloads, valid since 10.7)
ICNS_SLOTS = [
    (b"icp4", 16),
    (b"icp5", 32),
    (b"icp6", 64),
    (b"ic07", 128),
    (b"ic08", 256),
    (b"ic09", 512),
    (b"ic10", 1024),
]

BROWSER_CANDIDATES = [
    r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Google\Chrome\Application\chrome.exe",
    r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe",
    "/usr/bin/chromium",
    "/usr/bin/google-chrome",
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
]


def find_browser() -> str:
    env = os.environ.get("ATLAS_CHROMIUM")
    if env and Path(env).exists():
        return env
    for candidate in BROWSER_CANDIDATES:
        if Path(candidate).exists():
            return candidate
    sys.exit("error: no Chromium-based browser found; set ATLAS_CHROMIUM=<exe>")


def scalable_copy(source: Path, target: Path) -> None:
    """Strip the fixed width/height so the SVG fills the viewport."""
    text = source.read_text(encoding="utf-8")
    text = re.sub(r'(<svg[^>]*?)\swidth="\d+"\s+height="\d+"', r"\1", text, count=1)
    target.write_text(text, encoding="utf-8")


def render_master_svg(browser: str, svg: Path, size: int, workdir: Path) -> Image.Image:
    """Render once at the largest size; every icon size is a LANCZOS downscale.

    逐尺寸截图不可靠：Chromium 在 Windows 上对小窗口有最小宽度，
    `--window-size=256` 实际会按更宽的视口排版再裁切（128 以下反而正常），
    结果就是 256 的右缘被切掉。渲染一张 1024 再做降采样没有这个问题。
    """
    scaled = workdir / f"render-{svg.stem}.svg"
    scalable_copy(svg, scaled)
    out_png = workdir / f"master-{svg.stem}-{size}.png"
    subprocess.run(
        [
            browser,
            "--headless=new",
            "--disable-gpu",
            "--hide-scrollbars",
            "--force-device-scale-factor=1",
            "--default-background-color=00000000",
            f"--window-size={size},{size}",
            f"--screenshot={out_png.resolve()}",
            scaled.resolve().as_uri(),
        ],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    image = Image.open(out_png).convert("RGBA")
    if image.size != (size, size):
        image = image.resize((size, size), Image.LANCZOS)
    return image


def render(master: Image.Image, size: int) -> Image.Image:
    if master.size == (size, size):
        return master.copy()
    return master.resize((size, size), Image.LANCZOS)


def check_edges(image: Image.Image) -> None:
    """四周留白；外两圈不透明说明渲染被裁切了（或母图没留白）。"""
    width, height = image.size
    for y in range(height):
        for x in (0, 1, width - 2, width - 1):
            if image.getpixel((x, y))[3] != 0:
                sys.exit(f"error: render is clipped at x={x}, y={y}; the artwork must keep a transparent margin")
    for x in range(width):
        for y in (0, 1, height - 2, height - 1):
            if image.getpixel((x, y))[3] != 0:
                sys.exit(f"error: render is clipped at x={x}, y={y}; the artwork must keep a transparent margin")


def write_icns(pngs: dict[int, bytes], out_path: Path) -> None:
    body = b"".join(code + struct.pack(">I", len(data) + 8) + data for code, size in ICNS_SLOTS for data in [pngs[size]])
    out_path.write_bytes(b"icns" + struct.pack(">I", len(body) + 8) + body)


def dib_bytes(image: Image.Image) -> bytes:
    """32-bit BI_RGB DIB + AND mask, the form every Windows version understands."""
    rgba = image.convert("RGBA")
    width, height = rgba.size
    pixels = rgba.load()
    xor = bytearray()
    for y in range(height - 1, -1, -1):
        for x in range(width):
            r, g, b, a = pixels[x, y]
            xor += bytes((b, g, r, a))
    stride = ((width + 31) // 32) * 4
    mask = bytearray()
    for y in range(height - 1, -1, -1):
        row = bytearray(stride)
        for x in range(width):
            if pixels[x, y][3] == 0:
                row[x // 8] |= 0x80 >> (x % 8)
        mask += row
    header = struct.pack("<IiiHHIIiiII", 40, width, height * 2, 1, 32, 0, len(xor) + len(mask), 0, 0, 0, 0)
    return header + bytes(xor) + bytes(mask)


def write_ico(images: dict[int, Image.Image], pngs: dict[int, bytes], out_path: Path) -> None:
    """BMP entries below 256 px (maximum compatibility), PNG for 256."""
    payloads = []
    for size in ICO_SIZES:
        if size >= 256:
            payloads.append((size, pngs[size]))
        else:
            payloads.append((size, dib_bytes(images[size])))

    header = struct.pack("<HHH", 0, 1, len(payloads))
    offset = 6 + 16 * len(payloads)
    entries = bytearray()
    blobs = bytearray()
    for size, data in payloads:
        entries += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(data), offset)
        blobs += data
        offset += len(data)
    out_path.write_bytes(header + bytes(entries) + bytes(blobs))


def bigsur_of(master: Image.Image) -> Image.Image:
    """1024 canvas with the artwork at 80% — macOS icons leave a margin for the grid."""
    canvas = Image.new("RGBA", (1024, 1024), (0, 0, 0, 0))
    inner = int(1024 * 0.8)
    art = master.resize((inner, inner), Image.LANCZOS)
    offset = (1024 - inner) // 2
    canvas.paste(art, (offset, offset), art)
    return canvas


def normalize(image: Image.Image, pad_ratio: float = 0.06) -> Image.Image:
    """裁到不透明内容，四周补 6% 透明边，再降到 1024。

    矢量稿的阴影会贴到画布边缘（滤镜区域是 -15%~130%），所以不能直接当母图：
    既过不了 check_edges，缩小后阴影也会被切。裁边 + 补边一次解决。
    """
    bbox = image.getchannel("A").point(lambda v: 255 if v > 8 else 0).getbbox()
    if bbox:
        image = image.crop(bbox)
    side = max(image.size)
    pad = int(side * pad_ratio)
    canvas = Image.new("RGBA", (side + 2 * pad, side + 2 * pad), (0, 0, 0, 0))
    canvas.paste(image, ((canvas.width - image.width) // 2, (canvas.height - image.height) // 2), image)
    return canvas.resize((1024, 1024), Image.LANCZOS)


def tight(image: Image.Image, margin: float = 0.02) -> Image.Image:
    """裁到实心内容（alpha>200），阴影不要，四周只留 2%。

    小尺寸下阴影只会糊成一团深色，白占 10% 的像素；窗口图标 16 px 想要"看着大"，
    就得让瓦片自己铺满画布。
    """
    bbox = image.getchannel("A").point(lambda v: 255 if v > 200 else 0).getbbox()
    if bbox:
        image = image.crop(bbox)
    side = max(image.size)
    pad = int(side * margin)
    canvas = Image.new("RGBA", (side + 2 * pad, side + 2 * pad), (0, 0, 0, 0))
    canvas.paste(image, ((canvas.width - image.width) // 2, (canvas.height - image.height) // 2), image)
    return canvas.resize((1024, 1024), Image.LANCZOS)


def load_master() -> tuple[Image.Image, str, tempfile.TemporaryDirectory | None]:
    """返回 (1024 母图, 来源说明, 临时目录)。

    **矢量优先**：有 `org.atlas.Atlas.svg` 就以它为准，渲染后回写 PNG 母图 ——
    qrc 引用的是 PNG，两者不能各说各话。
    """
    if SVG.exists():
        browser = find_browser()
        workdir = tempfile.TemporaryDirectory(prefix="atlas-icons-")
        image = normalize(render_master_svg(browser, SVG, 1024, Path(workdir.name)))
        image.save(MASTER)
        return image, "org.atlas.Atlas.svg（已回写 PNG）", workdir
    if MASTER.exists():
        image = Image.open(MASTER).convert("RGBA")
        if image.size != (1024, 1024):
            image = image.resize((1024, 1024), Image.LANCZOS)
        return image, "org.atlas.Atlas.png", None
    sys.exit(f"error: neither {SVG.name} nor {MASTER.name} exists")


def main() -> None:
    master, source, workdir = load_master()
    try:
        check_edges(master)

        scratch = tempfile.TemporaryDirectory(prefix="atlas-raster-")
        compact = tight(master)
        compact.resize((256, 256), Image.LANCZOS).save(HERE / f"{APP_ID}.icon.png")
        pngs: dict[int, bytes] = {}
        images: dict[int, Image.Image] = {}
        for size in ICO_SIZES:
            # 16–48 px 用"铺满"版本：阴影在小尺寸上只是脏点，留白又让图标显小
            image = render(compact if size <= 48 else master, size)
            png = Path(scratch.name) / f"atlas_{size}.png"
            image.save(png)
            pngs[size] = png.read_bytes()
            images[size] = image
            if size == 256:
                image.save(HERE / f"{APP_ID}_256.png")
        write_ico(images, pngs, HERE / "atlas.ico")

        bigsur = bigsur_of(master)
        icns_pngs: dict[int, bytes] = {}
        for _, size in ICNS_SLOTS:
            image = render(bigsur, size)
            png = Path(scratch.name) / f"bigsur_{size}.png"
            image.save(png)
            icns_pngs[size] = png.read_bytes()
        write_icns(icns_pngs, HERE / "atlas.icns")

        print(f"ok: atlas.ico ({len(pngs)} sizes), atlas.icns ({len(icns_pngs)} slots), "
              f"{APP_ID}_256.png — from {source}")
    finally:
        if workdir is not None:
            workdir.cleanup()
        if 'scratch' in locals():
            scratch.cleanup()


if __name__ == "__main__":
    main()
