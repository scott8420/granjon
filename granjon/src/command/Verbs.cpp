#include "command/Verbs.hpp"

#include "model/Composite.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>

namespace granjon::command {

using model::Glyph;
using model::Record;

namespace {

std::string q(const std::string& s) { return "“" + s + "”"; }   // “name”

void say(std::string* why, const std::string& s) { if (why) *why = s; }

Glyph glyph_copy(const Font& f, const std::string& name) { return *f.glyph(name); }

// The one live outline drag. One pointer, one drag at a time: the originals
// at the press, so each move is "from the press", not cumulative.
struct Drag {
    std::vector<std::string> names;
    std::vector<Glyph>       at_press;
};
Drag& drag() { static Drag d; return d; }

void translate(Glyph& g, double dx, double dy) {
    for (auto& c : g.contours)
        for (auto& n : c.nodes) {
            n.x += dx;   n.y += dy;
            n.cx1 += dx; n.cy1 += dy;
            n.cx2 += dx; n.cy2 += dy;
        }
    for (auto& a : g.anchors) { a.x += dx; a.y += dy; }
}

}  // namespace

// ── glyphs ─────────────────────────────────────────────────────────────────

bool add_glyph(FontJournal& j, Font& f, const std::string& name,
               const std::vector<char32_t>& unicodes, const std::string& after, std::string* why) {
    if (!model::valid_glyph_name(name)) {
        say(why, "Glyph names use A-Z a-z 0-9 . _ - and start with a letter or underscore, so every "
                 "tool downstream (and the PostScript names in the font) can read them.");
        return false;
    }
    if (f.glyph(name)) { say(why, "There is already a glyph named " + q(name) + "."); return false; }
    return j.run("Add glyph " + q(name), {name}, [&] {
        Glyph g;
        g.name     = name;
        g.unicodes = unicodes;
        g.advance  = 500;
        g.order    = f.order_after(after);
        f.put(name, Record{g});
    });
}

bool add_glyphs(FontJournal& j, Font& f, const std::vector<model::NewGlyph>& glyphs,
                const std::string& label, std::string* why) {
    if (glyphs.empty()) { say(why, "Nothing to add."); return false; }
    std::vector<std::string> ids;
    for (const auto& ng : glyphs) {
        if (!model::valid_glyph_name(ng.name)) {
            say(why, q(ng.name) + " is not a usable glyph name: names use A-Z a-z 0-9 . _ - and start "
                     "with a letter or underscore.");
            return false;
        }
        if (f.glyph(ng.name) || std::find(ids.begin(), ids.end(), ng.name) != ids.end()) {
            say(why, "There is already a glyph named " + q(ng.name) + ".");
            return false;
        }
        ids.push_back(ng.name);
    }
    const std::string l = !label.empty() ? label
                        : glyphs.size() == 1 ? "Add glyph " + q(glyphs[0].name)
                                             : "Add " + std::to_string(glyphs.size()) + " glyphs";
    return j.run(l, ids, [&] {
        double order = f.order_after("");
        for (const auto& ng : glyphs) {
            Glyph g;
            g.name     = ng.name;
            g.unicodes = ng.unicodes;
            g.advance  = ng.advance >= 0 ? ng.advance : 500;
            g.components = ng.components;
            if (!g.components.empty()) g.status = model::GlyphStatus::Drafted;   // built, not blank
            g.order    = order;
            order += 1.0;
            f.put(ng.name, Record{g});
        }
    });
}

bool create_glyph_for(FontJournal& j, Font& f, const std::string& name, char32_t cp,
                      const std::string& after, bool* built, std::string* why) {
    const auto plan = model::plan_composite(f, cp);
    if (built) *built = plan.ok();
    if (!plan.ok()) return add_glyph(j, f, name, {cp}, after, why);
    if (!model::valid_glyph_name(name)) return add_glyph(j, f, name, {cp}, after, why);   // its refusal
    if (f.glyph(name)) { say(why, "There is already a glyph named " + q(name) + "."); return false; }
    return j.run("Add glyph " + q(name), {name}, [&] {
        Glyph g;
        g.name       = name;
        g.unicodes   = {cp};
        g.advance    = plan.advance;
        g.components = plan.components;
        g.status     = model::GlyphStatus::Drafted;
        g.order      = f.order_after(after);
        f.put(name, Record{g});
    });
}

bool remove_glyph(FontJournal& j, Font& f, const std::string& name, std::string* why) {
    if (!f.glyph(name)) return false;
    for (const auto& other : f.glyph_order())
        if (f.glyph(other)->uses(name)) {
            say(why, q(other) + " is built from " + q(name) + " as a component. Decompose or "
                     "remove " + q(other) + " first, or it loses part of its drawing.");
            return false;
        }
    // Kerning that names the glyph goes with it, in the same step.
    return j.run("Delete glyph " + q(name), {name, Font::kKerning}, [&] {
        model::Kerning k = f.kerning();
        for (auto it = k.pairs.begin(); it != k.pairs.end();)
            it = (it->first.first == name || it->first.second == name) ? k.pairs.erase(it) : std::next(it);
        for (auto& [g, members] : k.groups)
            members.erase(std::remove(members.begin(), members.end(), name), members.end());
        f.put(Font::kKerning, Record{k});
        f.put(name, std::nullopt);
    });
}

bool rename_glyph(FontJournal& j, Font& f, const std::string& from, const std::string& to,
                  std::string* why) {
    if (!f.glyph(from) || from == to) return false;
    if (!model::valid_glyph_name(to)) {
        say(why, "Glyph names use A-Z a-z 0-9 . _ - and start with a letter or underscore.");
        return false;
    }
    if (f.glyph(to)) { say(why, "There is already a glyph named " + q(to) + "."); return false; }

    std::vector<std::string> ids{from, to, Font::kKerning, Font::kMetricsKeys};
    for (const auto& n : f.glyph_order())
        if (n != from && f.glyph(n)->uses(from)) ids.push_back(n);

    return j.run("Rename " + q(from) + " to " + q(to), ids, [&] {
        Glyph g = glyph_copy(f, from);
        g.name = to;
        f.put(from, std::nullopt);
        f.put(to, Record{g});
        for (std::size_t i = 4; i < ids.size(); ++i) {
            Glyph u = glyph_copy(f, ids[i]);
            for (auto& c : u.components) if (c.base == from) c.base = to;
            f.put(ids[i], Record{u});
        }
        model::Kerning k = f.kerning();
        std::map<std::pair<std::string, std::string>, int> pairs;
        for (const auto& [key, v] : k.pairs) {
            auto p = key;
            if (p.first == from)  p.first = to;
            if (p.second == from) p.second = to;
            pairs[p] = v;
        }
        k.pairs = std::move(pairs);
        for (auto& [gname, members] : k.groups)
            for (auto& m : members) if (m == from) m = to;
        f.put(Font::kKerning, Record{k});
        model::MetricsKeys mk = f.metrics_keys();
        if (auto it = mk.keys.find(from); it != mk.keys.end()) {
            auto key = it->second;
            mk.keys.erase(it);
            mk.keys[to] = key;
        }
        f.put(Font::kMetricsKeys, Record{mk});
    });
}

bool set_unicodes(FontJournal& j, Font& f, const std::string& name, const std::vector<char32_t>& cps) {
    if (!f.glyph(name)) return false;
    return j.run("Unicode of " + q(name), {name}, [&] {
        Glyph g = glyph_copy(f, name);
        g.unicodes = cps;
        f.put(name, Record{g});
    });
}

bool set_status(FontJournal& j, Font& f, const std::string& name, model::GlyphStatus s) {
    if (!f.glyph(name)) return false;
    return j.run("Mark " + q(name) + " " + model::status_name(s), {name}, [&] {
        Glyph g = glyph_copy(f, name);
        g.status = s;
        f.put(name, Record{g});
    });
}

bool set_note(FontJournal& j, Font& f, const std::string& name, const std::string& note) {
    if (!f.glyph(name)) return false;
    return j.run("Note on " + q(name), {name}, [&] {
        Glyph g = glyph_copy(f, name);
        g.note = note;
        f.put(name, Record{g});
    }, "note:" + name);
}

bool set_advance(FontJournal& j, Font& f, const std::string& name, double advance) {
    if (!f.glyph(name)) return false;
    return j.run("Width of " + q(name), {name}, [&] {
        Glyph g = glyph_copy(f, name);
        g.advance = std::max(0.0, std::round(advance));
        f.put(name, Record{g});
    }, "advance:" + name);
}

// ── outline drag ───────────────────────────────────────────────────────────

void drag_begin(FontJournal& j, Font& f, const std::vector<std::string>& glyphs,
                const std::string& label) {
    Drag& d = drag();
    d.names.clear();
    d.at_press.clear();
    for (const auto& n : glyphs)
        if (f.glyph(n)) { d.names.push_back(n); d.at_press.push_back(glyph_copy(f, n)); }
    if (!d.names.empty()) j.begin(label, d.names);
}

void drag_translate(FontJournal& j, Font& f, double dx, double dy) {
    Drag& d = drag();
    if (!j.in_gesture()) return;
    // Through the door even mid-drag: inside the open gesture this just writes.
    j.run("Move", d.names, [&] {
        for (std::size_t i = 0; i < d.names.size(); ++i) {
            Glyph g = d.at_press[i];
            translate(g, dx, dy);
            f.put(d.names[i], Record{g});
        }
    });
}

void drag_points(FontJournal& j, Font& f, const geom::Points& pts, double dx, double dy, bool free_handles) {
    Drag& d = drag();
    if (!j.in_gesture() || d.names.empty()) return;
    j.run("Move", {d.names[0]}, [&] {
        Glyph g = glyph_copy(f, d.names[0]);
        geom::move_points(g, d.at_press[0], pts, {dx, dy}, free_handles);
        f.put(d.names[0], Record{g});
    });
}

void drag_sidebearing(FontJournal& j, Font& f, Side side, double dx, bool move_outline) {
    Drag& d = drag();
    if (!j.in_gesture() || d.names.empty()) return;
    j.run("Sidebearing", {d.names[0]}, [&] {
        Glyph g = d.at_press[0];
        if (side == Side::Right) {
            g.advance = std::max(0.0, g.advance + dx);
        } else if (move_outline) {
            geom::shift_x(g, dx);                        // drawing slides, advance fixed
        } else {
            const double shift = std::min(dx, g.advance);   // the origin can't pass the advance
            geom::shift_x(g, -shift);                    // origin moves right by shift
            g.advance -= shift;
        }
        f.put(d.names[0], Record{g});
    });
}

bool nudge(FontJournal& j, Font& f, const std::string& glyph, const geom::Points& pts,
           double dx, double dy) {
    if (!f.glyph(glyph) || pts.empty() || (dx == 0 && dy == 0)) return false;
    return j.run("Nudge", {glyph}, [&] {
        const Glyph was = glyph_copy(f, glyph);
        Glyph g = was;
        geom::move_points(g, was, pts, {dx, dy});
        g.snap();
        f.put(glyph, Record{g});
    }, "nudge:" + glyph);
}

bool drag_end(FontJournal& j, Font& f) {
    Drag& d = drag();
    if (!j.in_gesture()) return false;
    // Snap inside the gesture, so the step's AFTER is the integral outline.
    j.run("Snap", d.names, [&] {
        for (const auto& n : d.names) {
            Glyph g = glyph_copy(f, n);
            g.snap();
            f.put(n, Record{g});
        }
    });
    d.names.clear();
    d.at_press.clear();
    return j.commit();
}

void drag_cancel(FontJournal& j) {
    drag().names.clear();
    drag().at_press.clear();
    j.cancel();
}

// ── components & anchors (M5) ──────────────────────────────────────────────

namespace {
// Does `from` reach `target` through its components (any depth)?
bool reaches(const Font& f, const std::string& from, const std::string& target, int depth = 0) {
    if (from == target) return true;
    const Glyph* g = f.glyph(from);
    if (!g || depth > 16) return false;
    for (const auto& k : g->components)
        if (reaches(f, k.base, target, depth + 1)) return true;
    return false;
}
bool valid_anchor_name(const std::string& n) {
    if (n.empty() || n.size() > 63) return false;
    return std::all_of(n.begin(), n.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '_' || c == '.' || c == '-';
    });
}
}  // namespace

