# Granjon — ARC

The running story of the project, session by session, plus the backlog.
Newest session at the bottom.

---

## Backlog

(`Note.` items land here.)

- **granjon-cli** (Scott, s01): a command-line tool for external processing on fonts. Links `granjon_core` (GTK-free), so it shares the model, verbs, checks and exporters. Open question: batch processing of font files, driving outside tools (ttfautohint, fontmake), or both.

- **Editor follow-ons** (s03, mine -- not acted on): pen tool, insert / delete node, change node type, move / decompose components, add / rename anchors; per-glyph remembered zoom; context string you can type (beyond the fixed H / n); a coordinates card in the inspector for the picked point; hover highlight on nodes; Shift-constrained drags (horizontal / vertical / 45 degrees).

- **Overview follow-ons** (s02, mine -- not acted on): ~~remember the coverage set and cell size~~ (M4b: app settings); user char sets from `~/.config/granjon/charsets/*.txt` (same file shape as the bundled ones); scroll the overview to a glyph picked elsewhere; file suffixed glyphs (a.sc) beside their base instead of under Unencoded; GF Latin Core's unencoded members (.notdef, periodcentered.loclCAT ...) as a name checklist; group by script (needs Scripts.txt in the trim).

---

## s00 — planning

- Chose the project: a font editor, named for Robert Granjon (1513–1590), punchcutter.
- Decisions: cubic internal model; UFO-style GLIF source; OTF/TTF/WOFF2 export via our own sfnt writer; FreeType import, HarfBuzz shaping; SVG single-glyph only.
- Distinctive features: teach-as-you-correct checks, metrics keys, class-kerning audit, profile-based contact gap / optical area.
- Full design: `ARCHITECTURE.md`.

## s01 — spine (M1)

- Project laid out: `granjon/{backups/Old,docs,fixes,scripts,granjon}`; Cairn code seeded into `granjon/granjon/` (not yet renamed).
- Docs: RULES retitled for Granjon; CANON and the Cairn paradigm pages carried verbatim; Cairn's ARCHITECTURE kept in `docs/cairn/` for the rename.
- `scripts/granjon_backup.sh` — allowlist-based, paths from the script's location, previous zips retire to `backups/Old/`.
- Undo: adopted Cairn's `core::Journal` (before/after values by ID) over a command-class-per-verb stack. ARCHITECTURE §2 updated.
- Curvz confirmed (s360 headers): `BezierNode` = anchor + in/out handles + type; `BezierPath` = nodes + closed. Granjon keeps that shape; GLIF point lists only at I/O. ARCHITECTURE §3 updated.
- Boolean ops: Clipper2 1.5.4, as Curvz.
- M1 built:
  - Rename Cairn -> Granjon end to end (namespace, targets, app id `io.github.granjon.Granjon`, gresource, strings). New symbolic logo: a punch over its impression.
  - `model/`: `Node` (Curvz BezierNode shape), `Contour`, `Component`, `Anchor`, `Glyph` (snap to integer units, AGL name check), `FontInfo`, `Metrics`, `Kerning`, `MetricsKeys`, `Features`, and `Font` -- the data and the journal's store (`@info` ... ids, glyphs by name, order in the value).
  - `command/`: `Document` (font + journal, journal owned by the document), 13 verbs: add / remove / rename glyph, unicodes, status, note, advance, outline drag (gesture, snapped on release), family, style, UPM, metric, kern. Refusals say why.
  - Rename carries components, kerning pairs and group members, and metrics keys in one step. Delete refuses a glyph used as a component.
  - Window: header Undo/Redo, six views (Overview list, Glyph Editor, Spacing, Preview, Info, Report -- stubs except Overview and Info), status line. Info edits go through verbs, so Ctrl+Z works on them.
  - Selftest 164 pass / 0 fail: the verb-table gate (no stray, one step, undo restores, redo re-applies) for all 13 verbs, plus rename / refusal / drag / merge cases.
  - Cairn's `Stones` kept as the journal's own fixture, compiled into the selftest only.
  - Cairn bug fixed in passing: recents loaded before its file path was set (always read "").
