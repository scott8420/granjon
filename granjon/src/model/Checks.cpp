#include "model/Checks.hpp"

#include "geom/Edit.hpp"
#include "geom/Overlap.hpp"
#include "geom/Corners.hpp"
#include "model/Bounds.hpp"
#include "model/Spacing.hpp"
#include "unicode/UcdTable.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace granjon::model {

namespace {

std::string num(double v) {
    if (std::abs(v - std::round(v)) < 1e-9) return std::to_string(static_cast<long>(std::round(v)));
    char b[32];
    std::snprintf(b, sizeof b, "%.2f", v);
    return b;
}

struct Line { double y; const char* name; };

std::vector<Line> lines_of(const Metrics& m) {
    return {{0.0, "baseline"}, {double(m.x_height), "x-height"}, {double(m.cap_height), "cap height"},
            {double(m.ascender), "ascender"}, {double(m.descender), "descender"}};
}

// A round extreme: a node with handles, every handle level with it (a
// horizontal tangent), its neighbours both below (a top) or both above (a
// bottom). +1 top, -1 bottom, 0 neither.
int round_extreme(const Contour& c, int i) {
    const int n = static_cast<int>(c.nodes.size());
    if (n < 3) return 0;
    const Node& p = c.nodes[i];
    if (!p.has_in() && !p.has_out()) return 0;
    if (p.has_in() && p.cy1 != p.y) return 0;
    if (p.has_out() && p.cy2 != p.y) return 0;
    if (!c.closed && (i == 0 || i == n - 1)) return 0;
    const Node& a = c.nodes[(i - 1 + n) % n];
    const Node& b = c.nodes[(i + 1) % n];
    if (a.y < p.y && b.y < p.y) return 1;
    if (a.y > p.y && b.y > p.y) return -1;
    return 0;
}

std::string spot(int c, int i) { return std::to_string(c) + "." + std::to_string(i); }


// ── s10: stems and sides ───────────────────────────────────────────────────
// A straight vertical edge: a line segment (no handles) with |dx| under half
// a unit, at least `min_len` long. `ink_right`: the ink lies to its right
// (a left edge of a stem) -- going down on a contour running the right way.
struct VEdge { int c = -1, s = -1; double x = 0, y0 = 0, y1 = 0; bool ink_right = false; };

std::vector<VEdge> vertical_edges(const Glyph& g, double min_len) {
    std::vector<VEdge> out;
    const auto wrong = geom::wrong_way(g.contours);
    for (int c = 0; c < static_cast<int>(g.contours.size()); ++c) {
        const auto& con = g.contours[c];
        if (!con.closed) continue;
        const bool flip = std::find(wrong.begin(), wrong.end(), c) != wrong.end();
        const int n = static_cast<int>(con.nodes.size());
        for (int s = 0; s < n; ++s) {
            const Node& a = con.nodes[s];
            const Node& b = con.nodes[(s + 1) % n];
            if (a.has_out() || b.has_in() || std::abs(a.x - b.x) >= 0.5 || std::abs(b.y - a.y) < min_len) continue;
            VEdge e;
            e.c = c;
            e.s = s;
            e.x = (a.x + b.x) / 2;
            e.y0 = std::min(a.y, b.y);
            e.y1 = std::max(a.y, b.y);
            e.ink_right = (b.y < a.y) != flip;
            out.push_back(e);
        }
    }
    return out;
}

struct Stem { VEdge l, r; double w; };
std::vector<Stem> stems_of(const Glyph& g, double min_len) {
    const auto es = vertical_edges(g, min_len);
    std::vector<Stem> out;
    for (const auto& l : es) {
        if (!l.ink_right) continue;
        const VEdge* best = nullptr;
        for (const auto& r : es) {
            if (r.ink_right || r.x <= l.x + 1) continue;
            const double ov = std::min(l.y1, r.y1) - std::max(l.y0, r.y0);
            if (ov < 0.5 * std::min(l.y1 - l.y0, r.y1 - r.y0)) continue;
            if (!best || r.x < best->x) best = &r;
        }
        if (best) out.push_back({l, *best, best->x - l.x});
    }
    return out;
}

// 'L' / 'R': the kind of a glyph's left or right side -- 's' straight (a long
// vertical edge at the extreme), 'r' round (a curve node at the extreme), 0 neither.
char side_kind(const Glyph& g, bool left, double min_len) {
    if (g.contours.empty()) return 0;
    double ext = left ? 1e18 : -1e18;
    for (const auto& c : g.contours)
        for (const auto& p : c.nodes) ext = left ? std::min(ext, p.x) : std::max(ext, p.x);
    for (const auto& e : vertical_edges(g, min_len))
        if (std::abs(e.x - ext) < 0.5 && e.ink_right == left) return 's';
    // round: a bowl's extreme -- a smooth node, both handles plumb above / below it
    for (const auto& c : g.contours)
        for (const auto& p : c.nodes)
            if (std::abs(p.x - ext) < 0.5 && p.has_in() && p.has_out() && std::abs(p.cx1 - p.x) < 0.5 &&
                std::abs(p.cx2 - p.x) < 0.5)
                return 'r';
    return 0;
}

bool is_case(const Glyph& g, const char* cat) {
    return !g.unicodes.empty() && unicode::category(g.unicodes.front()) == cat;
}

// The font's stem: the preset's record, else the control glyph's widest-agreed stem.
double ref_stem(const Font& f, bool caps) {
    const auto& o = f.info().origin;
    if (caps && o.cap_stem_v > 0) return o.cap_stem_v;
    if (!caps && o.stem_v > 0) return o.stem_v;
    const Glyph* ctl = f.glyph(caps ? "H" : "n");
    if (!ctl) return 0;
    const auto& m = f.metrics();
    const auto st = stems_of(*ctl, 0.3 * (caps ? m.cap_height : m.x_height));
    return st.empty() ? 0 : std::round(st.front().w);
}

}  // namespace