bool add_component(FontJournal& j, Font& f, const std::string& glyph, const std::string& base,
                   double dx, double dy, std::string* why) {
    if (!f.glyph(glyph)) return false;
    if (!f.glyph(base)) { say(why, "There is no glyph named " + q(base) + "."); return false; }
    if (reaches(f, base, glyph)) {
        say(why, q(base) + " already uses " + q(glyph) + " -- a component can't contain itself.");
        return false;
    }
    return j.run("Add component " + q(base), {glyph}, [&] {
        Glyph g = glyph_copy(f, glyph);
        model::Component k{base};
        k.dx = std::round(dx);
        k.dy = std::round(dy);
        g.components.push_back(k);
        if (g.status == model::GlyphStatus::Empty) g.status = model::GlyphStatus::Drafted;
        f.put(glyph, Record{g});
    });
}

bool decompose(FontJournal& j, Font& f, const std::string& glyph, std::string* why) {
    const Glyph* was = f.glyph(glyph);
    if (!was) return false;
    if (was->components.empty()) { say(why, q(glyph) + " has no components."); return false; }
    return j.run("Decompose " + q(glyph), {glyph}, [&] {
        Glyph g = glyph_copy(f, glyph);
        for (const auto& k : g.components)
            for (auto& c : geom::component_contours(f, k)) g.contours.push_back(std::move(c));
        g.components.clear();
        g.snap();
        f.put(glyph, Record{g});
    });
}

