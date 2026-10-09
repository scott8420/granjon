#include "model/Bounds.hpp"

#include <algorithm>
#include <cmath>

namespace granjon::model {

namespace {

struct Acc {
    bool   any = false;
    Rect   r;
    void add(double x, double y) {
        if (!any) { r = {x, y, x, y}; any = true; return; }
        r.x0 = std::min(r.x0, x); r.y0 = std::min(r.y0, y);
        r.x1 = std::max(r.x1, x); r.y1 = std::max(r.y1, y);
    }
};

struct Affine {
    double xx = 1, xy = 0, yx = 0, yy = 1, dx = 0, dy = 0;
    void apply(double x, double y, double& ox, double& oy) const {
        ox = xx * x + yx * y + dx;
        oy = xy * x + yy * y + dy;
    }
    Affine then(const Component& c) const {   // this ∘ c
        Affine a;
        a.xx = xx * c.xx + yx * c.xy;
        a.xy = xy * c.xx + yy * c.xy;
        a.yx = xx * c.yx + yx * c.yy;
        a.yy = xy * c.yx + yy * c.yy;
        a.dx = xx * c.dx + yx * c.dy + dx;
        a.dy = xy * c.dx + yy * c.dy + dy;
        return a;
    }
};

// Roots in (0,1) of the derivative of a 1-D cubic with control values p0..p3.
void extrema(double p0, double p1, double p2, double p3, double ts[2], int& n) {
    n = 0;
    const double a = -p0 + 3 * p1 - 3 * p2 + p3;
    const double b = 2 * (p0 - 2 * p1 + p2);
    const double c = p1 - p0;
    const auto keep = [&](double t) { if (t > 0 && t < 1) ts[n++] = t; };
    if (std::abs(a) < 1e-12) {
        if (std::abs(b) > 1e-12) keep(-c / b);
        return;
    }
    const double d = b * b - 4 * a * c;
    if (d < 0) return;
    const double s = std::sqrt(d);
    keep((-b + s) / (2 * a));
    keep((-b - s) / (2 * a));
}

double at(double p0, double p1, double p2, double p3, double t) {
    const double u = 1 - t;
    return u * u * u * p0 + 3 * u * u * t * p1 + 3 * u * t * t * p2 + t * t * t * p3;
}

void add_glyph(const Font& f, const Glyph& g, const Affine& m, Acc& acc, int depth) {
    for (const auto& c : g.contours) {
        const auto& ns = c.nodes;
        const std::size_t count = ns.size();
        if (count == 0) continue;
        const std::size_t segs = c.closed ? count : count - 1;
        for (std::size_t i = 0; i < count; ++i) {
            double x, y;
            m.apply(ns[i].x, ns[i].y, x, y);
            acc.add(x, y);
        }
        for (std::size_t i = 0; i < segs; ++i) {
            const Node& a = ns[i];
            const Node& b = ns[(i + 1) % count];
            double P[4][2];
            m.apply(a.x, a.y, P[0][0], P[0][1]);
            m.apply(a.cx2, a.cy2, P[1][0], P[1][1]);
            m.apply(b.cx1, b.cy1, P[2][0], P[2][1]);
            m.apply(b.x, b.y, P[3][0], P[3][1]);
            for (int axis = 0; axis < 2; ++axis) {
                double ts[2];
                int n = 0;
                extrema(P[0][axis], P[1][axis], P[2][axis], P[3][axis], ts, n);
                for (int k = 0; k < n; ++k)
                    acc.add(at(P[0][0], P[1][0], P[2][0], P[3][0], ts[k]),
                            at(P[0][1], P[1][1], P[2][1], P[3][1], ts[k]));
            }
        }
    }
    if (depth >= 8) return;   // a component cycle stops here (a check flags it)
    for (const auto& k : g.components)
        if (const Glyph* base = f.glyph(k.base)) add_glyph(f, *base, m.then(k), acc, depth + 1);
}

}  // namespace

std::optional<Rect> bounds(const Font& f, const Glyph& g) {
    Acc acc;
    add_glyph(f, g, Affine{}, acc, 0);
    if (!acc.any) return std::nullopt;
    return acc.r;
}

std::optional<Sidebearings> sidebearings(const Font& f, const Glyph& g) {
    const auto b = bounds(f, g);
    if (!b) return std::nullopt;
    return Sidebearings{b->x0, g.advance - b->x1};
}

}  // namespace granjon::model