const std::vector<CheckInfo>& checks() {
    static const std::vector<CheckInfo> table = {
        {CheckId::Direction, "direction", "Contour direction",
         "CFF wants outer contours counter-clockwise and counters clockwise. The wrong way fills a counter "
         "solid in some renderers and confuses hinting and overlap removal."},
        {CheckId::Open, "open", "Open contours",
         "A glyph outline has to enclose its shape. An open contour has no inside: it draws nothing, or "
         "draws unpredictably, once the font is compiled."},
        {CheckId::Overlap, "overlaps", "Overlapping paths",
         "Paths that overlap fill twice where they meet: some printers and rasterisers show the seam or a "
         "dark double, and hinting and the contour direction can't be judged. Remove overlaps, or combine "
         "them with a boolean."},
        {CheckId::Extremes, "extremes", "Extreme points",
         "A curve should have a node where it is furthest left, right, up or down. Without one, hinting has "
         "nothing to hold the edge to, overshoots can't be measured, and small sizes lose the curve's edge."},
        {CheckId::InkTraps, "inktraps", "Ink traps (warning)",
         "Where two strokes meet at a tight angle -- p's bowl into its stem, inside v's point, n's shoulder -- "
         "ink spreads into the narrow white and fills it at small sizes or on soft paper. A trap (a small notch "
         "cut into the ink) keeps the join open. Advice, not a defect: skip it for display sizes.", true},
        {CheckId::Stems, "stems", "Stem weights (warning)",
         "A vertical stem a unit or a few off the font's stem reads as uneven colour in text -- the eye catches "
         "a 2 % difference across a line. Fix moves the stem's right edge to the font's weight; skip a stem "
         "that is meant to differ (a lighter r or t).", true},
        {CheckId::Sidebearings, "sidebearings", "Sidebearings (warning)",
         "Letters with the same kind of side -- straight like n and H, round like o and O -- usually share its "
         "sidebearing. A unit or a few off is drift, not intent. Fix sets it to the control's; a metrics key "
         "keeps it there for good.", true},
        {CheckId::Zones, "zones", "Near the metric lines",
         "A flat top or bottom a unit or three off the baseline, x-height or cap height reads as a mistake "
         "at text sizes and breaks hinting's alignment zones. Snap it to the line."},
        {CheckId::Overshoot, "overshoot", "Overshoot",
         "Rounds must reach a little past the line to look as tall as the flats. A round sitting on the line "
         "looks small; one off the font's overshoot looks uneven beside its neighbours."},
        {CheckId::Origin, "origin", "Origin and grid",
         "Coordinates off the unit grid round unpredictably on export. A drawn glyph with no width, or an "
         "outline outside its own width, collides with its neighbours."},
        {CheckId::Keys, "keys", "Metrics keys",
         "The glyph's sidebearings no longer match its metrics key -- a related glyph changed, or this one "
         "was moved by hand. Apply the key, or change it."},
    };
    return table;
}

