// geom::Offset (M15b) -- offset path, expand stroke.
#include "geom/Offset.hpp"
#include "geom/CubicSegment.hpp"
#include "geom/Edit.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace granjon::geom {

namespace {

using model::Contour;
using model::Node;

constexpr double kFitTol = 0.5;     // font units: a refit may stray this far from the true offset
constexpr int    kMaxSplit = 6;     // 2^6 = 64 pieces per segment at most
constexpr double kMiterLimit = 4.0; // a miter longer than 4 x the distance becomes a bevel

int wrap(int i, int n) { return ((i % n) + n) % n; }
Vec2 anchor(const Node& n) { return {n.x, n.y}; }
Vec2 right_of(Vec2 t) { return {t.y, -t.x}; }   // y-up: the right-hand normal

struct Piece { CubicSegment s; bool line; };

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
    return !c.nodes[static_cast<std::size_t>(s)].has_out() && !c.nodes[static_cast<std::size_t>(wrap(s + 1, n))].has_in();
}

Vec2 dir_start(const CubicSegment& s) {
    if (s.p1.dist_sq(s.p0) > 1e-12) return (s.p1 - s.p0).normalised();
    if (s.p2.dist_sq(s.p0) > 1e-12) return (s.p2 - s.p0).normalised();
    return (s.p3 - s.p0).normalised();
}
Vec2 dir_end(const CubicSegment& s) {
    if (s.p3.dist_sq(s.p2) > 1e-12) return (s.p3 - s.p2).normalised();
    if (s.p3.dist_sq(s.p1) > 1e-12) return (s.p3 - s.p1).normalised();
    return (s.p3 - s.p0).normalised();
}
Vec2 dir_at(const CubicSegment& s, double t) {
    if (t <= 1e-9) return dir_start(s);
    if (t >= 1 - 1e-9) return dir_end(s);
    const Vec2 d = s.tangent(t);
    if (d.length_sq() > 1e-12) return d.normalised();
    return (s.at(std::min(1.0, t + 1e-4)) - s.at(std::max(0.0, t - 1e-4))).normalised();
}

// The cubic with ends a, b, end tangents u (out of a), v (into b), handle
// lengths fitted to pass near the targets q[k] at t[k].
bool fit(Vec2 a, Vec2 u, Vec2 b, Vec2 v, const double* ts, const Vec2* qs, int k, CubicSegment& out) {
    double aa = 0, ab = 0, bb = 0, ar = 0, br = 0;
    for (int i = 0; i < k; ++i) {
        const double t = ts[i], mt = 1 - t;
        const Vec2 base = a * (mt * mt * mt + 3 * mt * mt * t) + b * (3 * mt * t * t + t * t * t);
        const Vec2 A = u * (3 * mt * mt * t);
        const Vec2 B = v * (-3 * mt * t * t);
        const Vec2 R = qs[i] - base;
        aa += A.dot(A); ab += A.dot(B); bb += B.dot(B); ar += A.dot(R); br += B.dot(R);
    }
    const double det = aa * bb - ab * ab;
    if (std::abs(det) < 1e-12) return false;
    const double la = (ar * bb - br * ab) / det;
    const double lb = (aa * br - ab * ar) / det;
    out = {a, a + u * std::max(0.0, la), b - v * std::max(0.0, lb), b};
    return la >= 0 && lb >= 0;
}

void offset_curve(const CubicSegment& S, double r, int depth, std::vector<Piece>& out) {
    const Vec2 u = dir_start(S), v = dir_end(S);
    const Vec2 a = S.p0 + right_of(u) * r, b = S.p3 + right_of(v) * r;
    // Five targets on the true offset; fit, then re-estimate each target's t
    // on the fitted curve and fit again (Schneider's reparameterisation) --
    // the offset's speed differs from the source's on a tight curve.
    double ts[5] = {1 / 6.0, 2 / 6.0, 3 / 6.0, 4 / 6.0, 5 / 6.0};
    Vec2 qs[5];
    for (int i = 0; i < 5; ++i) qs[i] = S.at(ts[i]) + right_of(dir_at(S, ts[i])) * r;
    CubicSegment C;
    bool ok = fit(a, u, b, v, ts, qs, 5, C);
    for (int it = 0; it < 3 && ok; ++it) {
        for (int i = 0; i < 5; ++i) ts[i] = std::clamp(C.nearest_t(qs[i]), 0.02, 0.98);
        ok = fit(a, u, b, v, ts, qs, 5, C);
    }
    if (ok)
        for (int i = 1; i < 8 && ok; ++i) {
            const double t = i / 8.0;
            ok = C.dist_to(S.at(t) + right_of(dir_at(S, t)) * r) <= kFitTol;
        }
    if (!ok && depth < kMaxSplit) {
        const auto [L, R] = S.split(0.5);
        offset_curve(L, r, depth + 1, out);
        offset_curve(R, r, depth + 1, out);
        return;
    }
    if (!ok && C.p0 == C.p3) C = {a, a, b, b};
    out.push_back({C, false});
}

