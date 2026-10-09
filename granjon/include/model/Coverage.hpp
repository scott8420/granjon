#pragma once
// model::Coverage -- how much of a character set a font maps, and the glyphs
// "create missing" would add. GTK-free; the overview's checklist and gaps,
// and (later) granjon-cli and the report, ask this.
#include "model/Font.hpp"

#include <set>
#include <string>
#include <vector>

namespace granjon::model {

// Every code point some glyph maps.
std::set<char32_t> mapped(const Font& f);
// The members of `cps` no glyph maps, in the order given.
std::vector<char32_t> missing_from(const Font& f, const std::vector<char32_t>& cps);

struct NewGlyph {
    std::string           name;
    std::vector<char32_t> unicodes;
};
// One new glyph per code point in `cps`, named as the add verb would
// (AGLFN, else uniXXXX), unique against the font AND each other.
std::vector<NewGlyph> glyphs_for(const Font& f, const std::vector<char32_t>& cps);

}  // namespace granjon::model
