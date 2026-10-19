#!/usr/bin/env python3
"""
Build components/ui/fonts/lv_font_montserrat_12_eu.c as 1-bpp Terminus 16n.

Montserrat is a vector face (soft on 1-bit OLED). Spleen 8x16 is a pixel
face but its 2-px stems read as bold. Terminus 16 Regular (ter-u16n) is
the same 8x16 cell with 1-px stems, plus Latin-1 / Latin Extended-A.

The C symbol stays lv_font_montserrat_12_eu so UI_PROFILE_MONO_128X64
aliases do not change.

Terminus Font 4.49.1, SIL OFL 1.1, https://terminus-font.sourceforge.net/
"""

from __future__ import annotations

import sys
import urllib.request
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
FONT_DIR = Path(__file__).resolve().parent / "fonts"
OUT = REPO / "components" / "ui" / "fonts" / "lv_font_montserrat_12_eu.c"

BDF_NAME = "ter-u16n.bdf"
BDF_URL = (
    "https://raw.githubusercontent.com/mikebeaton/terminus-font-4.49.1/"
    "master/ter-u16n.bdf"
)

RANGES = (
    (0x20, 0x7E),
    (0xA0, 0x17F),
    (0x218, 0x21B),
)


def ensure_bdf() -> Path:
    FONT_DIR.mkdir(parents=True, exist_ok=True)
    path = FONT_DIR / BDF_NAME
    if path.exists() and path.stat().st_size > 10000:
        return path
    print(f"downloading {BDF_URL}")
    urllib.request.urlretrieve(BDF_URL, path)
    return path


def pack_bpp1(bits: list[int], w: int, h: int) -> bytes:
    out = bytearray()
    acc = 0
    nbit = 0
    for y in range(h):
        row = bits[y * w : (y + 1) * w]
        for bit in row:
            acc = (acc << 1) | (1 if bit else 0)
            nbit += 1
            if nbit == 8:
                out.append(acc)
                acc = 0
                nbit = 0
    if nbit:
        acc <<= 8 - nbit
        out.append(acc)
    return bytes(out)


def bdf_bitmap_to_bpp1(hex_rows: list[str], box_w: int, box_h: int) -> bytes:
    bits: list[int] = []
    for y in range(box_h):
        h = hex_rows[y] if y < len(hex_rows) else ""
        h = h.strip()
        if not h:
            bits.extend([0] * box_w)
            continue
        if len(h) % 2:
            h += "0"
        nbits = len(h) * 4
        val = int(h, 16)
        for x in range(box_w):
            bit = (val >> (nbits - 1 - x)) & 1 if x < nbits else 0
            bits.append(bit)
    return pack_bpp1(bits, box_w, box_h)


def parse_bdf(path: Path) -> tuple[dict[int, tuple[int, tuple[int, int, int, int], bytes]], int, int]:
    glyphs: dict[int, tuple[int, tuple[int, int, int, int], bytes]] = {}
    ascent = 12
    descent = 4
    enc = None
    dwidth = 8
    bbx = (8, 16, 0, -4)
    rows: list[str] = []
    in_bitmap = False

    for raw in path.read_text(encoding="latin-1").splitlines():
        line = raw.strip()
        if line.startswith("FONT_ASCENT"):
            ascent = int(line.split()[1])
            continue
        if line.startswith("FONT_DESCENT"):
            descent = int(line.split()[1])
            continue
        if line.startswith("STARTCHAR"):
            enc = None
            dwidth = 8
            bbx = (8, 16, 0, -4)
            rows = []
            in_bitmap = False
            continue
        if line.startswith("ENCODING"):
            enc = int(line.split()[1])
            continue
        if line.startswith("DWIDTH"):
            dwidth = int(line.split()[1])
            continue
        if line.startswith("BBX"):
            p = line.split()
            bbx = (int(p[1]), int(p[2]), int(p[3]), int(p[4]))
            continue
        if line == "BITMAP":
            in_bitmap = True
            rows = []
            continue
        if line == "ENDCHAR":
            if enc is not None and enc >= 0:
                bw, bh, ox, oy = bbx
                bits = bdf_bitmap_to_bpp1(rows, bw, bh) if bw and bh else b""
                glyphs[enc] = (dwidth, bbx, bits)
            in_bitmap = False
            continue
        if in_bitmap:
            rows.append(line)

    return glyphs, ascent, descent


def c_bytes(data: bytes, indent: str = "    ") -> str:
    if not data:
        return ""
    parts = []
    line = indent
    for b in data:
        piece = f"0x{b:x}, "
        if len(line) + len(piece) > 80:
            parts.append(line.rstrip())
            line = indent + piece
        else:
            line += piece
    parts.append(line.rstrip())
    return "\n".join(parts)


