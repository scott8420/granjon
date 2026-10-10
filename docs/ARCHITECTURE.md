# Granjon — Architecture

A gtkmm 4 / C++ font editor for Linux, named for Robert Granjon (1513–1590), punchcutter.
Built from the Cairn starter spine. Personal tool first, released openly, no maintenance obligation.

**Guiding idea:** a tool that *teaches as it corrects*, in the spirit of the old Fontographer manual.
Every check explains why it matters. Nothing changes silently. Every change is undoable.

---

## 1. Scope by phase

| Phase | Contents |
|---|---|
| **v1** | Project model, glyph editor (Curvz port), components + anchors, metrics, class kerning, Unicode/cmap, checks & reports, export OTF (CFF) + TTF + WOFF2, import TTF/OTF |
| **v2** | OpenType feature compiler (GSUB/GPOS: liga, kern, mark/mkmk), auto-spacing |
| **v3** | Variable fonts (multi-master sources, fvar/gvar/CFF2) |
| Later / maybe | Color fonts (COLR/CPAL, SVG), cmap 14 variation selectors |
| Import only / skip | Type 1 (PFB/PFA), BDF/PCF bitmap |
| Backlog | `granjon-cli`: headless external processing (info, check, fix, build, import/export) linking the same `granjon_core` |

Script scope for v1: Latin, Greek, Cyrillic. The model must not block Arabic, Indic, or CJK.

---

## 2. Structure (MVC)

- **Model:** the font as data. No GTK types anywhere in the model.
- **View:** overview grid, glyph canvas, spacing/kerning strip, preview pane, info panel, report list.
- **Controller:** commands. Every user action becomes a command object pushed to the undo stack.

### Undo (first-class, from session 1)
- Uses Cairn's `core::Journal` (`include/core/Undo.hpp`, `docs/undo.md`), not one command class per verb.
- A step records each touched record's value **before and after**, by ID. Undo puts back before, redo puts back after. No per-verb inverse to write.
- Store IDs: one per glyph (glyph name), plus font-level records (`fontinfo`, `metrics`, `kerning`, `groups`, `metricskeys`, `features`, `charsets`). The record's place (glyph order) lives in its value.
- One door: every write goes through `Journal::run` or a gesture (`begin`/`commit`). The selftest walks the verb table and fails on a stray write.
- A drag is a gesture = one step. Slider/field edits merge by key inside 1500 ms.
- Fix-all and auto-build are one `run` over many IDs = one undo step.
- Selection travels with the step.
- The journal belongs to the font document, not the window.

---

## 3. Model

```
Font
 ├─ FontInfo        names, version, license, vendor, unitsPerEm
 ├─ Metrics         ascender, descender, xHeight, capHeight, overshoot zones
 ├─ Glyphs          ordered map name → Glyph
 ├─ Kerning         classes (left/right) + pairs (glyph|class × glyph|class → value)
 ├─ MetricsKeys     rules: "n: L=R=60", "m=n", "b.L=l.L", "o.R=o.L"
 ├─ Features        raw feature text (v2 compiles it)
 └─ CharSets        coverage targets (Latin-1, Adobe Latin 2/3, WGL4, GF Latin Core)

Glyph
 ├─ name            designer name; production name derived at export
 ├─ unicodes        vector<char32_t> — zero, one, or many
 ├─ advance         width
 ├─ contours        vector<Contour>
 ├─ components      vector<Component>   { baseGlyph, transform }
 ├─ anchors         vector<Anchor>      { name, x, y }   e.g. top, _top
 ├─ status          empty | drafted | done | review
 └─ note

Contour   closed, ordered vector<Node>
Node      { x, y, in-handle, out-handle, type: smooth | cusp | symmetric | corner }   (Curvz BezierNode)
```

**A glyph is a drawing. A codepoint is a mapping.** These are never conflated.

### Coordinates
- Integer font units on a unitsPerEm grid (default 1000), **y-up from the baseline**.
- The flip to screen y-down happens only in the view transform.
- Outlines are cubic internally; quadratics exist only in TTF export.

