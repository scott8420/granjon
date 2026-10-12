// geom::Corners (M15b) -- extremes, round corners, ink traps.
#include "geom/Corners.hpp"
#include "geom/CubicSegment.hpp"
#include "geom/Edit.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace granjon::geom {

namespace {

using model::Contour;
using model::Node;

int wrap(int i, int n) { return ((i % n) + n) % n; }
Vec2 anchor(const Node& n) { return {n.x, n.y}; }

bool line_between(const Node& a, const Node& b) { return !a.has_out() && !b.has_in(); }

int seg_count(const Contour& c) {
    const int n = static_cast<int>(c.nodes.size());
    if (n < 2) return 0;
    return c.closed ? n : n - 1;
}

CubicSegment seg_of(const Contour& c, int s) {
    const int n = static_cast<int>(c.nodes.size());
    const Node& a = c.nodes[static_cast<std::size_t>(s)];
    const Node& b = c.nodes[static_cast<std::size_t>(wrap(s + 1, n))];
    return {anchor(a), {a.cx2, a.cy2}, {b.cx1, b.cy1}, anchor(b)};
}
bool seg_line(const Contour& c, int s) {
    const int n = static_cast<int>(c.nodes.size());
    return line_between(c.nodes[static_cast<std::size_t>(s)], c.nodes[static_cast<std::size_t>(wrap(s + 1, n))]);
}

// The travel direction at a segment's end / start (unit; zero if degenerate).
Vec2 dir_end(const CubicSegment& s, bool line) {
    if (!line) {
        if (s.p3.dist_sq(s.p2) > 1e-12) return (s.p3 - s.p2).normalised();
        if (s.p3.dist_sq(s.p1) > 1e-12) return (s.p3 - s.p1).normalised();
    }
    return (s.p3 - s.p0).normalised();
}
Vec2 dir_start(const CubicSegment& s, bool line) {
    if (!line) {
        if (s.p1.dist_sq(s.p0) > 1e-12) return (s.p1 - s.p0).normalised();
        if (s.p2.dist_sq(s.p0) > 1e-12) return (s.p2 - s.p0).normalised();
    }
    return (s.p3 - s.p0).normalised();
}

// The piece t0 .. t1 of a cubic (de Casteljau, exact).
CubicSegment piece(const CubicSegment& s, double t0, double t1) {
    CubicSegment r = s;
    if (t1 < 1 - 1e-12) r = r.split(t1).first;
    if (t0 > 1e-12) r = r.split(t0 / t1).second;
    return r;
}

// The t at chord distance d from the segment's end (from_end) or start.
double t_at_distance(const CubicSegment& s, double d, bool from_end) {
    const Vec2 ref = from_end ? s.p3 : s.p0;
    auto dist = [&](double t) { return s.at(t).dist(ref); };
    constexpr int N = 64;
    double prev = from_end ? 1.0 : 0.0;
    for (int k = 1; k <= N; ++k) {
        const double t = from_end ? 1.0 - double(k) / N : double(k) / N;
        if (dist(t) >= d) {
            double lo = prev, hi = t;   // dist(lo) < d <= dist(hi)
            for (int it = 0; it < 40; ++it) {
                const double m = (lo + hi) / 2;
                (dist(m) < d ? lo : hi) = m;
            }
            return (lo + hi) / 2;
        }
        prev = t;
    }
    return from_end ? 0.0 : 1.0;
}

// The tangents into and out of node i (unit).
void tangents(const Contour& c, int i, Vec2& in, Vec2& out) {
    const int n = static_cast<int>(c.nodes.size());
    const int si = wrap(i - 1, n);
    in = dir_end(seg_of(c, si), seg_line(c, si));
    out = dir_start(seg_of(c, i), seg_line(c, i));
}

bool interior(const Contour& c, int i) {
    const int n = static_cast<int>(c.nodes.size());
    if (n < (c.closed ? 3 : 3)) return false;
    if (!c.closed && (i <= 0 || i >= n - 1)) return false;
    return i >= 0 && i < n;
}

// The quadratic roots of a t^2 + b t + c in (lo, hi).
void roots(double a, double b, double c, std::vector<double>& out) {
    if (std::abs(a) < 1e-12) {
        if (std::abs(b) > 1e-12) out.push_back(-c / b);
        return;
    }
    const double disc = b * b - 4 * a * c;
    if (disc < 0) return;
    const double q = std::sqrt(disc);
    out.push_back((-b + q) / (2 * a));
    out.push_back((-b - q) / (2 * a));
}

enum class Cut { Round, Trap };

// The shared surgery: every node in `cut` (with its distance) replaced by the
// cut-back ends of its two edges, joined by `kind`.
void cut_corners(Contour& c, const std::vector<double>& cut, Cut kind, double depth, const std::vector<Vec2>& apex_dir) {
    const int n = static_cast<int>(c.nodes.size());
    const int m = seg_count(c);
    struct Trim { CubicSegment s; bool line; };
    std::vector<Trim> tr(static_cast<std::size_t>(m));
    for (int s = 0; s < m; ++s) {
        const CubicSegment S = seg_of(c, s);
        const bool line = seg_line(c, s);
        const int e = wrap(s + 1, n);
        double t0 = 0, t1 = 1;
        const double L = S.p0.dist(S.p3);
        if (const double d = cut[static_cast<std::size_t>(s)]; d > 0)
            t0 = line ? (L > 0 ? d / L : 0) : t_at_distance(S, d, false);
        if (const double d = cut[static_cast<std::size_t>(e)]; d > 0)
            t1 = line ? (L > 0 ? 1 - d / L : 1) : t_at_distance(S, d, true);
        if (t1 <= t0) t1 = t0 = (t0 + t1) / 2;   // clamping should prevent this
        if (line) {
            const Vec2 a = lerp(S.p0, S.p3, t0), b = lerp(S.p0, S.p3, t1);
            tr[static_cast<std::size_t>(s)] = {{a, a, b, b}, true};
        } else {
            tr[static_cast<std::size_t>(s)] = {piece(S, t0, t1), false};
        }
    }
    std::vector<Node> out;
    out.reserve(c.nodes.size() * 2);
    for (int i = 0; i < n; ++i) {
        const Node& o = c.nodes[static_cast<std::size_t>(i)];
        const bool has_in_seg = c.closed || i > 0;
        const bool has_out_seg = c.closed || i < n - 1;
        const Trim* tin = has_in_seg ? &tr[static_cast<std::size_t>(wrap(i - 1, n))] : nullptr;
        const Trim* tout = has_out_seg && i < m ? &tr[static_cast<std::size_t>(i)] : nullptr;
        if (cut[static_cast<std::size_t>(i)] <= 0 || !tin || !tout) {
            Node k = o;
            if (tin && !tin->line) { k.cx1 = tin->s.p2.x; k.cy1 = tin->s.p2.y; }
            if (tout && !tout->line) { k.cx2 = tout->s.p1.x; k.cy2 = tout->s.p1.y; }
            out.push_back(k);
            continue;
        }
        const Vec2 A = tin->s.p3, B = tout->s.p0;
        Node a = Node::corner(A.x, A.y), b = Node::corner(B.x, B.y);
        if (!tin->line) { a.cx1 = tin->s.p2.x; a.cy1 = tin->s.p2.y; }
        if (!tout->line) { b.cx2 = tout->s.p1.x; b.cy2 = tout->s.p1.y; }
        if (kind == Cut::Round) {
            const Vec2 ta = dir_end(tin->s, tin->line), tb = dir_start(tout->s, tout->line);
            const double phi = std::acos(std::clamp(ta.dot(tb), -1.0, 1.0));   // how far it turns
            const double chord = A.dist(B);
            double h = chord / 3;
            if (phi > 1e-3 && phi < std::numbers::pi - 1e-3) {
                const double R = chord / (2 * std::sin(phi / 2));
                h = 4.0 / 3.0 * std::tan(phi / 4) * R;
            }
            a.cx2 = A.x + ta.x * h; a.cy2 = A.y + ta.y * h;
            b.cx1 = B.x - tb.x * h; b.cy1 = B.y - tb.y * h;
            a.type = b.type = Node::Type::Smooth;
            out.push_back(a);
            out.push_back(b);
        } else {
            const Vec2 T = anchor(o) + apex_dir[static_cast<std::size_t>(i)] * depth;
            a.type = a.has_in() ? Node::Type::Cusp : Node::Type::Corner;
            b.type = b.has_out() ? Node::Type::Cusp : Node::Type::Corner;
            out.push_back(a);
            out.push_back(Node::corner(T.x, T.y));
            out.push_back(b);
        }
    }
    c.nodes = std::move(out);
}

// The cut distance at each picked corner: d, clamped to each edge's chord.
std::vector<double> cut_lengths(const Contour& c, const std::vector<bool>& pick, double d) {
    const int n = static_cast<int>(c.nodes.size());
    std::vector<double> cut(static_cast<std::size_t>(n), 0.0);
    for (int i = 0; i < n; ++i) {
        if (!pick[static_cast<std::size_t>(i)]) continue;
        const int si = wrap(i - 1, n), so = i;
        const int pi = wrap(i - 1, n), ni = wrap(i + 1, n);
        const double Lin = seg_of(c, si).p0.dist(seg_of(c, si).p3);
        const double Lout = seg_of(c, so).p0.dist(seg_of(c, so).p3);
        const double kin = pick[static_cast<std::size_t>(pi)] ? 0.48 : 0.95;
        const double kout = pick[static_cast<std::size_t>(ni)] ? 0.48 : 0.95;
        cut[static_cast<std::size_t>(i)] = std::min({d, Lin * kin, Lout * kout});
    }
    return cut;
}

}  // namespace

