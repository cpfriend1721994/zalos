#!/usr/bin/env python3
"""Build a compact CBDT/CBLC color emoji fallback TTF with ASCII stripped."""

import io
import os
import sys
from fontTools.ttLib import TTFont
from PIL import Image


def shrink_png_strike(font: TTFont, target_ppem: int = 48) -> None:
    if "CBLC" not in font or "CBDT" not in font:
        raise RuntimeError("Input font must contain CBLC and CBDT tables")

    cblc = font["CBLC"]
    cbdt = font["CBDT"]

    # Keep only the smallest strike >= target_ppem, or the largest strike if smaller.
    strikes = cblc.strikes
    chosen_idx = 0
    best_ppem = None
    for idx, strike in enumerate(strikes):
        ppem = strike.bitmapSizeTable.ppemY
        if ppem >= target_ppem and (best_ppem is None or ppem < best_ppem):
            best_ppem = ppem
            chosen_idx = idx
    if best_ppem is None:
        chosen_idx = max(range(len(strikes)), key=lambda i: strikes[i].bitmapSizeTable.ppemY)

    chosen_strike = strikes[chosen_idx]
    orig_ppem = float(chosen_strike.bitmapSizeTable.ppemY)
    scale = min(1.0, float(target_ppem) / orig_ppem)
    new_ppem = int(round(orig_ppem * scale))

    def scale_val(v: int) -> int:
        return int(round(v * scale))

    def scale_signed(v: int) -> int:
        return int(round(v * scale))

    bst = chosen_strike.bitmapSizeTable
    bst.ppemX = new_ppem
    bst.ppemY = new_ppem
    for attr in ("hori", "vert"):
        lm = getattr(bst, attr, None)
        if lm is None:
            continue
        for k in (
            "ascender",
            "descender",
            "widthMax",
            "caretSlopeNumerator",
            "caretSlopeDenominator",
            "caretOffset",
            "minOriginSB",
            "minAdvanceSB",
            "maxBeforeBL",
            "minAfterBL",
        ):
            if hasattr(lm, k):
                setattr(lm, k, scale_signed(getattr(lm, k)))

    cbdt.strikeData = [cbdt.strikeData[chosen_idx]]
    cblc.strikes = [chosen_strike]

    strike_dict = cbdt.strikeData[0]
    for name, glyph in strike_dict.items():
        if hasattr(glyph, "metrics") and glyph.metrics is not None:
            m = glyph.metrics
            for k in ("BearingX", "BearingY", "Advance", "horiBearingX", "horiBearingY", "horiAdvance",
                      "vertBearingX", "vertBearingY", "vertAdvance"):
                if hasattr(m, k):
                    setattr(m, k, scale_signed(getattr(m, k)))
        img_bytes = getattr(glyph, "imageData", None)
        if not img_bytes:
            continue
        with Image.open(io.BytesIO(img_bytes)) as im:
            im = im.convert("RGBA")
            nw = max(1, scale_val(im.width))
            nh = max(1, scale_val(im.height))
            if nw != im.width or nh != im.height:
                im = im.resize((nw, nh), Image.Resampling.LANCZOS)
            # Indexed RGBA palette drastically cuts CBDT PNG size while preserving crisp transparency.
            q = im.quantize(colors=64, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.NONE)
            buf = io.BytesIO()
            q.save(buf, format="PNG", optimize=True)
            new_bytes = buf.getvalue()
            if hasattr(glyph, "metrics") and glyph.metrics is not None:
                # Re-sync width/height to resized PNG dimensions
                glyph.metrics.width = nw
                glyph.metrics.height = nh
            glyph.imageData = new_bytes

    # Update bigGlyphMetrics if shared in CBLC indexSubTables (formats 2 & 5)
    for subtable in chosen_strike.indexSubTables:
        if hasattr(subtable, "metrics") and subtable.metrics is not None:
            m = subtable.metrics
            for k in ("height", "width", "horiBearingX", "horiBearingY", "horiAdvance",
                      "vertBearingX", "vertBearingY", "vertAdvance"):
                if hasattr(m, k):
                    setattr(m, k, scale_signed(getattr(m, k)))


def strip_ascii_cmap(font: TTFont) -> None:
    if "cmap" not in font:
        return
    for table in font["cmap"].tables:
        to_remove = [cp for cp in table.cmap.keys() if cp <= 0x7F]
        for cp in to_remove:
            del table.cmap[cp]


def rename_family(font: TTFont, family: str = "Zalos Color Emoji") -> None:
    if "name" not in font:
        return
    ps_name = family.replace(" ", "")
    for rec in font["name"].names:
        if rec.nameID in (1, 16):
            rec.string = family
        elif rec.nameID == 4:
            rec.string = family
        elif rec.nameID == 6:
            rec.string = ps_name


def main() -> int:
    if len(sys.argv) not in (3, 4):
        print(f"Usage: {sys.argv[0]} <input-NotoColorEmoji.ttf> <output.ttf> [target_ppem=26]", file=sys.stderr)
        return 2
    in_path, out_path = sys.argv[1], sys.argv[2]
    target_ppem = int(sys.argv[3]) if len(sys.argv) == 4 else 26
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    font = TTFont(in_path)
    strip_ascii_cmap(font)
    rename_family(font)
    shrink_png_strike(font, target_ppem=target_ppem)
    font.save(out_path)
    size_mb = os.path.getsize(out_path) / (1024.0 * 1024.0)
    print(f"Wrote {out_path} ({size_mb:.2f} MiB)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
