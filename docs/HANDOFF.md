# Granjon handoff — s08 -> s09

# s09_m15_transform_corners

## Files to upload
- `s08_m14_drawing_kit_r3.zip` (the s08 ship -- the one zip to install; M14 + Select tool + Type it. Earlier s08 zips are superseded. Or a fresh `scripts/granjon_backup.sh -o s09_m15_transform_corners`)
- `RULES.md`, `HANDOFF.md`, `ARCHITECTURE.md`, `ARC.md`, `CANON.md`

---

## Status — s08
- **M14 drawing kit: crc** (Scott, on `s08_m14_drawing_kit.zip`: "excellent. crc.").
- **r2: crc** (Scott: "great! crc.").
- **r3 -- Type it** (`s08_m14_drawing_kit_r3.zip`; Scott: "a keyboard combination to show how to type the glyph in a text editor in the GLYPH panel ... doesn't create a glyph just shows how a user may create one"). Sandbox green, not crc. Selftest 1058 / 0 (9 new). No warnings in touched files. No new package.
  - GLYPH card, a **Type it** row, display only: ASCII -> "Key: n" / "Key: Shift+N" (US layout); anything else -> its Compose sequence when the system has one ("Compose ' e") and "Ctrl+Shift+U e9 Space" (GNOME / GTK / IBus, any code point). Tooltip: the same for GNOME, how to get a Compose key, Windows (Alt+0233 from A0..FF, else "00E9 then Alt+X"), macOS (Option+00E9 with Unicode Hex Input, BMP only). A part: "never typed"; no code point: "not typed directly" + why. Selectable text.
  - `unicode/TypeIt` (GTK-free): the system Compose table (`/usr/share/X11/locale/en_US.UTF-8/Compose`, read once; Fedora's libX11-common) -- <Multi_key> sequences only (dead keys depend on the layout), the shortest per character, keys spelled for reading. No table -> no Compose line, nothing else changes.
  - Shot: n -> "Key: n"; Odotbelow (U+1ECC) -> "Compose ! O", "Ctrl+Shift+U 1ecc Space", tooltip with Windows / macOS.
- **r2 -- the Select tool** (`s08_m14_drawing_kit_r2.zip`; Scott: "a way to select a complete path. Like a global selection tool over a localized selection tool ... a solid pointer is global and a hollow selector is local"). Sandbox green, not crc. Selftest 1049 / 0 (7 new). No warnings in touched files.
  - Tool panel, top: **Select** (the SOLID arrow, **V**) -- whole paths; then **Pick** (now the HOLLOW arrow, **A**) -- nodes and handles, as before. Illustrator's convention and keys. **V changed meaning**: it was Pick, it is now Select.
  - Select: click a path's outline or inside it -> the whole path (inside = the smallest path there, so a click in a counter takes the counter, in the ink takes the bowl); Shift+click adds / removes a path (Shift-drag on a picked one moves, as in Pick); a marquee takes every path it touches; drag moves them (snapping off the node nearest the press). Handles don't catch. Anchors, components, guides, the image, sidebearings behave as in Pick. **Double-click** a path -> into Pick (the hollow arrow), path still picked. Status line counts paths ("2 paths picked").
  - Escape from Measure / Hand / the shape tools returns to Select (was Pick). The pen still leaves to Pick. The app still opens on Pick.
  - geom: `whole_contours` (a node / handle ref -> all its contour's nodes) and `contour_at` (smallest closed contour containing a point), both in the selftest.
  - Sandbox shots on the sample H / O: click inside the left stem -> the stem; Shift+click the bar -> both; drag -> both moved; on O a marquee over the right side -> outer and counter whole; double-click -> Pick, "8 points picked".
- **M14 drawing kit: sandbox green, not crc** -- `s08_m14_drawing_kit.zip` is the one zip to install (M14a + b + c). Selftest **1042 / 0** (89 new over s07's 953). Built against Ubuntu 24.04, g++ 13.3, gtkmm 4.10 / GTK 4.14, RelWithDebInfo; no warnings in any file touched (the known GCC 13 memmove false positive x3 elsewhere). **No new package.** (Sandbox only, as s06: Clipper2 from a git clone via FETCHCONTENT_SOURCE_DIR_CLIPPER2 -- nothing changes on Scott's machine.)
- **M14c parts library: sandbox green.**
  - A part is a glyph left out of the compile -- the per-glyph flag `Glyph::skip_export`, saved as UFO's **`public.skipExportGlyphs`** (fontmake and the other UFO tools skip it too). Rename / delete / undo carry the flag with the glyph.
  - **Navigator + Overview: a PARTS section, first** (Scott, s05: "a category at the top of the navigator").
  - Export (OTF / TTF / WOFF2, so the Preview and the CODE card too): parts are left out ("N part(s) left out" in the log); a glyph using a part as a component exports it decomposed; kerning naming a part is skipped.
  - Inspector, GLYPH card: **Part -- not exported** check (one undo step) -- turns any glyph into a part or back.
  - Inspector, new **PARTS** card (after COMPONENTS): the font's parts in a list; **Place linked** (a component at the origin -- edit the part and every glyph using it follows) / **Place as contours** (plain editable copies; a part's own components resolved). What's placed comes **picked**, so a drag or the arrows put it in place. Name field + **Make part**: the picked contours are COPIED into a new part where they sit (this glyph unchanged); the name suggests `_part.<glyph>`; Enter or the button (not focus-leave -- see below).
  - Verbs, one step each, all in the verb table: `make_part`, `set_part`, `place_part`. Refusals: bad / taken name, nothing picked, not a part, a part into itself, a cycle, a part that draws nothing.
  - Sandbox shots (sample font): PARTS 1 at the top of navigator and overview; n's left stem -> Make part "_part.stem" -> PARTS 2; o -> Place as contours (the stem appears, picked) -> Place linked _part.serif (component at the origin, picked).
  - Caught in the sandbox and fixed before shipping: the name field first committed on focus-leave too, so clicking away made a stray "_part.part". Now Enter / the button only.
- **M14b booleans: sandbox green** (`s08_m14b_booleans_wip.zip`, checkpoint; holds M14a too). Selftest 1006 / 0 (26 new: 10 geometry, 12 verb, 4 verb-table gate). Same toolchain; no warnings in the new code. **No new package.**
- Inspector, OUTLINE card, a new row: **Union / Subtract / Intersect** on the PICKED contours (a contour counts when any of its nodes or handles is picked -- the "Reverse picked" rule). Glyphs' convention, which fits M14a: a freshly drawn shape comes picked, so **draw, then Subtract** cuts it out of the glyph.
  - **Union**: the picked contours become one outline (two or more picked; the button greys otherwise).
  - **Subtract**: the picked contours cut every unpicked contour whose box meets theirs, then go.
  - **Intersect**: those unpicked contours keep only what the picked ones cover; the picked ones go.
  - Contours not involved stay as they are, in order; the result takes the place of the first contour involved and is **picked**. One undo step each ("Undid: Subtract"), verb `boolean_contours` (in the verb table). Status line: "Subtract: 4 contours."
  - Refusals (status line, no step): too few picked; every contour picked (nothing to cut); the cutter meets nothing; a contour running the wrong way in either set ("correct directions first"); a result that would be empty; a rebuild that fails M10c's safety net.
- Engine: `geom::boolean_op(subject, clip, op)` -- the M10c engine with a subject / clip split (Clipper2 Difference / Intersection / Union, nonzero), the same tagged flattening and EXACT rebuild: original nodes keep place / handles / type, crossings are corners, a cut edge running backwards is the source curve reversed. `remove_overlaps` is now the union case of the same function (its "nothing crossed -> untouched" shortcut kept; the booleans skip it so a hole punched without a crossing still turns clockwise). All M10c tests unchanged and green.
- Tested: a circle punched inside a square -> 2 contours, hole clockwise with its 4 smooth nodes; a circle over the edge -> one contour, 2 crossings, the bite within 0.03 unit of the true radius; intersect = the half disk; union = square + half disk; a clockwise cutter cuts the same.
- Sandbox shots (n, an ellipse drawn across both stems): Subtract -> four stem pieces, curved cut edges; Intersect -> the two stem pieces inside the ellipse; Ctrl+A, Union -> one outline, corners at the crossings, the ellipse's curves kept.
- Known (not new in M14, shows more with booleans): the pick is not part of undo. After Subtract then Ctrl+Z, the pick still holds the RESULT's contour indices, which now name other contours -- e.g. all three read as picked, so Intersect says "every contour is picked". Stale (out-of-range) refs are dropped already; refs that are still in range are kept. Fix candidates: clear the pick when an undo / redo changes the shown glyph's contour count, or journal the pick with the step. Asked, not done.

- **M14a primitives: sandbox green** (`s08_m14a_primitives_wip.zip`, checkpoint -- not crc until Scott runs it). Selftest 980 / 0 (27 new in `src/selftest_draw.cpp`, the M14 file). Built against Ubuntu 24.04, g++ 13.3, gtkmm 4.10 / GTK 4.14, RelWithDebInfo; no warnings in the new files (the known GCC 13 memmove false positive x3). **No new package.**
- Three new tools in the tool panel, under the Pen: **Rectangle (R)**, **Ellipse (E)**, **Line (\\)**. Drag to draw; **Shift** = square / circle / 45 degrees, **Alt** = from the centre (the press is the middle). A free drag snaps like a node drag (metric lines, sidebearings, the glyph's nodes, guides, the grid when shown; Ctrl or Shift: no snap). Live dashed preview + the size in the readout ("572 x 500", "340 long"). Escape mid-drag: nothing drawn.
- Release = **one undo step** named for the shape ("Undid: Ellipse"), verb `add_shape` (in the verb table): one closed contour appended, snapped to units, turned counter-clockwise if it came in the other way, an Empty glyph becomes Drafted. The new contour is **picked**, so a drag / arrows / the inspector act on it at once. The tool stays on (draw several); V or Escape -> Pick.
- Shapes (`geom/Shapes`, GTK-free): rectangle = 4 corners from bottom-left; ellipse = 4 smooth nodes at the extremes (bottom, right, top, left), handles at kappa 0.5523 (a circle within 0.03 %); line = a bar of the **Stroke** width, butt ends, 4 corners.
- **Stroke** field in the editor bar, shown only with the Line tool: remembered (`editor.line_width` in settings.json); until set it is the font's stem -- a preset font's lowercase stem, else 8 % of the em.
- Own icons (`granjon-tool-rect / ellipse / line-symbolic.svg`) -- redraw if they don't read. Shortcuts window lists R / E / \\.
- Sandbox shots (Xvfb + xdotool, on the sample font's n): an ellipse mid-drag snapped to baseline and x-height, a Shift square below the baseline, an 80-unit line picked after drawing; Ctrl+Z -> "Undid: Line", Escape mid-drag drew nothing, two more Ctrl+Z -> n as it was.

## What to look at in r2 (Select)
- V (solid arrow): click inside a stem, inside a counter, on an outline. Shift+click a second path; drag both. Box across two paths.
- Double-click a path -> hollow arrow (A), then work its nodes. V back.
- Whether V = Select / A = Pick suits you (Illustrator's), or you'd rather keep V on the node tool; and whether the app should open on Select.

## What to look at in M14c
- Navigator: **PARTS** at the top (empty on an existing font until you make one).
- Any glyph: pick a contour (click a segment) -> PARTS card -> a name (or the suggested `_part.n`) -> **Make part**. It shows under PARTS; open it in the editor and change it.
- Another glyph: PARTS card -> pick it in the list -> **Place linked**; drag it into place. Edit the part -> the glyph follows. **Place as contours** -> plain contours you can cut / merge (M14b).
- GLYPH card **Part -- not exported** on an existing glyph -> it moves to PARTS.
- Export OTF: the log says "N part(s) left out"; GNOME Fonts shows no part glyph; a glyph with a linked part looks whole.
- Save, close, reopen: the parts are still parts (lib.plist: public.skipExportGlyphs).

## What to look at in M14b
- Editor: draw an ellipse across a stem (it comes picked) -> OUTLINE -> **Subtract**: the stem is cut, the curve follows the ellipse. Ctrl+Z, click empty canvas, then the same with **Intersect**.
- Ctrl+A on two overlapping shapes -> **Union**. A ring: draw a big circle and a small one inside it, pick the small one -> Subtract -> a counter (clockwise; the OUTLINE line should still say "all running the right way").
- On a glyph with a wrong-way counter (Report flags it): a cut through it is refused with "correct directions first".
- Whether "picked cuts the rest" is the convention you want (Glyphs' way), or Illustrator's "the frontmost cuts the ones behind".

## What to look at in M14a
- Editor on any glyph: R, drag a box (then with Shift, with Alt, with both). E, the same. \\ (backslash), drag a bar; change Stroke in the bar and drag another.
- After each: the shape is picked -- drag it, arrow it; Ctrl+Z takes it out in one step.
- Draw an ellipse over a stem and OUTLINE -> Remove overlaps: one outline (the M10c engine).
- Whether the three icons read; whether Shift-square should grow from the larger side (now) or the horizontal one.

## Decisions made in s08
- Primitives are added on RELEASE (one verb call), not built live in the model like the pen -- the preview is the canvas's own drawing, so a cancelled drag never touched the journal.
- Each shape is a NEW contour, never merged on drawing (overlaps are kept while drawing, as everywhere else; Remove overlaps / the booleans merge).
- Line = a filled bar (Scott's spec), not an open path: font outlines are closed. Butt ends. Width is a remembered preference, not per glyph.
- Keys R, E and backslash (Illustrator's line key; L is taken by "make lines").
- Parts: a per-glyph flag rather than a font-level list (rename / delete / undo stay trivially right); the UFO list is only the file format. Make part COPIES (the glyph you took it from is untouched -- Subtract or Delete if you want it gone). A part keeps its position (a serif drawn at the baseline places at the baseline) and its source's width. Linked placement is a plain component, so Decompose (COMPONENTS card) turns a linked part into contours later. Name suggestion `_part.<source glyph>` (Glyphs' underscore convention for non-exporting glyphs).
- Parts first in the navigator, overview too (Scott's s05 words).
- No starter parts yet: a preset font could come with `_part.stem / _part.serif / _part.bowl` built from its own numbers (Presets.cpp already makes exactly those pieces). Asked, not built.
- Booleans: picked = the cutter / the operand (Glyphs' convention, so draw -> Subtract is two moves); unpicked contours are involved only when their box meets the picked ones' (local, never a whole-glyph merge by surprise). A result that would be empty is refused rather than leaving an empty glyph.

## Status — s07
- **r4** (`s07_m13_background_r4.zip`, Scott, with a shot of the Granjon logo dropped behind "eight" on a dark desktop: "the bkgd color is too similar and too opaque" -- "allow a user to set the color of drop ins with transparency level"): **Preferences -> Editor -> Background image colour** -- one colour button with transparency: its colour is the ink every image is drawn in (default a warm orange, `#e0703c`, so it never reads as the grey outline fill), its transparency is the opacity a newly placed image starts at (default 30 %, was 40). Settings `editor.image_tint` / `editor.image_opacity`, bad values fall back. Per image: the IMAGE card's **Colour** button (writes UFO's `color`, one undo step, verb `set_image_color`) and **Default** (back to the app's colour); the Opacity slider stays the per-image transparency. A Replace keeps the old image's colour and opacity. Selftest 953 / 0 (11 more). Shot: dark desktop, orange ink behind the grey n; the Preferences row with its swatch.
- **r3** (`s07_m13_background_r3.zip`, Scott: "the dropped in image should be a pale color by using the black as a mask"): the scan is drawn as an INK MASK -- each pixel's darkness (over white, so a transparent PNG reads as paper) through a levels ramp (darkness under 40 / 255 clear, over 200 full), filled with the canvas's text colour at the image's opacity. Paper and speckle vanish; the ink is a pale grey on a light desktop and a pale light tone on a dark one -- no white sheet in dark mode. An image carrying a UFO `color` uses that tint instead. Built once per image (A8 surface), cached. Shot in both modes (light / dark via gsettings color-scheme). The file in images/ is untouched -- this is only how the canvas draws it.
- **r2** (`s07_m13_background_r2.zip`, Scott: "the scrollwheel ... should only work if field is active"): every number field and slider (all `widgets::SpinButton` / `widgets::Scale`, so the inspector, Preferences, the Overview / Spacing size sliders) takes the mouse wheel only while it has the keyboard focus (clicked into or tabbed to). Otherwise the wheel scrolls the panel it sits in. Built into `Named<>` (`guard_wheel`, `src/widgets/Wheel.cpp`), so new fields get it by construction. Shot: scrolling the inspector with the pointer over the spins scrolls the panel and edits nothing (undo stays grey); clicked into Height, two wheel notches -> 1000 to 1002.
- **M13 background layer: crc** (Scott, on r4: "excellent. crc." -- shot: the Granjon logo behind "eight", green at 22 %, locked, on the dark desktop). First ship `s07_m13_background.zip`; r2-r4 below. Selftest 942 / 0 (36 new). Built against Ubuntu 24.04, g++ 13.3, gtkmm 4.10 / GTK 4.14, RelWithDebInfo; no warnings in the new files. **No new package.**
- Per glyph, the UFO 3 way: `<image fileName=... xScale ... color=...>` in the .glif, the PNG in the font's `images/`; opacity and lock in the glyph lib (`granjon.image`). Other UFO tools (RoboFont, fontParts) read the placement; Granjon reads theirs.
- **PNG in the font, always** (UFO 3 allows only PNG): a JPEG (or anything GTK reads) is converted when placed. Same bytes on ten glyphs are stored once.
- **Place:** drop a file on the editor canvas, or the inspector's new **IMAGE** card -> Place… (Replace… when there is one). A fresh image spans descender to ascender, left edge on the origin.
- **Canvas:** behind everything, at its opacity; pixels show square close in. **Unlocked:** dashed frame; click it to pick (accent frame + 4 corner handles), drag to move, drag a corner to scale (aspect kept; Shift = from the centre), Delete removes, Escape drops the pick. Nodes / segments under the pointer still win over the image. **Locked:** no frame; clicks go through to marquee / pick -- the tracing state.
- **IMAGE card:** file + pixel size, Place… / Replace…, Fit, Remove, X, Y, Height (width follows), Opacity slider, Locked.
- **Tool panel:** a new **Image** toggle (picture icon, under Snap) shows / hides -- remembered like Fill / Grid, not an undo step.
- **One undo step each:** place, replace, remove, move (drag), scale (drag), typed X / Y / Height (merge in a row), opacity (merges), lock. A left-sidebearing drag or applied spacing carries the image with the drawing.
- **Never exported** (selftest: the OTF is byte-identical with and without).
- Sandbox shots (Xvfb + xdotool): the scan drawn upright behind n at 40 %, the picked frame + handles, a corner scale from top-right, a move, Ctrl+Z ("Undid: Move image"), Lock -> a drag over it marquees instead. The JPEG path ran through the app's own `place_image_file` (700 x 1000 JPEG -> `images/n_sketch.png`). **Not shot:** a real drag-and-drop from Files (xdotool can't drag a file in) -- the drop handler calls the same function.

## What to look at in M13
- Editor on any glyph: drag a PNG or JPEG from Files onto the canvas. It should sit from descender to ascender, at 40 %, dashed frame.
- Click the image (not on the outline): frame + handles. Drag it; drag a corner (and with Shift). Ctrl+Z after each.
- Inspector **IMAGE** card: type X / Y / Height; slide Opacity; **Locked** -> then draw over it with the pen and marquee across it (nothing should grab the image).
- The **Image** toggle in the tool panel hides / shows it.
- Save, close, reopen: the image comes back; `images/` holds only the placed files. Export OTF: no trace of it.
- Light and dark desktop: the scan should be ink only, no paper, in orange at 30 %. Preferences (Ctrl+,) -> Background image colour: pick a colour and slide its transparency; the canvas follows at once (images without their own colour), the next drop starts at that transparency. IMAGE card -> Colour on one image, Default to undo that (or Ctrl+Z). If a pencil sketch comes out too faint, the ramp (40 / 200 in `ink_mask`, GlyphCanvas.cpp) is the knob.
- The picture icon (mine) -- redraw if it doesn't read.

## Decisions made in s07
- Per glyph only. A font-wide sheet (one scan sliced per glyph) is asked, not built -- it would sit on top (slice -> place_image per glyph).
- The image bytes live in a pool on the Font, outside the journal (append-only for the document's life); the glyph's reference is the undoable record. Keeps undo snapshots small and a replace cheap.
- JPEG -> PNG on placing (UFO 3 is PNG-only); a PNG goes in byte for byte.
- Show / hide is a view toggle (a preference), not per glyph and not an undo step -- consistent with Fill / Grid. (The s06 handoff had said "one undo step each" including show / hide; I made this call.)
- Unlocked = grabbable (Glyphs-like); locked = invisible to the mouse. New images start unlocked (you usually move / scale first, then lock).
- The drag's snap rounds the offset to whole units; the scale is kept exactly as dragged.
- Opacity and lock are Granjon's own lib keys; UFO's `color` attribute is kept verbatim and, when present, is the tint (r3).
- r3: the scan is shown as a mask, never in its own colours (Scott).
- r4: the tint is a preference (colour + transparency in one button; the transparency is the starting opacity of new images), default orange, not the text colour -- the text colour read as the outline's own grey (Scott's shot). Per-image colour = UFO's `color`, so other UFO tools see it too.

## s08 goal — Milestone M14: drawing kit
Primitives (rectangle, ellipse -- Shift square / circle, Alt from centre; line = a filled bar of a set stroke width), boolean ops on picked contours (union / subtract / intersect, on the M10c engine), and the **parts library**: a navigator section of partial glyphs (serifs, bowls, stems) left out of the compile (UFO `public.skipExportGlyphs`), placed into real glyphs as a linked component or as editable contours. M13 is crc; start straight on M14. **Stop at M14** (or at M14a primitives if it runs long -- check in).

Then: M15 transform panel + corner tools + extreme points + offset stroke; M16 mark positioning, features editor, italic angle / font info. Later: print proof, ttfautohint, weight masters (v3).

## Still open
- **M14 questions (s08):** (1) booleans -- "picked cuts the rest" (Glyphs, now) or "the frontmost cuts the ones behind" (Illustrator)? (2) Should preset fonts start with parts (stem, serif, bowl from the preset's numbers)? (3) Undo and the pick: clear the pick when an undo / redo changes the shown glyph's contour count, or journal the pick with each step? (4) Shift-square grows from the larger side (now) -- right?
- Not done in M14 (follow-ons): booleans on components (decompose first today); a parts preview strip (the list is names only); drag a part from the navigator onto the canvas; rounded rectangle / polygon primitives.
- **Font-wide image sheet** (M13): wanted? (one scan of an alphabet, sliced per glyph). Asked in s07, no answer yet.
- M13 follow-ons: SVG backgrounds; rotate an image (the fields show offset only on a rotated / skewed one from another tool); a "trace" helper (auto-outline from the scan) only if wanted; very large scans decode at full size (a 6000 x 8000 scan is ~190 MB of pixels in memory) -- a downscaled preview if that bites.
- Packages: approved in s05. brotli-devel in use (M10b); Clipper2 1.5.4 fetched by CMake (M10c); harfbuzz-devel when the strip shapes. (FreeType is no longer needed for import.)
- A RULES.md addition proposed in s05, answer pending: "One zip to install" (the final message names the single zip to install; checkpoint zips carry `_wip`). s06 followed it: two `_wip` checkpoints, one final zip.
- M10 remainder: ttfautohint hook (optional external tool). WOFF2 glyf transform, CFF subroutinization (follow-ons).
- Overlap follow-ons: the self-overlay fallbacks (estimated, leaf, permyriad, Lslash at shifts like (1, 0), (0.5, 0.5), (150, 0) -- area 0.3-0.6 % off, returned as drawn); an overlaps finding in the Checks report; remove overlaps on a picked set of contours; the Z-tag rebuild back-ported to Curvz.
- granjon-cli scope (ARC backlog). `export_otf` is its first ready-made command.
- Editor follow-ons (now behind M12-M16): Shift-constrained point drags (the pen has 45 degrees; picking-mode drags don't yet), node type by key, a delete-without-refit modifier, pen snapping to metrics / nodes, composite follow-ons (Greek tonos, stacking, ogonek anchor, rebuild), overview follow-ons -- all in the ARC backlog.
- Profiles follow-ons: a profile cache + font-wide kern-to-gap over a pair list, per-class targets, collision check on proof strings.
- Spacing follow-ons: kerning groups editor (create / assign public.kern1 / kern2 from the UI), a strip text history / presets.
- Checks follow-ons (ARCHITECTURE §6): missing extreme points, overlaps, sidebearing consistency, stem drift, kerning class audit and outliers, collisions.
- Import follow-ons: WOFF / WOFF2 in (zlib / brotli), font collections, GSUB -> features.fea, glyph/group kerning kept as exceptions (now expanded to glyph pairs), seac accents, variable fonts (v3).
- Export follow-ons: italic angle / caret slope (no field in FontInfo yet), panose / family class, CFF subroutinization (size), PostScript blue zones from the metrics.
- M12 follow-ons: more presets (a slab, a humanist sans, an italic -- once italic angle exists, M16); preset numbers editable before Create (an 'Advanced' fold); starter glyphs for figures (zero, one) and a starter mark (acutecomb) so composites show something at once; isFixedPitch / panose for Mono on export; notes on more topics (diagonals and v/w apex overshoot, S-curves, figure heights, accents) -- they are just text + SVG in resources/primer/.
- The New Font window's Enter / Escape were tested by reading only (no window manager in the sandbox, so no keyboard focus there); the mouse path was shot.
- The inspector panel is narrower than some of its rows in a 1280 px window (the OUTLINE card's "Reverse picked", the KERN hint): clipped, not wrapped. Check on Scott's screen.
- selftest.cpp's main() wants splitting per milestone (M9's part is its own file now; the rest still one function).
- `GRANJON_SAMPLE` env still there.


## Status — s06 (for reference)
- **M12 presets + primer: crc** (Scott, on `s06_m12_presets_primer_r2.zip` -- the one zip to install; the two `_wip` zips were checkpoints). Selftest 878 / 0 (81 new). Built against Ubuntu 24.04, g++ 13.3, gtkmm 4.10 / GTK 4.14, RelWithDebInfo; no warnings in the new files. **No new package.** (Sandbox only: GitHub's archive URL was refused, so Clipper2 came from a git clone via FETCHCONTENT_SOURCE_DIR_CLIPPER2 -- nothing changes on Scott's machine.)
- **M12a preset core: sandbox green** (`s06_m12a_presets_wip.zip`, checkpoint). Not crc -- no UI yet.
- `model/Presets` (GTK-free): `resources/presets/presets.txt` (compiled in, like the char sets) holds Sans / Serif / Display / Mono -- UPM, metrics, overshoot, stems (lowercase / caps, vertical / horizontal), serif length + thickness, mono width, space, char set, font guides, and per control glyph its width and sidebearings; a shared `[keys]` table (b L=n R=o, u L=|n R=|n, D L=H R=O ...).
- `make_preset_font`: metrics, guides, `FontInfo::origin` (lib `granjon.preset`: id + the four stems), the control glyphs n o H O p v DRAWN, the set's glyphs (empty; accented letters as components -- 145 in GF Latin Core -- that fill in as the bases are drawn), keys (mono: W=600 on everything), glyph order = the set's order.
- The starter glyphs are built from parts -- stems, a crossbar at 0.52 cap height (optical centre), an arch contour tucked into the stem, ellipse rings with the round sides 6 % heavier, a counter ellipse cut into p's stem, serif slabs and head-serif wedges -- and merged by M10c's remove_overlaps (crossings -> corners, curves kept). Parts never share an edge (the rebuild's weak spot).
- Selftest 872 / 0 (75 new in `src/selftest_presets.cpp`): every preset parses, builds, the WHOLE Checks report is empty on the result, overshoot and descender exact, keys hold, mono centred, origin / guides / keys survive save + load, the parser refuses bad lines by number.
- **M12b chooser: sandbox green** (`s06_m12b_chooser_wip.zip`, checkpoint). ☰ **New…** / **Ctrl+N** -> (unsaved changes asked first) the **New Font** window: cards Blank / Sans / Serif / Display / Mono, each painting the glyphs that preset actually draws; the preset's blurb and numbers; Family; Character set (the preset's default; "Control glyphs only"); "Add the set's glyphs now". Create -> the font, the six control glyphs open as editor tabs with n in front, the Overview's coverage set follows the choice, the last pick is remembered (`new_font.preset`, `new_font.create_set` in settings.json). Inspector font page: a **STARTED FROM** card (preset + its stems) on a preset font. Enter creates from anywhere in the window, Escape cancels.

- **r2** (`s06_m12_presets_primer_r2.zip`): Presets.cpp `meet()` returns a plain struct, not std::pair -- silences the GCC aarch64 -Wpsabi note Scott saw (same fix as Overlap.cpp in s05).
- **M12c Primer: sandbox green.** ☰ **Primer: drawing type** / **F1** -> its own window beside the main one: six notes (Overshoot, Stem weight and contrast, Round and straight spacing, Joins and ink traps, Optical centre, Drawing order), each a title, an SVG figure drawn with the presets' own outlines, short text, and **TRY IT** links done in the main window: Measure on o / n (editor + Measure tool), Checks: Overshoot (Report, section unfolded), the Spacing strip set to "nnnn nnonoo nnoonn" / "HHHH HHOHOO HHOOHH", Apply spacing keys, the six control glyphs as tabs, the font page. Previous / Next walk the notes.
- Notes are data: `resources/primer/notes.txt` (format in its header: `[id]`, `title:`, `figure:`, `link: Label | target`, paragraphs, `*bold*`) and `resources/primer/*.svg` (1040 x 520, on white -- open them in Inkscape and redraw as you like). Compiled into the core; `core/Primer` parses; the selftest fails on a note without a title / body / figure, or a link the app can't follow.
- **F1 moved** from About to the Primer (GNOME's F1 = help). About is still the logo button and ☰ About.

## What to look at in M12
- **Ctrl+N** (or ☰ New…): the five cards -- do the previews read as Sans / Serif / Display / Mono? Pick one, name it, Create. The editor opens on **n** with o H O p v as tabs; the guides (crossbar, x-bar, serif) show; the inspector's Key says "in step". Overview: the set's glyphs, accented letters already built (empty until bases are drawn), coverage pointed at the set.
- The shapes: n's crotches, p's bowl into the stem, v's thin right arm (Serif), the Serif's slabs and head serif, Display's contrast. They are deliberately plain -- say what to change in the recipe (it's numbers in `resources/presets/presets.txt` plus the part rules in `src/model/Presets.cpp`).
- **Report (Ctrl+5)** on a fresh preset font: should say every check passes. **Spacing**: "nnonoo HHOHOO" -- the starting rhythm.
- Inspector, font page (click the font in the navigator): **STARTED FROM** -- the preset and its stems.
- **F1**: read a note, click its links; the Primer should stay open beside the window. On the light and dark desktop (the figures sit on white either way).
- Ctrl+N again: the chooser remembers the last pick and the set check. Blank = the old empty New.

## Decisions made in s06
- Presets and notes are DATA in resources/ (compiled into the GTK-free core like the char sets), so the selftest can build every preset and check every note, and Scott can edit both without touching code.
- Starter glyphs are built from simple parts and merged by M10c's overlap removal (the same engine as export), so they are ordinary editable outlines -- not components, not locked. Parts never share an edge.
- A preset font gets the whole set at once by default (empty glyphs, accented letters as components) with spacing keys from a shared table, and the glyph order of the set. Unchecking it keeps only the control glyphs; the Overview still shows the set's gaps.
- The preset is recorded as intent (FontInfo::origin -> lib `granjon.preset`: id + four stems); nothing enforces it. Shown in the inspector; a future stem-drift check (ARCHITECTURE §6) can read it.
- New Font is a modal window (New replaces the document); the Primer is non-modal (read beside the work).
- The Primer is its own window, not an inspector card (figures need width) and not a view (it would cover the canvas you're applying it to).
- F1 = Primer (help), not About.

---

## Status — s05 (for reference)
- **M10c overlap removal: crc** (Scott, on `s05_m10c_overlaps_r2.zip`, the last s05 ship; r2 silences a GCC aarch64 -Wpsabi note in Overlap.cpp).
- **Gap review (end of s05):** Scott raised primitives (square, oval, straight line), a parts library, presets and a primer; I added the rest. New milestones M12-M16 in ARCHITECTURE; order agreed ("okay"). Scott uploaded Curvz's code for it.
- M10c: **Remove overlaps** -- the union of a glyph's contours, nonzero (what the renderer fills), the CURVES KEPT: Clipper2 (built with its Z channel) works out the topology on a tagged flattening (Curvz's flatten), and the result is rebuilt from the original cubics cut exactly at the crossings -- original nodes keep their place, handles and type; crossings become corners. Not refit, not polygons. A safety net returns a glyph as drawn if its rebuild doesn't check out (area), with the reason.
- Where: on **export** (OTF, TTF, WOFF2, and so the Preview and the CODE card), always -- the source is never touched; **OUTLINE card -> Remove overlaps** (one undo step); **☰ Remove overlaps in every glyph** (one step). The verb skips a glyph with a contour running the wrong way (the union would fill that counter) and says "correct directions first".
- **Clipper2 is downloaded by CMake at the first configure** (network once; no package to install). build.sh's header says so.
- Selftest 797 / 0 (19 new). Sandbox: Termes overlaid on itself (shift 37, 23): 1084 / 1084 glyphs rebuilt, 11,624 crossings, ~150 ms; DejaVu Sans: 101 glyphs merged, OTF + TTF pass OTS / fontTools / FreeType, renders match the original except two where DejaVu stacks the dot below twice -- ours draws the dot once, without the darker double-antialiased edge (what overlap removal is for). Known: 1-4 fallbacks in the self-overlay stress at tiny or axis-aligned shifts (estimated, leaf, permyriad, Lslash; returned as drawn) -- follow-on.
- **M8 profiles:** crc (s05 open).
- **M9 export OTF: crc** (Scott, on r2: export via the dialog, GNOME Fonts reads the file, the Preview renders and follows the desktop's dark mode).
- **M10a export TTF: crc** (Scott). He noticed the OTF is slightly smaller than the TTF: same outline size (CFF 152 KB vs glyf 156 KB on Termes); the gap is post 2's stored names (~10 KB) and loca (~4 KB). CFF subroutines would shrink the OTF further (follow-on).
- **Packages: approved** (Scott, s05: "yes") -- brotli (WOFF2, now used), Clipper2 via FetchContent (M10c), HarfBuzz for the strip when it comes.
- **M10b WOFF2: crc** (Scott; `s05_m10b_woff2.zip`). **New build dependency: `brotli-devel`** (Fedora; Ubuntu `libbrotli-dev`) -- `sudo dnf install brotli-devel` before building. build.sh's header names it.
- M10b: `export/Woff2` -- the TrueType compile wrapped as WOFF 2.0: tables sorted by tag, one Brotli stream (font mode, quality 11), glyf / loca with the null transform (version 3), head flags bit 11 + checksums redone, version from head. ☰ **Export WOFF2 (web font)…**. Termes: 212 KB TTF -> 55 KB WOFF2 (~0.5 s at quality 11).
- Validation: selftest decodes the stream with libbrotlidec and gets every table back byte for byte (head: bit 11); OTS sanitizes it (the browsers' check); fontTools reads it, outlines / advances / cmap match the source; unwrapped it is our TTF byte-for-byte in size, FreeType renders all glyphs, HarfBuzz shapes all 25,579 pairs; headless Chromium loads it through @font-face (document.fonts: loaded) and kerns it (screenshot: font-kerning on vs none).
- Selftest 778 / 0 (6 new). The selftest links libbrotlidec (same package).
- **M11 import OTF / TTF: crc** (Scott; `s05_m11_import.zip` is the last s05 ship). Scott's order after M5h: "3, 1, 2" = M11 import, then M10b, then the editor follow-ons.
- M11: our own reader, no outside library (`import/`, namespace `granjon::reader`): CFF with a full Type 2 interpreter (local / global subrs, hint operators skipped, flex family, arithmetic ops; CID-keyed FDArray / FDSelect; charset names, cidNNNNN for CID) and TrueType glyf / loca (quadratics promoted to cubic exactly, all-off-curve contours handled, contours turned to the model's direction, composites -> components with their 2x2; point-matched components noted). Smooth inferred where handles line up. name (typographic family / subfamily first), head / hhea / OS/2 metrics (x-height / cap height measured when OS/2 lacks them; overshoot from O), cmap 4 / 12 / 6 / 0, post 1 / 2 names (else AGLFN from the cmap, else glyphNNNNN; invalid / repeated names replaced, counted), hmtx. Kerning: GPOS 'kern' PairPos 1 / 2 (Extension too) -> pairs, public.kern1 / kern2 groups, group pairs, with OpenType's first-subtable-wins carried into UFO precedence; old 'kern' table as fallback. Notes for what isn't read (GSUB, hints, variations, WOFF). Atomic: a failed read leaves the open font untouched.
- App: ☰ **Import font (OTF / TTF)…** (**Ctrl+Shift+O**): asks about unsaved changes, reads into a NEW unsaved document (no path -- Save asks where; closing asks first). Status line: format, glyphs, kern entries, ms, first note; the log has the rest.
- Validation (sandbox, `scripts/otf_check.py` comparing the ORIGINAL font file against the imported source): TeX Gyre Termes OTF -- 1090 outlines exact, 11,571 kern pairs shape identically (26 ms); our class-kerned Termes OTF -- 67 groups, 25,579 pairs identical; DejaVu Sans TTF -- 6,253 glyphs (5,524 composites), outlines within cu2qu tolerance, 5,918 code points, 2,727 kern pairs identical (47-57 ms). otf_check learned composites in the font and non-drawing contours (TrueType anchor points; all-off-curve contours). CID-keyed CFF is implemented but untested on a real CJK font (none in the sandbox).
- Selftest 772 / 0 (15 new in `src/selftest_import.cpp`: OTF round trip exact, TTF within tolerance and as-drawn direction, kerning identical both ways, names / metrics, truncated / junk / WOFF2 refused, failed import changes nothing). Headless: DejaVu imported through the dialog.
- **M5h tool panel: crc** (Scott: "excellent"; `s05_m5h_tool_panel.zip` is the last s05 ship). Scott (s05, on seeing the pen): "jog the ruler over to the right and add a tool panel and use standard icons for the toggle tools with hints."
- M5h: a tool panel down the editor canvas's left edge (the canvas and its left ruler move right of it). Tools, one at a time: **Pick (V)**, **Pen (P)**, **Measure** (hold M still works), **Hand** (hold Space still works); below a rule, the view toggles **Fill, Context, Grid, Snap** (remembered as before). Icons are Granjon's own symbolic SVGs (arrow, pen nib, ruler, hand, drop, context bars, grid, magnet -- `resources/icons/scalable/apps/granjon-tool-*` / `granjon-view-*`, in the gresource): the stock themes don't carry drawing tools on every desktop. Each tooltip: name, key, how to use it. Escape in Measure / Hand returns to Pick. The top bar keeps the title, Anchors, Fit, readout, zoom.
- Sandbox notes: icons need GTK's SVG pixbuf loader (installed here for the shots); the house CSS needs GTK >= 4.16 (var()), so the sandbox (4.14) shows the checked tools grey instead of the accent -- Scott's Fedora has it.
- **M5g pen tool: crc** (Scott, by moving on to the panel).
- M5g: **Pen** button in the editor bar / **P**. Click = corner node; click-drag = Symmetric node, handles along the drag (Alt: out-handle alone -> corner; Shift: 45-degree drags, and a plain click Shift-constrained to 45 degrees from the last node); click the first node = close (a drag there shapes the closing curve's arrival); click an open path's end node = go on from it (from the first node: the path is reversed in the next step); Enter / Escape end the path, Escape again leaves the pen. Each node one undo step (press-drag-release is a gesture). Dashed rubber band from the last node to the pointer (a curve when the last node has a handle); the first node rings when a click would close. Crosshair cursor. Status line guides each step. Conventions are the common ones (Illustrator / Glyphs) -- Curvz's source wasn't in hand.
- Selftest 757 / 0 (9 pen tests in `selftest_edit.cpp`, `pen_new_path` in the verb table). Headless: a triangle-with-a-curve drawn beside Termes O, open (rubber band) and closed.
- **M5f node editing: crc** (Scott). Scott chose the editor's drawing tools as next (s05: "gtg"); M5f is the node half, the pen tool is M5g.
- M5f: **Delete** removes picked nodes and refits the curve through them (end tangents kept, handle lengths least-squares fitted to the old outline with Schneider's reparameterisation -- deleting a just-inserted node gives the curve back within a unit), retracts picked handles, still deletes anchors / components; **double-click a segment** adds a node there (de Casteljau, the shape unchanged; smooth on a curve, corner on a line); **L / C** make the segments between picked nodes lines / curves (handles at 1/3, 2/3). All one undo step each, all in the verb table. Status line names the step.
- Selftest 744 / 0 (14 new in `src/selftest_edit.cpp`; one old test rewritten: "nodes are not deleted (yet)" -> "a node goes, in one step" + undo). Headless: double-click on Termes O's bowl -> node 5 of 7, smooth, outline unchanged; Delete -> back to 6, handles where they were.
- **M9c drawing codes + Export SVG: crc** (Scott, on r2 -- `s05_m9c_codes_svg_r2.zip`, the last s05 ship). r2 (Scott): the listing wraps long lines, and the SVG listing puts each contour on its own line (the exported file keeps one line). Scott asked (s05) whether a font's drawing data can be seen, and for both a code view and outline export.
- M9c: the inspector's **CODE** card (glyph page, last card): Format = CFF (the OTF's charstring disassembled -- operands then operator, batched groups one per line), TrueType (ttx-style `<pt x y on>` lines after cu2qu and the direction flip), or SVG path (what Export SVG writes); size / count summary; Copy. Follows edits (after a drag, at release); the format is remembered (`inspector.code_format`). ☰ **Export glyph as SVG…** and **Export Spacing text as SVG…**: plain filled paths, y flipped, no transforms, one `<path id="gN-name">` per glyph, the strip's kerning applied, viewBox in font units, width / height at 100 px per em, the text in `<desc>`.
- M10a: cu2qu ported from fontTools (cubic -> fewest quadratics within 1/1000 em, implied on-curves; same points as fontTools on the test curves), glyf / loca (short when it fits) / maxp 1.0 / post 2 / gasp; every contour reversed to TrueType's direction; OVERLAP_SIMPLE set; the rest shared with the OTF. App: **Export TTF…** (☰, **Ctrl+Shift+E**).
- Selftest 717 / 0 (16 new for TTF, 11 for codes + SVG). Built against Ubuntu 24.04, g++ 13.3, gtkmm 4.10 / GTK 4.14, pango 1.52, RelWithDebInfo; the known GCC 13 / fmt false positive x3. **No new package.**
- Outside validation (`scripts/otf_check.py`, now OTF or TTF; TrueType outlines compared with fontTools' own cu2qu of the source, i.e. what fontmake would ship): Termes + groups and Termes at 2048 UPM as TTF, and a TTF exported through the app's dialog, pass OTS, fontTools, outlines (1090 glyphs), FreeType, HarfBuzz (25,579 pairs). OTF vs TTF rendered by FreeType at 160 px overlay with no visible difference. Termes TTF: ~55 ms, 217 KB, 52,715 points.
- Before M10a: Scott ran M9 r1 and asked that the Preview page follow the desktop's dark mode -> r2 (see decisions).

What to look at in M10c (crc):
- First build fetches Clipper2 (network once).
- Draw two overlapping shapes in one glyph (pen, or paste a contour and drag it over another) -> OUTLINE card -> **Remove overlaps**: one outline, the curves where they were, corners at the crossings. Ctrl+Z.
- An O whose counter runs the wrong way: Remove overlaps refuses with "correct directions first".
- ☰ Remove overlaps in every glyph on an imported font (DejaVu Sans has overlapping accents); the status line counts glyphs. Export (OTF / TTF): the log says how many glyphs were merged.

What to look at in M10b (crc):
- **Install brotli-devel first**, then build.
- ☰ Export WOFF2 (web font)… -> a .woff2; drop it in a page with @font-face and open it in Firefox / Chrome.

What to look at in M11 (crc):
- ☰ Import font (OTF / TTF)… or Ctrl+Shift+O. Import an installed OTF and a TTF (e.g. /usr/share/fonts). Overview, editor (smooth nodes where expected; TTF curves now cubic), Spacing (kerning, groups in the KERN card), Preview (should look like the original).
- Import TeX Gyre Termes straight from its .otf now -- the M7 / M8 readouts on it are still unseen.
- Save it: it asks where (Family-Style.granjon).

What to look at in M5h (crc):
- Editor: the panel at the canvas's left. Hover each button -- the hint. Click Pen / Measure / Hand / Pick, and the keys V, P; hold M and Space inside Pick still work. Fill / Context / Grid / Snap as before.
- Whether the icons read as the tools at a glance (they are mine -- say which to redraw).

What to look at in M5g (crc):
- Editor, **P** (or the Pen button). Draw a box with clicks; close it on the first node. Draw a bowl with click-drags; try Alt mid-drag for a cusp, Shift for 45 degrees.
- End a path open (Enter), then click its end to go on; click its START to go on from there.
- Ctrl+Z after each node takes one node off. Escape twice: back to picking.
- Whether the pen's habits match Curvz's (if not, say what differs, or bring Curvz's pen code).

What to look at in M5f (crc):
- Editor (Ctrl+2): double-click any segment -> a node, picked. Drag it, or Delete it -> the curve closes up as it was.
- Pick a run of nodes on a curve and press **L** -> straight segments; **C** -> curves again (handles at thirds). Delete on a picked handle retracts it.
- Delete a real node of a round (an O extreme) -> the neighbours keep their tangents and the bowl stays close. Ctrl+Z undoes each in one step.
- The shortcuts window (Ctrl+?) lists the new keys under Editor.

What to look at in M9c (crc):
- Select a glyph; the inspector's last card, **CODE**. Switch CFF / TrueType / SVG; nudge a node in the editor and watch it follow. Copy, paste into a text editor. Compare with `ttx -t glyf` on an exported TTF: the <pt> lines should match.
- ☰ **Export Spacing text as SVG…** with a word in the Spacing strip; open the .svg in Inkscape / Illustrator: one path per letter, named, kerned, no font needed.

What to look at in M10a (crc):
- **Ctrl+Shift+E / ☰ Export TTF…:** proposes `Family-Style.ttf`; status line and log as for OTF (the log adds the glyf point count).
- Open the .ttf in GNOME Fonts beside the .otf: they should look the same. (Don't install a "TeX Gyre Termes" build -- it collides with the real one; My Termes is safe.)

What to look at in M9:
- **Ctrl+E / ☰ Export OTF…:** the dialog proposes `Family-Style.otf`, starts where the last export went (else the font's folder). The status line says glyphs / size / ms and the first note; all notes go to the log (`[io] export: ...`).
- **Install the .otf** (`~/.local/share/fonts/`, then `fc-cache`) and set text in another app (gedit, LibreOffice, Inkscape): names, kerning, metrics (line spacing = 1.2 em by the typo metrics).
- **Preview (Ctrl+4):** the waterfall and the paragraphs are the COMPILED font set by Pango, not Granjon's drawing. Kerning toggles the compiled GPOS. The page follows the desktop's light / dark (r2); the button, labelled with the other mode ("Light" on a dark desktop), flips it. The bar shows glyphs / KB / ms (tooltip: the notes) and "N missing" (tooltip lists them; they show as boxes). Edit something with the Preview showing (or come back to it): it recompiles a moment later.
- On Termes: whether the Preview's kerning matches the Spacing strip on the same pairs (it should, exactly -- the GPOS is built from the strip's lookup).

## Decisions made in s05
- M10c: the curves are kept (fontmake's booleanOperations approach) rather than Curvz's flatten-and-return-polygons: a font wants its nodes and handles. Clipper2 is built with Z only for Granjon (Curvz's build is without); the Z-tag rebuild is a candidate to bring back to Curvz. Export removes overlaps by default (fontmake's default); the source keeps them -- designers keep overlaps while drawing. Nonzero fill everywhere; a wrong-way counter is the Checks report's to fix (the verb refuses, export fills it as the renderer already did). Results snap to units. OVERLAP_SIMPLE stays set on TTF glyphs (harmless).
- M10b: WOFF2 wraps the TrueType build (as the web-font services ship); no glyf transform yet (version 3 is valid; the transform would shave another 10-25 %, a follow-on). Quality 11: slow-ish (~0.5 s on Termes), smallest file.
- M11: our own reader instead of FreeType (outlines would be FreeType's only; names, metrics, kerning classes are table parsing either way -- and no new package). Imports become a NEW unsaved document; the file on disk is never touched. Coordinates snap to units like every edit (TTF's 2/3-handles round within half a unit). Kerning: first-wins merge across subtables and lookups (noted when there is more than one lookup).
- M5h: tools are a toggle group in a panel; the view toggles live in the same panel under a rule (the top bar stays for glyph actions). Own icons rather than theme icons (availability). Measure and Hand became tools as well as holds.
- M5g: one undo step per node (not per path) -- a slip costs one Ctrl+Z. The pen draws open paths and closes them; it never adds to a closed contour (insert a node with a double-click instead). A new path starts Drafted on an Empty glyph. No auto-direction: the Checks report / Correct directions handle a path drawn clockwise.
- M5f: Delete keeps the shape by default (the refit), as Glyphs does; a "delete without refit" modifier can follow if wanted. Double-click a segment = add node (double-click a node still flips corner / smooth). L / C act on segments whose BOTH end nodes are picked. A contour left with fewer than two nodes is removed. Results snap to whole units like every edit.
- M9c: the code view shows what the FILES carry (production name noted beside yours; the CFF width written absolute for reading; the TTF after conversion), read-only. SVG export is outlines only (no <text>, no font, no transforms) -- the logo hand-off; em = 100 px by default (a setting can follow). Two menu items (glyph / Spacing text) rather than guessing from the front view.
- TTF (M10a): cu2qu at 1/1000 em (fontmake's default); contours all reversed (consistent, so nonzero fill is unchanged -- not the depth-based direction fix); OVERLAP_SIMPLE on every glyph's first point (overlaps kept until M10b); components decomposed (TrueType composites are a size follow-on); no hints, gasp = smooth at all sizes; loca short when the glyf fits; post 2 carries the production names. The Preview stays on the OTF.
- Thin strokes (tie accents, outline (C) / (R)) lose ~1 % area to the 1-unit tolerance -- the same as fontTools'; the checker compares against fontTools' conversion, not the cubic.
- Export OTF is Ctrl+E (free in the shortcut registry) with its own section in ☰; it always asks where (an "export again to the same file" can follow if you want one).
- The Preview sets the compiled font with Pango in a PRIVATE font map (fontconfig holding only that font, a fresh cache file per compile, stale files from dead runs removed); its page follows the desktop's light / dark, live, with a button for the other mode (setting `preview.invert`, replacing r1's `preview.dark`) -- the desktop's text stack, no new package; direct HarfBuzz stays for the strip, pending the package answer.
- Export never touches the font: no undo step (a product made from the font, not a choice on it).
- Outlines go out with **direction as drawn**: no silent direction correction (an overlapping contour can read as "inside" another and get reversed into a hole). Direction is the Checks report's job; CFF fills nonzero, as the editor shows. (Overlaps: removed on export since M10c.)
- Components are decomposed; coordinates rounded after the transform.
- Production names: an encoded glyph whose name doesn't spell its first code point by the AGL rules goes out as uniXXXX / uXXXXX (what fontmake does); unencoded glyphs keep their names. Termes: 419 renamed (anglearc, baht, f_f on U+FB00, Delta on U+0394 ...).
- No .notdef -> a box is supplied and noted.
- Vertical metrics, the Google Fonts scheme: typo = ascender / descender, typo line gap to 1.2 em, hhea = typo, USE_TYPO_METRICS on, win = drawn bbox. OS/2 unicode-range and code-page bits from the cmap, never hand-set.
- Kerning: PairPos format 1 first (every glyph pair an entry with a glyph side reaches, at `kern_lookup`'s value -- the strip's own lookup -- minus pairs the classes answer the same; a 0 exception stays), then format 2 for group / group. Subtables split under 64 KB; Extension lookup only when needed. DFLT + latn.
- No CFF hints, no BlueValues (none in the model; M10's ttfautohint covers TTF).
- head.created / modified honour SOURCE_DATE_EPOCH (reproducible builds); the selftest fixes the time.

## Working rules (carried verbatim from RULES.md -- DO NOT DELETE)

Standing rules for any session. Read at session open before acting.

Inherited from the Cairn seed at s00 and now Granjon's own; edit in place.

**⚠ DO NOT DELETE — carry forward verbatim into every handoff.**

## Come up for air

When I realize I'm taking up a lot of time on something — a complex
edit, a multi-step debug, a design decision that's branching — stop
and check in. Scott may have a suggestion that reforms the plan
before more time goes into the current path. This isn't between every
line of code; it's at the moments where a course correction would be
cheap if it's needed.

## Milestones are the checkpoint boundary

Having a compiler in the sandbox makes it easy to code continuously
and surface only at the end. That's the failure mode this rule exists
to stop. **Work stops at milestone boundaries, not when the budget
runs out.** A milestone is: a coherent unit of work, compiled, shipped
as a zip, reported. Then I stop and Scott decides what's next.

Not "I'll just add the next thing while I'm in here." The end of a
milestone is a full stop, and the cheapest possible moment for a
course correction.

## No questionnaires

No `ask_user_input_v0` popups. Ask clarifying questions in plain text
in chat.

## Acronyms

Scott uses acronyms freely. Keep track of the ones I learn. Current
list:

- **pls** — please
- **btw** — by the way
- **fmi** — for my information (look up the answer and tell him; not
  a question to discuss)
- **gtg** — good to go / continue (proceed with the work; not a sign-off)
- **lmd** — let me drive. Yes/no for now. Stop expanding and
  analyzing; respond with bare yes/no (or a sentence at most when
  asked for a reason) until Scott explicitly releases the mode.
- **crc** — compiles-runs-continue. Scott's report that the last ship
  compiled and ran; proceed to the next work without stopping for a
  green-light. **crc is always Scott's report from Scott's machine.**
  A green build in my sandbox is not crc and never substitutes for it.

If I see one I don't know, ask in plain text — don't guess.

## Two failures = drop to log mode

If the same edit / approach / fix fails two or more times, stop
trying variations. Switch to **log mode**: add `spdlog` instrumentation
to the code path that's failing, ship the change, Scott compiles and
runs, we read the log together. We use spdlog as our diagnostic tool
— load logs in to investigate, unload them when the bug is closed.
Never just keep retrying without diagnostics.

## Convergent evidence — traces and eyes must agree

A milestone is "done" only when both channels of evidence converge:
the empirical trace (what the code recorded) AND the visual
observation (what Scott saw). Neither channel dominates; neither one
validates by itself. When they disagree, the disagreement IS the
signal — the system is masking something from one of the channels,
and the gap is where the real bug lives.

Trace `ok` means "the verb dispatched cleanly," not "the user saw
the effect." A clean trace plus a "nothing happened" report is not
a conflict; it's complementary evidence of a masking layer between
the trace's last line and the user's eyes. Don't argue the trace
exonerates the code. Don't dismiss a visual report because the
trace looks clean. Investigate the gap.

The four cases:

| Trace | Visual | Status |
|---|---|---|
| Worked | Worked | Done. |
| Worked | Didn't work | Not done. Something masks. |
| Failed | Worked | Not done. Different bug; figure out why both are true. |
| Failed | Didn't work | Not done. Obvious. |

Only the first ships.

## I compile; Scott runs

**What I can do.** The sandbox installs the dev packages
(`libgtkmm-4.0-dev`, `libspdlog-dev`, cmake), so I can build.
**Shipping code I haven't compiled is not acceptable.** CANON's "an
un-run reference is worse than nothing" applies to me. If it didn't
build here, it doesn't ship.

**What I cannot do.** I have no display. I can compile, and I can run
the headless selftest, and that is the whole of what I can observe.
**A green build is the trace channel and nothing more.** The visual
channel is Scott's, permanently. "It compiles" and "the selftest
passes" are not "it works." Convergent evidence still governs "done,"
and I still can't reach one of the two channels.

**My toolchain is not Scott's toolchain.** Different distro (sandbox
is Ubuntu, Scott is Fedora), different package versions. Green here is
real evidence for Scott's machine but never proof. When I report a
build, I report what I built against.

**The environment is disposable.** The sandbox resets between tasks;
the install does not persist. Setup belongs in a checked-in script in
the repo, not in my head, and re-deriving it every session is a cost —
batch it, announce it, don't drip it.

### The budget — the anti-amok teeth

A compiler is a slot machine. Each fix-build-error cycle feels like
progress and none of it reaches Scott, so the limits are numbers, not
intentions:

- **Announce before any install or first configure.** One line, then
  go. Never a silent multi-minute operation.
- **Three consecutive failed builds and I stop and report.** Not four.
  I bring the actual compiler error, verbatim, not my paraphrase of
  it. (This nests under two-failures-drop-to-log-mode: if it's the
  same fix failing twice, instrument before the third try.)
- **The first green build is a checkpoint, not a launchpad.** Report
  it. Do not keep adding features because the loop is going well —
  that is exactly the amok pattern, and a good build is when a course
  correction is cheapest.
- **Compile what I just wrote.** Don't go implement more in order to
  have more to compile.
- **Never bend intent to satisfy the compiler.** If making it build
  requires a design change, stop and surface the fork. A build that
  went green by quietly becoming a different program is a failure
  wearing a success.

## Nothing lives only in the container

The container is disposable and resets between tasks, so anything I
built and didn't hand over evaporates with it. **A session that ends
with good code in `/home/claude` produced nothing.**

The test, at every checkpoint: *if this session died right now, what
does Scott have on disk?* If the answer is "nothing," ship before the
next edit.

- **Ship at the first green build, not at session end.** Zip it, put it
  in the outputs directory, present it, then continue. Delivery is a
  checkpoint that happens several times a session, not a closing
  ceremony.
- **Ship zips without `fixes/`** (Scott, s03). The zip I hand back
  holds `docs/`, `scripts/` and `granjon/` only -- old zips nested in
  `fixes/` get in the way when Scott moves the files into place.
- **Many small ships beat one big one.** Five deliverable increments
  that each compiled are worth more than one perfect tree I never
  handed over.
- **Write HANDOFF.md as I go.** It lives in outputs from the first
  checkpoint and gets updated at each one. Never composed at the end
  out of whatever budget is left — that is exactly when it doesn't get
  written.
- **Reserve the exit.** Stop *before* the tank is empty, with enough
  room to ship, write the handoff, and produce the push command. The
  last 20% of the budget belongs to landing, not to one more feature.
- **Don't burn the budget on reading.** `grep`/`sed` the section I
  need; never pull a whole CANON or a 900KB `json.hpp` into context to
  find one thing. Same economics in chat: long prose costs what code
  costs. Code goes in files; chat carries the narrative.

## Git pushes at end-of-session

When the session reaches its natural stop point — the same moment
I'd write the handoff — produce a single copy-paste command for the
git push. Scott runs the push; I produce the artifact. Use this
shape:

```bash
git add -A && git commit -F- << 'EOF' && git push
<commit subject line>

<commit body — narrate the session's headline work at arc-length,
then list other session work folded in as a secondary section if
the working tree had accumulated unpushed changes>
EOF
```

ASCII-only in the commit message body — Unicode (em dashes, arrows)
sometimes survives heredocs and sometimes doesn't depending on the
terminal. `--` for em dashes, `->` for arrows.

**If code and docs are separate repos**, ship a second copy-paste
push command for the docs repo, with its own commit message scoped to
that repo's content. The two histories serve different readers and
should narrate independently.

## Collaboration posture

I'm a collaborator, not an assistant. That posture means:

- Act on clear directives. Don't poll for permission on every
  implementation fork.
- Make calls on decisions that don't change the user-visible result
  or the architectural shape — the kind of decision an engineer
  pair-programming with Scott would make without stopping.
- Surface only the genuine forks: ones where Scott would prefer
  differently, or where I don't know his intent.
- Push back when I think something's a bad idea. Don't find ways
  to ask whether it's a bad idea.
- Own mistakes cleanly when they happen.

The line between "make the call" and "surface the fork" is "would
Scott prefer differently?" — not "could there be more than one
option?"

## Handoffs

- Markdown format.
- Name is always HANDOFF.md
- **Tagline format:** `s###_[description_with_underscores]`. Short
  and suggestive — describes what the session does, not paragraphs
  of context. Examples: `s003_tree_model`, `s007_dnd_reparent`.
- **Tagline points forward, not back.** When I write a HANDOFF.md I
  suggest the tagline for the NEXT session (`sN+1` — what it should
  tackle), not a restatement of the session that just finished.
- **The file OPENS with the next session's number and tagline, plus
  the files-to-upload block.** This metadata block goes at the very
  top, before any narrative. Not buried, not at the end. This is a
  standing requirement, not a nicety.
- Title line above it records the transition:
  `# Granjon handoff — sN -> sN+1`.
- The files-to-upload section names the *additional* uploads beyond
  the codebase zip (the reference docs — `RULES.md`, `HANDOFF.md`,
  `ARCHITECTURE.md`, `ARC.md`, `CANON.md` — plus any session-specific
  files).
- **The rules section above must be carried forward into every
  handoff verbatim, marked DO NOT DELETE.** Rules updates happen in
  conversation; the handoff captures the new state. Never trim or
  summarize the rules block.
- `RULES.md` also lives as a standalone reference doc alongside
  HANDOFF / ARCHITECTURE / ARC / CANON. The redundancy is
  intentional — rules ride in two places so losing one still keeps
  the rules alive.

## Undo from the first write

(Scott, s046.) Every user choice is an undoable event, and the undo
system ships with the model -- in the first milestone that writes
anything, not after the second surface exists. Every verb goes through
the one door (`Journal::run`), and the verb table the selftest walks
gains the verb in the same change that adds its button. See
`docs/undo.md` and CANON "Undo is foundation, not a feature".

## "Note." prefix — backlog without action

When a thought arises mid-session that's worth capturing but
shouldn't divert current work, Scott prefixes the message with
`Note.` That means *log this to the backlog and do NOT act on it
now.* Acknowledge briefly, note it in ARC.md's backlog, continue
current work.
