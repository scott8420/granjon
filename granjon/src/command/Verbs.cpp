#include "command/Verbs.hpp"

#include <algorithm>
#include <cmath>
#include <map>

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
            g.advance  = 500;
            g.order    = order;
            order += 1.0;
            f.put(ng.name, Record{g});
        }
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
        {"set_family",   [](FontJournal& j, Font& f) { set_family(j, f, "Garamond Nova"); }},
        {"set_style",    [](FontJournal& j, Font& f) { set_style(j, f, "Italic"); }},
        {"set_units_per_em", [](FontJournal& j, Font& f) { set_units_per_em(j, f, 2048); }},
        {"set_metric",   [](FontJournal& j, Font& f) { set_metric(j, f, Metrics::Field::XHeight, 480); }},
        {"set_kern",     [](FontJournal& j, Font& f) { set_kern(j, f, "H", "n", -20); }},
    };
    return table;
}

}  // namespace granjon::command
