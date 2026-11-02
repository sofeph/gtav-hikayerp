"""Font Awesome ikonlarını SVG dosyalarına dönüştürür (telefon ikonları için).

data/icons.json içindeki her "ad": "kod noktası" için mod/fonts/fa-solid-900.ttf'ten glifi alır ve
data/icons/<ad>.svg olarak yazar. Mod bu SVG'leri C++ tarafında kendisi çizer (font gerekmez).

Kullanım (proje kökünden):
    python tools/fa_to_svg.py
Gereksinim: pip install fonttools
"""

import json
import sys
from pathlib import Path

from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont

ROOT = Path(__file__).resolve().parents[1]
FONT = ROOT / "mod" / "fonts" / "fa-solid-900.ttf"
ICONS_JSON = ROOT / "data" / "icons.json"
OUT_DIR = ROOT / "data" / "icons"

LICENSE = ("Font Awesome Free {version} by @fontawesome - https://fontawesome.com "
           "License - https://fontawesome.com/license/free (Icons: CC BY 4.0)")


def main() -> int:
    font = TTFont(FONT)
    cmap = font.getBestCmap()
    glyphs = font.getGlyphSet()
    upem = font["head"].unitsPerEm
    ascent = font["hhea"].ascent
    descent = font["hhea"].descent
    height = ascent - descent
    version = font["name"].getDebugName(5) or ""
    version = version.split("Font Awesome version:")[-1].strip(" )") if "Font Awesome version:" in version else ""

    icons = json.loads(ICONS_JSON.read_text(encoding="utf-8"))["icons"]
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    written, missing = 0, []
    for name, hex_cp in sorted(icons.items()):
        glyph_name = cmap.get(int(hex_cp, 16))
        if not glyph_name:
            missing.append(f"{name} ({hex_cp})")
            continue
        glyph = glyphs[glyph_name]
        pen = SVGPathPen(glyphs)
        # Font: y yukarı, taban çizgisi 0. SVG: y aşağı, üst kenar 0.
        glyph.draw(TransformPen(pen, (1, 0, 0, -1, 0, ascent)))
        d = pen.getCommands()
        if not d:
            missing.append(f"{name} ({hex_cp}, boş glif)")
            continue
        width = glyph.width or upem
        svg = (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}">'
               f"<!--! {LICENSE.format(version=version)} -->"
               f'<path d="{d}"/></svg>\n')
        (OUT_DIR / f"{name}.svg").write_text(svg, encoding="utf-8")
        written += 1

    print(f"{written} SVG yazıldı: {OUT_DIR}")
    if missing:
        print("Fontta bulunamayan ikonlar (icons.json'daki kodu güncelle):", ", ".join(missing))
    return 0


if __name__ == "__main__":
    sys.exit(main())