bool add_anchor(FontJournal& j, Font& f, const std::string& glyph, const std::string& name,
                double x, double y, std::string* why) {
    const Glyph* was = f.glyph(glyph);
    if (!was) return false;
    if (!valid_anchor_name(name)) {
        say(why, "Anchor names use A-Z a-z 0-9 _ . - (\"top\", \"_top\" on a mark).");
        return false;
    }
    for (const auto& a : was->anchors)
        if (a.name == name) { say(why, q(glyph) + " already has an anchor " + q(name) + "."); return false; }
    return j.run("Add anchor " + q(name), {glyph}, [&] {
        Glyph g = glyph_copy(f, glyph);
        g.anchors.push_back({name, std::round(x), std::round(y)});
        f.put(glyph, Record{g});
    });
}

bool rename_anchor(FontJournal& j, Font& f, const std::string& glyph, int index, const std::string& name,
                   std::string* why) {
    const Glyph* was = f.glyph(glyph);
    if (!was || index < 0 || index >= static_cast<int>(was->anchors.size())) return false;
    if (was->anchors[index].name == name) return false;
    if (!valid_anchor_name(name)) {
        say(why, "Anchor names use A-Z a-z 0-9 _ . - (\"top\", \"_top\" on a mark).");
        return false;
    }
    for (const auto& a : was->anchors)
        if (a.name == name) { say(why, q(glyph) + " already has an anchor " + q(name) + "."); return false; }
    return j.run("Rename anchor " + q(was->anchors[index].name), {glyph}, [&] {
        Glyph g = glyph_copy(f, glyph);
        g.anchors[index].name = name;
        f.put(glyph, Record{g});
    });
}