// ── extremes ───────────────────────────────────────────────────────────────

std::vector<Extreme> missing_extremes(const Contour& c, int s, double tol) {
    std::vector<Extreme> out;
    if (s < 0 || s >= seg_count(c) || seg_line(c, s)) return out;
    const CubicSegment S = seg_of(c, s);
    for (int axis = 0; axis < 2; ++axis) {
        auto co = [axis](Vec2 p) { return axis == 0 ? p.x : p.y; };
        const double d0 = co(S.p1) - co(S.p0), d1 = co(S.p2) - co(S.p1), d2 = co(S.p3) - co(S.p2);
        std::vector<double> ts;
        roots(d0 - 2 * d1 + d2, 2 * (d1 - d0), d0, ts);
        for (double t : ts) {
            if (!(t > 0.01 && t < 0.99)) continue;
            const double v = co(S.at(t));
            const double a = co(S.p0), b = co(S.p3);
            const bool beyond = (v > std::max(a, b) + tol) || (v < std::min(a, b) - tol);
            if (beyond) out.push_back({t, axis});
        }
    }
    std::sort(out.begin(), out.end(), [](const Extreme& x, const Extreme& y) { return x.t < y.t; });
    // two at one t (a cusp-like spot): keep one
    out.erase(std::unique(out.begin(), out.end(), [](const Extreme& x, const Extreme& y) { return std::abs(x.t - y.t) < 1e-6; }),
              out.end());
    return out;
}

