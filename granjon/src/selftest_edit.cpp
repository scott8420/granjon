// selftest_edit -- M5f: node editing geometry (insert / delete / line <-> curve).
#include "command/Document.hpp"
#include "command/Verbs.hpp"
#include "geom/CubicSegment.hpp"
#include "geom/Edit.hpp"
#include "geom/NodeEdit.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <string>

namespace geom  = granjon::geom;
namespace model = granjon::model;

namespace {
using Check = std::function<void(const std::string&, bool, const std::string&)>;

// The farthest any sample of `a` lies from the outline `b` (dense sampling).
double distance(const model::Contour& a, const model::Contour& b) {
    double worst = 0;
    for (int s = 0; s < geom::segment_count(a); ++s)
        for (int i = 0; i <= 60; ++i) {
            const geom::Vec2 p = geom::segment(a, s).at(i / 60.0);
            double best = 1e18;
            for (int k = 0; k < geom::segment_count(b); ++k) best = std::min(best, geom::segment(b, k).dist_to(p));
            worst = std::max(worst, best);
        }
    return worst;
}
model::Node smooth(double x, double y, double ix, double iy, double ox, double oy) {
    return {x, y, ix, iy, ox, oy, model::Node::Type::Smooth};
}
model::Contour circle() {   // r = 250 about (250, 250), counter-clockwise, 4 smooth nodes
    const double k = 0.5523 * 250;
    model::Contour c;
    c.nodes = {smooth(500, 250, 500, 250 - k, 500, 250 + k), smooth(250, 500, 250 + k, 500, 250 - k, 500),
               smooth(0, 250, 0, 250 + k, 0, 250 - k), smooth(250, 0, 250 - k, 0, 250 + k, 0)};
    return c;
}
}  // namespace