### Curvz reuse (confirmed against s360 headers)
- Curvz `BezierNode` = anchor + in-handle + out-handle + type (Smooth/Cusp/Symmetric/Corner), doubles. `BezierPath` = `vector<BezierNode>` + `closed`, with hit-test, insert/delete node, move node/handle with type constraints, `reverse()`, `signed_area()`.
- Granjon keeps this anchor-with-handles shape in the model. It maps 1:1 to cubic GLIF: a handle equal to its anchor is absent (line segment); two handles = two off-curve points; `smooth` = type != Corner.
- GLIF's on/off-curve point list exists only at the I/O boundary (GlifReader/GlifWriter), as quadratics do only in TTF export.
- Curvz is y-down doc space; Granjon is y-up font units. The flip lives in the canvas view transform, and the `signed_area()` sign convention flips with it.
- Coordinates are double while editing and snapped to integer font units at gesture commit, so the stored model is always integral.
- Curvz pulls in `SceneNode.hpp` and cairomm from `BezierPath.hpp`; the port (s03) lifted `Vec2`, `CubicSegment` and BezierPath's editing rules into a GTK-free `geom/` that works on `model::Contour` directly (no BezierPath type -- the model already is its node list); the cairo drawing lives in `views/GlyphCanvas`.
- Curvz `BooleanOpsClipper` (Clipper2 1.5.4, FetchContent fallback) is the overlap-removal starting point.
- To add: components, anchors, and sidebearings as canvas objects; contour-direction check.

---

## 4. Persistence — native source format

