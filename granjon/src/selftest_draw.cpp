// selftest_draw -- M14: the drawing kit. M14a: the primitives (rectangle,
// ellipse, line) -- the drag rules, the shapes' geometry and direction, and
// the add_shape verb (one step, snapped, counter-clockwise, refusals).
// M14b: the booleans -- union / subtract / intersect on the M10c engine
// (areas, curves kept exact, holes clockwise) and the boolean_contours verb.
// M14c: parts -- public.skipExportGlyphs both ways, left out of the compile
// (a glyph using one exports decomposed), make / set / place.
#include "command/Document.hpp"
#include "command/Verbs.hpp"
#include "geom/CubicSegment.hpp"
#include "geom/Edit.hpp"
#include "geom/Overlap.hpp"
#include "geom/Shapes.hpp"
#include "export/Otf.hpp"
#include "import/FontReader.hpp"
#include "io/Ufo.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <string>

namespace geom    = granjon::geom;
namespace model   = granjon::model;
namespace command = granjon::command;

void selftest_draw(const std::function<void(const std::string&, bool, const std::string&)>& check_) {
    auto check = [&](const std::string& n, bool ok, const std::string& d = "") { check_(n, ok, d); };
    std::cout << "M14a primitives\n";
    using geom::Vec2;

    {   // the drag rules
        const auto b = geom::drag_box({100, 200}, {40, 260}, false, false);
        check("shapes: a box drag spans press .. pointer, either way round",
              b.lo == Vec2(40, 200) && b.hi == Vec2(100, 260));
        const auto sq = geom::drag_box({0, 0}, {50, -120}, true, false);
        check("shapes: Shift = square on the larger side, toward the pointer",
              sq.lo == Vec2(0, -120) && sq.hi == Vec2(120, 0));
        const auto c = geom::drag_box({300, 300}, {340, 360}, false, true);
        check("shapes: Alt = from the centre", c.lo == Vec2(260, 240) && c.hi == Vec2(340, 360));
        const auto sc = geom::drag_box({0, 0}, {30, 10}, true, true);
        check("shapes: Shift + Alt = a square about the press", sc.lo == Vec2(-30, -30) && sc.hi == Vec2(30, 30));
        check("shapes: a click (under a unit) is degenerate",
              geom::degenerate(geom::drag_box({5, 5}, {5.4, 90}, false, false)) &&
                  !geom::degenerate(geom::drag_box({5, 5}, {7, 9}, false, false)));
        Vec2 a, e;
        geom::drag_line({0, 0}, {100, 8}, true, false, a, e);
        check("shapes: a Shift line snaps to 45 degrees (here flat)", a == Vec2(0, 0) && std::abs(e.y) < 1e-9 && e.x > 99);
        geom::drag_line({50, 50}, {80, 90}, false, true, a, e);
        check("shapes: an Alt line is centred on the press", a == Vec2(20, 10) && e == Vec2(80, 90));
    }

    {   // geometry and direction
        const geom::Box b{{100, 0}, {400, 500}};
        const auto r = geom::shape_rect(b);
        check("shapes: rectangle = 4 corners, counter-clockwise from bottom-left",
              r.nodes.size() == 4 && r.closed && r.nodes[0].x == 100 && r.nodes[0].y == 0 &&
                  r.nodes[2].x == 400 && r.nodes[2].y == 500 && geom::signed_area(r) > 0 &&
                  std::all_of(r.nodes.begin(), r.nodes.end(), [](const model::Node& n) {
                      return n.type == model::Node::Type::Corner && !n.has_in() && !n.has_out(); }));
        check("shapes: rectangle area exact", std::abs(geom::signed_area(r) - 300.0 * 500) < 1e-6);
        const auto e = geom::shape_ellipse(b);
        const double want = 3.14159265358979 * 150 * 250;
        check("shapes: ellipse = 4 smooth nodes at the extremes, counter-clockwise",
              e.nodes.size() == 4 && e.nodes[0].x == 250 && e.nodes[0].y == 0 && e.nodes[1].x == 400 &&
                  e.nodes[2].y == 500 && e.nodes[3].x == 100 && geom::signed_area(e) > 0 &&
                  std::all_of(e.nodes.begin(), e.nodes.end(), [](const model::Node& n) {
                      return n.type == model::Node::Type::Smooth; }));
        check("shapes: ellipse area within 0.5 % of pi a b (the flattened area)", std::abs(geom::signed_area(e) - want) / want < 5e-3,
              std::to_string(geom::signed_area(e)) + " vs " + std::to_string(want));
        // A circle's midpoint on each quarter sits on the radius (kappa).
        const auto ci = geom::shape_ellipse({{0, 0}, {200, 200}});
        const Vec2 mid = geom::segment(ci, 0).at(0.5);
        check("shapes: a circle's quarter passes through the radius at 45 degrees",
              std::abs((mid - Vec2(100, 100)).length() - 100) < 0.05);
        const auto bar = geom::shape_bar({0, 0}, {300, 300}, 80);
        const double len = std::sqrt(2.0) * 300;
        check("shapes: a line is a bar: 4 corners, its width x its length, counter-clockwise",
              bar.nodes.size() == 4 && std::abs(geom::signed_area(bar) - 80 * len) < 1e-6 &&
                  std::abs((Vec2(bar.nodes[0].x, bar.nodes[0].y) - Vec2(bar.nodes[3].x, bar.nodes[3].y)).length() - 80) < 1e-9);
        const auto back = geom::shape_bar({300, 300}, {0, 0}, 80);
        check("shapes: a line drawn backwards is still counter-clockwise", geom::signed_area(back) > 0);
        check("shapes: a zero-length line or width is nothing",
              geom::shape_bar({5, 5}, {5, 5}, 80).nodes.empty() && geom::shape_bar({0, 0}, {9, 0}, 0).nodes.empty());
    }

    {   // the verb
        command::Document doc;
        command::make_fixture(doc.font);
        auto& j = doc.journal;
        auto& f = doc.font;
        const auto before = f.snapshot();
        const std::size_t n0 = f.glyph("n")->contours.size();
        int at = -1;
        std::string why;
        auto cw = geom::shape_rect({{10.4, 20.6}, {110.2, 220.5}});
        geom::reverse(cw);   // drawn clockwise: the verb turns it
        const bool ok = command::add_shape(j, f, "n", cw, "Rectangle", &at, &why);
        const auto& c = f.glyph("n")->contours.back();
        bool whole = true;
        for (const auto& nd : c.nodes) whole = whole && nd.x == std::round(nd.x) && nd.y == std::round(nd.y) &&
                                               nd.cx1 == std::round(nd.cx1) && nd.cy2 == std::round(nd.cy2);
        check("add_shape: one step, a new contour at the end, its index returned",
              ok && j.size() == 1 && at == static_cast<int>(n0) && f.glyph("n")->contours.size() == n0 + 1, why);
        check("add_shape: snapped to units, turned counter-clockwise", whole && geom::signed_area(c) > 0);
        check("add_shape: the step is named for the shape", j.undo_label() == "Rectangle", j.undo_label());
        j.undo();
        check("add_shape: undo takes it out", f.snapshot() == before);
        const std::size_t steps = j.size();
        check("add_shape: a sliver that snaps to nothing is refused, no step",
              !command::add_shape(j, f, "n", geom::shape_rect({{0, 0}, {200, 0.3}}), "Rectangle", nullptr, &why) &&
                  j.size() == steps && !why.empty(), why);
        check("add_shape: an empty contour (a click) is refused",
              !command::add_shape(j, f, "n", geom::shape_bar({1, 1}, {1, 1}, 50), "Line", nullptr, &why) && j.size() == steps);
        check("add_shape: an unknown glyph is refused",
              !command::add_shape(j, f, "no-such-glyph", geom::shape_rect({{0, 0}, {9, 9}}), "Rectangle") && j.size() == steps);
        // An empty glyph becomes Drafted -- the pen's rule.
        std::string empty;
        for (const auto& name : f.glyph_order())
            if (f.glyph(name)->contours.empty() && f.glyph(name)->components.empty()) { empty = name; break; }
        if (!empty.empty()) {
            command::add_shape(j, f, empty, geom::shape_ellipse({{0, 0}, {300, 300}}), "Ellipse");
            check("add_shape: an empty glyph is Drafted after", f.glyph(empty)->status != model::GlyphStatus::Empty);
        }
    }
    std::cout << "M14b booleans\n";
    {   // geometry: a 400 square and a circle r = 100
        using geom::BoolOp;
        const double pi = 3.14159265358979;
        auto area = [](const std::vector<model::Contour>& cs) { double a = 0; for (const auto& c : cs) a += geom::signed_area(c); return a; };
        auto near = [](double a, double b, double rel) { return std::abs(a - b) <= std::abs(b) * rel + 2.0; };
        const auto sq = geom::shape_rect({{0, 0}, {400, 400}});
        const auto inner = geom::shape_ellipse({{100, 100}, {300, 300}});    // inside the square
        const auto bite = geom::shape_ellipse({{300, 100}, {500, 300}});     // over its right edge
        const auto far = geom::shape_ellipse({{900, 900}, {1000, 1000}});    // nowhere near
        const double disk = pi * 100 * 100;
        // the flattened circle's own area (signed_area samples the curve)
        const double fdisk = geom::signed_area(inner);

        auto hole = geom::boolean_op({sq}, {inner}, BoolOp::Subtract);
        bool hole_cw = false, hole_smooth = false;
        for (const auto& c : hole.contours)
            if (geom::signed_area(c) < 0) {
                hole_cw = true;
                hole_smooth = c.nodes.size() == 4 && std::all_of(c.nodes.begin(), c.nodes.end(), [](const model::Node& n) {
                    return n.type == model::Node::Type::Smooth; });
            }
        check("bool: subtract a circle inside -> a square with a hole (outer ccw, hole cw, 4 smooth nodes)",
              hole.changed && !hole.fallback && hole.contours.size() == 2 && hole_cw && hole_smooth &&
                  near(area(hole.contours), 160000 - fdisk, 1e-4),
              std::to_string(hole.contours.size()) + " contours, area " + std::to_string(area(hole.contours)));
        auto b = geom::boolean_op({sq}, {bite}, BoolOp::Subtract);
        int corners = 0;
        for (const auto& c : b.contours) for (const auto& n : c.nodes) corners += n.type == model::Node::Type::Corner;
        check("bool: subtract a circle over the edge -> one contour, a half-disk bite, 2 crossings",
              b.changed && b.contours.size() == 1 && b.crossings == 2 && near(area(b.contours), 160000 - fdisk / 2, 1e-3),
              std::to_string(area(b.contours)) + " crossings " + std::to_string(b.crossings));
        // The bite's curve is the circle itself: every sample of the result's
        // curved runs lies on the radius.
        double worst = 0;
        if (b.contours.size() == 1)
            for (int si = 0; si < geom::segment_count(b.contours[0]); ++si) {
                const auto sg = geom::segment(b.contours[0], si);
                if (sg.p1 == sg.p0 && sg.p2 == sg.p3) continue;   // a line
                for (int i = 0; i <= 20; ++i)
                    worst = std::max(worst, std::abs((sg.at(i / 20.0) - geom::Vec2(400, 200)).length() - 100));
            }
        check("bool: the bite keeps the circle's curves (within 0.2 unit of the radius)", worst < 0.2, std::to_string(worst));
        const auto in = geom::boolean_op({sq}, {bite}, BoolOp::Intersect);
        check("bool: intersect -> the half disk inside the square",
              in.changed && in.contours.size() == 1 && near(area(in.contours), fdisk / 2, 1e-3), std::to_string(area(in.contours)));
        const auto un = geom::boolean_op({sq}, {bite}, BoolOp::Union);
        check("bool: union -> one outline, square + half disk",
              un.changed && un.contours.size() == 1 && near(area(un.contours), 160000 + fdisk / 2, 1e-3), std::to_string(area(un.contours)));
        const auto none = geom::boolean_op({sq}, {far}, BoolOp::Intersect);
        check("bool: intersect with a stranger -> nothing left (changed, no contour)", none.changed && none.contours.empty());
        const auto keep = geom::boolean_op({sq}, {far}, BoolOp::Subtract);
        check("bool: subtract a stranger -> the square as it was, the cutter gone",
              keep.contours.size() == 1 && keep.contours[0].nodes.size() == 4 && near(area(keep.contours), 160000, 1e-9));
        check("bool: (pi r^2 sanity)", std::abs(fdisk - disk) / disk < 5e-3);
        // A cutter drawn clockwise cuts the same (nonzero fills either way).
        auto cw = bite;
        geom::reverse(cw);
        check("bool: a clockwise cutter cuts the same", near(area(geom::boolean_op({sq}, {cw}, BoolOp::Subtract).contours), area(b.contours), 1e-6));
        // remove_overlaps is still the union with its shortcut: untouched contours come back as they were.
        const auto ro = geom::remove_overlaps({sq, far});
        check("bool: remove_overlaps unchanged -- no overlap, input untouched", !ro.changed && ro.contours.size() == 2);
    }

    {   // the verb
        command::Document doc;
        command::make_fixture(doc.font);
        auto& j = doc.journal;
        auto& f = doc.font;
        std::string why;
        std::vector<int> at;
        // n: two stems. A bar across both (picked, as drawn) subtracted: a notch out of each.
        int bar = -1;
        command::add_shape(j, f, "n", geom::shape_rect({{40, 200}, {520, 260}}), "Rectangle", &bar);
        const auto before = f.snapshot();
        const std::size_t steps = j.size();
        const bool ok = command::boolean_contours(j, f, "n", {bar}, geom::BoolOp::Subtract, &at, &why);
        const auto& n = *f.glyph("n");
        check("bool verb: subtract -- the cutter goes, each stem cut in two (4 contours), one step named Subtract",
              ok && n.contours.size() == 4 && j.size() == steps + 1 && j.undo_label() == "Subtract" && at.size() == 4,
              why + " " + std::to_string(n.contours.size()));
        bool whole = true, ccw = true;
        for (const auto& c : n.contours) {
            ccw = ccw && geom::signed_area(c) > 0;
            for (const auto& nd : c.nodes) whole = whole && nd.x == std::round(nd.x) && nd.y == std::round(nd.y);
        }
        check("bool verb: the pieces are snapped and run counter-clockwise", whole && ccw);
        j.undo();
        check("bool verb: undo brings the stems and the cutter back", f.snapshot() == before);
        // Intersect: the stems keep only the bar's band.
        command::boolean_contours(j, f, "n", {bar}, geom::BoolOp::Intersect, &at, &why);
        double h = 0;
        for (const auto& c : f.glyph("n")->contours)
            for (const auto& nd : c.nodes) h = std::max(h, nd.y);
        check("bool verb: intersect -- two pieces, the band 200..260", f.glyph("n")->contours.size() == 2 && h == 260, why);
        j.undo();
        // Union of a stem and the bar: one outline, the other stem joined too (all three picked).
        check("bool verb: union of the bar and both stems -> one outline",
              command::boolean_contours(j, f, "n", {0, 1, bar}, geom::BoolOp::Union, &at, &why) &&
                  f.glyph("n")->contours.size() == 1 && at == std::vector<int>{0}, why);
        j.undo();
        // Order: contours not involved keep their place.
        int dot = -1;
        command::add_shape(j, f, "n", geom::shape_ellipse({{200, 600}, {300, 700}}), "Ellipse", &dot);
        const auto dot_c = f.glyph("n")->contours[static_cast<std::size_t>(dot)];
        command::boolean_contours(j, f, "n", {0, bar}, geom::BoolOp::Union, &at, &why);
        const auto& cs = f.glyph("n")->contours;
        check("bool verb: uninvolved contours keep their order; the result takes the first one's place",
              cs.size() == 3 && at == std::vector<int>{0} && cs[2] == dot_c, std::to_string(cs.size()));
        j.undo();
        j.undo();
        const std::size_t s2 = j.size();
        check("bool verb: union of one contour is refused",
              !command::boolean_contours(j, f, "n", {bar}, geom::BoolOp::Union, nullptr, &why) && j.size() == s2 && !why.empty(), why);
        check("bool verb: subtract with nothing picked is refused",
              !command::boolean_contours(j, f, "n", {}, geom::BoolOp::Subtract, nullptr, &why) && j.size() == s2, why);
        check("bool verb: subtract with everything picked has nothing to cut",
              !command::boolean_contours(j, f, "n", {0, 1, bar}, geom::BoolOp::Subtract, nullptr, &why) && j.size() == s2, why);
        int sky = -1;
        command::add_shape(j, f, "n", geom::shape_rect({{0, 900}, {50, 950}}), "Rectangle", &sky);
        const std::size_t s3 = j.size();
        check("bool verb: a cutter that meets nothing is refused",
              !command::boolean_contours(j, f, "n", {sky}, geom::BoolOp::Subtract, nullptr, &why) && j.size() == s3, why);
        // O's counter runs the wrong way: a cut through O is refused.
        int slab = -1;
        command::add_shape(j, f, "O", geom::shape_rect({{0, 300}, {760, 400}}), "Rectangle", &slab);
        const std::size_t s4 = j.size();
        check("bool verb: a wrong-way counter among the cut contours is refused",
              !command::boolean_contours(j, f, "O", {slab}, geom::BoolOp::Subtract, nullptr, &why) && j.size() == s4 &&
                  why.find("wrong way") != std::string::npos, why);
        // Intersect that leaves nothing: refused.
        j.undo();   // the slab out
        command::correct_directions(j, f, {"O"});
        int inside = -1;
        command::add_shape(j, f, "O", geom::shape_rect({{300, 300}, {400, 400}}), "Rectangle", &inside);   // in the counter
        const std::size_t s5 = j.size();
        check("bool verb: an intersect that would leave nothing is refused",
              !command::boolean_contours(j, f, "O", {inside}, geom::BoolOp::Intersect, nullptr, &why) && j.size() == s5, why);
    }
    std::cout << "M14c parts\n";
    namespace fs = std::filesystem;
    {   // the lib, both ways
        command::Document doc;
        command::make_fixture(doc.font);
        const fs::path dir = fs::temp_directory_path() / "granjon_selftest_parts.granjon";
        fs::remove_all(dir);
        std::string err;
        check("parts: the fixture's part saves", granjon::io::save_ufo(doc.font, dir.string(), &err), err);
        std::ifstream in(dir / "lib.plist");
        std::stringstream ss;
        ss << in.rdbuf();
        check("parts: lib.plist lists it under public.skipExportGlyphs",
              ss.str().find("public.skipExportGlyphs") != std::string::npos && ss.str().find("_part.serif") != std::string::npos);
        model::Font back;
        check("parts: loads back a part, the rest not", granjon::io::load_ufo(dir.string(), back, &err) &&
                  back.glyph("_part.serif") && back.glyph("_part.serif")->skip_export && !back.glyph("H")->skip_export, err);
        {   // (the fixture's order numbers aren't load-stable; the flag is what M14c owns)
            std::string diff;
            for (const auto& n : doc.font.glyph_order())
                if (!back.glyph(n) || back.glyph(n)->skip_export != doc.font.glyph(n)->skip_export) diff += n + " ";
            check("parts: every glyph's part flag survives save + load", diff.empty(), diff);
        }
        fs::remove_all(dir);
        model::Font plain;
        model::make_new_font(plain);
        granjon::io::save_ufo(plain, dir.string(), &err);
        std::ifstream in2(dir / "lib.plist");
        std::stringstream ss2;
        ss2 << in2.rdbuf();
        check("parts: no parts, no key", ss2.str().find("skipExportGlyphs") == std::string::npos);
        fs::remove_all(dir);
    }
    {   // the compile
        command::Document doc;
        command::make_fixture(doc.font);
        auto& j = doc.journal;
        auto& f = doc.font;
        command::place_part(j, f, "o", "_part.serif", true, 0, -100);   // clear of the box (else export merges them)
        granjon::otf::OtfReport rep;
        const auto bytes = granjon::otf::compile_otf(f, {}, &rep);
        model::Font back;
        granjon::reader::import_font(bytes, back);
        const auto* o = back.glyph("o");
        bool noted = false;
        for (const auto& n : rep.notes) noted = noted || n.find("part(s) left out") != std::string::npos;
        check("parts: left out of the OTF (and the report says so)", !back.glyph("_part.serif") && noted);
        check("parts: a glyph with a part as component exports decomposed (box + serif)",
              o && o->contours.size() == 2 && o->components.empty(), o ? std::to_string(o->contours.size()) : "no o");
    }
    {   // names and the verbs
        command::Document doc;
        command::make_fixture(doc.font);
        auto& j = doc.journal;
        auto& f = doc.font;
        std::string why;
        check("parts: free names", command::free_part_name(f, "serif") == "_part.serif.1" &&
                                       command::free_part_name(f, "bowl left") == "_part.bowl_left" &&
                                       command::free_part_name(f, "_x") == "_x");
        const auto n_before = *f.glyph("n");
        const std::size_t s0 = j.size();
        check("make_part: copies the picked contour into a new part, one step",
              command::make_part(j, f, "n", {1}, "_part.stem", &why) && j.size() == s0 + 1 && f.glyph("_part.stem") &&
                  f.glyph("_part.stem")->skip_export && f.glyph("_part.stem")->contours.size() == 1 &&
                  f.glyph("_part.stem")->contours[0] == n_before.contours[1] && *f.glyph("n") == n_before, why);
        check("make_part: in the parts list, after the fixture's", command::parts(f) == std::vector<std::string>{"_part.serif", "_part.stem"});
        check("make_part: a taken name is refused", !command::make_part(j, f, "n", {0}, "_part.stem", &why) && j.size() == s0 + 1, why);
        check("make_part: a bad name is refused", !command::make_part(j, f, "n", {0}, "part stem", &why), why);
        check("make_part: nothing picked is refused", !command::make_part(j, f, "n", {}, "_part.x", &why), why);
        j.undo();
        check("make_part: undo removes it", !f.glyph("_part.stem"));
        check("set_part: marks, one step; again changes nothing",
              command::set_part(j, f, {"o"}, true) && f.glyph("o")->skip_export && !command::set_part(j, f, {"o"}, true));
        check("set_part: unmarks", command::set_part(j, f, {"o"}, false) && !f.glyph("o")->skip_export);
        // place as contours, moved
        std::vector<int> at;
        int k = -1;
        const std::size_t hc = f.glyph("H")->contours.size();
        check("place_part: as contours -- copies, moved, picked indices",
              command::place_part(j, f, "H", "_part.serif", false, 100.4, 50, &at, nullptr, &why) &&
                  f.glyph("H")->contours.size() == hc + 1 && at == std::vector<int>{static_cast<int>(hc)} &&
                  f.glyph("H")->contours[hc].nodes[0].x == 140 && f.glyph("H")->contours[hc].nodes[0].y == 50, why);
        check("place_part: the step is named", j.undo_label() == "Place part “_part.serif”", j.undo_label());
        check("place_part: as a component -- linked, its index",
              command::place_part(j, f, "n", "_part.serif", true, 0, 0, nullptr, &k, &why) && k == 0 &&
                  f.glyph("n")->components.size() == 1 && f.glyph("n")->components[0].base == "_part.serif", why);
        const std::size_t s1 = j.size();
        check("place_part: a glyph that is not a part is refused",
              !command::place_part(j, f, "n", "o", true, 0, 0, nullptr, nullptr, &why) && j.size() == s1, why);
        check("place_part: into itself is refused",
              !command::place_part(j, f, "_part.serif", "_part.serif", true, 0, 0, nullptr, nullptr, &why), why);
        // a cycle: a part that uses n, placed into n
        command::make_part(j, f, "H", {0}, "_part.h", &why);
        command::add_component(j, f, "_part.h", "n", 0, 0);
        const std::size_t s2 = j.size();
        check("place_part: a cycle is refused", !command::place_part(j, f, "n", "_part.h", true, 0, 0, nullptr, nullptr, &why) &&
                                                     j.size() == s2, why);
        command::add_glyph(j, f, "_part.blank", {}, "", &why);
        command::set_part(j, f, {"_part.blank"}, true);
        check("place_part: a part that draws nothing is refused",
              !command::place_part(j, f, "o", "_part.blank", false, 0, 0, nullptr, nullptr, &why), why);
        // a part made of a component places its drawing as contours
        command::add_component(j, f, "_part.blank", "_part.serif", 0, 100);
        at.clear();
        check("place_part: a part's components come as contours, resolved",
              command::place_part(j, f, "o", "_part.blank", false, 0, 0, &at, nullptr, &why) && at.size() == 1 &&
                  f.glyph("o")->contours.back().nodes[0].y == 100, why);
    }
    std::cout << "s08 Select tool (whole paths)\n";
    {
        model::Glyph g;
        g.contours = {geom::shape_rect({{0, 0}, {400, 400}}), geom::shape_rect({{100, 100}, {300, 300}}),
                      geom::shape_ellipse({{600, 0}, {800, 200}})};
        geom::reverse(g.contours[1]);   // a counter
        using P = geom::PointRef::Part;
        const auto w = geom::whole_contours(g, {{0, 2, P::Node}, {2, 1, P::Out}, {-1, 0, P::Anchor}});
        check("select: a node or handle grows to its whole path; anchors stay",
              w.size() == 4 + 4 + 1 && w.count({0, 0, P::Node}) && w.count({2, 3, P::Node}) && !w.count({2, 1, P::Out}) &&
                  w.count({-1, 0, P::Anchor}) && !w.count({1, 0, P::Node}));
        check("select: a stale ref passes through unchanged", geom::whole_contours(g, {{9, 0, P::Node}}).size() == 1);
        check("select: inside the bowl's ink -> the bowl", geom::contour_at(g, {50, 50}) == 0);
        check("select: inside the counter -> the counter (the smallest path there)", geom::contour_at(g, {200, 200}) == 1);
        check("select: inside the ellipse -> the ellipse", geom::contour_at(g, {700, 100}) == 2);
        check("select: outside every path -> none", geom::contour_at(g, {500, 500}) == -1 && geom::contour_at(g, {610, 10}) == -1);
        model::Contour open = g.contours[2];
        open.closed = false;
        g.contours[2] = open;
        check("select: an open path has no inside", geom::contour_at(g, {700, 100}) == -1);
    }
}