void selftest_edit(const Check& check_) {
    auto check = [&](const std::string& n, bool ok, const std::string& d = "") { check_(n, ok, d); };
    std::cout << "M5f node editing\n";

    {   // insert on a curve: the shape does not move
        model::Contour c = circle();
        const model::Contour was = c;
        const int at = geom::insert_node(c, 0, 0.3);
        check("insert: on a curve -- the new node sits after the segment's start", at == 1 && c.nodes.size() == 5);
        const double d1 = distance(c, was), d2 = distance(was, c);
        check("insert: the outline is unchanged", d1 < 1e-6 && d2 < 1e-6, std::to_string(std::max(d1, d2)));
        check("insert: the new node is smooth, handles in line", c.nodes[1].type == model::Node::Type::Smooth &&
              std::abs((c.nodes[1].cx1 - c.nodes[1].x) * (c.nodes[1].cy2 - c.nodes[1].y) -
                       (c.nodes[1].cy1 - c.nodes[1].y) * (c.nodes[1].cx2 - c.nodes[1].x)) < 1e-6);
        // ... and deleting it again gives the circle back (the fit recovers the split).
        model::Glyph g;
        g.contours = {c};
        geom::delete_nodes(g, {{0, 1}});
        const auto& r = g.contours[0];
        double handle_err = 0;
        for (int i = 0; i < 4; ++i) {
            handle_err = std::max({handle_err, std::abs(r.nodes[i].cx1 - was.nodes[i].cx1), std::abs(r.nodes[i].cy1 - was.nodes[i].cy1),
                                   std::abs(r.nodes[i].cx2 - was.nodes[i].cx2), std::abs(r.nodes[i].cy2 - was.nodes[i].cy2)});
        }
        check("delete: removing an inserted node restores the curve (handle fit)", r.nodes.size() == 4 && handle_err < 1.0,
              "handles within " + std::to_string(handle_err));
    }
    {   // delete a real node: tangents kept, the arc stays close
        model::Glyph g;
        g.contours = {circle()};
        const model::Contour was = g.contours[0];
        geom::delete_nodes(g, {{0, 1}});
        const auto& r = g.contours[0];
        const double cross = (r.nodes[0].cx2 - r.nodes[0].x) * (was.nodes[0].cy2 - was.nodes[0].y) -
                             (r.nodes[0].cy2 - r.nodes[0].y) * (was.nodes[0].cx2 - was.nodes[0].x);
        check("delete: the neighbours keep their tangents", r.nodes.size() == 3 && std::abs(cross) < 1e-6 &&
              r.nodes[0].cx2 == r.nodes[0].x && r.nodes[0].cy2 > r.nodes[0].y);
        // A half circle in one cubic is ~6 % of r off at worst; the fit must do no worse than that.
        const double d = distance(r, was);
        check("delete: the merged curve follows the old outline (half circle in one cubic)", d < 0.03 * 250 * 2,
              "worst " + std::to_string(d));
    }
    {   // lines, ends, whole contours
        model::Glyph g;
        model::Contour box;
        for (auto [x, y] : {std::pair{0.0, 0.0}, {100.0, 0.0}, {100.0, 100.0}, {0.0, 100.0}})
            box.nodes.push_back(model::Node::corner(x, y));
        g.contours = {box};
        geom::delete_nodes(g, {{0, 2}});
        check("delete: a corner of a box -- a triangle of lines", g.contours[0].nodes.size() == 3 &&
              geom::is_line(g.contours[0], 0) && geom::is_line(g.contours[0], 1) && geom::is_line(g.contours[0], 2));
        geom::delete_nodes(g, {{0, 0}, {0, 1}, {0, 2}});
        check("delete: every node of a contour -- the contour goes", g.contours.empty());
        model::Contour open = box;
        open.closed = false;
        open.nodes[0].cx2 = 30;   // a handle leaving the first node
        g.contours = {open};
        geom::delete_nodes(g, {{0, 1}});
        check("delete: an open contour keeps its ends", g.contours[0].nodes.size() == 3 && !g.contours[0].closed &&
              g.contours[0].nodes.front().x == 0 && g.contours[0].nodes.back().y == 100);
        geom::delete_nodes(g, {{0, 0}});
        check("delete: an open contour's first node -- the next one leads, no stray in-handle",
              g.contours[0].nodes.size() == 2 && !g.contours[0].nodes.front().has_in());
    }
    {   // line <-> curve
        model::Contour c;
        c.nodes = {model::Node::corner(0, 0), model::Node::corner(300, 0), model::Node::corner(300, 300)};
        geom::segment_to_curve(c, 0);
        check("curve: a line gets handles at a third and two thirds", !geom::is_line(c, 0) &&
              c.nodes[0].cx2 == 100 && c.nodes[1].cx1 == 200 && geom::is_line(c, 1));
        geom::segment_to_line(c, 0);
        check("line: the handles retract, corners stay corners", geom::is_line(c, 0) &&
              c.nodes[0].type == model::Node::Type::Corner && c.nodes[1].type == model::Node::Type::Corner);
        const int at = geom::insert_node(c, 1, 0.5);
        check("insert: on a line -- a corner at the point", at == 2 && c.nodes[2].x == 300 && c.nodes[2].y == 150 &&
              c.nodes[2].type == model::Node::Type::Corner && geom::is_line(c, 1) && geom::is_line(c, 2));
    }
    {   // picked segments, wrapping
        model::Glyph g;
        g.contours = {circle()};
        using P = geom::PointRef::Part;
        const auto segs = granjon::command::picked_segments(g, {{0, 3, P::Node}, {0, 0, P::Node}, {0, 1, P::Node}});
        check("segments: both ends picked, the closing segment included",
              segs.size() == 2 && segs[0] == std::pair{0, 0} && segs[1] == std::pair{0, 3});
    }

    // ── M5g: the pen ──
    std::cout << "M5g pen\n";
    {
        namespace cmd = granjon::command;
        granjon::command::Document doc;
        cmd::make_fixture(doc.font);
        auto& j = doc.journal;
        auto& f = doc.font;
        const auto before = f.snapshot();
        const std::size_t n0 = f.glyph("n")->contours.size();
        auto at = cmd::pen_press(j, f, "n", -1, {100, 0});
        cmd::drag_end(j, f);
        const int ci = at.contour;
        check("pen: a click starts a new open path -- one node, one step", ci == static_cast<int>(n0) &&
              f.glyph("n")->contours[ci].nodes.size() == 1 && !f.glyph("n")->contours[ci].closed && j.size() == 1);
        at = cmd::pen_press(j, f, "n", ci, {100, 100});
        cmd::pen_drag(j, f, "n", at, {20.4, 0}, false);
        cmd::pen_drag(j, f, "n", at, {50.2, 0}, false);
        cmd::drag_end(j, f);
        const auto& nd = f.glyph("n")->contours[ci].nodes[1];
        check("pen: click-drag -- symmetric handles along the drag, snapped", nd.type == model::Node::Type::Symmetric &&
              nd.cx2 == 150 && nd.cy2 == 100 && nd.cx1 == 50 && nd.cy1 == 100 && j.size() == 2);
        at = cmd::pen_press(j, f, "n", ci, {0, 100});
        cmd::pen_drag(j, f, "n", at, {0, -40}, true);
        cmd::drag_end(j, f);
        const auto& nc = f.glyph("n")->contours[ci].nodes[2];
        check("pen: Alt-drag -- the out-handle alone, a corner", nc.type == model::Node::Type::Corner &&
              nc.cx2 == 0 && nc.cy2 == 60 && !nc.has_in());
        at = cmd::pen_press(j, f, "n", ci, {}, true);
        cmd::pen_drag(j, f, "n", at, {0, -30}, false);
        cmd::drag_end(j, f);
        const auto& c = f.glyph("n")->contours[ci];
        check("pen: a click on the first node closes the path; its drag shapes the arrival", c.closed && c.nodes.size() == 3 &&
              c.nodes[0].cx1 == 100 && c.nodes[0].cy1 == 30 && j.size() == 4);
        check("pen: a closed path takes no more nodes", cmd::pen_press(j, f, "n", ci, {5, 5}).contour < 0 && !j.in_gesture());
        for (int i = 0; i < 4; ++i) j.undo();
        check("pen: four steps undo to the start", f.snapshot() == before);

        // Continue an open path from its first node: reversed, then appended.
        at = cmd::pen_press(j, f, "o", -1, {10, 10});
        cmd::drag_end(j, f);
        const int oc = at.contour;
        at = cmd::pen_press(j, f, "o", oc, {90, 10});
        cmd::drag_end(j, f);
        at = cmd::pen_press(j, f, "o", oc, {10, 90}, false, true);
        cmd::drag_end(j, f);
        const auto& oo = f.glyph("o")->contours[oc];
        check("pen: continuing from the first node -- the path reversed, the node at its new end",
              oo.nodes.size() == 3 && oo.nodes[0].x == 90 && oo.nodes[1].x == 10 && oo.nodes[1].y == 10 &&
              oo.nodes[2].x == 10 && oo.nodes[2].y == 90 && at.node == 2);
        at = cmd::pen_press(j, f, "o", oc, {50, 50});
        cmd::drag_cancel(j);
        check("pen: Escape mid-press (cancel) leaves no node", f.glyph("o")->contours[oc].nodes.size() == 3);
    }
    {
        const auto a = geom::constrain45({100, 10});
        const auto b = geom::constrain45({70, 75});
        check("pen: Shift -- 45-degree steps", std::abs(a.y) < 1e-9 && a.x > 99 && std::abs(b.x - b.y) < 1e-9);
    }
}
