#pragma once
// model::Bounds -- the exact bounding box of a glyph's drawing (contours plus
// components, resolved through the font), in font units. Cubic extrema are
// solved, not sampled, so sidebearings read true.
#include "model/Font.hpp"

#include <optional>

namespace granjon::model {

struct Rect {
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool operator==(const Rect&) const = default;
};

// nullopt for an empty drawing (space, a fresh glyph).
std::optional<Rect> bounds(const Font& f, const Glyph& g);

// Left and right sidebearings: xMin and advance - xMax. Empty drawing: nullopt.
struct Sidebearings { double left = 0, right = 0; };
std::optional<Sidebearings> sidebearings(const Font& f, const Glyph& g);

}  // namespace granjon::model