std::vector<Piece> offset_segment(const CubicSegment& S, bool line, double r) {
    std::vector<Piece> out;
    if (line) {
        if (S.p0.dist_sq(S.p3) < 1e-12) return out;
        const Vec2 n = right_of((S.p3 - S.p0).normalised()) * r;
        out.push_back({{S.p0 + n, S.p0 + n, S.p3 + n, S.p3 + n}, true});
        return out;
    }
    offset_curve(S, r, 0, out);
    return out;
}

// Appends nodes; the last node's out-handle is set by curve_to.
struct Builder {
    std::vector<Node> nodes;
    void start(Vec2 p) { nodes.push_back(Node::corner(p.x, p.y)); }
    Vec2 last() const { return anchor(nodes.back()); }
    void line_to(Vec2 p) {
        if (nodes.empty()) { start(p); return; }
        if (last().dist_sq(p) < 1e-12) return;
        nodes.push_back(Node::corner(p.x, p.y));
    }
    void curve_to(Vec2 c1, Vec2 c2, Vec2 p) {
        Node& a = nodes.back();
        a.cx2 = c1.x; a.cy2 = c1.y;
        Node b = Node::corner(p.x, p.y);
        b.cx1 = c2.x; b.cy1 = c2.y;
        nodes.push_back(b);
    }
    void piece(const Piece& pc) {
        if (nodes.empty()) start(pc.s.p0);
        else if (last().dist_sq(pc.s.p0) > 1e-12) line_to(pc.s.p0);
        if (pc.line) line_to(pc.s.p3);
        else curve_to(pc.s.p1, pc.s.p2, pc.s.p3);
    }
    // An arc about c from the last point, turning `sweep` radians (+ = counter-clockwise).
    void arc(Vec2 c, double sweep) {
        const Vec2 s = last() - c;
        const double rad = s.length();
        if (rad < 1e-9 || std::abs(sweep) < 1e-9) return;
        const int k = std::max(1, static_cast<int>(std::ceil(std::abs(sweep) / (std::numbers::pi / 2) - 1e-9)));
        const double step = sweep / k;
        const double h = 4.0 / 3.0 * std::tan(std::abs(step) / 4) * rad;
        double ang = std::atan2(s.y, s.x);
        for (int i = 0; i < k; ++i) {
            const double a0 = ang, a1 = ang + step;
            const Vec2 p0 = c + Vec2{std::cos(a0), std::sin(a0)} * rad;
            const Vec2 p1 = c + Vec2{std::cos(a1), std::sin(a1)} * rad;
            const double sg = step > 0 ? 1 : -1;
            const Vec2 t0 = Vec2{-std::sin(a0), std::cos(a0)} * sg, t1 = Vec2{-std::sin(a1), std::cos(a1)} * sg;
            curve_to(p0 + t0 * h, p1 - t1 * h, p1);
            ang = a1;
        }
    }
};

// The join at P between an offset side arriving along ta (now at E) and the
// next leaving along tb from S.
void join(Builder& b, Vec2 P, Vec2 ta, Vec2 tb, Vec2 S, double r, Join how) {
    const Vec2 E = b.last();
    if (E.dist_sq(S) < 1e-12) return;
    const double cr = ta.cross(tb);
    if (ta.dot(tb) > std::cos(3.0 * std::numbers::pi / 180.0)) {   // all but smooth: meet halfway
        const Vec2 mid = (E + S) / 2.0, dE = mid - E;
        Node& n = b.nodes.back();
        n.x += dE.x; n.y += dE.y; n.cx1 += dE.x; n.cy1 += dE.y; n.cx2 += dE.x; n.cy2 += dE.y;
        return;   // the next piece is moved to start here (side())
    }
    const bool opening = cr * r > 0;
    if (!opening) { b.line_to(S); return; }   // the sides cross: the loop winds back and drops out
    switch (how) {
        case Join::Miter: {
            // E + s ta = S - u tb
            const Vec2 d = S - E;
            const double s = d.cross(tb) / ta.cross(tb);
            const Vec2 M = E + ta * s;
            if (s > 0 && M.dist(P) <= kMiterLimit * std::abs(r)) b.line_to(M);
            b.line_to(S);
            break;
        }
        case Join::Round: {
            const double a0 = std::atan2((E - P).y, (E - P).x), a1 = std::atan2((S - P).y, (S - P).x);
            double sweep = a1 - a0;
            const double dir = cr > 0 ? 1 : -1;   // a left turn sweeps counter-clockwise
            while (sweep * dir < 0) sweep += 2 * std::numbers::pi * dir;
            while (std::abs(sweep) > 2 * std::numbers::pi) sweep -= 2 * std::numbers::pi * dir;
            b.arc(P, sweep);
            b.line_to(S);
            break;
        }
        case Join::Bevel: b.line_to(S); break;
    }
}

