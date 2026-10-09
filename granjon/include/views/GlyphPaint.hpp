#pragma once
// views::GlyphPaint -- draw a glyph's outline with cairo: contours plus
// components resolved through the font, y-up font units flipped to the
// screen here and nowhere else (ARCHITECTURE §3).
#include "model/Font.hpp"

#include <cairomm/context.h>

namespace granjon::views {

// Fit the em (descender .. ascender, 0 .. advance) into w x h, centred, and
// fill the outline in the context's current source colour (nonzero winding).
void paint_glyph_fitted(const Cairo::RefPtr<Cairo::Context>& cr, const model::Font& f,
                        const model::Glyph& g, double w, double h, double pad);

// Append the outline as a path, transformed by the context's current matrix
// (already flipped to y-up by the caller).
void append_glyph_path(const Cairo::RefPtr<Cairo::Context>& cr, const model::Font& f,
                       const model::Glyph& g, int depth = 0);

}  // namespace granjon::views
