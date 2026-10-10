#pragma once
// reader::FontReader -- M11: an OpenType font (OTF / CFF or TTF / glyf) read
// into the model. GTK-free, no outside library: the same tables the export
// writes, read back (ARCHITECTURE §8 named FreeType here; s05 chose our own
// reader -- FreeType gives outlines only, and kerning classes, names and
// metrics would still be ours to parse).
//
//   outlines     CFF: a full Type 2 interpreter (subroutines, hint operators
//                skipped, flex), CID-keyed fonts included. TrueType: points to
//                cubic nodes -- quadratics promoted EXACTLY -- contours turned
//                to the model's direction; composites become components.
//                Nodes whose handles line up are marked Smooth.
//   names        name table (typographic family / subfamily first), copyright,
//                license, vendor, version
//   metrics      UPM; ascender / descender (OS/2 typo, else hhea); x-height /
//                cap height (OS/2, else measured on x / H); overshoot from O
//   cmap         format 4 / 12 / 6 / 0 -> each glyph's code points
//   glyph names  post 2 / CFF charset; else AGLFN from the cmap, else glyphNNNNN
//   kerning      GPOS 'kern' PairPos: glyph pairs -> pairs, class pairs ->
//                public.kern1 / kern2 groups + group pairs, subtable order
//                kept as UFO precedence; else the old 'kern' table
// Not read (named in the report): hints, other OpenType features, WOFF /
// WOFF2 (compressed), CFF2 / variable fonts, collections beyond font 0.
#include "model/Font.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace granjon::reader {

struct ImportReport {
    std::string format;              // "CFF" / "TrueType"
    int    glyphs = 0;
    int    contours = 0;
    int    components = 0;
    int    encoded = 0;
    int    kern_pairs = 0;           // entries in the model's kerning
    int    kern_groups = 0;
    double ms = 0;
    std::vector<std::string> notes;
};

// The font's bytes -> `into` (replacing what it held; not undoable, like an
// open). On failure `into` is untouched and `why` says what is wrong.
bool import_font(const std::vector<std::uint8_t>& data, model::Font& into, ImportReport* rep = nullptr,
                 std::string* why = nullptr);
bool import_font_file(const std::filesystem::path& path, model::Font& into, ImportReport* rep = nullptr,
                      std::string* why = nullptr);

}  // namespace granjon::reader
