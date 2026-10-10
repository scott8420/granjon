// geom::NodeEdit (M5f).
#include "geom/NodeEdit.hpp"
#include "geom/CubicSegment.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

namespace granjon::geom {

using model::Contour;
using model::Node;

namespace {

int wrap(int i, int n) { return ((i % n) + n) % n; }
Vec2 anchor(const Node& n) { return {n.x, n.y}; }
Vec2 hin(const Node& n) { return {n.cx1, n.cy1}; }
Vec2 hout(const Node& n) { return {n.cx2, n.cy2}; }

void tidy_type(Node& n) {
    if (!n.has_in() && !n.has_out()) n.type = Node::Type::Corner;
    else if (n.type == Node::Type::Symmetric && (!n.has_in() || !n.has_out())) n.type = Node::Type::Smooth;
}

// The segment a -> b as a cubic (a line is a cubic with its handles on its ends).
CubicSegment seg(const Node& a, const Node& b) { return {anchor(a), hout(a), hin(b), anchor(b)}; }

// Remove node k from an interior position: prev p, next q.
void remove_interior(Contour& c, int k) {
    const int n = static_cast<int>(c.nodes.size());
    const int ip = wrap(k - 1, n), iq = wrap(k + 1, n);
    Node& p = c.nodes[static_cast<std::size_t>(ip)];
    Node& q = c.nodes[static_cast<std::size_t>(iq)];
    const Node& m = c.nodes[static_cast<std::size_t>(k)];
    const bool line_a = !p.has_out() && !m.has_in();
    const bool line_b = !m.has_out() && !q.has_in();
    if (!(line_a && line_b)) {
        const CubicSegment A = seg(p, m), B = seg(m, q);
        const Vec2 P0 = anchor(p), P3 = anchor(q);
        // End tangents: the outside handles if there are any, else toward the deleted node.
        Vec2 t0 = p.has_out() ? hout(p) - P0 : anchor(m) - P0;
        Vec2 t1 = q.has_in() ? hin(q) - P3 : anchor(m) - P3;
        const double l0 = t0.length(), l1 = t1.length();
        const double chord = (P3 - P0).length();
        double a1 = chord / 3, a2 = chord / 3;
        if (l0 > 1e-9 && l1 > 1e-9 && chord > 1e-9) {
            t0 = t0 / l0;
            t1 = t1 / l1;
            // Samples along the old outline, parameterised by arc length.
            std::vector<Vec2> pts;
            constexpr int kN = 24;
            for (int i = 0; i <= kN; ++i) pts.push_back(A.at(static_cast<double>(i) / kN));
            for (int i = 1; i <= kN; ++i) pts.push_back(B.at(static_cast<double>(i) / kN));
            std::vector<double> u(pts.size(), 0.0);
            for (std::size_t i = 1; i < pts.size(); ++i) u[i] = u[i - 1] + (pts[i] - pts[i - 1]).length();
            const double total = u.back();
            if (total > 1e-9) {
                for (auto& x : u) x /= total;
                // Least squares for the two handle lengths, then Schneider's
                // reparameterisation: each sample takes the parameter of its
                // nearest point on the fitted curve, and the fit is solved
                // again. Exact data (a split curve) comes back exactly.
                for (int round = 0; round < 6; ++round) {
                    double C00 = 0, C01 = 0, C11 = 0, X0 = 0, X1 = 0;
                    for (std::size_t i = 0; i < pts.size(); ++i) {
                        const double s = u[i], r = 1 - s;
                        const double b0 = r * r * r, b1 = 3 * s * r * r, b2 = 3 * s * s * r, b3 = s * s * s;
                        const Vec2 A1 = t0 * b1, A2 = t1 * b2;
                        const Vec2 tmp = pts[i] - (P0 * (b0 + b1) + P3 * (b2 + b3));
                        C00 += A1.dot(A1); C01 += A1.dot(A2); C11 += A2.dot(A2);
                        X0 += A1.dot(tmp); X1 += A2.dot(tmp);
                    }
                    const double det = C00 * C11 - C01 * C01;
                    if (std::abs(det) <= 1e-12) break;
                    const double s1 = (X0 * C11 - X1 * C01) / det, s2 = (C00 * X1 - C01 * X0) / det;
                    if (!(s1 > chord * 1e-3 && s2 > chord * 1e-3)) break;
                    a1 = s1;
                    a2 = s2;
                    const CubicSegment fit{P0, P0 + t0 * a1, P3 + t1 * a2, P3};
                    for (std::size_t i = 1; i + 1 < pts.size(); ++i) u[i] = fit.nearest_t(pts[i]);
                }
            }
        }
        const Vec2 o = P0 + t0 * (l0 > 1e-9 ? a1 : 0);
        const Vec2 i = P3 + t1 * (l1 > 1e-9 ? a2 : 0);
        p.cx2 = o.x; p.cy2 = o.y;
        q.cx1 = i.x; q.cy1 = i.y;
    }
    c.nodes.erase(c.nodes.begin() + k);
}

}  // namespace

bool is_line(const Contour& c, int segment) {
    const int n = static_cast<int>(c.nodes.size());
    if (n < 2) return true;
    return !c.nodes[static_cast<std::size_t>(wrap(segment, n))].has_out() &&
           !c.nodes[static_cast<std::size_t>(wrap(segment + 1, n))].has_in();
}

int insert_node(Contour& c, int segment, double t) {
    const int n = static_cast<int>(c.nodes.size());
    if (n < 2 || segment < 0 || segment >= (c.closed ? n : n - 1)) return -1;
    t = std::clamp(t, 1e-6, 1 - 1e-6);
    Node& a = c.nodes[static_cast<std::size_t>(segment)];
    Node& b = c.nodes[static_cast<std::size_t>(wrap(segment + 1, n))];
    Node m;
    if (is_line(c, segment)) {
        const Vec2 p = anchor(a) + (anchor(b) - anchor(a)) * t;
        m = Node::corner(p.x, p.y);
    } else {
        const auto [L, R] = seg(a, b).split(t);
        a.cx2 = L.p1.x; a.cy2 = L.p1.y;
        b.cx1 = R.p2.x; b.cy1 = R.p2.y;
        m.x = L.p3.x; m.y = L.p3.y;
        m.cx1 = L.p2.x; m.cy1 = L.p2.y;
        m.cx2 = R.p1.x; m.cy2 = R.p1.y;
        m.type = Node::Type::Smooth;
        tidy_type(m);
    }
    c.nodes.insert(c.nodes.begin() + segment + 1, m);
    return segment + 1;
}

void delete_nodes(model::Glyph& g, const std::set<std::pair<int, int>>& nodes) {
    std::map<int, std::vector<int>> by;
    for (const auto& [ci, ni] : nodes)
        if (ci >= 0 && ci < static_cast<int>(g.contours.size()) && ni >= 0 &&
            ni < static_cast<int>(g.contours[static_cast<std::size_t>(ci)].nodes.size()))
            by[ci].push_back(ni);
    std::vector<int> drop;
    for (auto& [ci, list] : by) {
        Contour& c = g.contours[static_cast<std::size_t>(ci)];
        std::sort(list.rbegin(), list.rend());
        for (int k : list) {
            const int n = static_cast<int>(c.nodes.size());
            if (n <= 2) { c.nodes.clear(); break; }
            if (!c.closed && (k == 0 || k == n - 1)) {
                c.nodes.erase(c.nodes.begin() + k);
                // The new end: its outward handle leads nowhere.
                Node& e = k == 0 ? c.nodes.front() : c.nodes.back();
                if (k == 0) { e.cx1 = e.x; e.cy1 = e.y; } else { e.cx2 = e.x; e.cy2 = e.y; }
                tidy_type(e);
                continue;
            }
            remove_interior(c, k);
        }
        if (c.nodes.size() < 2) drop.push_back(ci);
    }
    std::sort(drop.rbegin(), drop.rend());
    for (int ci : drop) g.contours.erase(g.contours.begin() + ci);
}

void retract_handle(Contour& c, int node, bool out) {
    if (node < 0 || node >= static_cast<int>(c.nodes.size())) return;
    Node& n = c.nodes[static_cast<std::size_t>(node)];
    if (out) { n.cx2 = n.x; n.cy2 = n.y; } else { n.cx1 = n.x; n.cy1 = n.y; }
    tidy_type(n);
}

void segment_to_line(Contour& c, int segment) {
    const int n = static_cast<int>(c.nodes.size());
    if (n < 2) return;
    retract_handle(c, wrap(segment, n), true);
    retract_handle(c, wrap(segment + 1, n), false);
}

void segment_to_curve(Contour& c, int segment) {
    const int n = static_cast<int>(c.nodes.size());
    if (n < 2 || !is_line(c, segment)) return;
    Node& a = c.nodes[static_cast<std::size_t>(wrap(segment, n))];
    Node& b = c.nodes[static_cast<std::size_t>(wrap(segment + 1, n))];
    const Vec2 p = anchor(a), q = anchor(b);
    const Vec2 o = p + (q - p) / 3.0, i = p + (q - p) * (2.0 / 3.0);
    a.cx2 = o.x; a.cy2 = o.y;
    b.cx1 = i.x; b.cy1 = i.y;
}

// ── the pen (M5g) ──────────────────────────────────────────────────────────

int pen_add(model::Glyph& g, int contour, Vec2 p) {
    if (contour < 0 || contour >= static_cast<int>(g.contours.size())) {
        Contour c;
        c.closed = false;
        c.nodes.push_back(Node::corner(p.x, p.y));
        g.contours.push_back(c);
        return static_cast<int>(g.contours.size()) - 1;
    }
    g.contours[static_cast<std::size_t>(contour)].nodes.push_back(Node::corner(p.x, p.y));
    return contour;
}

void pen_drag_handles(Contour& c, int node, Vec2 d, bool cusp) {
    if (node < 0 || node >= static_cast<int>(c.nodes.size())) return;
    Node& n = c.nodes[static_cast<std::size_t>(node)];
    n.cx2 = n.x + d.x; n.cy2 = n.y + d.y;
    if (cusp) {
        n.cx1 = n.x; n.cy1 = n.y;
        n.type = Node::Type::Corner;
    } else {
        n.cx1 = n.x - d.x; n.cy1 = n.y - d.y;
        n.type = Node::Type::Symmetric;
    }
    tidy_type(n);
}

void pen_close_handles(Contour& c, Vec2 d) {
    if (c.nodes.empty()) return;
    Node& n = c.nodes.front();
    n.cx1 = n.x - d.x; n.cy1 = n.y - d.y;
    // Smooth when the out-handle it has lies on the same line, else a corner.
    const Vec2 in = hin(n) - anchor(n), out = hout(n) - anchor(n);
    const double cross = in.x * out.y - in.y * out.x;
    if (!n.has_out()) n.type = Node::Type::Corner;
    else if (std::abs(cross) <= 1e-6 * std::max(1.0, in.length() * out.length()) && in.dot(out) < 0) n.type = Node::Type::Smooth;
    else n.type = Node::Type::Corner;
    tidy_type(n);
}

void reverse_open(Contour& c) {
    std::reverse(c.nodes.begin(), c.nodes.end());
    for (auto& n : c.nodes) { std::swap(n.cx1, n.cx2); std::swap(n.cy1, n.cy2); }
}

Vec2 constrain45(Vec2 d) {
    const double len = d.length();
    if (len < 1e-12) return d;
    const double step = 3.14159265358979323846 / 4;
    const double a = std::round(std::atan2(d.y, d.x) / step) * step;
    const Vec2 u{std::cos(a), std::sin(a)};
    const double along = d.dot(u);
    return u * along;
}

}  // namespace granjon::geom
