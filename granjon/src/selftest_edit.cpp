// selftest_edit -- M5f: node editing geometry (insert / delete / line <-> curve).
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
}
