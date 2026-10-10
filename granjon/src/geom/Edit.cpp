#include "geom/Edit.hpp"

#include <algorithm>
#include <cmath>

namespace granjon::geom {

using model::Contour;
using model::Node;
using Type = model::Node::Type;

namespace {
Vec2 anchor(const Node& n) { return {n.x, n.y}; }
int  wrap(int i, int n)    { return ((i % n) + n) % n; }
}  // namespace

int segment_count(const Contour& c) {
    const int n = static_cast<int>(c.nodes.size());
    if (n < 2) return 0;
    return c.closed ? n : n - 1;
}

CubicSegment segment(const Contour& c, int i) {
    const int n = static_cast<int>(c.nodes.size());
    const Node& a = c.nodes[wrap(i, n)];
    const Node& b = c.nodes[wrap(i + 1, n)];
    return {{a.x, a.y}, {a.cx2, a.cy2}, {b.cx1, b.cy1}, {b.x, b.y}};
}

// ── Curvz BezierPath::move_node / recompute_cusp_handles / move_handle_* ──

void move_node(Contour& c, int idx, Vec2 pos) {
    const int nn = static_cast<int>(c.nodes.size());
    if (idx < 0 || idx >= nn) return;
    Node& n = c.nodes[idx];
    const double dx = pos.x - n.x, dy = pos.y - n.y;
    n.x = pos.x;   n.y = pos.y;
    n.cx1 += dx;   n.cy1 += dy;
    n.cx2 += dx;   n.cy2 += dy;
    recompute_cusp_handles(c, idx);
    if (idx > 0 || c.closed)      recompute_cusp_handles(c, wrap(idx - 1, nn));
    if (idx < nn - 1 || c.closed) recompute_cusp_handles(c, wrap(idx + 1, nn));
}

void recompute_cusp_handles(Contour& c, int idx) {
    const int nn = static_cast<int>(c.nodes.size());
    if (idx < 0 || idx >= nn) return;
    Node& n = c.nodes[idx];
    if (n.type != Type::Cusp) return;
    const bool has_prev = idx > 0 || c.closed;
    const bool has_next = idx < nn - 1 || c.closed;
    if (!has_prev) return;   // no incoming segment to lock to

    const Vec2 a = anchor(n);
    const Node& prev = c.nodes[wrap(idx - 1, nn)];
    const Vec2 prev_out{prev.cx2, prev.cy2};
    // The arriving tangent: prev's out-handle -> this anchor (chord if absent).
    Vec2 tang = (a - prev_out).normalised();
    if ((a - prev_out).length() < 1e-9) tang = (a - anchor(prev)).normalised();

    double out_len = Vec2{n.cx2 - n.x, n.cy2 - n.y}.length();
    if (out_len < 1e-9 && has_next) out_len = (anchor(c.nodes[wrap(idx + 1, nn)]) - a).length() * 0.333;
    // The in-handle already encodes the arriving tangent; only the out-handle re-aims.
    n.cx2 = n.x + tang.x * out_len;
    n.cy2 = n.y + tang.y * out_len;
}

namespace {
// One body for both handles: `h` is the dragged one, `o` the other.
void move_handle(Node& n, double& hx, double& hy, double& ox, double& oy, Vec2 pos) {
    if (n.type == Type::Cusp) {
        // Length only, along the locked tangent; the other handle stays opposite.
        const Vec2 cur{hx - n.x, hy - n.y};
        const double cur_len = cur.length();
        if (cur_len < 1e-9) { hx = pos.x; hy = pos.y; return; }
        const Vec2 tang = cur / cur_len;
        const double len = std::max(0.0, (pos - anchor(n)).dot(tang));
        hx = n.x + tang.x * len;
        hy = n.y + tang.y * len;
        const double other = Vec2{ox - n.x, oy - n.y}.length();
        ox = n.x - tang.x * other;
        oy = n.y - tang.y * other;
        return;
    }
    hx = pos.x;
    hy = pos.y;
    if (n.type == Type::Symmetric) {
        ox = n.x - (pos.x - n.x);
        oy = n.y - (pos.y - n.y);
    } else if (n.type == Type::Smooth) {
        const double other = Vec2{ox - n.x, oy - n.y}.length();
        const Vec2 dir = Vec2{n.x - pos.x, n.y - pos.y}.normalised();
        ox = n.x + dir.x * other;
        oy = n.y + dir.y * other;
    }
}
}  // namespace

void move_handle_in(Contour& c, int idx, Vec2 pos) {
    if (idx < 0 || idx >= static_cast<int>(c.nodes.size())) return;
    Node& n = c.nodes[idx];
    move_handle(n, n.cx1, n.cy1, n.cx2, n.cy2, pos);
}

void move_handle_out(Contour& c, int idx, Vec2 pos) {
    if (idx < 0 || idx >= static_cast<int>(c.nodes.size())) return;
    Node& n = c.nodes[idx];
    move_handle(n, n.cx2, n.cy2, n.cx1, n.cy1, pos);
}


// ── node type (Curvz BezierPath::set_node_type, on a Contour) ────────────────

namespace {
// Handles along the bisector of the neighbour directions, a third of the
// average chord -- Curvz's seed for a node that has none.
void seed_bisect(Contour& c, int idx) {
    const int nn = static_cast<int>(c.nodes.size());
    Node& n = c.nodes[idx];
    const bool has_prev = idx > 0 || c.closed;
    const bool has_next = idx < nn - 1 || c.closed;
    const Vec2 a = anchor(n);
    if (has_prev && has_next) {
        const Vec2 pa = anchor(c.nodes[wrap(idx - 1, nn)]), na = anchor(c.nodes[wrap(idx + 1, nn)]);
        const double len = ((a - pa).length() + (na - a).length()) * 0.5 * 0.333;
        Vec2 bis = ((a - pa).normalised() + (na - a).normalised()).normalised();
        if (bis.length() < 1e-9) bis = (na - a).normalised();
        n.cx1 = n.x - bis.x * len; n.cy1 = n.y - bis.y * len;
        n.cx2 = n.x + bis.x * len; n.cy2 = n.y + bis.y * len;
    } else if (has_next) {
        const Vec2 na = anchor(c.nodes[wrap(idx + 1, nn)]);
        const Vec2 dir = (na - a).normalised();
        const double len = (na - a).length() * 0.333;
        n.cx1 = n.x - dir.x * len; n.cy1 = n.y - dir.y * len;
        n.cx2 = n.x + dir.x * len; n.cy2 = n.y + dir.y * len;
    } else if (has_prev) {
        const Vec2 pa = anchor(c.nodes[wrap(idx - 1, nn)]);
        const Vec2 dir = (a - pa).normalised();
        const double len = (a - pa).length() * 0.333;
        n.cx1 = n.x - dir.x * len; n.cy1 = n.y - dir.y * len;
        n.cx2 = n.x + dir.x * len; n.cy2 = n.y + dir.y * len;
    }
}
}  // namespace

void set_node_type(Contour& c, int idx, Type type) {
    const int nn = static_cast<int>(c.nodes.size());
    if (idx < 0 || idx >= nn) return;
    Node& n = c.nodes[idx];
    const Type old = n.type;
    n.type = type;
    const Vec2 in{n.cx1 - n.x, n.cy1 - n.y}, out{n.cx2 - n.x, n.cy2 - n.y};
    const bool in_degen = in.length() < 1e-9, out_degen = out.length() < 1e-9;
    switch (type) {
        case Type::Corner:
            break;   // handles stay exactly where they are
        case Type::Cusp:
            recompute_cusp_handles(c, idx);
            break;
        case Type::Symmetric:
            if (old == Type::Symmetric) break;
            if (in_degen && out_degen) { seed_bisect(c, idx); break; }
            if (old == Type::Corner) break;   // keep the geometry; enforced on the next handle drag
            if (!in_degen) { n.cx2 = n.x - in.x;  n.cy2 = n.y - in.y; }
            else           { n.cx1 = n.x - out.x; n.cy1 = n.y - out.y; }
            break;
        case Type::Smooth:
            if (old == Type::Cusp && !in_degen && !out_degen) {
                const Vec2 dir = out.normalised();
                n.cx1 = n.x - dir.x * in.length();
                n.cy1 = n.y - dir.y * in.length();
            } else if (old == Type::Corner) {
                if (in_degen && out_degen) { seed_bisect(c, idx); break; }
                // Granjon addition: a corner with handles goes smooth by lining
                // them up on their average direction, lengths kept (Curvz
                // re-seeded from the chord, throwing the designer's lengths away).
                const Vec2 dir = in_degen ? out.normalised() : out_degen ? (in * -1.0).normalised()
                                                                         : (out.normalised() - in.normalised()).normalised();
                if (dir.length() < 1e-9) break;
                const double li = in_degen ? out.length() : in.length();
                const double lo = out_degen ? in.length() : out.length();
                n.cx1 = n.x - dir.x * li; n.cy1 = n.y - dir.y * li;
                n.cx2 = n.x + dir.x * lo; n.cy2 = n.y + dir.y * lo;
            }
            break;
    }
}

// ── points ─────────────────────────────────────────────────────────────────

bool valid(const model::Glyph& g, const PointRef& p) {
    if (p.part == PointRef::Part::Anchor)
        return p.index >= 0 && p.index < static_cast<int>(g.anchors.size());
    if (p.part == PointRef::Part::Component)
        return p.index >= 0 && p.index < static_cast<int>(g.components.size());
    return p.contour >= 0 && p.contour < static_cast<int>(g.contours.size()) && p.index >= 0 &&
           p.index < static_cast<int>(g.contours[p.contour].nodes.size());
}

Vec2 position(const model::Glyph& g, const PointRef& p) {
    if (!valid(g, p)) return {};
    if (p.part == PointRef::Part::Anchor) return {g.anchors[p.index].x, g.anchors[p.index].y};
    if (p.part == PointRef::Part::Component) return {g.components[p.index].dx, g.components[p.index].dy};
    const Node& n = g.contours[p.contour].nodes[p.index];
    switch (p.part) {
        case PointRef::Part::In:  return {n.cx1, n.cy1};
        case PointRef::Part::Out: return {n.cx2, n.cy2};
        default:                  return {n.x, n.y};
    }
}

void move_points(model::Glyph& g, const model::Glyph& at_press, const Points& pts, Vec2 d, bool free_handles) {
    // Start from the press every time.
    g.contours   = at_press.contours;
    g.anchors    = at_press.anchors;
    g.components = at_press.components;
    using Part = PointRef::Part;
    auto node_picked = [&](int c, int i) { return pts.count({c, i, Part::Node}) > 0; };

    // 1. Picked nodes: translate with their handles (no cusp pass yet -- the
    //    neighbours may be moving too).
    for (const auto& p : pts) {
        if (p.part != Part::Node || !valid(g, p)) continue;
        Node& n = g.contours[p.contour].nodes[p.index];
        n.x += d.x;   n.y += d.y;
        n.cx1 += d.x; n.cy1 += d.y;
        n.cx2 += d.x; n.cy2 += d.y;
    }
    // 2. Picked handles of unpicked nodes: the node's type rule.
    for (const auto& p : pts) {
        if ((p.part != Part::In && p.part != Part::Out) || !valid(g, p)) continue;
        if (node_picked(p.contour, p.index)) continue;   // already rode along
        const Node& was = at_press.contours[p.contour].nodes[p.index];
        if (free_handles) g.contours[p.contour].nodes[p.index].type = Type::Corner;   // the rule no longer holds
        if (p.part == Part::In) move_handle_in(g.contours[p.contour], p.index, Vec2{was.cx1, was.cy1} + d);
        else                    move_handle_out(g.contours[p.contour], p.index, Vec2{was.cx2, was.cy2} + d);
    }
    // 3. Cusps at and next to a moved node re-aim (Curvz move_node's cascade).
    for (const auto& p : pts) {
        if (p.part != Part::Node || !valid(g, p)) continue;
        Contour& c = g.contours[p.contour];
        const int nn = static_cast<int>(c.nodes.size());
        recompute_cusp_handles(c, p.index);
        if (p.index > 0 || c.closed)      recompute_cusp_handles(c, wrap(p.index - 1, nn));
        if (p.index < nn - 1 || c.closed) recompute_cusp_handles(c, wrap(p.index + 1, nn));
    }
    // 4. Glyph anchors and components (a component moves by its offset).
    for (const auto& p : pts) {
        if (!valid(g, p)) continue;
        if (p.part == Part::Anchor) {
            g.anchors[p.index].x += d.x;
            g.anchors[p.index].y += d.y;
        } else if (p.part == Part::Component) {
            g.components[p.index].dx += d.x;
            g.components[p.index].dy += d.y;
        }
    }
}

void shift_x(model::Glyph& g, double dx) {
    for (auto& c : g.contours)
        for (auto& n : c.nodes) { n.x += dx; n.cx1 += dx; n.cx2 += dx; }
    for (auto& a : g.anchors) a.x += dx;
    for (auto& k : g.components) k.dx += dx;
}

// ── components ─────────────────────────────────────────────────────────────

void transform(Contour& c, const model::Component& k) {
    auto xf = [&](double& x, double& y) {
        const double nx = k.xx * x + k.yx * y + k.dx;
        const double ny = k.xy * x + k.yy * y + k.dy;
        x = nx;
        y = ny;
    };
    for (auto& n : c.nodes) { xf(n.x, n.y); xf(n.cx1, n.cy1); xf(n.cx2, n.cy2); }
}

std::vector<Contour> component_contours(const model::Font& f, const model::Component& k, int depth) {
    std::vector<Contour> out;
    const model::Glyph* base = f.glyph(k.base);
    if (!base || depth >= 8) return out;
    out = base->contours;
    for (const auto& inner : base->components)
        for (auto& c : component_contours(f, inner, depth + 1)) out.push_back(std::move(c));
    for (auto& c : out) transform(c, k);
    return out;
}

bool inside(const std::vector<Contour>& cs, Vec2 pt) {
    int wind = 0;
    auto edge = [&](Vec2 a, Vec2 b) {   // crossing number, signed
        if (a.y <= pt.y) {
            if (b.y > pt.y && (b - a).cross(pt - a) > 0) ++wind;
        } else if (b.y <= pt.y && (b - a).cross(pt - a) < 0) {
            --wind;
        }
    };
    for (const auto& c : cs) {
        const int n = segment_count(c);
        if (n == 0) continue;
        for (int s = 0; s < n; ++s) {
            const CubicSegment seg = segment(c, s);
            Vec2 prev = seg.p0;
            for (int i = 1; i <= 16; ++i) {
                const Vec2 cur = seg.at(i / 16.0);
                edge(prev, cur);
                prev = cur;
            }
        }
        if (!c.closed) edge(segment(c, n - 1).p3, segment(c, 0).p0);
    }
    return wind != 0;
}

int component_at(const model::Font& f, const model::Glyph& g, Vec2 pt) {
    for (int i = static_cast<int>(g.components.size()) - 1; i >= 0; --i)
        if (inside(component_contours(f, g.components[i]), pt)) return i;
    return -1;
}

// ── precision ──────────────────────────────────────────────────────────────

double grid_step(double px_per_unit, double min_px) {
    if (px_per_unit <= 0) return 1000;
    static const double mant[] = {1, 2, 5};
    for (double decade = 0.1; decade < 1e6; decade *= 10)
        for (double m : mant)
            if (m * decade * px_per_unit >= min_px) return m * decade;
    return 1e6;
}

double snap_axis(double v, const std::vector<double>& targets, double grid, double tol,
                 std::optional<double>* snapped) {
    if (snapped) snapped->reset();
    double best = v, best_d = tol;
    bool hit = false;
    for (double t : targets)
        if (std::abs(t - v) <= best_d) { best = t; best_d = std::abs(t - v); hit = true; }
    if (!hit && grid > 0) {
        const double g = std::round(v / grid) * grid;
        if (std::abs(g - v) <= tol) { best = g; hit = true; }
    }
    if (hit && snapped) *snapped = best;
    return best;
}

Vec2 axis_lock(Vec2 d) { return std::abs(d.x) >= std::abs(d.y) ? Vec2{d.x, 0} : Vec2{0, d.y}; }

// ── hit testing ────────────────────────────────────────────────────────────

Hit hit_test(const model::Glyph& g, Vec2 pt, double tol, bool show_handles) {
    Hit best;
    auto take = [&](Hit::Kind k, int c, int i, double dist, double t = 0) {
        if (dist < tol && dist < best.distance) best = {k, c, i, t, dist};
    };
    // 1. Node anchors -- later contours / nodes win ties (drawn on top).
    for (int c = static_cast<int>(g.contours.size()) - 1; c >= 0; --c)
        for (int i = static_cast<int>(g.contours[c].nodes.size()) - 1; i >= 0; --i)
            take(Hit::Kind::Node, c, i, anchor(g.contours[c].nodes[i]).dist(pt));
    if (best.kind != Hit::Kind::None) return best;
    // 2. Handles (absent ones have nothing to grab).
    if (show_handles) {
        for (int c = 0; c < static_cast<int>(g.contours.size()); ++c)
            for (int i = 0; i < static_cast<int>(g.contours[c].nodes.size()); ++i) {
                const Node& n = g.contours[c].nodes[i];
                if (n.has_in())  take(Hit::Kind::HandleIn,  c, i, Vec2{n.cx1, n.cy1}.dist(pt));
                if (n.has_out()) take(Hit::Kind::HandleOut, c, i, Vec2{n.cx2, n.cy2}.dist(pt));
            }
        if (best.kind != Hit::Kind::None) return best;
    }
    // 3. Glyph anchors.
    for (int i = 0; i < static_cast<int>(g.anchors.size()); ++i)
        take(Hit::Kind::Anchor, -1, i, Vec2{g.anchors[i].x, g.anchors[i].y}.dist(pt));
    if (best.kind != Hit::Kind::None) return best;
    // 4. Segments.
    for (int c = 0; c < static_cast<int>(g.contours.size()); ++c)
        for (int s = 0; s < segment_count(g.contours[c]); ++s) {
            const CubicSegment seg = segment(g.contours[c], s);
            const double t = seg.nearest_t(pt);
            take(Hit::Kind::Segment, c, s, seg.at(t).dist(pt), t);
        }
    return best;
}

Points points_in(const model::Glyph& g, Vec2 a, Vec2 b) {
    const double x0 = std::min(a.x, b.x), x1 = std::max(a.x, b.x);
    const double y0 = std::min(a.y, b.y), y1 = std::max(a.y, b.y);
    auto inside = [&](double x, double y) { return x >= x0 && x <= x1 && y >= y0 && y <= y1; };
    Points out;
    for (int c = 0; c < static_cast<int>(g.contours.size()); ++c)
        for (int i = 0; i < static_cast<int>(g.contours[c].nodes.size()); ++i) {
            const Node& n = g.contours[c].nodes[i];
            if (inside(n.x, n.y)) out.insert({c, i, PointRef::Part::Node});
        }
    for (int i = 0; i < static_cast<int>(g.anchors.size()); ++i)
        if (inside(g.anchors[i].x, g.anchors[i].y)) out.insert({-1, i, PointRef::Part::Anchor});
    return out;
}

}  // namespace granjon::geom
