// geom::Overlap (M10c). Flattening after Curvz's BooleanOpsClipper (s93);
// the tagged rebuild after booleanOperations (Frederik Berlaen et al.).
#include "geom/Overlap.hpp"
#include "geom/CubicSegment.hpp"
#include "geom/Edit.hpp"

#include <clipper2/clipper.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>

namespace granjon::geom {

namespace {

using model::Contour;
using model::Node;
using Clipper2Lib::PathD;
using Clipper2Lib::PathsD;
using Clipper2Lib::PointD;

constexpr double kFlattenTol = 0.1;   // font units (Curvz: 0.1 doc unit)
constexpr int    kMaxDepth = 16;
constexpr int    kPrecision = 3;      // Clipper2's decimal grid: 0.001 unit

struct Tag { int contour, segment; double t; bool anchor; };
struct Cross { std::int64_t e[4]; PointD pt; };

bool flat(const CubicSegment& s, double tol) {
    const double dx = s.p3.x - s.p0.x, dy = s.p3.y - s.p0.y, c2 = dx * dx + dy * dy;
    if (c2 < 1e-12) return (s.p1 - s.p0).length_sq() <= tol * tol && (s.p2 - s.p0).length_sq() <= tol * tol;
    const double a = (s.p1.x - s.p0.x) * dy - (s.p1.y - s.p0.y) * dx;
    const double b = (s.p2.x - s.p0.x) * dy - (s.p2.y - s.p0.y) * dx;
    return a * a <= tol * tol * c2 && b * b <= tol * tol * c2;
}

// Interior points of a segment with their t (end excluded).
void flatten(const CubicSegment& s, double t0, double t1, int depth, std::vector<std::pair<Vec2, double>>& out, bool last) {
    if (depth >= kMaxDepth || flat(s, kFlattenTol)) {
        if (!last) out.push_back({s.p3, t1});
        return;
    }
    const auto [a, b] = s.split(0.5);
    const double tm = (t0 + t1) / 2;
    flatten(a, t0, tm, depth + 1, out, false);
    flatten(b, tm, t1, depth + 1, out, last);
}

int wrap(int i, int n) { return ((i % n) + n) % n; }

// The segment an input edge (two tagged endpoints) belongs to.
std::optional<std::pair<int, int>> edge_segment(const std::vector<Tag>& tags, const std::vector<int>& nseg, std::int64_t za, std::int64_t zb) {
    if (za <= 0 || zb <= 0) return std::nullopt;
    const Tag& a = tags[static_cast<std::size_t>(za - 1)];
    const Tag& b = tags[static_cast<std::size_t>(zb - 1)];
    if (a.contour != b.contour) return std::nullopt;
    const int n = nseg[static_cast<std::size_t>(a.contour)];
    if (a.segment == b.segment) return std::pair{a.contour, a.segment};
    if (b.anchor && b.segment == wrap(a.segment + 1, n)) return std::pair{a.contour, a.segment};
    if (a.anchor && a.segment == wrap(b.segment + 1, n)) return std::pair{a.contour, b.segment};
    return std::pair{a.contour, a.anchor ? b.segment : a.segment};
}

CubicSegment sub(const CubicSegment& s, double t0, double t1) {
    const bool rev = t0 > t1;
    if (rev) std::swap(t0, t1);
    CubicSegment r = s;
    if (t1 < 1 - 1e-12) r = r.split(t1).first;
    if (t0 > 1e-12 && t1 > 1e-12) r = r.split(t0 / t1).second;
    if (rev) { std::swap(r.p0, r.p3); std::swap(r.p1, r.p2); }
    return r;
}

// A line kept as a cubic has its handles on its ends: the same points, but a
// vanishing tangent at the ends and an uneven t. Solve on the evenly
// parameterised copy (handles at thirds) -- the geometry is identical.
CubicSegment solvable(const CubicSegment& s, bool line) {
    if (!line) return s;
    return {s.p0, s.p0 + (s.p3 - s.p0) / 3.0, s.p0 + (s.p3 - s.p0) * (2.0 / 3.0), s.p3};
}

// Where two segments cross near p: Newton on S1(t1) = S2(t2). `ok` false when
// it does not converge (the caller keeps Clipper2's own point then).
// A plain struct, not std::pair: GCC on aarch64 notes pair's C++17 ABI change.
struct CrossT { double t1, t2; };
CrossT solve_cross(const CubicSegment& a, const CubicSegment& b, Vec2 p, bool* ok) {
    double t1 = a.nearest_t(p), t2 = b.nearest_t(p);
    *ok = false;
    for (int it = 0; it < 20; ++it) {
        const Vec2 f = a.at(t1) - b.at(t2);
        if (f.length_sq() < 1e-12) { *ok = true; break; }
        const Vec2 d1 = a.tangent(t1), d2 = b.tangent(t2) * -1.0;
        const double det = d1.x * d2.y - d1.y * d2.x;
        if (std::abs(det) < 1e-12) break;
        const double dt1 = (f.x * d2.y - f.y * d2.x) / det;
        const double dt2 = (d1.x * f.y - d1.y * f.x) / det;
        t1 = std::clamp(t1 - dt1, 0.0, 1.0);
        t2 = std::clamp(t2 - dt2, 0.0, 1.0);
    }
    if (!*ok && (a.at(t1) - b.at(t2)).length_sq() < 1e-6) *ok = true;
    return {t1, t2};
}

double polygon_area(const PathD& p) {
    double a = 0;
    for (std::size_t i = 0; i < p.size(); ++i) {
        const auto& u = p[i];
        const auto& v = p[(i + 1) % p.size()];
        a += u.x * v.y - v.x * u.y;
    }
    return a / 2;
}

}  // namespace

OverlapResult remove_overlaps(const std::vector<Contour>& in) {
    OverlapResult res;
    res.contours = in;
    std::vector<int> closed_ix;
    for (int i = 0; i < static_cast<int>(in.size()); ++i)
        if (in[static_cast<std::size_t>(i)].closed && in[static_cast<std::size_t>(i)].nodes.size() >= 2) closed_ix.push_back(i);
    if (closed_ix.empty()) return res;

    // ── flatten, tagged ──
    std::vector<Tag> tags;
    std::vector<int> nseg(in.size(), 0);
    PathsD subject;
    for (int ci : closed_ix) {
        const Contour& c = in[static_cast<std::size_t>(ci)];
        const int n = segment_count(c);
        nseg[static_cast<std::size_t>(ci)] = n;
        PathD path;
        for (int si = 0; si < n; ++si) {
            const Node& a = c.nodes[static_cast<std::size_t>(si)];
            tags.push_back({ci, si, 0.0, true});
            path.push_back(PointD(a.x, a.y, static_cast<std::int64_t>(tags.size())));
            const CubicSegment s = segment(c, si);
            if (!c.nodes[static_cast<std::size_t>(si)].has_out() && !c.nodes[static_cast<std::size_t>((si + 1) % c.nodes.size())].has_in()) continue;
            std::vector<std::pair<Vec2, double>> pts;
            flatten(s, 0, 1, 0, pts, true);
            for (const auto& [p, t] : pts) {
                tags.push_back({ci, si, t, false});
                path.push_back(PointD(p.x, p.y, static_cast<std::int64_t>(tags.size())));
            }
        }
        subject.push_back(std::move(path));
    }

    // ── union, nonzero, crossings tagged ──
    std::vector<Cross> crosses;
    Clipper2Lib::ClipperD clip(kPrecision);
    clip.PreserveCollinear(true);
    clip.SetZCallback([&](const PointD& e1b, const PointD& e1t, const PointD& e2b, const PointD& e2t, PointD& pt) {
        crosses.push_back({{e1b.z, e1t.z, e2b.z, e2t.z}, pt});
        pt.z = -static_cast<std::int64_t>(crosses.size());
    });
    clip.AddSubject(subject);
    PathsD out;
    if (!clip.Execute(Clipper2Lib::ClipType::Union, Clipper2Lib::FillRule::NonZero, out)) return res;

    double clip_area = 0;
    for (const auto& p : out) clip_area += polygon_area(p);

    // ── untouched? (no crossing, every contour back whole) ──
    {
        bool whole = crosses.empty() && out.size() == closed_ix.size();
        if (whole) {
            std::set<int> seen;
            for (const auto& p : out) {
                std::set<std::pair<int, int>> anchors;
                int ci = -1;
                for (const auto& v : p) {
                    if (v.z <= 0) { whole = false; break; }
                    const Tag& t = tags[static_cast<std::size_t>(v.z - 1)];
                    if (ci < 0) ci = t.contour;
                    if (t.contour != ci) { whole = false; break; }
                    if (t.anchor) anchors.insert({t.contour, t.segment});
                }
                if (!whole || ci < 0 || static_cast<int>(anchors.size()) != nseg[static_cast<std::size_t>(ci)] || !seen.insert(ci).second) {
                    whole = false;
                    break;
                }
            }
        }
        if (whole) return res;   // no overlaps: the input, as drawn
    }

    // ── rebuild each loop from the original segments ──
    auto seg_of = [&](int ci, int si) { return segment(in[static_cast<std::size_t>(ci)], si); };
    // A crossing's segments and the exact point / t on each.
    struct Solved { Vec2 p; std::map<std::pair<int, int>, double> t; std::set<std::pair<int, int>> segs; };
    std::vector<Solved> solved(crosses.size());
    for (std::size_t k = 0; k < crosses.size(); ++k) {
        const auto& x = crosses[k];
        Solved s;
        s.p = {x.pt.x, x.pt.y};
        auto e1 = edge_segment(tags, nseg, x.e[0], x.e[1]);
        auto e2 = edge_segment(tags, nseg, x.e[2], x.e[3]);
        if (e1) s.segs.insert(*e1);
        if (e2) s.segs.insert(*e2);
        if (e1 && e2) {
            auto is_line_seg = [&](std::pair<int, int> sg) {
                const Contour& c = in[static_cast<std::size_t>(sg.first)];
                return !c.nodes[static_cast<std::size_t>(sg.second)].has_out() &&
                       !c.nodes[static_cast<std::size_t>((sg.second + 1) % c.nodes.size())].has_in();
            };
            const CubicSegment a = solvable(seg_of(e1->first, e1->second), is_line_seg(*e1));
            const CubicSegment b = solvable(seg_of(e2->first, e2->second), is_line_seg(*e2));
            bool ok = false;
            const auto [t1, t2] = solve_cross(a, b, s.p, &ok);
            if (ok) s.p = (a.at(t1) + b.at(t2)) * 0.5;
            // t on the segments as stored (a line's own t never cuts anything: lines go out straight)
            s.t[*e1] = is_line_seg(*e1) ? t1 : (ok ? t1 : seg_of(e1->first, e1->second).nearest_t(s.p));
            s.t[*e2] = is_line_seg(*e2) ? t2 : (ok ? t2 : seg_of(e2->first, e2->second).nearest_t(s.p));
        }
        solved[k] = std::move(s);
    }
    res.crossings = static_cast<int>(crosses.size());

    std::vector<std::pair<int, Contour>> loops;   // (sort key, contour)
    int unresolved = 0;
    for (const auto& p : out) {
        const std::size_t m = p.size();
        std::vector<std::size_t> nodes;
        for (std::size_t i = 0; i < m; ++i)
            if (p[i].z < 0 || (p[i].z > 0 && tags[static_cast<std::size_t>(p[i].z - 1)].anchor)) nodes.push_back(i);
        if (nodes.empty()) { res.contours = in; res.fallback = true; res.why = "a result loop with no node"; return res; }
        // Start at the earliest original node (start points survive where they can).
        std::size_t start = 0;
        int best = INT32_MAX;
        for (std::size_t k = 0; k < nodes.size(); ++k) {
            const auto z = p[nodes[k]].z;
            if (z <= 0) continue;
            const Tag& t = tags[static_cast<std::size_t>(z - 1)];
            const int key = t.contour * 100000 + t.segment;
            if (key < best) { best = key; start = k; }
        }
        std::rotate(nodes.begin(), nodes.begin() + static_cast<long>(start), nodes.end());

        auto t_on = [&](std::int64_t z, std::pair<int, int> seg, bool* ok) -> double {
            *ok = true;
            if (z < 0) {
                const Solved& s = solved[static_cast<std::size_t>(-z - 1)];
                if (auto it = s.t.find(seg); it != s.t.end()) return it->second;
                return seg_of(seg.first, seg.second).nearest_t(s.p);
            }
            const Tag& t = tags[static_cast<std::size_t>(z - 1)];
            const int n = nseg[static_cast<std::size_t>(seg.first)];
            if (t.contour == seg.first && t.segment == seg.second) return 0.0;
            if (t.contour == seg.first && t.segment == wrap(seg.second + 1, n)) return 1.0;
            *ok = false;
            return 0.0;
        };
        auto node_pos = [&](std::int64_t z) -> Vec2 {
            if (z < 0) return solved[static_cast<std::size_t>(-z - 1)].p;
            const Tag& t = tags[static_cast<std::size_t>(z - 1)];
            const Node& n = in[static_cast<std::size_t>(t.contour)].nodes[static_cast<std::size_t>(t.segment)];
            return {n.x, n.y};
        };

        std::vector<CubicSegment> pieces;
        std::vector<bool> is_line;
        for (std::size_t k = 0; k < nodes.size(); ++k) {
            const std::size_t ia = nodes[k], ib = nodes[(k + 1) % nodes.size()];
            const std::int64_t za = p[ia].z, zb = p[ib].z;
            std::optional<std::pair<int, int>> seg;
            // interior samples name the segment
            bool sampled = false;
            for (std::size_t i = (ia + 1) % m; i != ib; i = (i + 1) % m)
                if (p[i].z > 0) { const Tag& t = tags[static_cast<std::size_t>(p[i].z - 1)]; seg = std::pair{t.contour, t.segment}; sampled = true; break; }
            if (!seg) {
                std::vector<std::pair<int, int>> ca, cb;
                auto cands = [&](std::int64_t z, std::vector<std::pair<int, int>>& c) {
                    if (z < 0) { for (const auto& s : solved[static_cast<std::size_t>(-z - 1)].segs) c.push_back(s); return; }
                    const Tag& t = tags[static_cast<std::size_t>(z - 1)];
                    c.push_back({t.contour, t.segment});
                    c.push_back({t.contour, wrap(t.segment - 1, nseg[static_cast<std::size_t>(t.contour)])});
                };
                cands(za, ca);
                cands(zb, cb);
                for (const auto& s : ca)
                    if (std::find(cb.begin(), cb.end(), s) != cb.end()) {
                        bool ok1 = false, ok2 = false;
                        t_on(za, s, &ok1);
                        t_on(zb, s, &ok2);
                        if (ok1 && ok2) { seg = s; break; }
                    }
            }
            const Vec2 A = node_pos(za), B = node_pos(zb);
            if (!seg) {
                // No curve sample between the two nodes: the source was straight
                // here (a curve leaves samples unless it is within the flattening
                // tolerance of its chord) -- e.g. two contours' edges running
                // collinear, a bar flush with a stem. A line, not a failure.
                if (sampled) ++unresolved;
                pieces.push_back({A, A, B, B});
                is_line.push_back(true);
                continue;
            }
            bool ok1 = false, ok2 = false;
            const double ta = t_on(za, *seg, &ok1), tb = t_on(zb, *seg, &ok2);
            const Contour& sc = in[static_cast<std::size_t>(seg->first)];
            const bool line = !sc.nodes[static_cast<std::size_t>(seg->second)].has_out() &&
                              !sc.nodes[static_cast<std::size_t>((seg->second + 1) % sc.nodes.size())].has_in();
            if (!ok1 || !ok2 || line) {
                if ((!ok1 || !ok2) && sampled && !line) ++unresolved;
                pieces.push_back({A, A, B, B});
                is_line.push_back(true);
                continue;
            }
            CubicSegment c = sub(seg_of(seg->first, seg->second), ta, tb);
            // pin the ends to the nodes (a crossing is the average of two curves)
            c.p1 = c.p1 + (A - c.p0);
            c.p2 = c.p2 + (B - c.p3);
            c.p0 = A;
            c.p3 = B;
            pieces.push_back(c);
            is_line.push_back(false);
        }
        Contour rc;
        rc.closed = true;
        for (std::size_t k = 0; k < nodes.size(); ++k) {
            const std::int64_t z = p[nodes[k]].z;
            const CubicSegment& next = pieces[k];
            const CubicSegment& prev = pieces[(k + nodes.size() - 1) % nodes.size()];
            Node n = Node::corner(next.p0.x, next.p0.y);
            if (!is_line[(k + nodes.size() - 1) % nodes.size()]) { n.cx1 = prev.p2.x; n.cy1 = prev.p2.y; }
            if (!is_line[k]) { n.cx2 = next.p1.x; n.cy2 = next.p1.y; }
            if (z > 0) {
                const Tag& t = tags[static_cast<std::size_t>(z - 1)];
                n.type = in[static_cast<std::size_t>(t.contour)].nodes[static_cast<std::size_t>(t.segment)].type;
            }
            if (!n.has_in() && !n.has_out()) n.type = Node::Type::Corner;
            else if (n.type == Node::Type::Symmetric && (!n.has_in() || !n.has_out())) n.type = Node::Type::Smooth;
            rc.nodes.push_back(n);
        }
        loops.push_back({best, std::move(rc)});
    }
    std::stable_sort(loops.begin(), loops.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    // ── the safety net ──
    double area = 0;
    std::vector<Contour> rebuilt;
    for (auto& [k, c] : loops) { area += signed_area(c); rebuilt.push_back(std::move(c)); }
    if (unresolved > 0 || std::abs(area - clip_area) > 2.0 + std::abs(clip_area) * 0.002) {
        res.contours = in;
        res.fallback = true;
        res.why = unresolved ? std::to_string(unresolved) + " piece(s) not traced to a source segment"
                             : "area " + std::to_string(area) + " vs " + std::to_string(clip_area);
        return res;
    }
    for (std::size_t i = 0; i < in.size(); ++i)
        if (!in[i].closed || in[i].nodes.size() < 2) rebuilt.push_back(in[i]);   // open contours as they were
    res.contours = std::move(rebuilt);
    res.changed = true;
    return res;
}

}  // namespace granjon::geom
