#pragma once
// geom::CubicSegment -- Curvz's CubicSegment, lifted GTK-free. The subset the
// editor needs: evaluate, tangent, nearest point, split, control-box. (Curvz's
// Bezier-clipping intersect() comes over with overlap removal, not before.)
#include "geom/Vec2.hpp"

#include <utility>

namespace granjon::geom {

// p0 (anchor) -> p1 (out-handle) -> p2 (in-handle) -> p3 (anchor)
struct CubicSegment {
    Vec2 p0, p1, p2, p3;

    Vec2   at(double t) const;            // De Casteljau
    Vec2   tangent(double t) const;       // first derivative, not normalised
    double nearest_t(Vec2 pt) const;      // sampled seed + Newton-Raphson
    double dist_to(Vec2 pt) const;
    std::pair<CubicSegment, CubicSegment> split(double t) const;
    void   bbox(Vec2& out_min, Vec2& out_max) const;   // of the control polygon
};

}  // namespace granjon::geom
