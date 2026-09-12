#!/usr/bin/env python3
"""Rebuild the Emery bitmap BDF strikes from the vendored native sources."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

HERE = Path(__file__).resolve().parent


@dataclass
class Glyph:
    codepoint: int
    advance: int
    width: int
    height: int
    xoff: int
    yoff: int
    rows: list[int]


def scale_glyph(glyph: Glyph, scale: int) -> Glyph:
    width = glyph.width * scale
    rows = []
    for row in glyph.rows:
        expanded = 0
        for x in range(glyph.width):
            if row & (1 << (glyph.width - 1 - x)):
                for sx in range(scale):
                    expanded |= 1 << (width - 1 - (x * scale + sx))
        rows.extend([expanded] * scale)
    return Glyph(
        glyph.codepoint,
        glyph.advance * scale,
        width,
        glyph.height * scale,
        glyph.xoff * scale,
        glyph.yoff * scale,
        rows,
    )


def bitmap_hex(row: int, width: int) -> str:
    byte_width = (width + 7) // 8
    return f"{row << (byte_width * 8 - width):0{byte_width * 2}X}"


def write_bdf(
    path: Path,
    family: str,
    size: int,
    ascent: int,
    descent: int,
    glyphs: list[Glyph],
    comments: list[str],
    monospaced: bool = False,
) -> None:
    min_x = min((g.xoff for g in glyphs), default=0)
    max_x = max((g.xoff + g.width for g in glyphs), default=0)
    spacing = "M" if monospaced else "P"
    out = [
        "STARTFONT 2.1",
        f"FONT -pebble-{family.lower()}-medium-r-normal--{size}-{size * 10}-75-75-{spacing}-0-iso10646-1",
        f"SIZE {size} 75 75",
        f"FONTBOUNDINGBOX {max_x - min_x} {ascent + descent} {min_x} {-descent}",
        *(f'COMMENT "{comment}"' for comment in comments),
        "STARTPROPERTIES 8",
        f'FAMILY_NAME "{family}"',
        'WEIGHT_NAME "Medium"',
        'SLANT "R"',
        f"PIXEL_SIZE {size}",
        f"FONT_ASCENT {ascent}",
        f"FONT_DESCENT {descent}",
        f'SPACING "{spacing}"',
        'CHARSET_REGISTRY "ISO10646"',
        "ENDPROPERTIES",
        f"CHARS {len(glyphs)}",
    ]
    for glyph in sorted(glyphs, key=lambda item: item.codepoint):
        out += [
            f"STARTCHAR uni{glyph.codepoint:04X}",
            f"ENCODING {glyph.codepoint}",
            f"SWIDTH {round(glyph.advance * 1000 / size)} 0",
            f"DWIDTH {glyph.advance} 0",
            f"BBX {glyph.width} {glyph.height} {glyph.xoff} {glyph.yoff}",
            "BITMAP",
            *(bitmap_hex(row, glyph.width) for row in glyph.rows),
            "ENDCHAR",
        ]
    out.append("ENDFONT")
    path.write_text("\n".join(out) + "\n", encoding="ascii")


def gallery_glyphs(source: Path, top: int, baseline: int) -> list[Glyph]:
    data = json.loads(source.read_text(encoding="ascii"))
    glyphs = []
    for codepoint_text, source_rows in data.items():
        codepoint = int(codepoint_text)
        points = {
            (x, y - top)
            for y, row in enumerate(source_rows)
            for x in range(16)
            if row & (1 << x)
        }
        if not points:
            continue
        max_source_x = max(x for x, _ in points)
        # Retain the x-2 bearing, with one blank pixel after the rightmost ink.
        advance = max_source_x
        min_x, max_x = min(x for x, _ in points), max(x for x, _ in points)
        min_y, max_y = min(y for _, y in points), max(y for _, y in points)
        width, height = max_x - min_x + 1, max_y - min_y + 1
        rows = []
        for y in range(min_y, max_y + 1):
            row = 0
            for x in range(min_x, max_x + 1):
                if (x, y) in points:
                    row |= 1 << (width - 1 - (x - min_x))
            rows.append(row)
        # drawTest paints source x at x-2. BDF yoff is relative to the baseline.
        glyphs.append(
            Glyph(
                codepoint, advance, width, height, min_x - 2, baseline - 1 - max_y, rows
            )
        )

    # BitFontMaker has no space bitmap: its drawTest default word spacing is 5 pixels.
    glyphs.append(Glyph(0x20, 5, 1, 1, 0, 0, [0]))
    if not any(g.codepoint == 0x2026 for g in glyphs):
        dot = next(g for g in glyphs if g.codepoint == ord("."))
        width = dot.width + dot.advance * 2
        rows = [
            row | (row << dot.advance) | (row << (dot.advance * 2)) for row in dot.rows
        ]
        glyphs.append(
            Glyph(0x2026, dot.advance * 3, width, dot.height, dot.xoff, dot.yoff, rows)
        )
    return glyphs


def parse_cozette() -> list[Glyph]:
    lines = (HERE / "cozette-1.30.0.bdf").read_text(encoding="ascii").splitlines()
    glyphs = []
    index = 0
    while index < len(lines):
        if not lines[index].startswith("STARTCHAR "):
            index += 1
            continue
        end = lines.index("ENDCHAR", index)
        block = lines[index:end]
        codepoint = int(
            next(line.split()[1] for line in block if line.startswith("ENCODING "))
        )
        if codepoint == 0x2026 or 0x20 <= codepoint <= 0xFF:
            advance = int(
                next(line.split()[1] for line in block if line.startswith("DWIDTH "))
            )
            width, height, xoff, yoff = map(
                int, next(line.split()[1:] for line in block if line.startswith("BBX "))
            )
            bitmap = block.index("BITMAP")
            byte_width = (width + 7) // 8
            rows = [
                int(line, 16) >> (byte_width * 8 - width)
                for line in block[bitmap + 1 : bitmap + 1 + height]
            ]
            glyphs.append(Glyph(codepoint, advance, width, height, xoff, yoff, rows))
        index = end + 1
    return glyphs


def main() -> None:
    gallery = [
        (
            "chikarego",
            "ChiKareGo2",
            16,
            0,
            12,
            4,
            [
                "Giles Booth; BitFontMaker2 gallery 3780",
                "Creative Commons Attribution (version unspecified by source)",
            ],
        ),
        (
            "pixelva",
            "Pixelva",
            12,
            3,
            9,
            3,
            ["HomeStarRunnerTron; BitFontMaker2 gallery 5892", "Public Domain"],
        ),
    ]
    for stem, family, native_size, top, ascent, descent, comments in gallery:
        native = gallery_glyphs(HERE / f"{stem}.json", top, ascent)
        for scale in (1, 2, 3):
            write_bdf(
                HERE / f"{stem}-{native_size * scale}.bdf",
                family,
                native_size * scale,
                ascent * scale,
                descent * scale,
                [scale_glyph(glyph, scale) for glyph in native],
                comments + ["U+2026 derived by repeating U+002E"],
            )

    cozette = parse_cozette()
    for scale in (1, 2, 3):
        write_bdf(
            HERE / f"cozette-{13 * scale}.bdf",
            "Cozette",
            13 * scale,
            10 * scale,
            3 * scale,
            [scale_glyph(glyph, scale) for glyph in cozette],
            ["Cozette v.1.30.0 by Ines", "MIT License"],
            monospaced=True,
        )


if __name__ == "__main__":
    main()