const CheckInfo& check_info(CheckId id) {
    for (const auto& c : checks()) if (c.id == id) return c;
    return checks().front();
}

std::string Finding::key() const { return std::string(check_info(check).tag) + "|" + glyph + "|" + detail; }

double zone_tolerance(const Font& f) { return 3.0 * std::max(16, f.info().units_per_em) / 1000.0; }

bool is_skipped(const Font& f, const Finding& x) { return f.skips().keys.count(x.key()) > 0; }

std::vector<Finding> check_glyph(const Font& f, const std::string& name) {
    std::vector<Finding> out;
    const Glyph* g = f.glyph(name);
    if (!g) return out;
    const auto& m = f.metrics();
    const double tol = zone_tolerance(f);
    const double os = m.overshoot;
    auto add = [&](CheckId id, std::string msg, std::string detail, bool fixable, int c = -1, int i = -1,
                   double target = 0) {
        Finding x;
        x.check = id;
        x.glyph = name;
        x.message = std::move(msg);
        x.detail = std::move(detail);
        x.fixable = fixable;
        x.contour = c;
        x.index = i;
        x.target = target;
        out.push_back(std::move(x));
    };

    // Direction.
    for (int c : geom::wrong_way(g->contours)) {
        const bool outer = geom::depth(g->contours, c) % 2 == 0;
        add(CheckId::Direction,
            "Contour " + std::to_string(c + 1) + (outer ? " (an outer) runs clockwise" : " (a counter) runs counter-clockwise"),
            std::to_string(c), true, c);
    }
    // Open contours.
    for (int c = 0; c < static_cast<int>(g->contours.size()); ++c)
        if (!g->contours[c].closed && g->contours[c].nodes.size() >= 2)
            add(CheckId::Open, "Contour " + std::to_string(c + 1) + " is open", std::to_string(c), true, c);

    // Overlaps (s10): any shared area between two paths, a path drawn twice,
    // a path crossing itself. Direction above judges nesting only.
    for (const auto& o : geom::overlapping(g->contours)) {
        std::string msg;
        if (o.a == o.b) msg = "Contour " + std::to_string(o.a + 1) + " crosses itself";
        else if (o.same) msg = "Contours " + std::to_string(o.a + 1) + " and " + std::to_string(o.b + 1) + " are the same shape, one on the other";
        else msg = "Contours " + std::to_string(o.a + 1) + " and " + std::to_string(o.b + 1) + " overlap";
        add(CheckId::Overlap, msg, std::to_string(o.a) + "+" + std::to_string(o.b), true, o.b);
    }

    // Extreme points (M15b): a curve bulging more than a unit past its nodes.
    for (int c = 0; c < static_cast<int>(g->contours.size()); ++c)
        if (const int k = geom::count_missing_extremes(g->contours[c]); k > 0)
            add(CheckId::Extremes,
                "Contour " + std::to_string(c + 1) + " lacks " + std::to_string(k) + (k == 1 ? " extreme point" : " extreme points"),
                std::to_string(c), true, c);

    // Ink traps (s10, a warning): tight crotches without a trap -- letters and
    // figures only (a symbol's crotch is its own business).
    bool lettered = false;
    for (char32_t cp : g->unicodes) {
        const auto cat = unicode::category(cp);
        lettered = lettered || cat.starts_with("L") || cat.starts_with("N");
    }
    if (lettered || (g->unicodes.empty() && g->name.find('.') != std::string::npos)) {
        const auto wrong = geom::wrong_way(g->contours);
        const double em = std::max(16, f.info().units_per_em);
        for (int c = 0; c < static_cast<int>(g->contours.size()); ++c) {
            const auto& con = g->contours[c];
            const bool left = std::find(wrong.begin(), wrong.end(), c) == wrong.end();
            for (int i = 0; i < static_cast<int>(con.nodes.size()); ++i) {
                if (!geom::is_crotch(con, i, left) || geom::is_trap_apex(con, i, 0.08 * em, left)) continue;
                const double a = geom::white_angle(con, i);
                if (a >= kTightCrotch) continue;
                add(CheckId::InkTraps,
                    "Node " + std::to_string(i + 1) + " of contour " + std::to_string(c + 1) + ": a " +
                        std::to_string(static_cast<int>(std::lround(a))) + "-degree crotch, no trap",
                    spot(c, i), true, c, i);
            }
        }
    }

    // Stems and sides (s10, warnings): lower- and uppercase letters only.
    {
        const bool lower = is_case(*g, "Ll"), upper = is_case(*g, "Lu");
        if ((lower || upper) && !g->contours.empty()) {
            const double min_len = 0.3 * (upper ? m.cap_height : m.x_height);
            if (const double ref = ref_stem(f, upper); ref > 0)
                for (const auto& st : stems_of(*g, min_len)) {
                    const double d = ref - st.w;
                    if (std::abs(d) < 1 || std::abs(d) > 0.025 * ref) continue;   // drift, not design
                    add(CheckId::Stems,
                        "The stem at x " + num(std::round(st.l.x)) + " is " + num(st.w) + " wide; the font's " +
                            (upper ? "capital" : "lowercase") + " stem is " + num(ref),
                        std::to_string(st.r.c) + "." + std::to_string(st.r.s), true, st.r.c, st.r.s, d);
                }
            const bool keyed = check_key(f, name).has_key;
            const auto sb = sidebearings(f, *g);
            if (!keyed && sb)
                for (int side = 0; side < 2; ++side) {
                    const bool left = side == 0;
                    const char k = side_kind(*g, left, 1.7 * min_len);   // a straight side: half the height or more
                    if (!k) continue;
                    const char* ctl_name = upper ? (k == 's' ? "H" : "O") : (k == 's' ? "n" : "o");
                    if (name == ctl_name) continue;
                    const Glyph* ctl = f.glyph(ctl_name);
                    if (!ctl || side_kind(*ctl, left, 1.7 * min_len) != k) continue;
                    const auto csb = sidebearings(f, *ctl);
                    if (!csb) continue;
                    const double want = std::round(left ? csb->left : csb->right), have = left ? sb->left : sb->right;
                    const double d = want - have;
                    if (std::abs(d) < 1 || std::abs(d) > std::max(3.0, 0.05 * std::abs(want))) continue;
                    add(CheckId::Sidebearings,
                        std::string(left ? "Left" : "Right") + " side " + num(std::round(have)) + " -- " + ctl_name + "'s " +
                            (k == 's' ? "straight" : "round") + " side is " + num(want),
                        left ? "L" : "R", true, -1, -1, want);
                }
        }
    }

    // Zones (flat nodes) and overshoot (round extremes).
    const auto lines = lines_of(m);
    for (int c = 0; c < static_cast<int>(g->contours.size()); ++c) {
        const auto& con = g->contours[c];
        for (int i = 0; i < static_cast<int>(con.nodes.size()); ++i) {
            const Node& p = con.nodes[i];
            const int ext = round_extreme(con, i);
            const std::string where = "Node " + std::to_string(i + 1) + " of contour " + std::to_string(c + 1);
            if (ext == 0) {
                const Line* best = nullptr;
                double bd = 0;
                for (const auto& l : lines) {
                    const double d = p.y - l.y;
                    if (d != 0 && std::abs(d) <= tol && (!best || std::abs(d) < std::abs(bd))) { best = &l; bd = d; }
                    if (d == 0) { best = nullptr; break; }   // on a line: fine, whatever else is near
                }
                if (best)
                    add(CheckId::Zones,
                        where + " is " + num(std::abs(bd)) + (bd > 0 ? " above the " : " below the ") + best->name,
                        spot(c, i) + "|" + best->name, true, c, i, best->y);
                continue;
            }
            if (os <= 0) continue;
            // Round: the nearest line on its side, within reach of an overshoot.
            const Line* best = nullptr;
            double bd = 0;
            for (const auto& l : lines) {
                const bool top_line = l.y > 0;
                if (l.y == 0 && ext > 0) continue;                       // a top never overshoots the baseline
                if (std::string(l.name) == "descender" && ext > 0) continue;
                if (top_line && ext < 0) continue;                       // a bottom only at baseline / descender
                const double beyond = ext > 0 ? p.y - l.y : l.y - p.y;   // + = past the line
                if (beyond < -tol || beyond > 2 * os + tol) continue;
                if (!best || std::abs(beyond - os) < std::abs(bd - os)) { best = &l; bd = beyond; }
            }
            if (!best || bd == os) continue;
            const double target = ext > 0 ? best->y + os : best->y - os;
            std::string msg = where + (ext > 0 ? " (a round top)" : " (a round bottom)");
            if (std::abs(bd) <= tol) msg += " sits on the " + std::string(best->name) + " -- no overshoot";
            else msg += " overshoots the " + std::string(best->name) + " by " + num(bd) + "; the font's is " + num(os);
            add(CheckId::Overshoot, msg, spot(c, i) + "|" + best->name, true, c, i, target);
        }
    }

    // Origin and grid.
    if (!g->is_integral())
        add(CheckId::Origin, "Coordinates off the unit grid", "grid", true);
    const bool drawn = !g->contours.empty() || !g->components.empty();
    if (drawn) {
        bool mark = false;
        for (char32_t cp : g->unicodes) mark = mark || unicode::category(cp).starts_with("M");
        if (g->advance <= 0 && !mark && g->unicodes.size() > 0)
            add(CheckId::Origin, "Drawn, but no width", "width", false);
        if (g->advance > 0)
            if (const auto sb = sidebearings(f, *g); sb && (sb->left >= g->advance || sb->right >= g->advance))
                add(CheckId::Origin, "The drawing sits outside its own width", "box", false);
    }

    // Metrics keys.
    if (const auto k = check_key(f, name); k.off()) {
        std::string msg = "Off its metrics key --";
        const auto sb = sidebearings(f, *g);
        if (k.left_off && sb)  msg += " L " + num(std::round(*k.left)) + " (is " + num(sb->left) + ")";
        if (k.right_off && sb) msg += " R " + num(std::round(*k.right)) + " (is " + num(sb->right) + ")";
        if (k.width_off)       msg += " W " + num(std::round(*k.width)) + " (is " + num(g->advance) + ")";
        add(CheckId::Keys, msg, "key", true);
    }
    return out;
}