bool delete_points(FontJournal& j, Font& f, const std::string& glyph, const geom::Points& pts,
                   std::string* why) {
    const Glyph* was = f.glyph(glyph);
    if (!was) return false;
    std::vector<int> anchors, comps;
    for (const auto& p : pts) {
        if (!geom::valid(*was, p)) continue;
        if (p.part == geom::PointRef::Part::Anchor) anchors.push_back(p.index);
        if (p.part == geom::PointRef::Part::Component) comps.push_back(p.index);
    }
    if (anchors.empty() && comps.empty()) {
        say(why, "Delete takes anchors and components for now -- deleting nodes comes with the pen tool.");
        return false;
    }
    const std::size_t n = anchors.size() + comps.size();
    return j.run(n == 1 ? (anchors.empty() ? "Delete component" : "Delete anchor")
                        : "Delete " + std::to_string(n) + " items",
                 {glyph}, [&] {
        Glyph g = glyph_copy(f, glyph);
        std::sort(anchors.rbegin(), anchors.rend());   // back to front: indices stay good
        std::sort(comps.rbegin(), comps.rend());
        for (int i : anchors) g.anchors.erase(g.anchors.begin() + i);
        for (int i : comps) g.components.erase(g.components.begin() + i);
        f.put(glyph, Record{g});
    });
}

