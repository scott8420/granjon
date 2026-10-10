#pragma once
// otf::Codes -- M9c: a glyph's drawing as the files carry it, and as artwork.
// GTK-free (the inspector's CODE card and Export SVG call it; so can the cli).
//
//   glyph_code    text for reading: the CFF charstring disassembled (the very
//                 bytes the OTF holds), the TrueType points (ttx's <pt> lines,
//                 after cu2qu and the direction flip), or the SVG path data
//   text_svg      glyphs set in a line (advances + kerning, as the strip does)
//                 as one SVG of plain filled paths, y flipped -- the "convert
//                 to outlines" hand-off. One <path> per glyph, named.
#include "model/Font.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace granjon::otf {

enum class CodeFormat { Cff, TrueType, Svg };

struct GlyphCode {
    std::string text;      // the listing
    std::string summary;   // one line: sizes / counts
};
GlyphCode glyph_code(const model::Font& f, const std::string& glyph, CodeFormat fmt);

struct SvgOptions {
    double em_px = 100;    // width / height attributes: one em is this many px (viewBox stays in font units)
    double margin = 0;     // font units around the drawing
};
std::string text_svg(const model::Font& f, const std::vector<std::string>& glyphs, const SvgOptions& opt = {});
bool export_svg(const model::Font& f, const std::vector<std::string>& glyphs, const std::filesystem::path& to,
                const SvgOptions& opt = {}, std::string* why = nullptr);

}  // namespace granjon::otf
