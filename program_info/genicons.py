#!/usr/bin/env python3
"""Generate the binary icon assets from `org.atlas.Atlas.svg`.

Outputs (all committed, so a normal build never needs this script):
    atlas.ico                 Windows multi-size icon (embedded by atlas.rc)
    atlas.icns                macOS icon bundle (from the padded Big Sur artwork)
    org.atlas.Atlas_256.png   store / README 256 px raster
    org.atlas.Atlas.bigsur.svg  1024 canvas, tile at 80% (macOS grid)

Requirements: a Chromium-based browser (Edge or Chrome) for SVG rasterising and
Pillow for assembling the containers. Override the browser with
ATLAS_CHROMIUM=/path/to/msedge.exe.

The upstream `genicons.sh` uses inkscape + icotool + oxipng; none of those are
installed on the author's Windows box, so this script renders the SVG in a
headless browser instead. Output is visually equivalent at icon sizes; if you do
have inkscape, either path is fine.
"""

from __future__ import annotations

import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
APP_ID = "org.atlas.Atlas"
SVG = HERE / f"{APP_ID}.svg"
BIGSUR = HERE / f"{APP_ID}.bigsur.svg"

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


def render_master(browser: str, svg: Path, size: int, workdir: Path) -> Image.Image:
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
    """瓦片四周留了约 4.7% 的透明边；外两圈不透明说明渲染被裁切了。"""
    width, height = image.size
    for y in range(height):
        for x in (0, 1, width - 2, width - 1):
            if image.getpixel((x, y))[3] != 0:
                sys.exit(f"error: render is clipped at x={x}, y={y}; the SVG did not fill the viewport")
    for x in range(width):
        for y in (0, 1, height - 2, height - 1):
            if image.getpixel((x, y))[3] != 0:
                sys.exit(f"error: render is clipped at x={x}, y={y}; the SVG did not fill the viewport")


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


def write_bigsur(source: Path) -> None:
    """1024 canvas with the tile at 80% — macOS icons leave a margin for the grid."""
    text = source.read_text(encoding="utf-8")
    defs_start = text.index("<defs>")
    defs_end = text.index("</defs>") + len("</defs>")
    defs = text[defs_start:defs_end]
    body = text[defs_end : text.rindex("</svg>")]
    scale = 1024 * 0.8 / 512
    offset = (1024 - 512 * scale) / 2
    out = (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1024 1024" width="1024" height="1024" '
        'role="img" aria-label="Atlas">\n'
        "  <title>Atlas</title>\n"
        f"  {defs}\n"
        f'  <g transform="translate({offset:.2f} {offset:.2f}) scale({scale:.4f})">{body}</g>\n'
        "</svg>\n"
    )
    BIGSUR.write_text(out, encoding="utf-8")


def main() -> None:
    if not SVG.exists():
        sys.exit(f"error: {SVG} not found")
    browser = find_browser()
    workdir = Path(tempfile.mkdtemp(prefix="atlas-icons-"))
    try:
        master = render_master(browser, SVG, 1024, workdir)
        check_edges(master)

        pngs: dict[int, bytes] = {}
        images: dict[int, Image.Image] = {}
        for size in ICO_SIZES:
            image = render(master, size)
            png = workdir / f"atlas_{size}.png"
            image.save(png)
            pngs[size] = png.read_bytes()
            images[size] = image
            if size == 256:
                image.save(HERE / f"{APP_ID}_256.png")
        write_ico(images, pngs, HERE / "atlas.ico")

        write_bigsur(SVG)
        bigsur = render_master(browser, BIGSUR, 1024, workdir)
        icns_pngs: dict[int, bytes] = {}
        for _, size in ICNS_SLOTS:
            image = render(bigsur, size)
            png = workdir / f"bigsur_{size}.png"
            image.save(png)
            icns_pngs[size] = png.read_bytes()
        write_icns(icns_pngs, HERE / "atlas.icns")

        # README / 关于页用的 logo：与应用图标同一张图，深浅主题都能看
        shutil.copyfile(SVG, HERE / f"{APP_ID}.logo.svg")
        shutil.copyfile(SVG, HERE / f"{APP_ID}.logo-darkmode.svg")
        shutil.copyfile(SVG, HERE / f"{APP_ID}.Source.svg")
        shutil.copyfile(SVG, HERE / f"{APP_ID}.logo.source.svg")
        shutil.copyfile(SVG, HERE / "atlas-monochrome.Source.svg")

        theme_icon = HERE.parent / "launcher" / "resources" / "multimc" / "scalable" / "launcher.svg"
        if theme_icon.parent.exists():
            shutil.copyfile(SVG, theme_icon)

        print(f"ok: atlas.ico ({len(pngs)} sizes), atlas.icns ({len(icns_pngs)} slots), "
              f"{APP_ID}_256.png, {APP_ID}.bigsur.svg")
    finally:
        shutil.rmtree(workdir, ignore_errors=True)


if __name__ == "__main__":
    main()