- M1b (UI shell, Scott: "metadata panel right, navigator left, views as toggle buttons on the headerbar"; "our modern look has its roots in web and mac motifs"):
  - Three panes: Navigator (source list grouped by Unicode block, search, status dots, +/- through verbs) | view stack | Inspector (rounded cards; font cards when nothing is selected -- the M1 Info view moved here). Both panels collapsible (F9, Ctrl+I).
  - Header as a unified toolbar: logo, navigator toggle, linked undo/redo, a segmented view switcher (`win.view`, Ctrl+1..5), inspector toggle, menu.
  - Overview: glyph cards drawn from the model (`views/GlyphPaint`), double-click -> editor.
  - `resources/granjon.css`: Curvz's tokens verbatim (+ `--ok-fg`), `window.granjon-light` following the desktop, user `~/.config/granjon/styles.css` seeded with a stub. Needs GTK >= 4.16 for `var()`; the sandbox has 4.14, so it was eyeballed through a flattened copy.
  - Core: `model/Bounds` (exact cubic extrema, components through the font) for LSB/RSB; `unicode/Blocks` (hand table until M3's UCD); codepoint parsing (U+0041 / 0041 / the character).
  - Found and fixed: a GTK4 ToggleButton's CSS node is `button.toggle`, not `togglebutton`; a ListBox ignores `set_header` without a header func; a SpinButton's inner text eats Ctrl+Z even with its undo off -- a CAPTURE-phase controller hands Ctrl+Z to the journal when focus is in a spin (verified by scripted keypress: two + presses, one Ctrl+Z, back to 700).
  - `GRANJON_SAMPLE=1` opens the fixture font, for looking at until M2 opens real ones.
  - Selftest 175 / 0.
- M1c (Scott: "an easy search for glyph on the headerbar ... name, char, unicode"; "I felt it was hard to find a glyph"):
  - One search, in the header (Ctrl+F / Ctrl+K); the navigator's own field removed.
  - `model::find_glyphs`: exact name (exact case first), codepoint (the character, U+, 0x, 4+ hex digits), prefix, substring, partial hex; `/name` exact. A codepoint nothing maps -> `missing` + a suggested name (the letter itself, else uniXXXX, made unique).
  - Typing narrows navigator + overview; dropdown of eight hits with drawings; arrows move, Enter selects, Escape clears; "Create é as uni00E9" goes through add_glyph.
  - Found and fixed: a shown popover kept its old height when the list shrank (pop down and up on rebuild); after Enter the keyboard stayed in the search field so Ctrl+Z undid its text -- clear() now unsets the window focus. Verified by scripted keys: type U+00E9, Down, Enter (created), Ctrl+Z (removed, "Undid: Add glyph").
  - Selftest 190 / 0.
- Logo: Scott's own `granjon-logo-symbolic.svg` (a G on a tilted slug with a hedera) replaces the punch. Cleaned for symbolic use: root `color="black"` removed (it would pin the mark black on the dark motif), zero-width strokes dropped. Rendered and checked at 16 / 32 / 128 px and in the header.
- M2 (persist), on Scott's gtg in the same session:
  - `io/Plist` (deterministic writer, libxml2 reader), `io/Glif` (GLIF 2 both ways; Node <-> point list; qcurve promotion; Cusp / Symmetric in the glyph lib), `io/Ufo` (UFO 3 folder; swap-in save; read-everything-then-replace load; UFO 3 file-name rule).
  - `Journal::stamp()` (core/Undo.hpp): a unique id per state, so "modified" survives undo / redo / merge. `Document::new_font / open / save / modified`.
  - File menu: New (Ctrl+N), Open (Ctrl+O, a folder), Open recent (now fonts), Save (Ctrl+S), Save as (Ctrl+Shift+S). Anything that would drop unsaved changes asks Save / Discard / Cancel -- including the window's X and Ctrl+Q. Title carries a dot while modified. "Remember a folder" removed.
  - Cross-checked against fontTools 4.65: UFOReader(validate=True) reads every Granjon file; a fontTools-written UFO (curves, a qcurve, kerning, glyph order) loads and re-saves with the same point order.
  - Scripted UI run: Ctrl+S -> save dialog (initial name Untitled-Regular.granjon) -> saved; edit -> Ctrl+Q -> Save / Discard / Cancel; Cancel, Ctrl+Z -> unmodified.
  - Selftest 257 / 0.
- Panel toggles combined into one linked widget [navigator | inspector] at the header's end, as in Folio and jot (Scott). Checked headless: Ctrl+I unchecks the right half.

## s02 — overview + Unicode (M3)

- M2 run report not received at session open; took the s02 upload as go-ahead.
- Unicode data, one source of truth, compiled into `granjon_core`: `scripts/ucd_trim.py` trims UCD 17.0 (`UnicodeData.txt`, `Blocks.txt` from unicode.org) to `resources/ucd/ucd.txt` (name, category, decomposition, simple case; 40,535 named code points + 20 algorithmic ranges) and copies the AGL / AGLFN (adobe-type-tools, BSD-3). `tools/embed.cpp` (a host tool CMake builds first) turns the data into `granjon-data.cpp`. Category and case come from this table, not GLib, so the GTK-free core needs no new dependency and the selftest sees what the app sees.
- `unicode/UcdTable`: name_of (Hangul names composed; controls take their Unicode 1 name), category, decomposition (canonical vs compat), upper / lower / other_case, find_by_name (every query word starts a word of the name; exact name first, then fewest extra words). `unicode/Blocks` now reads the UCD's 300+ blocks (the M1b hand table is gone; sections sort in code point order). `unicode/Agl`: nice_name (AGLFN, else uniXXXX / uXXXXX), codepoints_for_name (the AGL spec: suffix, ligature, uni / u forms).
- Char sets (`resources/charsets/`): Latin-1 191, GF Latin Core 319 (googlefonts/glyphsets, encoded members), Adobe Latin 2 250 / 3 331 (adobe-type-tools, MIT), WGL4 651 (from alanwood.net's table of the WGL4.0 spec, less its two Private Use points). `model/Coverage`: mapped, missing_from, glyphs_for (AGL-named, unique).
- Verb `add_glyphs`: many glyphs as ONE step ("Create 186 missing (Latin-1)"); refuses the whole batch on a bad / taken name. In the verb table.
- Find: AGL names ("eacute" finds the glyph mapping é; offers to create it under that name), Unicode names ("cedilla", "cyrillic zhe"; 3+ letters). `FindResult::create` replaces missing / suggested: up to four "Create é as eacute" rows. New glyphs are named AGLFN-first (`name_for_codepoint`).
- Overview rebuilt: toolbar (Coverage checklist menu, Create N missing, cell-size slider), block sections with counts and "N missing" badges, dashed gap cells showing the character (click: its name in the status line; double-click: create). One selection across the section boxes.
- Case navigation: `model::other_case_glyph` (code point, or the AGL reading for unencoded names: a.sc -> A.sc), Alt+C (`win.other-case`, in the shortcuts registry) and a button on the inspector's Unicode card, which also shows Name and Category now.
- Looked at under Xvfb with the CSS flattened (sandbox GTK 4.14 has no `var()`): sections, gap cells and toolbar render; Latin-1 on the sample font reads 186 missing.
- Selftest 301 / 0.

## s03 — editor canvas (M4)

- M2 + M3 confirmed by Scott at session open (compile + run on Fedora).
- Forks settled before code (Scott): context glyphs are FIXED (H beside capitals / figures, n beside the rest), not glyph-order neighbours; dragging the left sidebearing moves the ORIGIN with the drawing fixed on screen, a modifier (Alt) slides the drawing instead; a toolbar toggle for shading.
- `geom/` (GTK-free, in `granjon_core`): Curvz's `Vec2`, `CubicSegment` (at / tangent / nearest_t / split / bbox) and BezierPath's editing rules lifted straight onto `model::Contour` -- `move_node`, `move_handle_in/out` (Smooth keeps the other handle collinear, Symmetric mirrors, Cusp is length-only), `recompute_cusp_handles`, hit-test (nodes > handles > anchors > segments, Curvz's priority), marquee, `move_points` (always press + delta), `shift_x`.
- Verbs: `drag_points`, `drag_sidebearing` (Right = width; Left = origin moves, RSB holds; Left + modifier = drawing slides in a fixed width), `nudge` (merges per glyph). All three in the verb table, so the gate walks them. `drag_translate` stays (the selftest's whole-glyph drag).
- `views/GlyphCanvas` replaces the Editor stub: guides with overshoot zones and labels, sidebearing lines with LSB / width / RSB, context glyphs ghosted, components dashed and tinted (read-only), outline shaded or not, nodes and handles in Curvz's shapes and palette (corner square, smooth hollow circle, symmetric filled circle, cusp triangle, first node a diamond), anchors as red diamonds with names, marquee. y-up flip only in the canvas transform.
- Input: click / Shift+click pick, segment click picks its ends, marquee, drag = one step snapped on release, Escape cancels mid-drag, arrows nudge 1 / 10, Ctrl+A, Ctrl+0 fit, Ctrl+= / Ctrl+-, Ctrl+scroll zooms to the pointer, scroll / middle-drag / Space+drag pan. The status line reads the pick (type + coordinates). Editor shortcuts listed in the shortcuts registry (doc-only rows).
- Shell: mid-drag only the canvas repaints; the overview (which rebuilds every cell) and the panels catch up on the repaint after release.
- Looked at under Xvfb (CSS flattened; sandbox GTK 4.14): the fixture's boxes and a hand-made curved UFO (o with counter + anchor, a two-component glyph) draw right; scripted drags checked -- corner node drag, left-sidebearing drag (origin moved, drawing stayed, width 760 -> 674), Shift+arrow nudge, smooth out-handle drag (in-handle stays collinear), Ctrl+Z / Ctrl+Shift+Z.
- Test font (Scott: "load a base font like Times"): TeX Gyre Termes Regular (Times clone, CFF so already cubic, GUST Font License) converted with ufo-extractor, then loaded and re-saved by Granjon -- `Termes-Regular.granjon`, 1090 glyphs, 25,549 nodes, round-trip identical, loads in 0.27 s. No anchors or components (CFF flattens them).
- Fixed in passing: the zoom label stayed blank when fit() ran inside the first draw (a label set mid-snapshot does not relayout) -- now set on the next idle.
- Selftest 350 / 0.
- M4: crc from Scott (Fedora), looked at on Termes ("very slick").
- M4b preferences (Scott: "should we start a prefs dialog for what will obviously be many custom settings?"):
  - `core/Settings`: one typed struct, one JSON file (`~/.config/granjon/settings.json`), the Recents pump shape. Tolerant load: missing file / key, wrong type, out-of-range value -> that field's default; unknown keys dropped on save; written beside then renamed. Not undoable -- app state, not the font's.
  - Settings now: reopen the last font at launch (default on; `GRANJON_SAMPLE=1` still wins), last font, Open dialog's folder (also Save as for an untitled font), editor Fill / Context, the two context glyphs (a name or a character), the slide-the-drawing modifier (Alt / Ctrl / Shift), overview cell size and coverage set.
  - The views' own controls ARE preferences: the editor's Fill / Context, the overview slider and Coverage menu write through. `ViewContext` carries `settings`, `save_settings()` and `settings_changed`; listeners re-apply only what differs, so the round trip settles at once.
  - `PreferencesWindow` (Ctrl+, and the menu): General / Editor / Overview cards in the inspector's style, write-through (no Apply), Reset to defaults (keeps last font and folder), the file's path shown. Hide-on-close singleton; follows the window's light / dark motif.
  - Checked headless: open Termes -> quit -> relaunch reopened it; Preferences changes moved the overview's slider live and landed in the file.
  - Selftest 360 / 0 (settings: defaults, round-trip, no temp left, wrong type / bad modifier / empty context / out-of-range cell / garbage each fall back).
