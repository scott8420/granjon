#pragma once
// geom::Offset (M15b) -- offset path and expand stroke, GTK-free.
//
// Each segment is offset along its normal and refitted as cubics (end
// tangents kept, the two handle lengths solved by least squares against five
// points of the true offset, reparameterised three times; a piece that strays more than 0.5 unit
// from the true offset is split in two and refitted, up to 64 pieces).
// Corners: where the offset opens a gap, a join fills it (miter -- bevel past
// 4 x the distance --, round, or bevel); where the offset sides cross, they
// are joined straight across and the little loop that leaves winds the other
// way. The raw outlines then go through the positive union (Overlap.hpp):
// loops, inside-out pieces and overlaps drop out, curves stay exact.
//
// Contours must run the right way (outers counter-clockwise, counters
// clockwise): the ink is on the left, so "bolder" is to the right.
#include "geom/Overlap.hpp"
#include "model/Outline.hpp"

#include <vector>

namespace granjon::geom {

enum class Join { Miter, Round, Bevel };
enum class Cap  { Butt, Round, Square };

// The closed contours offset by d: d > 0 bolder (every edge moves out of the
// ink), d < 0 lighter. Open contours come back as they were. `changed` false
// when d is 0 or there is nothing closed; `fallback` as remove_overlaps.
OverlapResult offset_contours(const std::vector<model::Contour>& cs, double d, Join join = Join::Miter);

// Each contour (open or closed) becomes the outline of a stroke `width` wide
// centred on it: an open path a band with `cap` ends, a closed one a ring.
OverlapResult expand_stroke(const std::vector<model::Contour>& cs, double width, Join join = Join::Miter,
                            Cap cap = Cap::Butt);

// The raw offset of ONE contour before the clean-up (the selftest looks at
// it): closed -> one closed contour; open -> one side only, open.
model::Contour offset_raw(const model::Contour& c, double d, Join join);

}  // namespace granjon::geom
