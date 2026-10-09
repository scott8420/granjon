#pragma once
// model::Outline -- the drawing parts of a glyph: Node, Contour, Component,
// Anchor. GTK-free, plain values (every one is copyable and ==, because the
// undo journal stores whole glyphs by value).
//
// ── coordinates ────────────────────────────────────────────────────────────
// Font units, Y-UP from the baseline. The flip to screen y-down lives only in
// the canvas view transform. Values are double while a drag is live and are
// snapped to integers when the gesture commits (Glyph::snap), so the stored
// model is always integral.
//
// ── Node is Curvz's BezierNode ─────────────────────────────────────────────
// An anchor with an in-handle and an out-handle (cubic). A handle equal to its
// anchor is absent: two absent handles either side of a segment = a line. At
// the GLIF boundary a node becomes one on-curve point plus its off-curve
// neighbours; GLIF's point list never appears in the model.
#include <string>
#include <vector>

namespace granjon::model {

struct Node {
    enum class Type { Smooth, Cusp, Symmetric, Corner };
    double x = 0, y = 0;       // anchor (on-curve)
    double cx1 = 0, cy1 = 0;   // in-handle
    double cx2 = 0, cy2 = 0;   // out-handle
    Type   type = Type::Corner;

    // A corner node with no handles: the common case for straight outlines.
    static Node corner(double x, double y) { return {x, y, x, y, x, y, Type::Corner}; }
    bool has_in()  const { return cx1 != x || cy1 != y; }
    bool has_out() const { return cx2 != x || cy2 != y; }
    bool operator==(const Node&) const = default;
};

struct Contour {
    std::vector<Node> nodes;
    bool closed = true;   // font outlines are closed; open is a check finding
    bool operator==(const Contour&) const = default;
};

// A reference to another glyph, placed by an affine transform
// (x' = xx*x + yx*y + dx,  y' = xy*x + yy*y + dy -- UFO's order).
struct Component {
    std::string base;
    double xx = 1, xy = 0, yx = 0, yy = 1, dx = 0, dy = 0;
    bool operator==(const Component&) const = default;
};

// "top" on a base glyph meets "_top" on a mark.
struct Anchor {
    std::string name;
    double x = 0, y = 0;
    bool operator==(const Anchor&) const = default;
};

}  // namespace granjon::model