bool set_node_type(FontJournal& j, Font& f, const std::string& glyph, const geom::Points& pts,
                   model::Node::Type type) {
    const Glyph* was = f.glyph(glyph);
    if (!was) return false;
    std::set<std::pair<int, int>> nodes;
    for (const auto& p : pts) {
        if (!geom::valid(*was, p)) continue;
        using P = geom::PointRef::Part;
        if (p.part == P::Node || p.part == P::In || p.part == P::Out) nodes.insert({p.contour, p.index});
    }
    Glyph g = *was;
    for (const auto& [c, i] : nodes) geom::set_node_type(g.contours[c], i, type);
    if (g == *was) return false;
    static const char* names[] = {"Smooth", "Cusp", "Symmetric", "Corner"};
    const std::string label = std::string(names[static_cast<int>(type)]) +
                              (nodes.size() == 1 ? " node" : " (" + std::to_string(nodes.size()) + " nodes)");
    return j.run(label, {glyph}, [&] {
        Glyph h = glyph_copy(f, glyph);
        for (const auto& [c, i] : nodes) geom::set_node_type(h.contours[c], i, type);
        f.put(glyph, Record{h});
    });
}

bool set_position(FontJournal& j, Font& f, const std::string& glyph, const geom::PointRef& p,
                  double x, double y) {
    const Glyph* was = f.glyph(glyph);
    if (!was || !geom::valid(*was, p)) return false;
    const geom::Vec2 at = geom::position(*was, p);
    const double dx = std::round(x) - at.x, dy = std::round(y) - at.y;
    if (dx == 0 && dy == 0) return false;
    const std::string key = "pos:" + glyph + ":" + std::to_string(static_cast<int>(p.part)) + ":" +
                            std::to_string(p.contour) + ":" + std::to_string(p.index);
    return j.run("Position", {glyph}, [&] {
        Glyph g = glyph_copy(f, glyph);
        const Glyph press = g;
        geom::move_points(g, press, {p}, {dx, dy});
        g.snap();
        f.put(glyph, Record{g});
    }, key);
}

bool add_default_anchors(FontJournal& j, Font& f, const std::vector<std::string>& glyphs, std::string* why) {
    std::vector<std::string> ids;
    std::size_t count = 0;
    for (const auto& n : glyphs) {
        const Glyph* g = f.glyph(n);
        if (!g) continue;
        const auto add = model::default_anchors(f, *g);
        if (add.empty()) continue;
        ids.push_back(n);
        count += add.size();
    }
    if (ids.empty()) {
        say(why, "Nothing to add: the anchors are there already, or these are not letters or marks.");
        return false;
    }
    const std::string label = "Default anchors (" + std::to_string(count) + " in " + std::to_string(ids.size()) +
                              (ids.size() == 1 ? " glyph)" : " glyphs)");
    return j.run(label, ids, [&] {
        for (const auto& n : ids) {
            Glyph g = glyph_copy(f, n);
            for (auto& a : model::default_anchors(f, g)) g.anchors.push_back(std::move(a));
            f.put(n, Record{g});
        }
    });
}

// ── font records ───────────────────────────────────────────────────────────

bool set_family(FontJournal& j, Font& f, const std::string& family) {
    return j.run("Family name", {Font::kInfo}, [&] {
        model::FontInfo i = f.info();
        i.family = family;
        f.put(Font::kInfo, Record{i});
    });
}

