#pragma once
// model::Glyph -- one drawing. A glyph is a drawing; a codepoint is a mapping.
// They are never conflated: a glyph may carry zero, one, or many codepoints.
#include "model/Outline.hpp"

#include <optional>
#include <string>
#include <vector>

namespace granjon::model {

enum class GlyphStatus { Empty, Drafted, Done, Review };
const char* status_name(GlyphStatus s);

// M13: the background image -- a scan or sketch placed behind the glyph to
// trace. UFO 3's <image>: a PNG in the font's images/ folder, placed by an
// affine (xScale xyScale yxScale yScale xOffset yOffset) from image pixels
// (y up, the bottom-left pixel corner at 0,0) to font units:
//     x = xx * u + yx * v + dx        y = xy * u + yy * v + dy
// `color` is UFO's optional tint, kept verbatim. Opacity and lock are
// Granjon's (glyph lib, granjon.image). Never exported -- a tracing aid.
struct GlyphImage {
    std::string file;                 // name in images/ (the font's image pool)
    double xx = 1, xy = 0, yx = 0, yy = 1, dx = 0, dy = 0;
    std::string color;
    double opacity = 0.4;             // 0..1
    bool   locked  = false;           // locked: the mouse goes through it
    bool operator==(const GlyphImage&) const = default;
};

struct Glyph {
    std::string                name;      // designer name == the store id
    std::vector<char32_t>      unicodes;
    double                     advance = 0;
    std::vector<Contour>       contours;
    std::vector<Component>     components;
    std::vector<Anchor>        anchors;
    std::vector<Guide>         guides;    // this glyph's guidelines (M5c)
    std::optional<GlyphImage>  image;     // the background image (M13)
    GlyphStatus                status = GlyphStatus::Empty;
    std::string                note;
    double                     order = 0; // place in the glyph order (sorts ascending)
    // M14c: a PART -- a piece to build glyphs from (a serif, a bowl, a stem),
    // left out of the compile. UFO's public.skipExportGlyphs (font lib), so
    // fontmake and the other UFO tools skip it too. Placed into real glyphs
    // as a component (decomposed on export) or as plain contours.
    bool                       skip_export = false;

    bool operator==(const Glyph&) const = default;

    // Round every coordinate and the advance to integer font units.
    void snap();
    bool is_integral() const;
    bool uses(const std::string& base) const;   // has a component on `base`
};

// AGL-safe designer name: [A-Za-z0-9._-], not empty, not starting with a
// digit or '.' (except ".notdef"/".null"), at most 63 chars.
bool valid_glyph_name(const std::string& name);

}  // namespace granjon::model
