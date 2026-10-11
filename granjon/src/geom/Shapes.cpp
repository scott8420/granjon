// geom::Shapes (M14a) -- see the header.
#include "geom/Shapes.hpp"
#include "geom/NodeEdit.hpp"

#include <algorithm>
#include <cmath>

namespace granjon::geom {

using model::Contour;
using model::Node;

Box drag_box(Vec2 press, Vec2 now, bool square, bool from_centre) {
    Vec2 d = now - press;
    if (square) {
        const double s = std::max(std::abs(d.x), std::abs(d.y));
        d = {d.x < 0 ? -s : s, d.y < 0 ? -s : s};
    }
    const Vec2 a = from_centre ? press - d : press;
    const Vec2 b = press + d;
    return {{std::min(a.x, b.x), std::min(a.y, b.y)}, {std::max(a.x, b.x), std::max(a.y, b.y)}};
}

void drag_line(Vec2 press, Vec2 now, bool constrain, bool from_centre, Vec2& a, Vec2& b) {
    Vec2 d = now - press;
    if (constrain) d = constrain45(d);
    a = from_centre ? press - d : press;
    b = press + d;
}

bool degenerate(const Box& b) { return b.hi.x - b.lo.x < 1.0 || b.hi.y - b.lo.y < 1.0; }

Contour shape_rect(const Box& b) {
    Contour c;
    for (const Vec2 p : {b.lo, Vec2{b.hi.x, b.lo.y}, b.hi, Vec2{b.lo.x, b.hi.y}})
        c.nodes.push_back(Node::corner(p.x, p.y));
    return c;
}

Contour shape_ellipse(const Box& b) {
    const double cx = (b.lo.x + b.hi.x) / 2, cy = (b.lo.y + b.hi.y) / 2;
    const double rx = (b.hi.x - b.lo.x) / 2, ry = (b.hi.y - b.lo.y) / 2;
    const double kx = kEllipseKappa * rx, ky = kEllipseKappa * ry;
    auto smooth = [](double x, double y, double ix, double iy, double ox, double oy) {
        return Node{x, y, ix, iy, ox, oy, Node::Type::Smooth};
    };
    Contour c;
    c.nodes = {
        smooth(cx, cy - ry, cx - kx, cy - ry, cx + kx, cy - ry),   // bottom
        smooth(cx + rx, cy, cx + rx, cy - ky, cx + rx, cy + ky),   // right
        smooth(cx, cy + ry, cx + kx, cy + ry, cx - kx, cy + ry),   // top
        smooth(cx - rx, cy, cx - rx, cy + ky, cx - rx, cy - ky),   // left
    };
    return c;
}

Contour shape_bar(Vec2 a, Vec2 b, double width) {
    Contour c;
    const Vec2 dir = (b - a).normalised();
    if (dir.length_sq() == 0 || width <= 0) return c;
    // perp() is 90 degrees counter-clockwise: a -> b along the bottom edge
    // (right of travel), back along the top -- counter-clockwise.
    const Vec2 n = dir.perp() * (width / 2);
    for (const Vec2 p : {a - n, b - n, b + n, a + n}) c.nodes.push_back(Node::corner(p.x, p.y));
    return c;
}

}  // namespace granjon::geom
