#include "model/Checks.hpp"

#include "geom/Edit.hpp"
#include "model/Bounds.hpp"
#include "model/Spacing.hpp"
#include "unicode/UcdTable.hpp"

#include <algorithm>
#include <cmath>
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

}  // namespace

const std::vector<CheckInfo>& checks() {
    static const std::vector<CheckInfo> table = {
        {CheckId::Direction, "direction", "Contour direction",
         "CFF wants outer contours counter-clockwise and counters clockwise. The wrong way fills a counter "
         "solid in some renderers and confuses hinting and overlap removal."},
        {CheckId::Open, "open", "Open contours",
         "A glyph outline has to enclose its shape. An open contour has no inside: it draws nothing, or "
         "draws unpredictably, once the font is compiled."},
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

std::optional<Glyph> fix_glyph(const Font& f, const std::string& name, const std::vector<Finding>& xs) {
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
    // 2. the grid
    for (const auto& x : xs)
        if (x.glyph == name && x.check == CheckId::Origin && x.fixable) { g.snap(); break; }
    // 3. close
    for (const auto& x : xs)
        if (x.glyph == name && x.check == CheckId::Open && x.contour >= 0 && x.contour < static_cast<int>(g.contours.size()))
            g.contours[x.contour].closed = true;
    // 4. reverse (each contour once)
    std::set<int> rev;
    for (const auto& x : xs)
        if (x.glyph == name && x.check == CheckId::Direction && x.contour >= 0 &&
            x.contour < static_cast<int>(g.contours.size()))
            rev.insert(x.contour);
    for (int c : rev) geom::reverse(g.contours[c]);
    if (g == *was) return std::nullopt;
    return g;
}

}  // namespace granjon::model