std::optional<Glyph> fix_glyph(const Font& f, const std::string& name, const std::vector<Finding>& xs,
                               const FixAmounts& amounts) {
    const Glyph* was = f.glyph(name);
    if (!was) return std::nullopt;
    Glyph g = *was;
    auto valid_node = [&](const Finding& x) {
        return x.contour >= 0 && x.contour < static_cast<int>(g.contours.size()) && x.index >= 0 &&
               x.index < static_cast<int>(g.contours[x.contour].nodes.size());
    };
    // 1. node moves (indices as found)
    for (const auto& x : xs) {
        if (x.glyph != name || (x.check != CheckId::Zones && x.check != CheckId::Overshoot) || !valid_node(x)) continue;
        const Glyph at = g;
        const double dy = x.target - g.contours[x.contour].nodes[x.index].y;
        geom::move_points(g, at, {{x.contour, x.index, geom::PointRef::Part::Node}}, {0, dy});
    }
    // 1b. stems (s10): the right edge's two nodes across by the difference
    for (const auto& x : xs) {
        if (x.glyph != name || x.check != CheckId::Stems || x.contour < 0 || x.contour >= static_cast<int>(g.contours.size())) continue;
        const int n = static_cast<int>(g.contours[x.contour].nodes.size());
        if (x.index < 0 || x.index >= n) continue;
        const Glyph at = g;
        geom::move_points(g, at, {{x.contour, x.index, geom::PointRef::Part::Node}, {x.contour, (x.index + 1) % n, geom::PointRef::Part::Node}},
                          {x.target, 0});
    }
    // 2. the grid
    for (const auto& x : xs)
        if (x.glyph == name && x.check == CheckId::Origin && x.fixable) { g.snap(); break; }
    // 3. close
    for (const auto& x : xs)
        if (x.glyph == name && x.check == CheckId::Open && x.contour >= 0 && x.contour < static_cast<int>(g.contours.size()))
            g.contours[x.contour].closed = true;
    // 3a. ink traps (by node index -- before anything else changes them),
    // one call per contour; `amounts`, else 1.5 % / 2.5 % of the em.
    {
        std::map<int, std::set<int>> traps;
        for (const auto& x : xs)
            if (x.glyph == name && x.check == CheckId::InkTraps && x.contour >= 0 &&
                x.contour < static_cast<int>(g.contours.size()))
                traps[x.contour].insert(x.index);
        if (!traps.empty()) {
            const double em = std::max(16, f.info().units_per_em);
            const auto wrong = geom::wrong_way(g.contours);
            for (const auto& [ci, nodes] : traps) {
                const bool left = std::find(wrong.begin(), wrong.end(), ci) == wrong.end();
                const double tw = amounts.trap_width > 0 ? amounts.trap_width : std::round(0.015 * em);
                const double td = amounts.trap_depth > 0 ? amounts.trap_depth : std::round(0.025 * em);
                if (geom::ink_traps(g.contours[ci], nodes, tw, td, left) > 0) {
                    Glyph t;
                    t.contours = {g.contours[ci]};
                    t.snap();
                    g.contours[ci] = t.contours[0];
                }
            }
        }
    }
    // 3b. extreme points (node indices change from here on), snapped
    for (const auto& x : xs)
        if (x.glyph == name && x.check == CheckId::Extremes && x.contour >= 0 &&
            x.contour < static_cast<int>(g.contours.size()))
            if (geom::add_extremes(g.contours[x.contour]) > 0) {
                Glyph t;
                t.contours = {g.contours[x.contour]};
                t.snap();
                g.contours[x.contour] = t.contours[0];
            }
    // 4. reverse (each contour once)
    std::set<int> rev;
    for (const auto& x : xs)
        if (x.glyph == name && x.check == CheckId::Direction && x.contour >= 0 &&
            x.contour < static_cast<int>(g.contours.size()))
            rev.insert(x.contour);
    for (int c : rev) geom::reverse(g.contours[c]);
    // 5. overlaps (s10): the whole glyph's Remove overlaps -- only when every
    // contour now runs the right way (the union reads nonzero), and only when
    // the rebuild passed its own check (else the outline stays as drawn).
    bool merge = false;
    for (const auto& x : xs) merge = merge || (x.glyph == name && x.check == CheckId::Overlap);
    if (merge && geom::wrong_way(g.contours).empty()) {
        auto r = geom::remove_overlaps(g.contours);
        if (r.changed && !r.fallback) {
            Glyph t;
            t.contours = std::move(r.contours);
            t.snap();
            g.contours = std::move(t.contours);
        }
    }
    // 6. sidebearings (s10): left first (the drawing moves, the right side kept), then right
    for (const char* which : {"L", "R"})
        for (const auto& x : xs) {
            if (x.glyph != name || x.check != CheckId::Sidebearings || x.detail != which) continue;
            const auto sb = sidebearings(f, g);
            if (!sb) continue;
            if (x.detail == "L") {
                const double d = x.target - sb->left;
                geom::shift_x(g, d);
                g.advance += d;
            } else {
                g.advance += x.target - sb->right;
            }
        }
    if (g == *was) return std::nullopt;
    return g;
}

}  // namespace granjon::model
