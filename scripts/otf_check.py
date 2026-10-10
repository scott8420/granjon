#!/usr/bin/env python3
"""otf_check.py -- validate a Granjon-exported OTF or TTF with outside tools (M9, M10).

    python3 scripts/otf_check.py Font.otf|Font.ttf [Font.granjon]

Runs whatever of these is installed (pip install fonttools opentype-sanitizer
freetype-py uharfbuzz ufoLib2) and says which it skipped:
  ots       OTS sanitizer -- what Chrome / Firefox run before using a web font
  fontTools decompiles every table; with the source given, compares each
            glyph's outline (components decomposed) -- exactly for CFF; for
            TrueType against fontTools' cu2qu of the source: contour count,
            area (0.5 %) and bbox (2 units)
            -- advance and code points
  freetype  loads and renders every glyph
  harfbuzz  with the source given: shapes every kerning pair the source
            implies and compares the advance change with UFO precedence
Exit status 0 = everything that ran passed.
"""
import sys

fails = 0


def say(ok, what, detail=""):
    global fails
    if not ok:
        fails += 1
    print(("  PASS  " if ok else "  FAIL  ") + what + (f"  ({detail})" if detail else ""))


def skip(what, why):
    print(f"  SKIP  {what}  ({why})")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    otf = sys.argv[1]
    src = sys.argv[2] if len(sys.argv) > 2 else None

    # -- OTS ----------------------------------------------------------------
    try:
        import ots
        r = ots.sanitize(otf, "/dev/null", capture_output=True)
        say(r.returncode == 0, "ots: sanitizes", (r.stderr or b"").decode(errors="replace").strip()[:300])
    except ImportError:
        skip("ots", "pip install opentype-sanitizer")

    # -- fontTools ----------------------------------------------------------
    try:
        from fontTools.ttLib import TTFont
        from fontTools.pens.recordingPen import DecomposingRecordingPen, RecordingPen
    except ImportError:
        skip("fontTools", "pip install fonttools")
        return 1 if fails else 0
    font = TTFont(otf)
    try:
        for tag in font.keys():
            font[tag]
        say(True, "fontTools: every table decompiles", " ".join(sorted(font.keys())))
    except Exception as e:  # noqa: BLE001
        say(False, "fontTools: every table decompiles", repr(e))
        return 1
    gs = font.getGlyphSet()
    order = font.getGlyphOrder()
    say(order[0] == ".notdef", "fontTools: .notdef is glyph 0")
    bad = []
    for name in order:
        try:
            gs[name].draw(RecordingPen())
        except Exception as e:  # noqa: BLE001
            bad.append(f"{name}: {e!r}")
    say(not bad, f"fontTools: all {len(order)} charstrings draw", "; ".join(bad[:5]))

    ufo = None
    if src:
        try:
            import ufoLib2
            ufo = ufoLib2.Font.open(src, validate=False)
        except ImportError:
            skip("source comparison", "pip install ufoLib2")
    if ufo is not None:
        cmap = font.getBestCmap()
        names = list(ufo.keys())
        lib_order = ufo.lib.get("public.glyphOrder", names)
        # Map source -> production name through the cmap / order
        prod = {}
        for n in order:
            prod[n] = n
        src_to_otf = {}
        for g in ufo:
            if g.unicodes:
                src_to_otf[g.name] = cmap.get(g.unicodes[0])
            elif g.name in gs:
                src_to_otf[g.name] = g.name

        def norm(rec):
            out = []
            for op, pts in rec:
                if op == "closePath" or op == "endPath":
                    out.append(("close",))
                    continue
                out.append((op, tuple((round(x), round(y)) for x, y in pts)))
            return out

        def contours(rec):
            # Split into contours; drop a final closing lineTo back to the start
            # (fontTools' T2 pen emits it, the UFO does not) and compare as sets
            # of point lists rotated to the same start.
            cs, cur = [], []
            for item in norm(rec):
                if item[0] == "moveTo":
                    cur = [item]
                elif item[0] == "close":
                    if len(cur) > 1 and cur[-1][0] == "lineTo" and cur[-1][1][-1] == cur[0][1][0]:
                        cur.pop()
                    cs.append(tuple(cur))
                    cur = []
                else:
                    cur.append(item)
            if cur:
                cs.append(tuple(cur))
            return sorted(cs)

        truetype = "glyf" in font
        from fontTools.pens.areaPen import AreaPen
        from fontTools.pens.boundsPen import BoundsPen
        from fontTools.pens.cu2quPen import Cu2QuPen
        from fontTools.pens.roundingPen import RoundingPen

        def drawing_only(rec):
            # The recording's contours that draw something: a lone point (a
            # TrueType anchor) does not, and an all-off-curve contour has no
            # moveTo in fontTools' recording -- split on closePath / endPath.
            out, cur = [], []
            for op, pts in rec.value:
                cur.append((op, pts))
                if op in ("closePath", "endPath"):
                    if any(o in ("lineTo", "curveTo", "qCurveTo") for o, _ in cur):
                        out.extend(cur)
                    cur = []
            if any(o in ("lineTo", "curveTo", "qCurveTo") for o, _ in cur):
                out.extend(cur)
            r = RecordingPen()
            r.value = out
            return r

        def area_bbox(rec):
            # contours that draw, area (sign-free: TrueType runs the other way), bbox
            rec = drawing_only(rec)
            ap, bp = AreaPen(), BoundsPen(None)
            rec.replay(ap)
            rec.replay(bp)
            n = sum(1 for op, _ in rec.value if op in ("closePath", "endPath"))
            if bp.bounds is None:
                return None
            return n, round(abs(ap.value), 1), bp.bounds

        outline_bad, adv_bad, missing = [], [], []
        hmtx = font["hmtx"]
        for g in ufo:
            o = src_to_otf.get(g.name)
            if o is None or o not in gs:
                missing.append(g.name)
                continue
            a = DecomposingRecordingPen(ufo)
            g.draw(a)
            b = DecomposingRecordingPen(gs)   # the font's composites resolved too
            gs[o].draw(b)
            if truetype:
                # Against fontTools' own cu2qu of the source (what fontmake
                # would ship), not the cubic: thin strokes lose ~1 % area to a
                # 1-unit tolerance either way.
                ref = RecordingPen()
                a.replay(Cu2QuPen(RoundingPen(ref), font["head"].unitsPerEm / 1000.0, reverse_direction=True))
                sa, sb = area_bbox(ref), area_bbox(b)
                if sa is None or sb is None:
                    if (sa is None) != (sb is None):
                        outline_bad.append(g.name)
                elif (sa[0] != sb[0] or abs(sa[1] - sb[1]) > max(4.0, abs(sa[1]) * 0.005)
                      or any(abs(u - v) > 2 for u, v in zip(sa[2], sb[2]))):
                    outline_bad.append(f"{g.name} {sa[:2]} vs {sb[:2]}")
            elif contours(a.value) != contours(b.value):
                outline_bad.append(g.name)
            if hmtx[o][0] != round(g.width):
                adv_bad.append(f"{g.name} {hmtx[o][0]} != {round(g.width)}")
        say(not missing, "source: every glyph is in the font", ", ".join(missing[:10]))
        say(not outline_bad, f"source: outlines match ({len(ufo)} glyphs)", ", ".join(outline_bad[:10]))
        say(not adv_bad, "source: advances match", "; ".join(adv_bad[:5]))
        want = {}
        for n in lib_order:
            if n in ufo:
                for u in ufo[n].unicodes:
                    want.setdefault(u, n)
        cm_bad = [hex(u) for u, n in want.items() if cmap.get(u) != src_to_otf.get(n) and cmap.get(u) is None]
        say(not cm_bad and len(cmap) == len(want), f"source: cmap carries every code point ({len(cmap)})", ", ".join(cm_bad[:10]))

    # FreeType / HarfBuzz take a plain sfnt: unwrap a WOFF / WOFF2 first.
    if font.flavor:
        import tempfile, io
        plain = TTFont(otf)
        plain.flavor = None
        buf = io.BytesIO()
        plain.save(buf)
        tmp = tempfile.NamedTemporaryFile(suffix=".ttf" if "glyf" in plain else ".otf", delete=False)
        tmp.write(buf.getvalue())
        tmp.close()
        say(True, f"unwrapped the {font.flavor.upper()} for FreeType / HarfBuzz", f"{len(buf.getvalue())} bytes")
        otf = tmp.name

    # -- FreeType -------------------------------------------------------------
    try:
        import freetype
        face = freetype.Face(otf)
        face.set_char_size(48 * 64)
        errs = []
        for i in range(face.num_glyphs):
            try:
                face.load_glyph(i, freetype.FT_LOAD_RENDER)
            except Exception as e:  # noqa: BLE001
                errs.append(f"{i}: {e}")
        say(not errs, f"freetype: loads and renders all {face.num_glyphs} glyphs", "; ".join(errs[:5]))
        say(face.family_name.decode() == font["name"].getDebugName(1), "freetype: family name",
            face.family_name.decode())
    except ImportError:
        skip("freetype", "pip install freetype-py")

    # -- HarfBuzz kerning -----------------------------------------------------
    if ufo is not None:
        try:
            import uharfbuzz as hb
        except ImportError:
            skip("harfbuzz", "pip install uharfbuzz")
            return 1 if fails else 0
        data = open(otf, "rb").read()
        hbfont = hb.Font(hb.Face(data))
        k = ufo.kerning
        groups = ufo.groups
        g1 = {m: gname for gname, ms in groups.items() if gname.startswith("public.kern1.") for m in ms}
        g2 = {m: gname for gname, ms in groups.items() if gname.startswith("public.kern2.") for m in ms}

        def ufo_kern(l, r):
            for key in ((l, r), (l, g2.get(r)), (g1.get(l), r), (g1.get(l), g2.get(r))):
                if None not in key and key in k:
                    return k[key]
            return 0

        def members(side):
            return [m for m in groups.get(side, [])] if side.startswith("public.kern") else [side]

        pairs = set()
        for (l, r) in k.keys():
            for a in members(l):
                for b in members(r):
                    if a in ufo and b in ufo and ufo[a].unicodes and ufo[b].unicodes:
                        pairs.add((a, b))
        bad = []
        for a, b in sorted(pairs):
            ua, ub = ufo[a].unicodes[0], ufo[b].unicodes[0]
            buf = hb.Buffer()
            buf.add_codepoints([ua, ub])
            buf.guess_segment_properties()
            buf.direction = "ltr"
            hb.shape(hbfont, buf, {"kern": True, "liga": False})
            got = buf.glyph_positions[0].x_advance - round(ufo[a].width)
            if got != ufo_kern(a, b):
                bad.append(f"{a}/{b} {got} != {ufo_kern(a, b)}")
        say(not bad, f"harfbuzz: {len(pairs)} kerning pairs shape to the source's values", "; ".join(bad[:5]))

    print("-" * 47)
    print("all passed" if not fails else f"{fails} failed")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
