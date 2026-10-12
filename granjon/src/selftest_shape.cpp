// selftest_shape -- s10. Overlaps: their own finding (any shared area, a path
// drawn twice, a path crossing itself), and the direction check judging
// NESTING only (an overlapping path is not "wrong way"). M15b follows here.
#include "command/Document.hpp"
#include "command/Verbs.hpp"
#include "geom/Corners.hpp"
#include "geom/Edit.hpp"
#include "geom/Offset.hpp"
#include "geom/Overlap.hpp"
#include "geom/Shapes.hpp"
#include "model/Checks.hpp"
#include "model/Presets.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <string>

namespace geom    = granjon::geom;
namespace model   = granjon::model;
namespace command = granjon::command;

namespace {

int count(const std::vector<model::Finding>& xs, model::CheckId id) {
    return static_cast<int>(std::count_if(xs.begin(), xs.end(), [&](const auto& x) { return x.check == id; }));
}

model::Contour bowtie() {   // a figure eight: (0,0) (100,100) (100,0) (0,100)
    model::Contour c;
    c.closed = true;
    for (auto [x, y] : {std::pair{0.0, 0.0}, {100.0, 100.0}, {100.0, 0.0}, {0.0, 100.0}}) {
        model::Node n;
        n.x = x;
        n.y = y;
        n.cx1 = n.cx2 = x;
        n.cy1 = n.cy2 = y;
        c.nodes.push_back(n);
    }
    return c;
}

model::Contour doc_lens_make() {
    command::Document d;
    command::make_fixture(d.font);
    return d.font.glyph("lens")->contours[0];
}
const model::Contour& doc_lens() {
    static const model::Contour c = doc_lens_make();
    return c;
}

}  // namespace

