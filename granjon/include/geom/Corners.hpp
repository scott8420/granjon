#pragma once
// geom::Corners (M15b) -- outline surgery on a contour's nodes, GTK-free:
//
//   extremes      add a node at every horizontal / vertical extreme of a curve
//                 that has none (fonts want them: hinting, interpolation and
//                 the rasterisers' dropout control all lean on them). The new
//                 node is smooth, its handles exactly level / plumb.
//   round         round a corner: cut both edges back by the radius (a chord
//                 distance along each, curves cut exactly by de Casteljau) and
//                 join the cuts with a cubic arc, tangent to both edges. A 90
//                 degree corner between lines gets a quarter circle.
//   ink trap      a V notch cut INTO the ink at a corner where the ink is the
//                 reflex side (a crotch: n's arch into its stem, the inside of
//                 v's apex): the edges cut back by `width`, the apex `depth`
//                 into the ink along the corner's bisector.
//
// A corner = a node where the outline turns (the tangents in and out differ
// by more than 3 degrees). Smooth nodes are left alone. The cut on an edge is
// clamped so the two ends of one edge never cross (under half the edge when
// both of its ends are cut). Open contours: their two end nodes never count.
#include "geom/Vec2.hpp"
#include "model/Outline.hpp"

#include <set>
#include <vector>

namespace granjon::geom {

// The extremes a curve segment is missing: (t, axis) with axis 0 = an x
// extreme (vertical tangent), 1 = a y extreme. Only extremes that stand more
// than `tol` units beyond both of the segment's ends, t inside (0.01, 0.99).
struct Extreme { double t; int axis; };
std::vector<Extreme> missing_extremes(const model::Contour& c, int segment, double tol = 1.0);
// How many the whole contour lacks.
int count_missing_extremes(const model::Contour& c, double tol = 1.0);
// Insert them; the count added.
int add_extremes(model::Contour& c, double tol = 1.0);

// Whether node i is a corner (see above).
bool is_corner(const model::Contour& c, int i);
// Whether the ink is the reflex side at node i (the outline turns right while
// the ink is on its left -- which it is on every contour running the right
// way; pass `ink_left = false` for one running the wrong way).
bool is_crotch(const model::Contour& c, int i, bool ink_left = true);

// The angle of the WHITE wedge at a crotch, degrees (180 = no turn). Small =
// tight: ink fills it in at small sizes (s10: the ink-trap warning, < 60).
double white_angle(const model::Contour& c, int i);
// A crotch that is the apex of an ink trap already: its two edges short lines
// (under `span`) ending at convex corners -- the notch ink_traps cuts.
bool is_trap_apex(const model::Contour& c, int i, double span, bool ink_left = true);

// Round the given nodes (those that are corners). The count rounded.
int round_corners(model::Contour& c, const std::set<int>& nodes, double radius);
// Ink traps at the given nodes (those that are crotches). The count cut.
int ink_traps(model::Contour& c, const std::set<int>& nodes, double width, double depth, bool ink_left = true);

}  // namespace granjon::geom
