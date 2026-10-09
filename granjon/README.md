# Granjon

A font editor for Linux, gtkmm 4 / C++23. Named for Robert Granjon
(1513–1590), punchcutter. It teaches as it corrects: every check says why it
matters, nothing changes silently, and every change is undoable.

Built from the Cairn seed. Design: `../docs/ARCHITECTURE.md`. Story so far:
`../docs/ARC.md`.

## Build

```
./build.sh
./build/granjon             # the app
./build/granjon_selftest    # the headless core gate (no display)
```

Deps (Fedora): `gtkmm4.0-devel spdlog-devel libxml2-devel cmake gcc-c++`.
Deps (Ubuntu/Debian): `libgtkmm-4.0-dev libspdlog-dev libxml2-dev cmake g++`.
The Unicode data needs no network at build time. To move to a newer Unicode
or char-set revision, fetch the upstream files (URLs in the script's header)
and run `../scripts/ucd_trim.py RAW_DIR`.

Later milestones add FreeType, HarfBuzz, Brotli and Clipper2.

## Layout

| Where | What |
|---|---|
| `include/model/`, `src/model/` | The font as data: `Font` (also the undo store), `Glyph`, `Node`/`Contour`/`Component`/`Anchor`, font records. GTK-free. |
| `include/command/`, `src/command/` | `Document` (font + journal) and every verb that writes the font, plus the verb table the selftest walks. GTK-free. |
| `include/io/`, `src/io/` | UFO 3 source format: plist, GLIF, the folder (save swaps in a complete new folder). libxml2. GTK-free. |
| `include/unicode/`, `src/unicode/` | Unicode 17.0 data compiled into the core: names, categories, decompositions, case (`UcdTable`), blocks, AGL / AGLFN glyph names (`Agl`), char sets (`CharSet`). GTK-free. |
| `resources/ucd/`, `resources/charsets/` | The bundled data, trimmed by `../scripts/ucd_trim.py` and checked in; `tools/embed.cpp` turns it into a C++ source at build time. |
| `include/core/` | Cairn's stones: the undo journal (`Undo.hpp`), shortcuts registry, recents, text map. GTK-free. |
| `Shell.hpp` + `src/Shell_*.cpp` | The window, split by category (zones / bindings / handlers / helpers); the header is the index. |
| `src/selftest.cpp` | The gate: every verb goes through the one door, undo restores, redo re-applies. |

The GTK-free code builds into `granjon_core`, which has no gtkmm on its link
line, so a UI include in the model fails to compile (`../docs/two-layer-seam.md`).
A future `granjon-cli` links the same library.

## Handing the tree to a session

```
../scripts/granjon_backup.sh -o s002_ufo_persist   # -> ../backups/s002_ufo_persist.zip
```