UFO 3 folder, named `Name.granjon` (`Name.ufo` works the same): diffable, archivable, git-friendly, and readable by fontTools / fontmake (checked: fontTools' UFOReader with validation reads Granjon's output; Granjon reads fontTools' and re-saves the same point order).

```
MyFont.granjon/
 ├─ metainfo.plist          creator io.github.granjon.Granjon, formatVersion 3
 ├─ fontinfo.plist          names, version, copyright, license, vendor, UPM, vertical metrics
 ├─ groups.plist            kerning classes          (only when there are some)
 ├─ kerning.plist           first -> {second -> value}
 ├─ features.fea            verbatim                 (only when there is some)
 ├─ lib.plist               public.glyphOrder; granjon.overshoot, granjon.metricsKeys
 ├─ layercontents.plist     public.default -> glyphs
 └─ glyphs/
     ├─ contents.plist      glyph name -> file name (UFO 3 user-name rule)
     └─ *.glif              GLIF 2; status in the glyph lib (granjon.status)
```

- **Node <-> GLIF:** a closed contour starts at node 0 (on-curve); the closing segment's off-curves come last, so start points survive. Curve segments = two off-curves + `curve`; `smooth="yes"` for Smooth / Symmetric; Cusp / Symmetric ride in `granjon.nodeTypes` only when present. Reading also takes `qcurve` (quadratics promoted to cubic exactly) and contours that begin on off-curves.
- **Deterministic output:** sorted keys, fixed file names, shortest-round-trip numbers; save -> load -> save is byte-identical (selftest).
- **Safe save:** a complete folder is written beside the target (`.saving`) and swapped in; a failed save leaves the old font; deleted glyphs leave no stale `.glif`.
- **Safe load:** everything is read into fresh records first; only then the open font is replaced (under `Font::Loading`, not undoable). A bad folder leaves the open font untouched and says what's wrong.
- **Modified:** `Journal::stamp()` names the state; the document remembers the stamp it saved / loaded at. Undo back to it is unmodified again; a merge into the saved step is modified.

---

## 5. Unicode subsystem

- **Data (M3, s02):** one source of truth, compiled into `granjon_core`: a trimmed UCD 17.0 (`resources/ucd/ucd.txt` -- name, general category, decomposition, simple upper / lower; algorithmic ranges for CJK / Hangul / Tangut), `blocks.txt`, the AGL and AGLFN, and the char sets. `scripts/ucd_trim.py` regenerates them from upstream (checked in, no network at build); `tools/embed.cpp` turns them into `granjon-data.cpp` at build time. Category and case come from the same table rather than GLib, so the GTK-free core (selftest, granjon-cli) sees exactly what the app sees. Script (sc) is not bundled yet -- add when grouping by script is wanted.
- **Modules:** `unicode/UcdTable` (name_of, category, decomposition, upper / lower / other_case, find_by_name), `unicode/Blocks` (block_of, sections in code point order), `unicode/Agl` (nice_name, codepoints_for_name), `unicode/CharSet` (Latin-1 191, GF Latin Core 319, Adobe Latin 2 250 / 3 331, WGL4 651 -- less its two PUA points), `model/Coverage` (mapped, missing_from, glyphs_for).
- **Uses:**
  - Overview grouping by block or script.
  - Search by character name ("cedilla").
  - Case navigation (A ↔ a).
- **Glyph names:** AGL conventions (`A`, `eacute`, `uni0416`, `u1F600`). Production names are generated on export.
- **Composites:** the UCD decomposition plus anchors auto-build components. For example, é is built from `e` and `acutecomb`, aligned anchor `top` to anchor `_top`. The same anchors later feed GPOS mark/mkmk generation.
- **Coverage:** char-set definition files are shown as checklists with a missing count. The overview grid highlights gaps.
- **Preview input:** UTF-8, plus `/glyphname` escapes for unencoded glyphs.

---

## 6. Checks & corrections

Every check produces a **report list** with a "why this matters" note. Each item offers fix, skip, or fix-all, and all fixes go through undo.

**Origin & alignment**
- Set origin by reference (extreme, point, or anchor) to a chosen LSB, for one glyph or a selection.
- Zone snap: points within tolerance of baseline, x-height, cap height, ascender, or descender (overshoot zone for rounds) are flagged, then snapped.
- Overshoot check: missing or inconsistent overshoot on round glyphs.

**Outlines**
- Contour direction (CFF wants outer contours counterclockwise; TT reverses on export).
- Open contours, missing extreme points, overlaps.

**Metrics**
- Metrics keys propagate: edit one glyph and its dependents update.
- Consistency report: mismatched sidebearings across related glyphs, stem-width drift.

**Kerning**
- Class audit: glyphs missing from their expected class; exceptions that contradict their class.
- Outliers: extreme values, and kerns that fight the spacing (these usually point to bad sidebearings).
- Collision check on shaped proof strings.
- Built-in proof strings (`HOHOH nonon` control strings, standard pair lists).

### Profiles & tangent kerning
- Per-glyph **edge profile**: the leftmost and rightmost x at sampled y, cached and invalidated on edit.
- For each pair, two readouts:
  - **Contact gap** (tangent / closest approach), drawn as a labeled line between the touching points.
  - **Optical area**: whitespace between profiles within the x-height or cap band, with deep overhangs clamped.
- **Kern-to-gap:** a target gap per font or class produces a proposed kern value.
- **Disagreement flag:** a sharp split between the two readouts is flagged. Contact gap alone misjudges open shapes like "To" and "LT".

---

## 7. Views

Window layout (M1b, Mac/web roots -- the Curvz house look):

```
┌ logo  undo|redo │ [Overview|Editor|Spacing|Preview|Report] │ [find glyph…] [◧|◨] ☰ ┐
├──────────────┬──────────────────────────────────────────┬────────────────────────────┤
│ Navigator    │  the current view                        │ Inspector                  │
│              │                                          │  glyph selected: cards --  │
│  FONT        │                                          │  Glyph (preview, name,     │
│  BASIC LATIN │                                          │  status), Metrics (width,  │
│   ● H  U+48  │                                          │  LSB/RSB), Unicode, Note   │
│  ...         │                                          │  font selected: Names,     │
│  [+][−]  7   │                                          │  Grid, Vertical metrics    │
├──────────────┴──────────────────────────────────────────┴────────────────────────────┤
│ status strip (refusals in the warning colour)                                        │
└──────────────────────────────────────────────────────────────────────────────────────┘
```

- **One selection** (`views::ViewContext`): a glyph name or "" for the font, shared by every view. Undo hands it back.
- **View switcher:** a segmented pill in the header, radio targets of `win.view` (Ctrl+1..5). Panel toggles are one linked pair [◧|◨] (navigator | inspector), as Folio / jot: F9, Ctrl+I.
- **Find glyph** (M1c, header, Ctrl+F / Ctrl+K): one field takes a name, the character, U+00E9 / 0xE9 / 00E9, or /name (`model::find_glyphs`, ranked). While typing, the navigator and overview narrow to the hits (`ViewContext::filter`) and a dropdown shows the top eight; Enter selects, Escape clears. M3: also an AGL name (`eacute` finds the glyph mapping é whatever it is called) and a Unicode name, 3+ letters ("cedilla", "cyrillic zhe": every word starts a word of the name). Characters asked for that no glyph maps come back as up to four "Create é as eacute" rows (the add verb). New glyphs are named AGLFN-first, else uniXXXX / uXXXXX.
- **Case navigation** (M3): Alt+C, or the inspector's Unicode card: A <-> a by code point; unencoded suffixed glyphs go to their partner (a.sc -> A.sc). The Unicode card also shows the character's name and category.
- **Styling:** `resources/granjon.css` -- Curvz's tokens (dark default, `window.granjon-light` motif following the desktop) and the rules; user overrides in `~/.config/granjon/styles.css`. Needs GTK >= 4.16 (CSS `var()`).

Views:
1. **Overview (M3):** glyph cards (drawing, name, codepoint), status-tinted, grouped by Unicode block under section headers (code point order within a block; Unencoded last). Toolbar: **Coverage** menu (the char sets as a checklist: have / total, missing count, tick on the chosen one), **Create N missing** (`add_glyphs`, ONE undo step), cell-size slider (40-200 px; captions hide below 56). The chosen set's gaps are dashed cells showing the character; click says what it is, double-click creates it. Gaps hide while the search narrows. Click selects; double-click opens the editor.
2. **Editor (M4):** `views/GlyphCanvas`. Metric guides with overshoot zones; sidebearing lines (drag the right one for width; the left one moves the origin with the drawing fixed, Alt slides the drawing in a fixed width); fixed context glyphs ghosted (H beside capitals / figures, n beside the rest); components dashed, read-only; outline shaded or not (Fill toggle); Curvz node shapes; anchors named. Pick / marquee / drag (one step, snapped on release) / nudge; Ctrl+scroll zoom, scroll / middle / Space pan. Editing geometry is GTK-free in `geom/` (Curvz's Vec2, CubicSegment, BezierPath rules on `model::Contour`).
3. **Spacing / kerning:** a text strip laid out from the model (advances + class kerning -- the same lookup the compiled GPOS is built from; HarfBuzz in the strip itself still to come). Click a glyph to select, a join to pick the pair (drag / arrows kern it, Alt = exception), drag the band under a glyph for its sidebearings; metrics keys marked when off. Profile readouts arrive in M8. (M6, M8)
4. **Preview (M9):** `views/PreviewPane`. The font compiled (the bytes Export writes) and set by Pango in a private font map whose fontconfig holds only that font -- the desktop's own text stack, GPOS kerning included. Waterfall 10-96 px of a sample (remembered), two paragraphs, Kerning on / off; the page follows the desktop's light / dark, with a button for the other mode; recompiles a short pause after a step while shown. Missing characters counted in the bar.
5. **Report:** results from all checks, with click-to-navigate. (M7)

Panels (always present, collapsible): **Navigator** (source list by Unicode block) and **Inspector** (what was the Info view, plus the glyph cards).

---

## 8. Import / export pipeline

| Direction | Path |
|---|---|
| Import TTF/OTF | FreeType reads outlines → decompile into model (quadratics promoted to cubic) |
| Import SVG | single glyph; flatten arcs/quads/relative/transforms → cubic; flip y |
| Export OTF | model → CFF Type 2 charstrings (1:1 cubic) + sfnt tables |
| Export TTF | model → cubic→quadratic (error tolerance) → glyf/loca + sfnt tables |
| Export WOFF2 | compressed wrapper over OTF/TTF |
| Export SVG | a glyph or the Spacing text as plain outline paths (M9c) -- the logo hand-off, and the Curvz round-trip |

### Pre-export steps
- Decompose or keep components per the format.
- Remove overlaps (boolean path operations).
- Correct contour direction. (M9 decision: OTF goes out **as drawn** -- no silent correction, since an overlapping contour can test as "inside" and be reversed into a hole; direction is the Checks report's job, and CFF fills nonzero as the editor shows. Revisit with overlap removal, M10.)
- Generate production names.
- Compute OS/2 `ulUnicodeRange` and code page bits from actual coverage. These are never hand-set.

### sfnt writer
There is no C++ fontTools, so we write our own table writers:
- `head`, `hhea`, `maxp`, `OS/2`, `name`, `cmap` (formats 4 and 12), `post`, `hmtx`, `CFF ` or `glyf`/`loca`, and `kern`/`GPOS` (pair kerning).
- Validation runs against the output (FreeType reload; external `ots-sanitize` / fontbakery if available).

### Hinting
No manual hinting. `ttfautohint` runs as an optional external step on TTF.

---

## 9. Dependencies (to confirm on Fedora)

- gtkmm 4, C++23 (match Cairn)
- FreeType (import, rendering checks) -- not linked yet
- HarfBuzz (shaping for the kerning strip) -- not linked yet
- Pango + fontconfig (M9 Preview: the compiled font in a private font map) -- come with gtkmm, no extra package
- libxml2 (plist / GLIF) -- chosen over libxml++ (small files, no extra C++ wrapper)   [M2]
- Clipper2 1.5.4 (as Curvz: `find_package`, FetchContent fallback) for boolean ops
- Brotli (WOFF2)
- Optional external tools: ttfautohint, ots-sanitize

---

## 10. Project layout

```
granjon/                        project root (not a git repo)
 ├─ backups/                    session zips; previous ones retire to Old/
 │   └─ Old/
 ├─ docs/                       RULES  CANON  ARCHITECTURE  ARC  HANDOFF
 │   │                          + Cairn paradigm pages (undo, two-layer-seam, ...)
 │   └─ cairn/                  Cairn's own ARCHITECTURE, for the M1 rename
 ├─ fixes/                      drop-in fix files between sessions
 ├─ scripts/                    granjon_backup.sh, ucd_trim.py
 └─ granjon/                    the code (git repo)
     ├─ include/        model/  command/  core/  widgets/
     ├─ src/            model/  command/  core/  Shell_*.cpp
     ├─ resources/              ucd/ (trimmed UCD, AGL), charsets/, css, icons
     ├─ tools/                  embed.cpp (data -> C++ at build time)
     ├─ third_party/
     ├─ build.sh
     ├─ CMakeLists.txt
     └─ README.md
```

### Source modules (initial)
```
model/      Profiles [M8] (edge profiles, contact / optical gap, kern-to-gap), Checks [M7] (six checks, fixes, skip keys), Spacing [M6] (strip text, kerning lookup, layout, metrics keys), Composite [M5], Find [M1c], Bounds [M1b], Font (= undo store), Glyph, Outline (Node, Contour, Component, Anchor), FontRecords (FontInfo, Metrics, Kerning, MetricsKeys, Features)   [M1: built]
command/    Document (font + journal), Verbs + verb table   [M1: built]
io/         Plist, Glif, Ufo (save_ufo / load_ufo)   [M2: built]
unicode/    Embedded, UcdTable, Blocks, Agl, CharSet   [M3: built]  (model/Coverage)
geom/       Vec2, CubicSegment, Edit (Curvz node rules, hit-test)   [M4: built]; Profile, BooleanOps, CubicToQuad, Direction to come
core/       Settings (app preferences, JSON, not undoable)   [M4b: built]
views/      ViewContext, Navigator, Inspector, OverviewGrid, GlyphPaint, GlyphSearch [M1b/c], GlyphCanvas [M4], SpacingStrip [M6], ReportList [M7], PreviewPane [M9]
check/      Check, Report, concrete checks
export/     Sfnt (writer, assembly), OtfData (generated spec tables), Cff, Tables (head hhea maxp OS/2 name cmap post hmtx), Gpos (kern), Otf (prepare, compile, export)   [M9: built]; Quad (cu2qu), Ttf (glyf loca maxp1 post2 gasp)   [M10a: built]; Codes (drawing-code listings, SVG)   [M9c: built]; WOFF2 to come
import/     FtImporter, SvgImporter
```

---

## 11. Milestones (checkpoint breaks)

Work stops at each milestone for a compile/run report before continuing.

1. **M1 Spine:** Cairn → Granjon rename, model classes, FontStore + Journal, empty main window with view stubs.
1c. **M1c Find glyph:** header search, filter, create-missing.
1b. **M1b UI shell:** three-pane layout, segmented view switcher, navigator, inspector cards, overview cards, house CSS.
2. **M2 Persist:** UFO read/write round-trip (save, reload, byte-compare).   [s01: done]
3. **M3 Overview:** glyph grid, Unicode data, char-set coverage.   [s02: done]
4. **M4 Editor:** Curvz canvas port with metric guides and sidebearings.   [s03: done]
5. **M5 Components & anchors:** auto-build composites from decomposition.   [s03: done]
5b. **M5b Precision:** node types, handle fields, Shift / Alt / Ctrl drags, adaptive grid, snapping.   [s03: done]
5c. **M5c Guides:** rulers, glyph + font guidelines, snap to guides, align / distribute, measure.   [s03: done]
5d. **M5d Direction:** contour arrows, wrong-way tint, correct / reverse.   [s03: done]
5e. **M5e Node navigation:** amber start node, open-end bar, Make first, Tab / Ctrl+Tab / ` traversal.   [s04: done]
6. **M6 Spacing:** spacing strip (own layout; HarfBuzz with export), metrics keys, class kerning.   [s04: done]
7. **M7 Checks:** report framework plus first checks (origin, zones, overshoot, direction).   [s04: done]
8. **M8 Profiles:** contact gap, optical area, kern-to-gap.   [s04: built, awaiting crc]
9. **M9 Export OTF:** sfnt writer + CFF; validate by FreeType reload.   [s05: done]
9c. **M9c Drawing codes + SVG:** inspector CODE card (CFF / TrueType / SVG path), Export SVG of a glyph or the Spacing text.   [s05: built, awaiting crc]
10. **M10 Export TTF / WOFF2:** cubic → quadratic, overlap removal, ttfautohint hook.   [s05: M10a TTF done; M10b WOFF2 + overlaps to come]
11. **M11 Import:** TTF/OTF via FreeType; SVG glyph import.