bool set_style(FontJournal& j, Font& f, const std::string& style) {
    return j.run("Style name", {Font::kInfo}, [&] {
        model::FontInfo i = f.info();
        i.style = style;
        f.put(Font::kInfo, Record{i});
    });
}

bool set_units_per_em(FontJournal& j, Font& f, int upm, std::string* why) {
    if (upm < 16 || upm > 16384) {
        say(why, "Units per em must be 16..16384 (the OpenType limit). 1000 is the PostScript "
                 "tradition; 2048 the TrueType one.");
        return false;
    }
    return j.run("Units per em", {Font::kInfo}, [&] {
        model::FontInfo i = f.info();
        i.units_per_em = upm;
        f.put(Font::kInfo, Record{i});
    }, "upm");
}

bool set_metric(FontJournal& j, Font& f, model::Metrics::Field which, int value) {
    return j.run(model::field_name(which), {Font::kMetrics}, [&] {
        model::Metrics m = f.metrics();
        m.set(which, value);
        f.put(Font::kMetrics, Record{m});
    }, std::string("metric:") + model::field_name(which));
}

bool set_kern(FontJournal& j, Font& f, const std::string& left, const std::string& right, int value) {
    return j.run("Kern " + left + " " + right, {Font::kKerning}, [&] {
        model::Kerning k = f.kerning();
        if (value == 0) k.pairs.erase({left, right});
        else            k.pairs[{left, right}] = value;
        f.put(Font::kKerning, Record{k});
    }, "kern:" + left + "|" + right);
}

// ── the verb table ─────────────────────────────────────────────────────────

void make_fixture(Font& f) {
    model::make_new_font(f);
    Font::Loading load(f);
    const auto box = [](double x0, double y0, double x1, double y1) {
        model::Contour c;   // counter-clockwise, y-up
        for (auto [x, y] : {std::pair{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}})
            c.nodes.push_back(model::Node::corner(x, y));
        return c;
    };
    double order = 10;
    const auto add = [&](const std::string& n, char32_t cp, double adv, std::vector<model::Contour> cs) {
        Glyph g;
        g.name = n; g.unicodes = {cp}; g.advance = adv; g.order = order++;
        g.contours = std::move(cs);
        g.status = model::GlyphStatus::Drafted;
        f.put(n, Record{g});
        return g;
    };
    add("H", U'H', 700, {box(80, 0, 170, 700), box(530, 0, 620, 700), box(170, 320, 530, 400)});
    add("O", U'O', 760, {box(60, -12, 700, 712)});
    add("n", U'n', 560, {box(70, 0, 150, 500), box(410, 0, 490, 500)});
    add("o", U'o', 540, {box(50, -10, 490, 510)});
    Glyph od = add("Odotbelow", U'Ọ', 760, {});
    od.components = {{"O"}};
    od.anchors = {{"bottom", 380, 0}};
    f.put(od.name, Record{od});

    model::Kerning k;
    k.groups["public.kern1.O"] = {"O", "Odotbelow"};
    k.pairs[{"public.kern1.O", "H"}] = -10;
    k.pairs[{"H", "O"}] = -15;
    f.put(Font::kKerning, Record{k});
    model::MetricsKeys mk;
    mk.keys["n"] = {"=60", "=60", ""};
    f.put(Font::kMetricsKeys, Record{mk});
}