def main() -> int:
    bdf = ensure_bdf()
    src, ascent, descent = parse_bdf(bdf)

    codepoints = []
    for a, b in RANGES:
        codepoints.extend(range(a, b + 1))

    glyphs = []
    bitmap = bytearray()
    missing = 0

    for cp in codepoints:
        rec = src.get(cp)
        if rec is None:
            missing += 1
            adv, bw, bh, ox, oy, bits = 8 * 16, 0, 0, 0, 0, b""
        else:
            dwidth, bbx, bits = rec
            bw, bh, ox, oy = bbx
            adv = dwidth * 16
            if ox < -128:
                ox = -128
            if ox > 127:
                ox = 127
            if oy < -128:
                oy = -128
            if oy > 127:
                oy = 127
            if bw > 255:
                bw = 255
            if bh > 255:
                bh = 255

        idx = len(bitmap)
        bitmap.extend(bits)
        glyphs.append((idx, adv, bw, bh, ox, oy))

    line_height = 16
    base_line = descent

    cmaps = [
        (0x20, 0x7E - 0x20 + 1, 1),
        (0xA0, 0x17F - 0xA0 + 1, 1 + (0x7E - 0x20 + 1)),
        (0x218, 4, 1 + (0x7E - 0x20 + 1) + (0x17F - 0xA0 + 1)),
    ]

    lines = []
    w = lines.append
    w("/*******************************************************************************")
    w(" * Size: 16 px")
    w(" * Bpp: 1")
    w(" * Opts: ter-u16n.bdf (Terminus 4.49.1 Regular) --bpp 1 --range 0x20-0x7E")
    w(" *        --range 0x00A0-0x00FF --range 0x0100-0x017F --range 0x0218-0x021B")
    w(" * Generated by scripts/gen_font_12_eu_bpp1.py for the SSD1306 1-bit panel.")
    w(" * Terminus Font is SIL OFL 1.1, (c) Dimitar Toshkov Zhekov.")
    w(" ******************************************************************************/")
    w("")
    w('#ifdef LV_LVGL_H_INCLUDE_SIMPLE')
    w('#include "lvgl.h"')
    w("#else")
    w('#include "lvgl.h"')
    w("#endif")
    w("")
    w("#ifndef LV_FONT_MONTSERRAT_12_EU")
    w("#define LV_FONT_MONTSERRAT_12_EU 1")
    w("#endif")
    w("")
    w("#if LV_FONT_MONTSERRAT_12_EU")
    w("")
    w("/*-----------------")
    w(" *    BITMAPS")
    w(" *----------------*/")
    w("")
    w("/*Store the image of the glyphs*/")
    w("static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {")

    for i, cp in enumerate(codepoints):
        idx, adv, bw, bh, ox, oy = glyphs[i]
        ch = chr(cp)
        if not ch.isprintable():
            label = ""
        elif ch == '"':
            label = '\\"'
        elif ch == '\\':
            label = '\\\\'
        else:
            label = ch
        w(f'    /* U+{cp:04X} "{label}" */')
        start = idx
        end = glyphs[i + 1][0] if i + 1 < len(glyphs) else len(bitmap)
        chunk = bytes(bitmap[start:end])
        if chunk:
            w(c_bytes(chunk))
        w("")

    w("};")
    w("")
    w("")
    w("/*---------------------")
    w(" *  GLYPH DESCRIPTION")
    w(" *--------------------*/")
    w("")
    w("static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {")
    w("    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */")
    for idx, adv, bw, bh, ox, oy in glyphs:
        w(f"    ,{{.bitmap_index = {idx}, .adv_w = {adv}, .box_w = {bw}, .box_h = {bh}, .ofs_x = {ox}, .ofs_y = {oy}}}")
    w("};")
    w("")
    w("/*---------------------")
    w(" *  CHARACTER MAPPING")
    w(" *--------------------*/")
    w("")
    w("static const lv_font_fmt_txt_cmap_t cmaps[] =")
    w("{")
    for i, (start, length, gid) in enumerate(cmaps):
        comma = "," if i + 1 < len(cmaps) else ""
        w("    {")
        w(f"        .range_start = {start}, .range_length = {length}, .glyph_id_start = {gid},")
        w("        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY")
        w(f"    }}{comma}")
    w("};")
    w("")
    w("/*-----------------")
    w(" *    KERNING")
    w(" *----------------*/")
    w("")
    w("#if LVGL_VERSION_MAJOR == 8")
    w("static  lv_font_fmt_txt_glyph_cache_t cache;")
    w("#endif")
    w("")
    w("#if LVGL_VERSION_MAJOR >= 8")
    w("static const lv_font_fmt_txt_dsc_t font_dsc = {")
    w("#else")
    w("static lv_font_fmt_txt_dsc_t font_dsc = {")
    w("#endif")
    w("    .glyph_bitmap = glyph_bitmap,")
    w("    .glyph_dsc = glyph_dsc,")
    w("    .cmaps = cmaps,")
    w("    .kern_dsc = NULL,")
    w("    .kern_scale = 0,")
    w("    .cmap_num = 3,")
    w("    .bpp = 1,")
    w("    .kern_classes = 0,")
    w("    .bitmap_format = 0,")
    w("#if LVGL_VERSION_MAJOR == 8")
    w("    .cache = &cache")
    w("#endif")
    w("};")
    w("")
    w("#if LVGL_VERSION_MAJOR >= 8")
    w("const lv_font_t lv_font_montserrat_12_eu = {")
    w("#else")
    w("lv_font_t lv_font_montserrat_12_eu = {")
    w("#endif")
    w("    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,")
    w("    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,")
    w(f"    .line_height = {line_height},")
    w(f"    .base_line = {base_line},")
    w("#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)")
    w("    .subpx = LV_FONT_SUBPX_NONE,")
    w("#endif")
    w("#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8")
    w("    .underline_position = -1,")
    w("    .underline_thickness = 1,")
    w("#endif")
    w("    .dsc = &font_dsc,")
    w("#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9")
    w("    .fallback = NULL,")
    w("#endif")
    w("    .user_data = NULL,")
    w("};")
    w("")
    w("#endif /*#if LV_FONT_MONTSERRAT_12_EU*/")
    w("")

    text = "\n".join(lines)
    OUT.write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote {OUT} ({OUT.stat().st_size} bytes, {len(bitmap)} bitmap bytes, "
          f"{len(glyphs)} glyphs, {missing} missing, "
          f"line_height={line_height} base_line={base_line})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
