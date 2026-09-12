#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Pebble Technology
# SPDX-License-Identifier: Apache-2.0

"""Append the Emery classic fonts to the Obelix host-test resource pack."""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from pbpack import ResourcePack
from resources.resource_map.resource_generator_font import (
    FontResourceGenerator,
)
from resources.types.resource_definition import ResourceDefinition

PACK = ROOT / "tests/fixtures/resources/system_resources_obelix.pbpack"
VERSION_HEADER = (
    ROOT / "tests/overrides/default/resources/obelix/resource/resource_version.auto.h"
)
FIRST_FONT_ID = 624
FONT_SPECS = (
    ("CHIKAREGO_16", "chikarego-16.bdf"),
    ("CHIKAREGO_32", "chikarego-32.bdf"),
    ("CHIKAREGO_48", "chikarego-48.bdf"),
    ("PIXELVA_12", "pixelva-12.bdf"),
    ("PIXELVA_24", "pixelva-24.bdf"),
    ("PIXELVA_36", "pixelva-36.bdf"),
    ("COZETTE_13", "cozette-13.bdf"),
    ("COZETTE_26", "cozette-26.bdf"),
    ("COZETTE_39", "cozette-39.bdf"),
)


def main():
    with PACK.open("rb") as source:
        old_pack = ResourcePack.deserialize(source, is_system=True)

    pack = ResourcePack(is_system=True)
    for entry in old_pack.table_entries[: FIRST_FONT_ID - 1]:
        pack.add_resource(old_pack.contents[entry.content_index])
    while len(pack.table_entries) < FIRST_FONT_ID - 1:
        pack.add_resource(b"")

    font_dir = ROOT / "resources/normal/obelix/fonts"
    for name, filename in FONT_SPECS:
        definition = ResourceDefinition("font", name, str(font_dir / filename))
        definition.max_glyph_size = 512
        definition.character_list = None
        definition.character_regex = "[ -ÿ…]"
        definition.compatibility = None
        definition.compress = None
        definition.extended = False
        definition.tracking_adjust = None
        definition.pixel_height = None
        pack.add_resource(
            FontResourceGenerator.build_font_data(str(font_dir / filename), definition)
        )

    with PACK.open("wb") as output:
        crc = pack.serialize(output)

    version_header = VERSION_HEADER.read_text()
    VERSION_HEADER.write_text(re.sub(r"(?<=\.crc = )\d+", str(crc), version_header))


if __name__ == "__main__":
    main()
