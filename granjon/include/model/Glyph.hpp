#pragma once
// model::Glyph -- one drawing. A glyph is a drawing; a codepoint is a mapping.
// They are never conflated: a glyph may carry zero, one, or many codepoints.
#include "model/Outline.hpp"

#include <string>
#include <vector>

namespace granjon::model {

enum class GlyphStatus { Empty, Drafted, Done, Review };
const char* status_name(GlyphStatus s);

struct Glyph {
    std::string                name;      // designer name == the store id
    std::vector<char32_t>      unicodes;
    double                     advance = 0;
    std::vector<Contour>       contours;
    std::vector<Component>     components;
    std::vector<Anchor>        anchors;
    std::vector<Guide>         guides;    // this glyph's guidelines (M5c)
    GlyphStatus                status = GlyphStatus::Empty;
    std::string                note;
    double                     order = 0; // place in the glyph order (sorts ascending)

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
