#include "geom/CubicSegment.hpp"

#include <algorithm>
#include <cmath>

namespace granjon::geom {

Vec2 CubicSegment::at(double t) const {
    const double u = 1.0 - t;
    return p0 * (u * u * u) + p1 * (3 * u * u * t) + p2 * (3 * u * t * t) + p3 * (t * t * t);
}

Vec2 CubicSegment::tangent(double t) const {
    const double u = 1.0 - t;
    return (p1 - p0) * (3 * u * u) + (p2 - p1) * (6 * u * t) + (p3 - p2) * (3 * t * t);
}

std::pair<CubicSegment, CubicSegment> CubicSegment::split(double t) const {
    const Vec2 p01 = lerp(p0, p1, t), p12 = lerp(p1, p2, t), p23 = lerp(p2, p3, t);
    const Vec2 p012 = lerp(p01, p12, t), p123 = lerp(p12, p23, t);
    const Vec2 mid = lerp(p012, p123, t);
    return {CubicSegment{p0, p01, p012, mid}, CubicSegment{mid, p123, p23, p3}};
}

double CubicSegment::nearest_t(Vec2 pt) const {
    // Seed from 9 samples, then minimise |B(t)-pt|^2 by Newton-Raphson.
    double best_t = 0.0, best_d = 1e18;
    for (int i = 0; i <= 8; ++i) {
        const double t = i / 8.0;
        const double d = at(t).dist_sq(pt);
        if (d < best_d) { best_d = d; best_t = t; }
    }
    double t = best_t;
    for (int iter = 0; iter < 10; ++iter) {
        const Vec2 b = at(t), b1 = tangent(t), diff = b - pt;
        const double fp = 2.0 * diff.dot(b1);
        const double u = 1.0 - t;
        const Vec2 b2 = (p2 - p1 * 2 + p0) * (6 * u) + (p3 - p2 * 2 + p1) * (6 * t);
        const double fpp = 2.0 * (b1.dot(b1) + diff.dot(b2));
        if (std::abs(fpp) < 1e-12) break;
        const double dt = fp / fpp;
        t = std::clamp(t - dt, 0.0, 1.0);
        if (std::abs(dt) < 1e-7) break;
    }
    return std::clamp(t, 0.0, 1.0);
}

double CubicSegment::dist_to(Vec2 pt) const { return at(nearest_t(pt)).dist(pt); }

void CubicSegment::bbox(Vec2& mn, Vec2& mx) const {
    mn = {std::min({p0.x, p1.x, p2.x, p3.x}), std::min({p0.y, p1.y, p2.y, p3.y})};
    mx = {std::max({p0.x, p1.x, p2.x, p3.x}), std::max({p0.y, p1.y, p2.y, p3.y})};
}

}  // namespace granjon::geom