int count_missing_extremes(const Contour& c, double tol) {
    int k = 0;
    for (int s = 0; s < seg_count(c); ++s) k += static_cast<int>(missing_extremes(c, s, tol).size());
    return k;
}

int add_extremes(Contour& c, double tol) {
    int added = 0;
    for (int s = seg_count(c) - 1; s >= 0; --s) {   // last first: earlier indices hold
        const auto ex = missing_extremes(c, s, tol);
        int at = s;
        double done = 0;   // the original t the current segment `at` starts from
        for (const auto& e : ex) {
            const double local = (e.t - done) / (1 - done);
            const int n = static_cast<int>(c.nodes.size());
            Node& a = c.nodes[static_cast<std::size_t>(at)];
            Node& b = c.nodes[static_cast<std::size_t>(wrap(at + 1, n))];
            const auto [L, R] = CubicSegment{anchor(a), {a.cx2, a.cy2}, {b.cx1, b.cy1}, anchor(b)}.split(local);
            a.cx2 = L.p1.x; a.cy2 = L.p1.y;
            b.cx1 = R.p2.x; b.cy1 = R.p2.y;
            Node k;
            k.x = L.p3.x; k.y = L.p3.y;
            k.cx1 = L.p2.x; k.cy1 = L.p2.y;
            k.cx2 = R.p1.x; k.cy2 = R.p1.y;
            if (e.axis == 0) k.cx1 = k.cx2 = k.x;   // a vertical tangent: handles plumb
            else k.cy1 = k.cy2 = k.y;                // a horizontal one: level
            k.type = Node::Type::Smooth;
            c.nodes.insert(c.nodes.begin() + at + 1, k);
            ++added;
            ++at;
            done = e.t;
        }
    }
    return added;
}