// One side of a chain of segments (all of an open contour, or a closed one
// with its wrap join), offset r to the right.
void side(Builder& b, const Contour& c, double r, Join how) {
    const int m = seg_count(c);
    const int n = static_cast<int>(c.nodes.size());
    std::vector<std::vector<Piece>> ps(static_cast<std::size_t>(m));
    for (int s = 0; s < m; ++s) ps[static_cast<std::size_t>(s)] = offset_segment(seg_of(c, s), seg_line(c, s), r);
    bool first = true;
    for (int s = 0; s < m; ++s) {
        const auto& pcs = ps[static_cast<std::size_t>(s)];
        if (pcs.empty()) continue;
        if (!first) {
            int prev = s - 1;
            while (prev >= 0 && ps[static_cast<std::size_t>(prev)].empty()) --prev;
            if (prev >= 0) {
                const CubicSegment A = seg_of(c, prev), B = seg_of(c, s);
                join(b, anchor(c.nodes[static_cast<std::size_t>(s)]), dir_end(A), dir_start(B), pcs.front().s.p0, r, how);
            }
        }
        for (std::size_t k = 0; k < pcs.size(); ++k) {
            Piece pc = pcs[k];
            if (k == 0 && !b.nodes.empty()) {   // a smooth join moved the last point: start the piece there
                const Vec2 d = b.last() - pc.s.p0;
                if (d.length_sq() > 1e-12 && d.length_sq() < 1e-2 * std::max(1.0, r * r)) { pc.s.p0 += d; pc.s.p1 += d; }
            }
            b.piece(pc);
        }
        first = false;
    }
    if (c.closed && m > 0 && !b.nodes.empty()) {   // the wrap join, then fold the end onto the start
        int last = m - 1;
        while (last >= 0 && ps[static_cast<std::size_t>(last)].empty()) --last;
        int firsts = 0;
        while (firsts < m && ps[static_cast<std::size_t>(firsts)].empty()) ++firsts;
        if (last >= 0 && firsts < m) {
            const Vec2 S0 = anchor(b.nodes.front());
            join(b, anchor(c.nodes[static_cast<std::size_t>(wrap(last + 1, n))]), dir_end(seg_of(c, last)),
                 dir_start(seg_of(c, firsts)), S0, r, how);
            const double near = std::max(1e-2, 0.01 * r * r);
            if (b.nodes.size() > 1 && b.last().dist_sq(S0) < near) {
                // fold the end onto the start (at the end's place: a smooth
                // join put it halfway), keeping both sides' handles
                Node& f = b.nodes.front();
                const Node e = b.nodes.back();
                const double dx = e.x - f.x, dy = e.y - f.y;
                f.x = e.x; f.y = e.y;
                f.cx2 += dx; f.cy2 += dy;
                f.cx1 = e.cx1; f.cy1 = e.cy1;
                b.nodes.pop_back();
            }
        }
    }
}

void tidy_types(Contour& c) {
    for (auto& p : c.nodes) {
        if (!p.has_in() && !p.has_out()) { p.type = Node::Type::Corner; continue; }
        if (p.has_in() && p.has_out()) {
            const Vec2 a = Vec2{p.x - p.cx1, p.y - p.cy1}.normalised(), b = Vec2{p.cx2 - p.x, p.cy2 - p.y}.normalised();
            p.type = a.dot(b) > 0.9995 ? Node::Type::Smooth : Node::Type::Cusp;
        } else {
            p.type = Node::Type::Corner;
        }
    }
}

// The cap from the last point (one side's end) across to `to` round the path's end P (direction t).
void cap(Builder& b, Vec2 P, Vec2 t, Vec2 to, double h, Cap how) {
    switch (how) {
        case Cap::Butt: b.line_to(to); break;
        case Cap::Square:
            b.line_to(b.last() + t * h);
            b.line_to(to + t * h);
            b.line_to(to);
            break;
        case Cap::Round: {
            // from the right side round the front to the left: counter-clockwise
            b.arc(P, std::numbers::pi);
            b.line_to(to);
            break;
        }
    }
}