void selftest_shape(const std::function<void(const std::string&, bool, const std::string&)>& check_) {
    auto check = [&](const std::string& n, bool ok, const std::string& d = "") { check_(n, ok, d); };
    std::cout << "s10 overlaps\n";
    using geom::Relation;

    const auto A = geom::shape_rect({{0, 0}, {100, 100}});
    const auto B = geom::shape_rect({{50, 50}, {150, 150}});
    const auto C = geom::shape_rect({{20, 20}, {80, 80}});
    const auto D = geom::shape_rect({{100, 0}, {200, 100}});   // shares A's right edge
    const auto E = geom::shape_rect({{300, 0}, {400, 100}});
    auto Ccw = C;
    geom::reverse(Ccw);                                        // clockwise: a proper counter
    {
        check("overlaps: a partial overlap", geom::relation(A, B) == Relation::Overlap);
        check("overlaps: wholly inside / holding", geom::relation(C, A) == Relation::Inside &&
                                                       geom::relation(A, C) == Relation::Contains);
        check("overlaps: the same shape twice", geom::relation(A, A) == Relation::Same);
        check("overlaps: meeting along an edge is apart", geom::relation(A, D) == Relation::Apart);
        check("overlaps: far apart", geom::relation(A, E) == Relation::Apart);
        check("overlaps: a counter's direction doesn't change its nesting", geom::relation(Ccw, A) == Relation::Inside);
    }
    {
        const auto d = geom::nest_depths({A, Ccw});
        check("nesting: outer 0, counter 1", d == std::vector<int>{0, 1});
        const auto o = geom::nest_depths({A, B});
        check("nesting: overlapping paths are both outers", o == std::vector<int>{0, 0});
        check("direction: an overlapping path is not 'wrong way'", geom::wrong_way({A, B}).empty());
        check("direction: a counter running clockwise is right", geom::wrong_way({A, Ccw}).empty());
        check("direction: a counter running counter-clockwise is wrong", geom::wrong_way({A, C}) == std::vector<int>{1});
        check("direction: a duplicate is not a counter", geom::wrong_way({A, A}).empty());
    }
    {
        const auto ov = geom::overlapping({A, B, E}, true);
        check("overlapping: one pair, with its shared region",
              ov.size() == 1 && ov[0].a == 0 && ov[0].b == 1 && !ov[0].same && ov[0].region.size() == 1);
        double area = 0;
        if (ov.size() == 1)
            for (std::size_t i = 0; i < ov[0].region[0].size(); ++i) {
                const auto& p = ov[0].region[0][i];
                const auto& q = ov[0].region[0][(i + 1) % ov[0].region[0].size()];
                area += p.x * q.y - q.x * p.y;
            }
        check("overlapping: the region is the 50 x 50 square", std::abs(std::abs(area / 2) - 2500) < 1, std::to_string(area / 2));
        check("overlapping: nested and touching are not overlaps", geom::overlapping({A, Ccw, D}).empty());
        const auto dup = geom::overlapping({A, A});
        check("overlapping: a duplicate is flagged as the same shape", dup.size() == 1 && dup[0].same);
        const auto self = geom::overlapping({bowtie()});
        check("overlapping: a path crossing itself", self.size() == 1 && self[0].a == 0 && self[0].b == 0);
        check("overlapping: an ellipse doesn't cross itself", geom::overlapping({geom::shape_ellipse({{0, 0}, {300, 200}})}).empty());
    }
    {   // the finding and its fix, on the fixture's n (two stems)
        command::Document doc;
        command::make_fixture(doc.font);
        auto& j = doc.journal;
        auto& f = doc.font;
        const auto before = model::check_glyph(f, "n");
        check("checks: the fixture's n has no overlap", count(before, model::CheckId::Overlap) == 0);
        const auto& n0 = *f.glyph("n");
        // a copy of the left stem, offset (Clone's 20 / 20)
        auto clone = n0.contours[0];
        for (auto& p : clone.nodes) { p.x += 20; p.y += 20; p.cx1 += 20; p.cy1 += 20; p.cx2 += 20; p.cy2 += 20; }
        int at = -1;
        command::add_shape(j, f, "n", clone, "Clone", &at);
        const auto xs = model::check_glyph(f, "n");
        check("checks: a clone over its original -> one overlap finding", count(xs, model::CheckId::Overlap) == 1);
        check("checks: ... and no direction finding (s09's false 'wrong way')", count(xs, model::CheckId::Direction) == 0);
        auto it = std::find_if(xs.begin(), xs.end(), [](const auto& x) { return x.check == model::CheckId::Overlap; });
        check("checks: the overlap finding is fixable and names both", it != xs.end() && it->fixable &&
                                                                          it->message.find("overlap") != std::string::npos);
        const std::size_t steps = j.size();
        const std::size_t ncont = f.glyph("n")->contours.size();
        std::vector<model::Finding> one;
        if (it != xs.end()) one.push_back(*it);
        const bool fixed = command::fix_findings(j, f, one);
        const auto& n1 = *f.glyph("n");
        check("checks: Fix merges them (one undo step)", fixed && j.size() == steps + 1 && n1.contours.size() == ncont - 1,
              std::to_string(n1.contours.size()));
        check("checks: after the fix, no overlap and nothing else new",
              count(model::check_glyph(f, "n"), model::CheckId::Overlap) == 0 &&
                  count(model::check_glyph(f, "n"), model::CheckId::Direction) == 0);
        j.undo();
        check("checks: undo brings the clone back", f.glyph("n")->contours.size() == ncont);
        // a duplicate in place
        command::add_shape(j, f, "n", n0.contours[1], "Duplicate", &at);
        const auto ys = model::check_glyph(f, "n");
        bool same = false;
        for (const auto& x : ys) same = same || (x.check == model::CheckId::Overlap && x.message.find("same shape") != std::string::npos);
        check("checks: a duplicate in place is 'the same shape'", same);
    }

    std::cout << "M15b extremes, corners, ink traps\n";
    {   // extremes
        const auto& lens = doc_lens();
        check("extremes: an ellipse turned 45 degrees lacks 4 (one per quarter)", geom::count_missing_extremes(lens) == 4,
              std::to_string(geom::count_missing_extremes(lens)));
        check("extremes: an upright ellipse lacks none",
              geom::count_missing_extremes(geom::shape_ellipse({{0, 0}, {300, 200}})) == 0);
        check("extremes: lines never do", geom::count_missing_extremes(A) == 0);
        auto e = lens;
        const int added = geom::add_extremes(e);
        check("extremes: 4 added, none missing after", added == 4 && geom::count_missing_extremes(e) == 0 && e.nodes.size() == 8);
        check("extremes: the outline is unchanged (area)",
              std::abs(geom::signed_area(e) - geom::signed_area(lens)) < 0.002 * std::abs(geom::signed_area(lens)));
        int level = 0;
        for (const auto& p : e.nodes)
            if ((p.cx1 == p.x && p.cx2 == p.x) || (p.cy1 == p.y && p.cy2 == p.y)) ++level;
        check("extremes: each new node's handles are exactly level or plumb", level == 4, std::to_string(level));
    }
    {   // round corners
        auto sq = A;   // 100 x 100
        const int k = geom::round_corners(sq, {0, 1, 2, 3}, 20);
        const double want = 10000 - 4 * (400 - 3.14159265 * 400 / 4);
        check("round: four corners -> eight nodes", k == 4 && sq.nodes.size() == 8);
        check("round: radius 20 -> quarter circles (area)", std::abs(geom::signed_area(sq) - want) < 2,
              std::to_string(geom::signed_area(sq)) + " vs " + std::to_string(want));
        check("round: still counter-clockwise, still closed", geom::signed_area(sq) > 0 && sq.closed);
        bool tangent = true;
        for (const auto& p : sq.nodes) tangent = tangent && p.type == model::Node::Type::Smooth;
        check("round: the cut ends are smooth (tangent) nodes", tangent);
        auto big = A;
        geom::round_corners(big, {0, 1, 2, 3}, 500);
        check("round: a radius past half an edge is clamped (no crossing)", big.nodes.size() == 8 &&
                                                                            geom::overlapping({big}).empty());
        auto one = A;
        geom::round_corners(one, {2}, 30);
        check("round: one corner alone", one.nodes.size() == 5);
        auto el = geom::shape_ellipse({{0, 0}, {300, 200}});
        check("round: smooth nodes are not corners", geom::round_corners(el, {0, 1, 2, 3}, 20) == 0);
    }
    {   // ink traps: an L (the inner corner is a crotch)
        model::Contour L;
        for (auto [x, y] : {std::pair{0.0, 0.0}, {300.0, 0.0}, {300.0, 80.0}, {80.0, 80.0}, {80.0, 400.0}, {0.0, 400.0}})
            L.nodes.push_back(model::Node::corner(x, y));
        int crotches = 0;
        for (int i = 0; i < 6; ++i) crotches += geom::is_crotch(L, i) ? 1 : 0;
        check("ink trap: an L has one crotch (its inner corner)", crotches == 1 && geom::is_crotch(L, 3));
        auto t = L;
        const int k = geom::ink_traps(t, {0, 1, 2, 3, 4, 5}, 20, 30);
        check("ink trap: only the crotch is cut (one node -> three)", k == 1 && t.nodes.size() == 8);
        const geom::Vec2 probe{80 - 10 / std::sqrt(2.0), 80 - 10 / std::sqrt(2.0)};   // 10 into the ink (south-west) along the bisector
        check("ink trap: the notch cuts into the ink", geom::inside({L}, probe) && !geom::inside({t}, probe));
        check("ink trap: the apex is `depth` from the corner",
              std::abs(geom::Vec2(t.nodes[4].x, t.nodes[4].y).dist({80, 80}) - 30) < 1e-6);
        check("ink trap: a convex corner is no crotch", !geom::is_crotch(L, 0) && geom::ink_traps(L, {0}, 20, 30) == 0);
    }
    {   // the verbs
        command::Document doc;
        command::make_fixture(doc.font);
        auto& j = doc.journal;
        auto& f = doc.font;
        std::string why;
        int done = 0;
        const auto before = f.snapshot();
        const bool r = command::round_corners(j, f, "H", geom::whole_contours(*f.glyph("H"), {{0, 0, geom::PointRef::Part::Node}}), 20, &done, &why);
        check("round verb: the stem's four corners, one step", r && done == 4 && j.size() == 1 &&
                                                                   f.glyph("H")->contours[0].nodes.size() == 8, why);
        check("round verb: snapped to units", f.glyph("H")->is_integral());
        j.undo();
        check("round verb: undo", f.snapshot() == before);
        check("round verb: refused on smooth nodes (no step)",
              !command::round_corners(j, f, "lens", {{0, 0, geom::PointRef::Part::Node}}, 20, nullptr, &why) && !why.empty(), why);
        const bool t = command::ink_traps(j, f, "O", geom::whole_contours(*f.glyph("O"), {{1, 0, geom::PointRef::Part::Node}}), 15, 25, &done, &why);
        check("ink trap verb: a square counter's 4 corners (a wrong-way counter handled)", t && done == 4 &&
                                                                                          f.glyph("O")->contours[1].nodes.size() == 12, why);
        check("ink trap verb: refused on the outer (no crotch)",
              !command::ink_traps(j, f, "O", geom::whole_contours(*f.glyph("O"), {{0, 0, geom::PointRef::Part::Node}}), 15, 25, nullptr, &why), why);
        const auto xs = model::check_glyph(f, "lens");
        check("checks: the turned ellipse lacks extreme points", count(xs, model::CheckId::Extremes) == 1);
        std::vector<model::Finding> ex;
        for (const auto& x : xs) if (x.check == model::CheckId::Extremes) ex.push_back(x);
        command::fix_findings(j, f, ex);
        const auto ys = model::check_glyph(f, "lens");
        check("checks: Fix adds them; no new findings (grid, zones)", count(ys, model::CheckId::Extremes) == 0 &&
                                                                         count(ys, model::CheckId::Origin) == 0 &&
                                                                         f.glyph("lens")->contours[0].nodes.size() == 8);
    }

    std::cout << "M15b offset, expand stroke\n";
    {
        auto area = [](const std::vector<model::Contour>& cs) {
            double a = 0;
            for (const auto& c : cs) a += geom::signed_area(c);
            return a;
        };
        auto near = [](double a, double b, double tol) { return std::abs(a - b) <= tol; };
        using geom::Join;
        using geom::Cap;
        const auto sq = geom::shape_rect({{0, 0}, {100, 100}});
        auto r = geom::offset_contours({sq}, 10, Join::Miter);
        check("offset: a square +10 miter -> 120 square", r.changed && !r.fallback && r.contours.size() == 1 &&
                                                             r.contours[0].nodes.size() == 4 && near(area(r.contours), 14400, 1),
              std::to_string(area(r.contours)));
        r = geom::offset_contours({sq}, 10, Join::Round);
        check("offset: +10 round -> rounded corners", near(area(r.contours), 14314.16, 3), std::to_string(area(r.contours)));
        r = geom::offset_contours({sq}, 10, Join::Bevel);
        check("offset: +10 bevel -> corners cut", near(area(r.contours), 14200, 1), std::to_string(area(r.contours)));
        r = geom::offset_contours({sq}, -10, Join::Miter);
        check("offset: -10 -> 80 square", r.contours.size() == 1 && r.contours[0].nodes.size() == 4 && near(area(r.contours), 6400, 1),
              std::to_string(area(r.contours)));
        r = geom::offset_contours({sq}, -60, Join::Miter);
        check("offset: thinned past nothing -> nothing left", r.changed && r.contours.empty(), std::to_string(r.contours.size()));
        const auto circ = geom::shape_ellipse({{-100, -100}, {100, 100}});
        r = geom::offset_contours({circ}, 20, Join::Miter);
        check("offset: a circle +20 -> radius 120, curves kept",
              r.contours.size() == 1 && near(area(r.contours), 3.14159265 * 14400, 0.003 * 3.14159265 * 14400) &&
                  r.contours[0].nodes.size() <= 16,
              std::to_string(area(r.contours)) + " / " + (r.contours.empty() ? "0" : std::to_string(r.contours[0].nodes.size())));
        r = geom::offset_contours({circ}, -30, Join::Miter);
        check("offset: a circle -30 -> radius 70", near(area(r.contours), 3.14159265 * 4900, 0.003 * 3.14159265 * 4900),
              std::to_string(area(r.contours)));
        auto hole = geom::shape_rect({{100, 100}, {200, 200}});
        geom::reverse(hole);
        r = geom::offset_contours({geom::shape_rect({{0, 0}, {300, 300}}), hole}, 10, Join::Miter);
        check("offset: bolder -- the outer grows, the counter shrinks",
              r.contours.size() == 2 && near(area(r.contours), 320.0 * 320 - 80 * 80, 1), std::to_string(area(r.contours)));
        model::Contour L;
        for (auto [x, y] : {std::pair{0.0, 0.0}, {300.0, 0.0}, {300.0, 80.0}, {80.0, 80.0}, {80.0, 400.0}, {0.0, 400.0}})
            L.nodes.push_back(model::Node::corner(x, y));
        r = geom::offset_contours({L}, 10, Join::Miter);
        check("offset: an L +10 -- the inner corner's loop drops out",
              r.contours.size() == 1 && r.contours[0].nodes.size() == 6 && near(area(r.contours), 64000, 1),
              std::to_string(area(r.contours)) + " / " + (r.contours.empty() ? "0" : std::to_string(r.contours[0].nodes.size())));
        r = geom::offset_contours({L}, -10, Join::Miter);
        check("offset: an L -10", r.contours.size() == 1 && r.contours[0].nodes.size() == 6 && near(area(r.contours), 36000, 1),
              std::to_string(area(r.contours)));
        bool ccw = true;
        for (const auto& c : r.contours) ccw = ccw && geom::signed_area(c) > 0;
        check("offset: the result runs the right way", ccw && geom::wrong_way(r.contours).empty());

        model::Contour line;
        line.closed = false;
        line.nodes = {model::Node::corner(0, 0), model::Node::corner(100, 0)};
        r = geom::expand_stroke({line}, 20, Join::Miter, Cap::Butt);
        check("stroke: a line, butt ends -> 100 x 20", r.contours.size() == 1 && near(area(r.contours), 2000, 1) &&
                                                          r.contours[0].closed,
              std::to_string(area(r.contours)));
        r = geom::expand_stroke({line}, 20, Join::Miter, Cap::Round);
        check("stroke: round ends", near(area(r.contours), 2000 + 3.14159265 * 100, 3), std::to_string(area(r.contours)));
        r = geom::expand_stroke({line}, 20, Join::Miter, Cap::Square);
        check("stroke: square ends", near(area(r.contours), 2400, 1), std::to_string(area(r.contours)));
        model::Contour bend = line;
        bend.nodes.push_back(model::Node::corner(100, 100));
        r = geom::expand_stroke({bend}, 20, Join::Miter, Cap::Butt);
        check("stroke: a bend, mitred", r.contours.size() == 1 && near(area(r.contours), 4000, 1), std::to_string(area(r.contours)));
        r = geom::expand_stroke({sq}, 20, Join::Miter, Cap::Butt);
        check("stroke: a closed square -> a ring", r.contours.size() == 2 && near(area(r.contours), 8000, 1) &&
                                                       geom::wrong_way(r.contours).empty(),
              std::to_string(area(r.contours)));
        model::Contour arc;   // an open quarter circle, radius 100
        arc.closed = false;
        {
            model::Node a = model::Node::corner(100, 0), b = model::Node::corner(0, 100);
            a.cx2 = 100; a.cy2 = 55.23;
            b.cx1 = 55.23; b.cy1 = 100;
            arc.nodes = {a, b};
        }
        r = geom::expand_stroke({arc}, 20, Join::Miter, Cap::Butt);
        const double want = 3.14159265 / 4 * (110.0 * 110 - 90 * 90);
        check("stroke: a curve -> a curved band", r.contours.size() == 1 && near(area(r.contours), want, 0.004 * want),
              std::to_string(area(r.contours)) + " vs " + std::to_string(want));
    }

    {   // every preset's drawn glyphs: bolder, lighter, round corners -- clean results
        std::string bad;
        int tried = 0;
        for (const auto& p : model::presets()) {
            command::Document d;
            model::make_preset_font(d.font, {p.id, "T", "@none", false});
            for (const char* name : {"n", "o", "H", "O", "p", "v"}) {
                const model::Glyph* g = d.font.glyph(name);
                if (!g || g->contours.empty()) continue;
                double a0 = 0;
                for (const auto& c : g->contours) a0 += geom::signed_area(c);
                for (double dd : {12.0, -12.0}) {
                    ++tried;
                    const auto r = geom::offset_contours(g->contours, dd, geom::Join::Miter);
                    double a1 = 0;
                    for (const auto& c : r.contours) a1 += geom::signed_area(c);
                    const bool ok = r.changed && !r.fallback && !r.contours.empty() && geom::wrong_way(r.contours).empty() &&
                                    geom::overlapping(r.contours).empty() && (dd > 0 ? a1 > a0 : a1 < a0);
                    if (!ok) bad += p.id + "/" + name + (dd > 0 ? "+ " : "- ") + (r.fallback ? r.why : "") + " ";
                }
                model::Glyph rc = *g;
                for (auto& c : rc.contours) {
                    std::set<int> all;
                    for (int i = 0; i < static_cast<int>(c.nodes.size()); ++i) all.insert(i);
                    geom::round_corners(c, all, 15);
                }
                if (!geom::overlapping(rc.contours).empty() || !geom::wrong_way(rc.contours).empty())
                    bad += p.id + "/" + name + " round ";
            }
        }
        check("offset: every preset's n o H O p v, +12 / -12 -- clean, right way, no overlaps; round corners too",
              bad.empty() && tried > 20, bad);
    }

    std::cout << "s10 ink-trap warning\n";
    {
        command::Document d;
        model::make_preset_font(d.font, {"sans", "T", "@none", false});
        auto& j = d.journal;
        auto& f = d.font;
        const auto xs = model::check_glyph(f, "p");
        std::vector<model::Finding> traps;
        for (const auto& x : xs) if (x.check == model::CheckId::InkTraps) traps.push_back(x);
        check("ink-trap warning: p's two bowl joins (31 degrees) are flagged", traps.size() == 2,
              std::to_string(traps.size()));
        check("ink-trap warning: it is a warning", model::check_info(model::CheckId::InkTraps).warning &&
                                                       !model::check_info(model::CheckId::Overlap).warning);
        int h = 0;
        for (const auto& x : model::check_glyph(f, "H")) h += x.check == model::CheckId::InkTraps;
        check("ink-trap warning: H's square crotches (90) are not", h == 0);
        const std::size_t steps = j.size();
        const std::size_t before = f.glyph("p")->contours[0].nodes.size();
        const bool ok = command::fix_findings(j, f, traps);
        check("ink-trap warning: Fix cuts both traps, one step", ok && j.size() == steps + 1 &&
                                                                     f.glyph("p")->contours[0].nodes.size() == before + 4);
        int left = 0, other = 0;
        for (const auto& x : model::check_glyph(f, "p")) {
            left += x.check == model::CheckId::InkTraps;
            other += !model::check_info(x.check).warning;
        }
        check("ink-trap warning: after the fix -- no warning left (the trap's apex is not a crotch to trap), no new findings",
              left == 0 && other == 0, std::to_string(left) + " / " + std::to_string(other));
    }
}