// ── corners ────────────────────────────────────────────────────────────────

bool is_corner(const Contour& c, int i) {
    if (!interior(c, i)) return false;
    Vec2 in, out;
    tangents(c, i, in, out);
    if (in.length_sq() < 1e-12 || out.length_sq() < 1e-12) return false;
    return in.dot(out) < std::cos(3.0 * std::numbers::pi / 180.0);
}

bool is_crotch(const Contour& c, int i, bool ink_left) {
    if (!is_corner(c, i)) return false;
    Vec2 in, out;
    tangents(c, i, in, out);
    const double turn = in.cross(out);   // > 0 = a left turn
    return ink_left ? turn < 0 : turn > 0;
}

double white_angle(const Contour& c, int i) {
    if (!interior(c, i)) return 180;
    Vec2 in, out;
    tangents(c, i, in, out);
    if (in.length_sq() < 1e-12 || out.length_sq() < 1e-12) return 180;
    return std::acos(std::clamp((in * -1.0).dot(out), -1.0, 1.0)) * 180.0 / std::numbers::pi;
}

bool is_trap_apex(const Contour& c, int i, double span, bool ink_left) {
    const int n = static_cast<int>(c.nodes.size());
    if (!c.closed || n < 4 || !is_crotch(c, i, ink_left)) return false;
    const int a = wrap(i - 1, n), b = wrap(i + 1, n);
    if (!seg_line(c, a) || !seg_line(c, i)) return false;
    if (seg_of(c, a).p0.dist(seg_of(c, a).p3) > span || seg_of(c, i).p0.dist(seg_of(c, i).p3) > span) return false;
    return is_corner(c, a) && !is_crotch(c, a, ink_left) && is_corner(c, b) && !is_crotch(c, b, ink_left);
}

int round_corners(Contour& c, const std::set<int>& nodes, double radius) {
    const int n = static_cast<int>(c.nodes.size());
    if (radius <= 0 || n < 3) return 0;
    std::vector<bool> pick(static_cast<std::size_t>(n), false);
    int k = 0;
    for (int i : nodes)
        if (i >= 0 && i < n && is_corner(c, i)) { pick[static_cast<std::size_t>(i)] = true; ++k; }
    if (k == 0) return 0;
    cut_corners(c, cut_lengths(c, pick, radius), Cut::Round, 0, {});
    return k;
}

int ink_traps(Contour& c, const std::set<int>& nodes, double width, double depth, bool ink_left) {
    const int n = static_cast<int>(c.nodes.size());
    if (width <= 0 || depth <= 0 || n < 3) return 0;
    std::vector<bool> pick(static_cast<std::size_t>(n), false);
    std::vector<Vec2> dir(static_cast<std::size_t>(n));
    int k = 0;
    for (int i : nodes)
        if (i >= 0 && i < n && is_crotch(c, i, ink_left)) {
            Vec2 in, out;
            tangents(c, i, in, out);
            const Vec2 d = in - out;   // away from the white wedge, into the ink
            if (d.length_sq() < 1e-12) continue;
            dir[static_cast<std::size_t>(i)] = d.normalised();
            pick[static_cast<std::size_t>(i)] = true;
            ++k;
        }
    if (k == 0) return 0;
    cut_corners(c, cut_lengths(c, pick, width), Cut::Trap, depth, dir);
    return k;
}

}  // namespace granjon::geom
