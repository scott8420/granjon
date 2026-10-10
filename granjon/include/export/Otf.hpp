#pragma once
// otf::Otf -- M9: the font compiled to an OpenType (CFF) file; M10: TrueType. GTK-free; the
// app's Export and the Preview call it, and so will granjon-cli.
//
//   compile_otf   model -> bytes. Read-only on the font: export never edits
//                 the source (no undo step; it is not a user choice on the
//                 font, it is a product made from it).
//   export_otf    compile + safe write (a temp file beside the target, then
//                 renamed over it: a failed export leaves the old file).
//
// What goes in (ARCHITECTURE §8): head hhea maxp OS/2 name cmap post hmtx
// CFF, and GPOS 'kern' when the font kerns. Components are decomposed,
// outlines go out as drawn (direction, overlaps: the Checks report's job --
// overlap removal arrives with M10), coordinates rounded to units.
#include "export/Sfnt.hpp"
#include "model/Font.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace granjon::otf {

struct OtfOptions {
    // head.created / modified, Unix seconds. nullopt: SOURCE_DATE_EPOCH when
    // set (reproducible builds), else now.
    std::optional<std::int64_t> time;
    // Replaces the family name in every name record and the PostScript name
    // (the Preview compiles under a private name so the system's own copy of
    // the family -- or an older preview -- never answers for it).
    std::string family_override;
};

struct OtfReport {
    int         glyphs = 0;
    int         encoded = 0;             // code points in cmap
    bool        made_notdef = false;     // the font had none; a box was supplied
    int         renamed = 0;             // production names that differ from the designer's
    int         decomposed = 0;          // glyphs whose components were flattened
    int         open_contours = 0;       // written closed (CFF closes every path)
    int         kern_glyph_pairs = 0;    // PairPos format 1 entries (exceptions, glyph/group)
    int         kern_class_pairs = 0;    // non-zero cells in the class matrices
    int         gpos_subtables = 0;
    int         tt_points = 0;           // TTF: points in glyf (on + off curve)
    std::size_t bytes = 0;
    double      ms = 0;
    std::vector<std::string> notes;      // what Scott should know, one line each
};

Bytes compile_otf(const model::Font& f, const OtfOptions& opt = {}, OtfReport* rep = nullptr);
bool  export_otf(const model::Font& f, const std::filesystem::path& to, const OtfOptions& opt = {},
                 OtfReport* rep = nullptr, std::string* why = nullptr);

// M10a: the same font as TrueType (quadratic glyf outlines, 1/1000 em).
Bytes compile_ttf(const model::Font& f, const OtfOptions& opt = {}, OtfReport* rep = nullptr);
bool  export_ttf(const model::Font& f, const std::filesystem::path& to, const OtfOptions& opt = {},
                 OtfReport* rep = nullptr, std::string* why = nullptr);
// Safe write: a temp file beside `to`, renamed over it.
bool  write_font_file(const Bytes& font, const std::filesystem::path& to, std::string* why = nullptr);

// The names the file carries.
std::string postscript_name(const std::string& family, const std::string& style);
// uniXXXX / uXXXXX for an encoded glyph whose name does not already spell its
// first code point by the AGL rules; the designer's name otherwise.
std::string production_name(const model::Glyph& g);
// "Family-Style.otf" -- the default file name (ext ".otf" / ".ttf").
std::string default_file_name(const model::FontInfo& info, const std::string& ext = ".otf");

}  // namespace granjon::otf
