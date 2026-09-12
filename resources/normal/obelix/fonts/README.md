# Emery classic bitmap fonts

This directory contains pixel-designed BDF strikes for the Emery classic Mac
prototype. Run `python3 generate.py` here to reproduce all nine generated BDFs
using only the Python standard library. Scaling is nearest-neighbour integer
replication; there is no smoothing, interpolation, or outline conversion.

## Sources and metrics

* **ChiKareGo2**, by Giles Booth: native 16-pixel grid, baseline 12 (12-pixel
  ascent and 4-pixel descent), with 2× and 3× strikes at 32 and 48 pixels.
  The raw gallery bitmap rows are vendored as `chikarego.json` from
  [BitFontMaker2 gallery 3780](https://www.pentacom.jp/pentacom/bitfontmaker2/gallery/?id=3780).
  The gallery says “Creative Commons Attribution” but gives no version or
  licence URL, so no version is asserted here. Attribution: **ChiKareGo2 by
  Giles Booth**.
* **Pixelva**, by HomeStarRunnerTron: its editor origin is shifted up three
  rows, producing native 12-pixel line metrics with baseline 9 (9-pixel ascent
  and 3-pixel descent), plus 24- and 36-pixel strikes. Bitmap pixels that
  overhang those line metrics are retained. Its raw rows are `pixelva.json`, from
  [BitFontMaker2 gallery 5892](https://www.pentacom.jp/pentacom/bitfontmaker2/gallery/?id=5892).
  The gallery marks it **Public Domain**.
* **Cozette v.1.30.0**, by Ines: native 13-pixel metrics (10-pixel ascent,
  3-pixel descent), plus 26- and 39-pixel strikes. The version-matched complete
  release source is `cozette-1.30.0.bdf`; `Cozette-LICENSE.txt` contains its MIT
  licence. Upstream: [Cozette v.1.30.0](https://github.com/the-moonwitch/Cozette/releases/tag/v.1.30.0).

For the gallery fonts, pixels retain BitFontMaker2's `x - 2` bearing.
Advances are tightened by one native pixel from the gallery preview, leaving
one blank pixel after the rightmost ink. Space retains the default five-pixel
advance. Actual bitmap bounds are retained, including accents and descenders.

ChiKareGo2 and Pixelva contain only codepoints present in their gallery data,
plus space. Cozette is intentionally subset to U+0020–U+00FF and U+2026.
Pixelva lacks the ASCII backtick, vertical bar and tilde. Missing characters
try the system fallback font, then the primary font's wildcard or space;
there is no general fallback to the previous Gothic face. This prototype is
not a complete multilingual replacement. Where a gallery source lacks U+2026,
the generator derives it by repeating the period bitmap three times;
generated BDF comments identify this change.

## Firmware use

Only normal Emery firmware selects these fonts. Headings and menu labels
use ChiKareGo2, body text and captions use Pixelva, and clocks and numeric
headings use Cozette. Smaller, Default and Larger body text select the 12-,
24- and 36-pixel Pixelva strikes. Fixed launcher labels use native ChiKareGo2.
Existing app fonts and recovery firmware remain unchanged.

The Obelix and QEMU Emery resource maps convert BDF to one-bit PBF at build
time. The watch does not run FreeType, scale glyphs or apply antialiasing.
After regenerating assets, rebuild firmware with `pbl build` and refresh
host-test font resources with `python3 tests/fixtures/regenerate_obelix_fonts.py`
from the repository root.