// Drop corner nodes with no handles sitting on a straight run (a miter's
// leftovers: line in, line out, the same direction), and nodes on top of
// their neighbour.
void drop_collinear(Contour& c) {
    bool again = true;
    while (again && c.nodes.size() > 3) {
        again = false;
        const int n = static_cast<int>(c.nodes.size());
        for (int i = 0; i < n; ++i) {
            if (!c.closed && (i == 0 || i == n - 1)) continue;
            const Node& p = c.nodes[static_cast<std::size_t>(wrap(i - 1, n))];
            const Node& k = c.nodes[static_cast<std::size_t>(i)];
            const Node& q = c.nodes[static_cast<std::size_t>(wrap(i + 1, n))];
            const bool line_in = !p.has_out() && !k.has_in(), line_out = !k.has_out() && !q.has_in();
            const Vec2 a = anchor(k) - anchor(p), b = anchor(q) - anchor(k);
            const bool dup = a.length_sq() < 1e-12 && line_in;
            const bool straight = line_in && line_out && a.length_sq() > 1e-12 && b.length_sq() > 1e-12 &&
                                  std::abs(a.normalised().cross(b.normalised())) < 1e-9 && a.dot(b) > 0;
            if (dup || straight) {
                c.nodes.erase(c.nodes.begin() + i);
                again = true;
                break;
            }
        }
    }
}

}  // namespace

Contour offset_raw(const Contour& c, double d, Join how) {
    Builder b;
    side(b, c, d, how);
    Contour out;
    out.closed = c.closed;
    out.nodes = std::move(b.nodes);
    tidy_types(out);
    return out;
}

OverlapResult offset_contours(const std::vector<Contour>& cs, double d, Join how) {
    OverlapResult res;
    res.contours = cs;
    if (d == 0) return res;
    std::vector<Contour> raw, open;
    for (const auto& c : cs) {
        if (!c.closed || c.nodes.size() < 2) { open.push_back(c); continue; }
        raw.push_back(offset_raw(c, d, how));
    }
    if (raw.empty()) return res;
    res = union_positive(raw);
    if (res.fallback) { res.contours = cs; return res; }
    for (auto& c : res.contours) drop_collinear(c);
    for (auto& c : open) res.contours.push_back(std::move(c));
    res.changed = true;
    return res;
}

OverlapResult expand_stroke(const std::vector<Contour>& cs, double width, Join how, Cap cp) {
    OverlapResult res;
    res.contours = cs;
    const double h = width / 2;
    if (h <= 0) return res;
    std::vector<Contour> raw;
    for (const auto& c : cs) {
        if (c.nodes.size() < 2 || seg_count(c) == 0) continue;
        if (c.closed) {   // a ring: the outside bolder, the inside lighter and turned round
            raw.push_back(offset_raw(c, h, how));
            Contour in = offset_raw(c, -h, how);
            reverse(in);
            raw.push_back(std::move(in));
            continue;
        }
        Contour back = c;
        reverse(back);   // an open path: the order flips, the ends stay loose
        Builder b;
        side(b, c, h, how);
        const Vec2 end = anchor(c.nodes.back()), beg = anchor(c.nodes.front());
        const Vec2 tend = dir_end(seg_of(c, seg_count(c) - 1)), tbeg = dir_start(seg_of(c, 0));
        const Vec2 b0 = end + right_of(tend * -1.0) * h;   // the other side's first point
        cap(b, end, tend, b0, h, cp);
        Builder rb;
        side(rb, back, h, how);
        for (std::size_t i = 0; i < rb.nodes.size(); ++i) {
            const Node& k = rb.nodes[i];
            if (i == 0) {   // carries on from the cap
                if (b.last().dist_sq(anchor(k)) > 1e-12) b.line_to(anchor(k));
                b.nodes.back().cx2 = k.cx2;
                b.nodes.back().cy2 = k.cy2;
                continue;
            }
            b.nodes.push_back(k);
        }
        const Vec2 a0 = anchor(b.nodes.front());
        cap(b, beg, tbeg * -1.0, a0, h, cp);
        if (b.nodes.size() > 1 && b.last().dist_sq(a0) < 1e-6) {
            const Node e = b.nodes.back();
            b.nodes.pop_back();
            if (e.has_in()) { b.nodes.front().cx1 = e.cx1; b.nodes.front().cy1 = e.cy1; }
        }
        Contour band;
        band.closed = true;
        band.nodes = std::move(b.nodes);
        tidy_types(band);
        if (signed_area(band) < 0) reverse(band);
        raw.push_back(std::move(band));
    }
    if (raw.empty()) return res;
    res = union_positive(raw);
    if (res.fallback) { res.contours = cs; return res; }
    for (auto& c : res.contours) drop_collinear(c);
    res.changed = true;
    return res;
}

}  // namespace granjon::geom