const std::vector<Verb>& verbs() {
    using model::Metrics;
    static const std::vector<Verb> table = {
        {"add_glyph",    [](FontJournal& j, Font& f) { add_glyph(j, f, "A", {U'A'}, "H"); }},
        {"add_glyphs",   [](FontJournal& j, Font& f) {
             add_glyphs(j, f, model::glyphs_for(f, {U'A', U'\u00E9', U'\u0416'}), "Create 3 missing"); }},
        {"remove_glyph", [](FontJournal& j, Font& f) { remove_glyph(j, f, "H"); }},
        {"rename_glyph", [](FontJournal& j, Font& f) { rename_glyph(j, f, "O", "O.alt"); }},
        {"set_unicodes", [](FontJournal& j, Font& f) { set_unicodes(j, f, "o", {U'o', U'о'}); }},
        {"set_status",   [](FontJournal& j, Font& f) { set_status(j, f, "n", model::GlyphStatus::Done); }},
        {"set_note",     [](FontJournal& j, Font& f) { set_note(j, f, "n", "stems too light"); }},
        {"set_advance",  [](FontJournal& j, Font& f) { set_advance(j, f, "n", 580); }},
        {"drag",         [](FontJournal& j, Font& f) {
             drag_begin(j, f, {"H", "o"});
             drag_translate(j, f, 3.4, -1.2);
             drag_translate(j, f, 12.6, 4.4);
             drag_end(j, f); }},
        {"drag_points",  [](FontJournal& j, Font& f) {
             drag_begin(j, f, {"o"}, "Move points");
             drag_points(j, f, {{0, 0, geom::PointRef::Part::Node}, {0, 1, geom::PointRef::Part::Out}}, 4.6, -2.2);
             drag_points(j, f, {{0, 0, geom::PointRef::Part::Node}, {0, 1, geom::PointRef::Part::Out}}, 9.4, 3.1);
             drag_end(j, f); }},
        {"drag_sidebearing", [](FontJournal& j, Font& f) {
             drag_begin(j, f, {"n"}, "Left sidebearing");
             drag_sidebearing(j, f, Side::Left, 7.3, false);
             drag_sidebearing(j, f, Side::Left, 12.6, false);
             drag_end(j, f); }},
        {"nudge",        [](FontJournal& j, Font& f) {
             nudge(j, f, "H", {{0, 0, geom::PointRef::Part::Node}}, 10, 0); }},
        {"add_component", [](FontJournal& j, Font& f) { add_component(j, f, "n", "o", 40, 0); }},
        {"set_node_type", [](FontJournal& j, Font& f) {
             set_node_type(j, f, "o", {{0, 0, geom::PointRef::Part::Node}, {0, 2, geom::PointRef::Part::Node}},
                           model::Node::Type::Smooth); }},
        {"create_glyph_for", [](FontJournal& j, Font& f) { create_glyph_for(j, f, "Odieresis", U'\u00D6', "O"); }},
        {"decompose",    [](FontJournal& j, Font& f) { decompose(j, f, "Odotbelow"); }},
        {"add_anchor",   [](FontJournal& j, Font& f) { add_anchor(j, f, "H", "top", 350, 700); }},
        {"rename_anchor", [](FontJournal& j, Font& f) { rename_anchor(j, f, "Odotbelow", 0, "center"); }},
        {"delete_points", [](FontJournal& j, Font& f) {
             delete_points(j, f, "Odotbelow", {{-1, 0, geom::PointRef::Part::Component}}); }},
        {"set_position", [](FontJournal& j, Font& f) {
             set_position(j, f, "o", {0, 0, geom::PointRef::Part::Node}, 61, -9); }},
        {"add_default_anchors", [](FontJournal& j, Font& f) { add_default_anchors(j, f, {"H", "n", "o"}); }},
        {"drag_component", [](FontJournal& j, Font& f) {
             drag_begin(j, f, {"Odotbelow"}, "Move component");
             drag_points(j, f, {{-1, 0, geom::PointRef::Part::Component}}, 13.7, -4.2);
             drag_end(j, f); }},
        {"set_family",   [](FontJournal& j, Font& f) { set_family(j, f, "Garamond Nova"); }},
        {"set_style",    [](FontJournal& j, Font& f) { set_style(j, f, "Italic"); }},
        {"set_units_per_em", [](FontJournal& j, Font& f) { set_units_per_em(j, f, 2048); }},
        {"set_metric",   [](FontJournal& j, Font& f) { set_metric(j, f, Metrics::Field::XHeight, 480); }},
        {"set_kern",     [](FontJournal& j, Font& f) { set_kern(j, f, "H", "n", -20); }},
    };
    return table;
}

}  // namespace granjon::command
