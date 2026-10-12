#include "command/Verbs.hpp"

#include "model/Composite.hpp"
#include "model/Spacing.hpp"
#include "model/Checks.hpp"
#include "model/Profiles.hpp"
#include "geom/NodeEdit.hpp"
#include "geom/Corners.hpp"
#include "geom/Offset.hpp"
#include "geom/Overlap.hpp"
#include "geom/Shapes.hpp"
#include "model/Bounds.hpp"
#include "unicode/UcdTable.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
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
std::vector<model::Guide> guides_of(const Font& f, const std::string& owner) {
    if (owner.empty()) return f.info().guides;
    const Glyph* g = f.glyph(owner);
    return g ? g->guides : std::vector<model::Guide>{};
}
// Write `gs` back to its owner (inside a step).
void put_guides(Font& f, const std::string& owner, std::vector<model::Guide> gs) {
    if (owner.empty()) {
        model::FontInfo i = f.info();
        i.guides = std::move(gs);
        f.put(Font::kInfo, Record{i});
    } else {
        Glyph g = glyph_copy(f, owner);
        g.guides = std::move(gs);
        f.put(owner, Record{g});
    }
}
std::string owner_id(const std::string& owner) { return owner.empty() ? std::string(Font::kInfo) : owner; }
model::Guide rounded(model::Guide g) { g.x = std::round(g.x); g.y = std::round(g.y); return g; }

// The one live guide drag.
struct GuideDrag { std::string owner; int index = -1; bool live = false; };
GuideDrag& guide_drag() { static GuideDrag d; return d; }

void translate(Glyph& g, double dx, double dy) {
    for (auto& c : g.contours)
        for (auto& n : c.nodes) {
            n.x += dx;   n.y += dy;
            n.cx1 += dx; n.cy1 += dy;
            n.cx2 += dx; n.cy2 += dy;
        }
    for (auto& a : g.anchors) { a.x += dx; a.y += dy; }
    if (g.image) { g.image->dx += dx; g.image->dy += dy; }   // the scan stays under the drawing
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

    std::vector<std::string> ids{from, to, Font::kKerning, Font::kMetricsKeys, Font::kSkips};
    for (const auto& n : f.glyph_order())
        if (n != from && f.glyph(n)->uses(from)) ids.push_back(n);

    return j.run("Rename " + q(from) + " to " + q(to), ids, [&] {
        Glyph g = glyph_copy(f, from);
        g.name = to;
        f.put(from, std::nullopt);
        f.put(to, Record{g});
        for (std::size_t i = 5; i < ids.size(); ++i) {
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
        // Skipped findings follow the glyph ("check|glyph|detail").
        model::CheckSkips sk;
        for (const auto& key : f.skips().keys) {
            const auto a = key.find('|'), b = a == std::string::npos ? a : key.find('|', a + 1);
            if (b != std::string::npos && key.substr(a + 1, b - a - 1) == from)
                sk.keys.insert(key.substr(0, a + 1) + to + key.substr(b));
            else
                sk.keys.insert(key);
        }
        f.put(Font::kSkips, Record{sk});
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

// ── background image (M13) ─────────────────────────────────────────────────

const std::string& sample_png() {
    static const std::string png(
        "\x89\x50\x4e\x47\x0d\x0a\x1a\x0a\x00\x00\x00\x0d\x49\x48\x44\x52\x00\x00\x00\x04\x00\x00\x00\x02"
        "\x08\x02\x00\x00\x00\xf0\xca\xea\x34\x00\x00\x00\x0e\x49\x44\x41\x54\x78\x9c\x63\x38\x81\x04"
        "\x18\x90\x39\x00\xf3\xda\x12\xc1\xa2\x3e\x32\x8b\x00\x00\x00\x00\x49\x45\x4e\x44\xae\x42\x60\x82",
        71);
    return png;
}

model::GlyphImage image_fitted(const Font& f, model::GlyphImage img, int px_w, int px_h) {
    const auto& m = f.metrics();
    const double span = std::max(1, m.ascender - m.descender);
    const double s = px_h > 0 ? span / px_h : 1.0;
    (void)px_w;
    img.xx = s; img.yy = s; img.xy = 0; img.yx = 0;
    img.dx = 0;
    img.dy = m.descender;
    return img;
}

bool place_image(FontJournal& j, Font& f, const std::string& glyph, const model::GlyphImage& img, std::string* why) {
    const Glyph* was = f.glyph(glyph);
    if (!was) return false;
    if (!f.image(img.file)) {
        if (why) *why = "The image " + q(img.file) + " isn't in this font.";
        return false;
    }
    model::GlyphImage im = img;
    im.opacity = std::clamp(im.opacity, 0.0, 1.0);
    if (was->image == im) return false;
    return j.run(std::string(was->image ? "Replace image on " : "Place image on ") + q(glyph), {glyph}, [&] {
        Glyph g = glyph_copy(f, glyph);
        g.image = im;
        f.put(glyph, Record{g});
    });
}

bool remove_image(FontJournal& j, Font& f, const std::string& glyph, std::string* why) {
    const Glyph* was = f.glyph(glyph);
    if (!was) return false;
    if (!was->image) { if (why) *why = q(glyph) + " has no image to remove."; return false; }
    return j.run("Remove image from " + q(glyph), {glyph}, [&] {
        Glyph g = glyph_copy(f, glyph);
        g.image.reset();
        f.put(glyph, Record{g});
    });
}

bool set_image_frame(FontJournal& j, Font& f, const std::string& glyph, const model::GlyphImage& frame) {
    const Glyph* was = f.glyph(glyph);
    if (!was || !was->image) return false;
    if (std::abs(frame.xx * frame.yy - frame.xy * frame.yx) < 1e-9) return false;   // flat: nothing to see
    model::GlyphImage im = *was->image;
    im.xx = frame.xx; im.xy = frame.xy; im.yx = frame.yx; im.yy = frame.yy; im.dx = frame.dx; im.dy = frame.dy;
    if (im == *was->image) return false;
    return j.run("Image on " + q(glyph), {glyph}, [&] {
        Glyph g = glyph_copy(f, glyph);
        g.image = im;
        f.put(glyph, Record{g});
    }, "imageframe:" + glyph);
}

bool set_image_opacity(FontJournal& j, Font& f, const std::string& glyph, double opacity) {
    const Glyph* was = f.glyph(glyph);
    if (!was || !was->image) return false;
    opacity = std::clamp(std::round(opacity * 100) / 100, 0.0, 1.0);
    if (was->image->opacity == opacity) return false;
    return j.run("Image opacity on " + q(glyph), {glyph}, [&] {
        Glyph g = glyph_copy(f, glyph);
        g.image->opacity = opacity;
        f.put(glyph, Record{g});
    }, "imageopacity:" + glyph);
}

bool set_image_locked(FontJournal& j, Font& f, const std::string& glyph, bool locked) {
    const Glyph* was = f.glyph(glyph);
    if (!was || !was->image || was->image->locked == locked) return false;
    return j.run(std::string(locked ? "Lock image on " : "Unlock image on ") + q(glyph), {glyph}, [&] {
        Glyph g = glyph_copy(f, glyph);
        g.image->locked = locked;
        f.put(glyph, Record{g});
    });
}

std::string ufo_color(double r, double g, double b, double a) {
    auto c = [](double v) {
        v = std::clamp(v, 0.0, 1.0);
        char buf[16];
        std::snprintf(buf, sizeof buf, "%.3f", v);
        std::string s = buf;
        while (s.size() > 1 && s.back() == '0') s.pop_back();   // 0.500 -> 0.5, 1.000 -> 1.
        if (s.back() == '.') s.pop_back();
        return s;
    };
    return c(r) + "," + c(g) + "," + c(b) + "," + c(a);
}

bool set_image_color(FontJournal& j, Font& f, const std::string& glyph, const std::string& color) {
    const Glyph* was = f.glyph(glyph);
    if (!was || !was->image || was->image->color == color) return false;
    return j.run(std::string(color.empty() ? "Default colour for image on " : "Colour of image on ") + q(glyph), {glyph}, [&] {
        Glyph g = glyph_copy(f, glyph);
        g.image->color = color;
        f.put(glyph, Record{g});
    });
}

void image_drag_begin(FontJournal& j, Font& f, const std::string& glyph, const std::string& label) {
    const Glyph* g = f.glyph(glyph);
    if (!g || !g->image) return;
    drag_begin(j, f, {glyph}, label);   // drag_end snaps (the offset) and commits
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

void drag_transform(FontJournal& j, Font& f, const geom::Points& pts, const geom::Affine& m) {
    Drag& d = drag();
    if (!j.in_gesture() || d.names.empty()) return;
    j.run("Transform", {d.names[0]}, [&] {
        Glyph g;
        geom::transform_points(g, d.at_press[0], pts, m);
        f.put(d.names[0], Record{g});
    });
}

bool transform_from(FontJournal& j, Font& f, const std::string& glyph, const model::Glyph& base,
                    const geom::Points& pts, const geom::Affine& m, const std::string& label,
                    const std::string& merge_key) {
    if (!f.glyph(glyph)) return false;
    return j.run(label, {glyph}, [&] {
        Glyph g;
        geom::transform_points(g, base, pts, m);
        g.snap();
        f.put(glyph, Record{g});
    }, merge_key);
}

bool transform_points(FontJournal& j, Font& f, const std::string& glyph, const geom::Points& pts,
                      const geom::Affine& m, const std::string& label, const std::string& merge_key) {
    if (!f.glyph(glyph) || m.identity()) return false;
    return j.run(label, {glyph}, [&] {
        const Glyph was = glyph_copy(f, glyph);
        Glyph g;
        geom::transform_points(g, was, pts, m);
        g.snap();
        f.put(glyph, Record{g});
    }, merge_key);   // r1: a run of step presses can fold into one step
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
    if (GuideDrag& gd = guide_drag(); gd.live) {   // a guide drag: whole units on release
        gd.live = false;
        auto gs = guides_of(f, gd.owner);
        if (gd.index >= 0 && gd.index < static_cast<int>(gs.size())) {
            gs[gd.index] = rounded(gs[gd.index]);
            j.run("Snap", {owner_id(gd.owner)}, [&] { put_guides(f, gd.owner, gs); });
        }
    }
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
    guide_drag().live = false;
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
    std::set<std::pair<int, int>> nodes;
    std::vector<geom::PointRef> handles;
    for (const auto& p : pts) {
        if (!geom::valid(*was, p)) continue;
        switch (p.part) {
            case geom::PointRef::Part::Anchor:    anchors.push_back(p.index); break;
            case geom::PointRef::Part::Component: comps.push_back(p.index); break;
            case geom::PointRef::Part::Node:      nodes.insert({p.contour, p.index}); break;
            default:                              handles.push_back(p); break;
        }
    }
    // A handle whose node also goes is gone with it.
    std::erase_if(handles, [&](const geom::PointRef& h) { return nodes.count({h.contour, h.index}) > 0; });
    if (anchors.empty() && comps.empty() && nodes.empty() && handles.empty()) {
        say(why, "Nothing picked to delete.");
        return false;
    }
    std::string label;
    const std::size_t n = anchors.size() + comps.size() + nodes.size() + handles.size();
    if (n == 1) label = !nodes.empty() ? "Delete node" : !handles.empty() ? "Retract handle"
                      : !anchors.empty() ? "Delete anchor" : "Delete component";
    else if (n == nodes.size()) label = "Delete " + std::to_string(n) + " nodes";
    else label = "Delete " + std::to_string(n) + " items";
    return j.run(label, {glyph}, [&] {
        Glyph g = glyph_copy(f, glyph);
        for (const auto& h : handles)
            geom::retract_handle(g.contours[static_cast<std::size_t>(h.contour)], h.index, h.part == geom::PointRef::Part::Out);
        geom::delete_nodes(g, nodes);
        std::sort(anchors.rbegin(), anchors.rend());   // back to front: indices stay good
        std::sort(comps.rbegin(), comps.rend());
        for (int i : anchors) g.anchors.erase(g.anchors.begin() + i);
        for (int i : comps) g.components.erase(g.components.begin() + i);
        g.snap();
        f.put(glyph, Record{g});
    });
}

PenAt pen_press(FontJournal& j, Font& f, const std::string& glyph, int contour, geom::Vec2 p, bool close,
                bool from_start) {
    PenAt at;
    const Glyph* was = f.glyph(glyph);
    if (!was) return at;
    const bool have = contour >= 0 && contour < static_cast<int>(was->contours.size());
    if ((close || from_start) && !have) return at;
    if (have && was->contours[static_cast<std::size_t>(contour)].closed) return at;   // the pen draws open paths only
    drag_begin(j, f, {glyph}, close ? "Close path" : have ? "Add node" : "New path");
    if (!j.in_gesture()) return at;
    j.run("Pen", {glyph}, [&] {
        Glyph g = glyph_copy(f, glyph);
        if (close) {
            auto& c = g.contours[static_cast<std::size_t>(contour)];
            c.closed = true;
            at = {contour, 0, true};
        } else {
            if (from_start) geom::reverse_open(g.contours[static_cast<std::size_t>(contour)]);
            const int ci = geom::pen_add(g, have ? contour : -1, p);
            at = {ci, static_cast<int>(g.contours[static_cast<std::size_t>(ci)].nodes.size()) - 1, false};
        }
        if (g.status == model::GlyphStatus::Empty) g.status = model::GlyphStatus::Drafted;
        f.put(glyph, Record{g});
    });
    return at;
}

void pen_drag(FontJournal& j, Font& f, const std::string& glyph, const PenAt& at, geom::Vec2 d, bool cusp) {
    if (!j.in_gesture() || at.contour < 0) return;
    j.run("Pen", {glyph}, [&] {
        Glyph g = glyph_copy(f, glyph);
        if (at.contour >= static_cast<int>(g.contours.size())) return;
        auto& c = g.contours[static_cast<std::size_t>(at.contour)];
        if (at.closed) geom::pen_close_handles(c, d);
        else geom::pen_drag_handles(c, at.node, d, cusp);
        f.put(glyph, Record{g});
    });
}

bool insert_node(FontJournal& j, Font& f, const std::string& glyph, int contour, int segment, double t, int* index) {
    const Glyph* was = f.glyph(glyph);
    if (!was || contour < 0 || contour >= static_cast<int>(was->contours.size())) return false;
    if (segment < 0 || segment >= geom::segment_count(was->contours[static_cast<std::size_t>(contour)])) return false;
    int at = -1;
    const bool ok = j.run("Add node", {glyph}, [&] {
        Glyph g = glyph_copy(f, glyph);
        at = geom::insert_node(g.contours[static_cast<std::size_t>(contour)], segment, t);
        g.snap();
        f.put(glyph, Record{g});
    });
    if (index) *index = at;
    return ok;
}

std::vector<std::pair<int, int>> picked_segments(const model::Glyph& g, const geom::Points& pts) {
    std::vector<std::pair<int, int>> out;
    for (int ci = 0; ci < static_cast<int>(g.contours.size()); ++ci) {
        const auto& c = g.contours[static_cast<std::size_t>(ci)];
        const int n = static_cast<int>(c.nodes.size());
        for (int s = 0; s < geom::segment_count(c); ++s)
            if (pts.count({ci, s, geom::PointRef::Part::Node}) && pts.count({ci, (s + 1) % n, geom::PointRef::Part::Node}))
                out.push_back({ci, s});
    }
    return out;
}

bool set_segments(FontJournal& j, Font& f, const std::string& glyph, const geom::Points& pts, bool curve,
                  std::string* why) {
    const Glyph* was = f.glyph(glyph);
    if (!was) return false;
    const auto segs = picked_segments(*was, pts);
    if (segs.empty()) {
        say(why, "Pick the nodes at both ends of a segment (or a run of nodes) to make it a " +
                 std::string(curve ? "curve." : "line."));
        return false;
    }
    std::vector<std::pair<int, int>> todo;
    for (const auto& [ci, s] : segs)
        if (geom::is_line(was->contours[static_cast<std::size_t>(ci)], s) == curve) todo.push_back({ci, s});
    if (todo.empty()) {
        say(why, std::string("Already ") + (curve ? "curves." : "lines."));
        return false;
    }
    const std::string what = curve ? "curve" : "line";
    return j.run(todo.size() == 1 ? "Make " + what : "Make " + std::to_string(todo.size()) + " " + what + "s", {glyph}, [&] {
        Glyph g = glyph_copy(f, glyph);
        for (const auto& [ci, s] : todo) {
            auto& c = g.contours[static_cast<std::size_t>(ci)];
            if (curve) geom::segment_to_curve(c, s);
            else geom::segment_to_line(c, s);
        }
        g.snap();
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

// ── guides + alignment (M5c) ───────────────────────────────────────────────


bool add_guide(FontJournal& j, Font& f, const std::string& owner, const model::Guide& g) {
    if (!owner.empty() && !f.glyph(owner)) return false;
    return j.run(owner.empty() ? "Add font guide" : "Add guide", {owner_id(owner)}, [&] {
        auto gs = guides_of(f, owner);
        gs.push_back(rounded(g));
        put_guides(f, owner, std::move(gs));
    });
}

bool remove_guide(FontJournal& j, Font& f, const std::string& owner, int index) {
    const auto gs0 = guides_of(f, owner);
    if (index < 0 || index >= static_cast<int>(gs0.size())) return false;
    return j.run(owner.empty() ? "Delete font guide" : "Delete guide", {owner_id(owner)}, [&] {
        auto gs = guides_of(f, owner);
        gs.erase(gs.begin() + index);
        put_guides(f, owner, std::move(gs));
    });
}

bool set_guide(FontJournal& j, Font& f, const std::string& owner, int index, const model::Guide& g) {
    const auto gs0 = guides_of(f, owner);
    if (index < 0 || index >= static_cast<int>(gs0.size())) return false;
    const bool dragging = j.in_gesture() && guide_drag().live;
    const model::Guide want = dragging ? g : rounded(g);
    if (gs0[index] == want) return false;
    return j.run("Guide", {owner_id(owner)}, [&] {
        auto gs = guides_of(f, owner);
        gs[index] = want;
        put_guides(f, owner, std::move(gs));
    }, "guide:" + owner + ":" + std::to_string(index));
}

bool move_guide_owner(FontJournal& j, Font& f, const std::string& glyph, bool to_font, int index, int* moved_to) {
    if (!f.glyph(glyph)) return false;
    const std::string from = to_font ? glyph : std::string{}, to = to_font ? std::string{} : glyph;
    const auto src = guides_of(f, from);
    if (index < 0 || index >= static_cast<int>(src.size())) return false;
    const int dest_index = static_cast<int>(guides_of(f, to).size());
    const bool ok = j.run(to_font ? "Guide to the whole font" : "Guide to this glyph only",
                          {owner_id(from), owner_id(to)}, [&] {
        auto a = guides_of(f, from);
        const model::Guide g = a[index];
        a.erase(a.begin() + index);
        put_guides(f, from, std::move(a));
        auto b = guides_of(f, to);
        b.push_back(g);
        put_guides(f, to, std::move(b));
    });
    if (ok && moved_to) *moved_to = dest_index;
    return ok;
}

void guide_drag_begin(FontJournal& j, Font& f, const std::string& owner, int index) {
    const auto gs = guides_of(f, owner);
    if (index < 0 || index >= static_cast<int>(gs.size())) return;
    guide_drag() = {owner, index, true};
    drag().names.clear();   // drag_end then snaps no glyph outline -- only the guide below
    drag().at_press.clear();
    j.begin("Move guide", {owner_id(owner)});
}

bool align_points(FontJournal& j, Font& f, const std::string& glyph, const geom::Points& pts, Align how) {
    const Glyph* was = f.glyph(glyph);
    if (!was) return false;
    using P = geom::PointRef::Part;
    std::vector<geom::PointRef> refs;
    for (const auto& p : pts)
        if (geom::valid(*was, p) && (p.part == P::Node || p.part == P::Anchor)) refs.push_back(p);
    if (refs.size() < 2) return false;
    double x0 = 1e18, x1 = -1e18, y0 = 1e18, y1 = -1e18;
    for (const auto& r : refs) {
        const auto v = geom::position(*was, r);
        x0 = std::min(x0, v.x); x1 = std::max(x1, v.x);
        y0 = std::min(y0, v.y); y1 = std::max(y1, v.y);
    }
    // Each point's target.
    std::vector<geom::Vec2> to(refs.size());
    if (how == Align::DistributeH || how == Align::DistributeV) {
        const bool h = how == Align::DistributeH;
        std::vector<std::size_t> order(refs.size());
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            const auto va = geom::position(*was, refs[a]), vb = geom::position(*was, refs[b]);
            return h ? va.x < vb.x : va.y < vb.y;
        });
        const double lo = h ? x0 : y0, hi = h ? x1 : y1;
        for (std::size_t k = 0; k < order.size(); ++k) {
            auto v = geom::position(*was, refs[order[k]]);
            const double at = lo + (hi - lo) * static_cast<double>(k) / static_cast<double>(order.size() - 1);
            (h ? v.x : v.y) = at;
            to[order[k]] = v;
        }
    } else {
        for (std::size_t i = 0; i < refs.size(); ++i) {
            auto v = geom::position(*was, refs[i]);
            switch (how) {
                case Align::Left:    v.x = x0; break;
                case Align::HCenter: v.x = (x0 + x1) / 2; break;
                case Align::Right:   v.x = x1; break;
                case Align::Top:     v.y = y1; break;
                case Align::VMiddle: v.y = (y0 + y1) / 2; break;
                case Align::Bottom:  v.y = y0; break;
                default: break;
            }
            to[i] = v;
        }
    }
    Glyph g = *was;
    for (std::size_t i = 0; i < refs.size(); ++i) {
        const Glyph before = g;
        geom::move_points(g, before, {refs[i]}, to[i] - geom::position(before, refs[i]));
    }
    g.snap();
    if (g == *was) return false;
    static const char* names[] = {"Align left", "Align centre", "Align right", "Align top", "Align middle",
                                  "Align bottom", "Distribute across", "Distribute up"};
    return j.run(names[static_cast<int>(how)], {glyph}, [&] { f.put(glyph, Record{g}); });
}

// ── direction (M5d) ────────────────────────────────────────────────────────

bool reverse_contours(FontJournal& j, Font& f, const std::string& glyph, const std::vector<int>& contours) {
    const Glyph* was = f.glyph(glyph);
    if (!was) return false;
    std::set<int> idx;
    for (int i : contours)
        if (i >= 0 && i < static_cast<int>(was->contours.size()) && was->contours[i].nodes.size() >= 2) idx.insert(i);
    if (idx.empty()) return false;
    return j.run(idx.size() == 1 ? "Reverse contour" : "Reverse " + std::to_string(idx.size()) + " contours",
                 {glyph}, [&] {
        Glyph g = glyph_copy(f, glyph);
        for (int i : idx) geom::reverse(g.contours[i]);
        f.put(glyph, Record{g});
    });
}

bool correct_directions(FontJournal& j, Font& f, const std::vector<std::string>& glyphs, std::size_t* fixed) {
    std::vector<std::string> ids;
    std::size_t n = 0;
    for (const auto& name : glyphs)
        if (const Glyph* g = f.glyph(name)) {
            const auto w = geom::wrong_way(g->contours);
            if (!w.empty()) { ids.push_back(name); n += w.size(); }
        }
    if (fixed) *fixed = n;
    if (ids.empty()) return false;
    const std::string label = "Correct directions (" + std::to_string(n) + (n == 1 ? " contour" : " contours") +
                              (ids.size() == 1 ? ")" : " in " + std::to_string(ids.size()) + " glyphs)");
    return j.run(label, ids, [&] {
        for (const auto& name : ids) {
            Glyph g = glyph_copy(f, name);
            for (int i : geom::wrong_way(g.contours)) geom::reverse(g.contours[i]);
            f.put(name, Record{g});
        }
    });
}

bool remove_overlaps(FontJournal& j, Font& f, const std::vector<std::string>& glyphs, std::size_t* changed,
                     std::size_t* fell_back, std::size_t* wrong_way) {
    std::map<std::string, std::vector<model::Contour>> out;
    std::size_t fb = 0, ww = 0;
    for (const auto& name : glyphs)
        if (const Glyph* g = f.glyph(name); g && !g->contours.empty()) {
            if (!geom::wrong_way(g->contours).empty()) { ++ww; continue; }
            auto r = geom::remove_overlaps(g->contours);
            if (r.fallback) ++fb;
            if (!r.changed) continue;
            Glyph t;
            t.contours = std::move(r.contours);
            t.snap();
            if (t.contours != g->contours) out[name] = std::move(t.contours);
        }
    if (changed) *changed = out.size();
    if (fell_back) *fell_back = fb;
    if (wrong_way) *wrong_way = ww;
    if (out.empty()) return false;
    std::vector<std::string> ids;
    for (const auto& [n, c] : out) ids.push_back(n);
    const std::string label = ids.size() == 1 ? "Remove overlaps"
                                              : "Remove overlaps (" + std::to_string(ids.size()) + " glyphs)";
    return j.run(label, ids, [&] {
        for (auto& [name, cs] : out) {
            Glyph g = glyph_copy(f, name);
            g.contours = std::move(cs);
            f.put(name, Record{g});
        }
    });
}

namespace {
struct Bx { double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18; };
void grow(Bx& b, const model::Contour& c) {   // the control hull: every node and handle
    for (const auto& n : c.nodes)
        for (const auto [x, y] : {std::pair{n.x, n.y}, {n.cx1, n.cy1}, {n.cx2, n.cy2}}) {
            b.x0 = std::min(b.x0, x); b.y0 = std::min(b.y0, y);
            b.x1 = std::max(b.x1, x); b.y1 = std::max(b.y1, y);
        }
}
bool meets(const Bx& a, const Bx& b) { return a.x0 <= b.x1 && b.x0 <= a.x1 && a.y0 <= b.y1 && b.y0 <= a.y1; }
}  // namespace

// ── M15b: extremes, corners, ink traps ─────────────────────────────────────

namespace {

// Commit `g`'s contours in `touched` snapped to units (the rest as they are).
void put_snapped(Font& f, const std::string& glyph, Glyph g, const std::set<int>& touched) {
    for (int i : touched) {
        Glyph t;
        t.contours = {g.contours[static_cast<std::size_t>(i)]};
        t.snap();
        g.contours[static_cast<std::size_t>(i)] = std::move(t.contours[0]);
    }
    f.put(glyph, Record{g});
}

std::map<int, std::set<int>> picked_nodes(const Glyph& g, const geom::Points& pts) {
    std::map<int, std::set<int>> by;
    for (const auto& p : pts)
        if (p.part == geom::PointRef::Part::Node && p.contour >= 0 && p.contour < static_cast<int>(g.contours.size()) &&
            p.index >= 0 && p.index < static_cast<int>(g.contours[static_cast<std::size_t>(p.contour)].nodes.size()))
            by[p.contour].insert(p.index);
    return by;
}

std::string plural(int n, const char* one, const char* many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

}  // namespace

bool add_extremes(FontJournal& j, Font& f, const std::string& glyph, const std::vector<int>& contours, int* added,
                  std::string* why) {
    auto refuse = [&](const std::string& r) { if (why) *why = r; return false; };
    const Glyph* was = f.glyph(glyph);
    if (!was) return refuse("No glyph.");
    std::set<int> want;
    for (int i : contours) if (i >= 0 && i < static_cast<int>(was->contours.size())) want.insert(i);
    if (contours.empty()) for (int i = 0; i < static_cast<int>(was->contours.size()); ++i) want.insert(i);
    Glyph g = *was;
    int n = 0;
    std::set<int> touched;
    for (int i : want)
        if (const int k = geom::add_extremes(g.contours[static_cast<std::size_t>(i)]); k > 0) { n += k; touched.insert(i); }
    if (added) *added = n;
    if (n == 0) return refuse("No extreme points missing.");
    return j.run("Add extreme points (" + std::to_string(n) + ")", {glyph}, [&] { put_snapped(f, glyph, g, touched); });
}

bool round_corners(FontJournal& j, Font& f, const std::string& glyph, const geom::Points& pts, double radius,
                   int* done, std::string* why) {
    auto refuse = [&](const std::string& r) { if (why) *why = r; return false; };
    const Glyph* was = f.glyph(glyph);
    if (!was) return refuse("No glyph.");
    if (!(radius >= 1)) return refuse("Round corners: the radius must be 1 unit or more.");
    Glyph g = *was;
    int n = 0;
    std::set<int> touched;
    for (const auto& [ci, nodes] : picked_nodes(*was, pts))
        if (const int k = geom::round_corners(g.contours[static_cast<std::size_t>(ci)], nodes, radius); k > 0) {
            n += k;
            touched.insert(ci);
        }
    if (done) *done = n;
    if (n == 0) return refuse("Round corners: pick one or more corner nodes (smooth nodes have no corner).");
    return j.run("Round " + plural(n, "corner", "corners"), {glyph}, [&] { put_snapped(f, glyph, g, touched); });
}

bool ink_traps(FontJournal& j, Font& f, const std::string& glyph, const geom::Points& pts, double width, double depth,
               int* done, std::string* why) {
    auto refuse = [&](const std::string& r) { if (why) *why = r; return false; };
    const Glyph* was = f.glyph(glyph);
    if (!was) return refuse("No glyph.");
    if (!(width >= 1) || !(depth >= 1)) return refuse("Ink trap: width and depth must be 1 unit or more.");
    // The ink is on the left of travel on a contour running the right way, on
    // the right of one running the wrong way.
    const auto wrong = geom::wrong_way(was->contours);
    Glyph g = *was;
    int n = 0;
    std::set<int> touched;
    for (const auto& [ci, nodes] : picked_nodes(*was, pts)) {
        const bool ink_left = std::find(wrong.begin(), wrong.end(), ci) == wrong.end();
        if (const int k = geom::ink_traps(g.contours[static_cast<std::size_t>(ci)], nodes, width, depth, ink_left); k > 0) {
            n += k;
            touched.insert(ci);
        }
    }
    if (done) *done = n;
    if (n == 0) return refuse("Ink trap: pick a crotch -- a corner where the ink wraps round the white (n's arch into its stem).");
    return j.run(plural(n, "Ink trap", "Ink traps"), {glyph}, [&] { put_snapped(f, glyph, g, touched); });
}

namespace {

// Replace `targets` with `made` (at the first target's place), snapped.
bool replace_contours(FontJournal& j, Font& f, const std::string& glyph, const std::set<int>& targets,
                      std::vector<model::Contour> made, const std::string& label, std::vector<int>* result) {
    const Glyph* was = f.glyph(glyph);
    Glyph t;
    t.contours = std::move(made);
    t.snap();
    Glyph g = *was;
    std::vector<model::Contour> out;
    std::vector<int> at;
    const int first = *targets.begin();
    for (int i = 0; i < static_cast<int>(g.contours.size()); ++i) {
        if (i == first)
            for (auto& c : t.contours) { at.push_back(static_cast<int>(out.size())); out.push_back(c); }
        if (!targets.count(i)) out.push_back(g.contours[static_cast<std::size_t>(i)]);
    }
    g.contours = std::move(out);
    if (result) *result = at;
    return j.run(label, {glyph}, [&] { f.put(glyph, Record{g}); });
}

std::string signed_units(double d) {
    const long v = std::lround(d);
    return (v > 0 ? "+" : "") + std::to_string(v);
}

}  // namespace

bool offset_contours(FontJournal& j, Font& f, const std::string& glyph, const std::vector<int>& contours, double d,
                     geom::Join join, std::vector<int>* result, std::string* why) {
    auto refuse = [&](const std::string& r) { if (why) *why = r; return false; };
    const Glyph* was = f.glyph(glyph);
    if (!was) return refuse("No glyph.");
    if (std::abs(d) < 0.5) return refuse("Offset: the distance must be at least 1 unit, either way.");
    std::set<int> tg;
    const int n = static_cast<int>(was->contours.size());
    for (int i : contours) if (i >= 0 && i < n) tg.insert(i);
    if (contours.empty()) for (int i = 0; i < n; ++i) tg.insert(i);
    for (auto it = tg.begin(); it != tg.end();) {
        const auto& c = was->contours[static_cast<std::size_t>(*it)];
        if (!c.closed || c.nodes.size() < 2) it = tg.erase(it); else ++it;
    }
    if (tg.empty()) return refuse("Offset: no closed path to offset (an open one wants Expand stroke).");
    const auto wrong = geom::wrong_way(was->contours);
    for (int i : wrong) if (tg.count(i)) return refuse("Offset: a path runs the wrong way -- correct directions first.");
    std::vector<model::Contour> in;
    for (int i : tg) in.push_back(was->contours[static_cast<std::size_t>(i)]);
    auto r = geom::offset_contours(in, d, join);
    if (r.fallback) return refuse("Offset: the outline couldn't be rebuilt cleanly (" + r.why + "); left as drawn.");
    if (r.contours.empty()) return refuse("Offset " + signed_units(d) + " would leave nothing -- the shape is thinner than that.");
    return replace_contours(j, f, glyph, tg, std::move(r.contours), "Offset " + signed_units(d), result);
}

bool expand_stroke(FontJournal& j, Font& f, const std::string& glyph, const std::vector<int>& contours, double width,
                   geom::Join join, geom::Cap cap, std::vector<int>* result, std::string* why) {
    auto refuse = [&](const std::string& r) { if (why) *why = r; return false; };
    const Glyph* was = f.glyph(glyph);
    if (!was) return refuse("No glyph.");
    if (width < 1) return refuse("Expand stroke: the width must be 1 unit or more.");
    std::set<int> tg;
    const int n = static_cast<int>(was->contours.size());
    for (int i : contours) if (i >= 0 && i < n && was->contours[static_cast<std::size_t>(i)].nodes.size() >= 2) tg.insert(i);
    if (contours.empty())
        for (int i = 0; i < n; ++i) if (was->contours[static_cast<std::size_t>(i)].nodes.size() >= 2) tg.insert(i);
    if (tg.empty()) return refuse("Expand stroke: pick a path (two nodes or more).");
    std::vector<model::Contour> in;
    for (int i : tg) in.push_back(was->contours[static_cast<std::size_t>(i)]);
    auto r = geom::expand_stroke(in, width, join, cap);
    if (r.fallback) return refuse("Expand stroke: the outline couldn't be rebuilt cleanly (" + r.why + ").");
    if (r.contours.empty()) return refuse("Expand stroke: nothing came of it.");
    return replace_contours(j, f, glyph, tg, std::move(r.contours),
                            "Expand stroke " + std::to_string(std::lround(width)), result);
}

bool boolean_contours(FontJournal& j, Font& f, const std::string& glyph, const std::vector<int>& picked,
                      geom::BoolOp op, std::vector<int>* result, std::string* why) {
    auto refuse = [&](const std::string& r) { if (why) *why = r; return false; };
    const Glyph* was = f.glyph(glyph);
    if (!was) return refuse("No glyph.");
    const int n = static_cast<int>(was->contours.size());
    std::set<int> pk;
    for (int i : picked)
        if (i >= 0 && i < n && was->contours[static_cast<std::size_t>(i)].closed &&
            was->contours[static_cast<std::size_t>(i)].nodes.size() >= 2)
            pk.insert(i);
    const char* name = op == geom::BoolOp::Union ? "Union" : op == geom::BoolOp::Subtract ? "Subtract" : "Intersect";
    std::vector<int> subj, clip;   // contour indices
    if (op == geom::BoolOp::Union) {
        if (pk.size() < 2) return refuse("Union: pick two or more contours (a node or a segment of each).");
        subj.assign(pk.begin(), pk.end());
    } else {
        // s09 (Scott: "the top path determines the boolean action"): the TOP
        // picked path -- last in contour order, see arrange_contours -- works
        // on the other picked paths. Unpicked paths take no part.
        if (pk.size() < 2)
            return refuse(std::string(name) + ": pick two or more paths -- the top one " +
                          (op == geom::BoolOp::Subtract ? "cuts the others." : "keeps what the others share with it."));
        const int top = *pk.rbegin();
        Bx cut;
        grow(cut, was->contours[static_cast<std::size_t>(top)]);
        bool any = false;
        for (int i : pk) {
            if (i == top) continue;
            subj.push_back(i);
            Bx b;
            grow(b, was->contours[static_cast<std::size_t>(i)]);
            any = any || meets(b, cut);
        }
        if (!any) return refuse(std::string(name) + ": the top path doesn't meet the other picked paths.");
        clip = {top};
    }
    auto gather = [&](const std::vector<int>& ix) {
        std::vector<model::Contour> cs;
        for (int i : ix) cs.push_back(was->contours[static_cast<std::size_t>(i)]);
        return cs;
    };
    const auto s_cs = gather(subj), c_cs = gather(clip);
    if (!geom::wrong_way(s_cs).empty() || !geom::wrong_way(c_cs).empty())
        return refuse(std::string(name) + ": a contour runs the wrong way -- correct directions first (it would fill a counter).");
    auto r = geom::boolean_op(s_cs, c_cs, op);
    if (r.fallback) return refuse(std::string(name) + ": the outline could not be rebuilt cleanly (" + r.why + "); nothing changed.");
    Glyph tmp;
    tmp.contours = std::move(r.contours);
    tmp.snap();
    std::erase_if(tmp.contours, [](const model::Contour& c) { return c.closed && std::abs(geom::signed_area(c)) < 0.5; });
    if (tmp.contours.empty()) return refuse(std::string(name) + ": nothing would be left -- nothing changed.");
    // Untouched contours keep their order; the result takes the first involved one's place.
    std::set<int> involved(subj.begin(), subj.end());
    involved.insert(clip.begin(), clip.end());
    const int first = *involved.begin();
    std::vector<model::Contour> out;
    std::vector<int> at;
    for (int i = 0; i < n; ++i) {
        if (i == first)
            for (auto& c : tmp.contours) { at.push_back(static_cast<int>(out.size())); out.push_back(c); }
        if (!involved.count(i)) out.push_back(was->contours[static_cast<std::size_t>(i)]);
    }
    if (out == was->contours) return refuse(std::string(name) + ": nothing to change.");
    const bool ok = j.run(name, {glyph}, [&] {
        Glyph g = glyph_copy(f, glyph);
        g.contours = out;
        f.put(glyph, Record{g});
    });
    if (ok && result) *result = at;
    return ok;
}

// ── arrange (s09): the paths' order, bottom (0) to top (last) ───────────────

bool arrange_contours(FontJournal& j, Font& f, const std::string& glyph, const std::vector<int>& picked,
                      Arrange how, std::vector<int>* result, std::string* why) {
    auto refuse = [&](const std::string& r) { if (why) *why = r; return false; };
    const Glyph* was = f.glyph(glyph);
    if (!was) return refuse("No glyph.");
    const int n = static_cast<int>(was->contours.size());
    std::vector<char> pk(static_cast<std::size_t>(n), 0);
    int count = 0;
    for (int i : picked)
        if (i >= 0 && i < n && !pk[static_cast<std::size_t>(i)]) { pk[static_cast<std::size_t>(i)] = 1; ++count; }
    if (count == 0) return refuse("Arrange: pick a path first.");
    // order[k] = the old index of the path that ends up at k.
    std::vector<int> order(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) order[static_cast<std::size_t>(i)] = i;
    const auto picked_at = [&](int k) { return pk[static_cast<std::size_t>(order[static_cast<std::size_t>(k)])] != 0; };
    switch (how) {
    case Arrange::Front:
    case Arrange::Back: {
        std::stable_partition(order.begin(), order.end(), [&](int i) {
            return (pk[static_cast<std::size_t>(i)] != 0) == (how == Arrange::Back);
        });
        break;
    }
    case Arrange::Forward:   // each picked path steps over the unpicked one above it
        for (int k = n - 2; k >= 0; --k)
            if (picked_at(k) && !picked_at(k + 1)) std::swap(order[static_cast<std::size_t>(k)], order[static_cast<std::size_t>(k + 1)]);
        break;
    case Arrange::Backward:
        for (int k = 1; k < n; ++k)
            if (picked_at(k) && !picked_at(k - 1)) std::swap(order[static_cast<std::size_t>(k)], order[static_cast<std::size_t>(k - 1)]);
        break;
    }
    const char* label = how == Arrange::Front ? "Bring to front" : how == Arrange::Back ? "Send to back"
                      : how == Arrange::Forward ? "Bring forward" : "Send backward";
    bool same = true;
    for (int k = 0; k < n; ++k) same = same && order[static_cast<std::size_t>(k)] == k;
    if (same)
        return refuse(how == Arrange::Front || how == Arrange::Forward ? "Already on top." : "Already at the bottom.");
    std::vector<int> at;
    std::vector<model::Contour> out;
    for (int k = 0; k < n; ++k) {
        out.push_back(was->contours[static_cast<std::size_t>(order[static_cast<std::size_t>(k)])]);
        if (picked_at(k)) at.push_back(k);
    }
    const bool ok = j.run(label, {glyph}, [&] {
        Glyph g = glyph_copy(f, glyph);
        g.contours = out;
        f.put(glyph, Record{g});
    });
    if (ok && result) *result = at;
    return ok;
}

// ── parts (M14c) ─────────────────────────────────────────────────────────────

std::vector<std::string> parts(const Font& f) {
    std::vector<std::string> out;
    for (const auto& n : f.glyph_order())
        if (f.glyph(n)->skip_export) out.push_back(n);
    return out;
}

std::string free_part_name(const Font& f, const std::string& stem) {
    std::string base;
    for (char c : stem) {
        const bool ok = std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-';
        base += ok ? c : '_';
    }
    if (base.empty()) base = "part";
    if (base.rfind("_", 0) != 0) base = "_part." + base;
    if (!f.glyph(base)) return base;
    for (int k = 1;; ++k)
        if (const std::string n = base + "." + std::to_string(k); !f.glyph(n)) return n;
}

bool make_part(FontJournal& j, Font& f, const std::string& glyph, const std::vector<int>& contours,
               const std::string& name, std::string* why) {
    const Glyph* src = f.glyph(glyph);
    if (!src) return false;
    if (!model::valid_glyph_name(name)) {
        say(why, q(name) + " is not a usable glyph name: A-Z a-z 0-9 . _ - , starting with a letter or underscore.");
        return false;
    }
    if (f.glyph(name)) { say(why, "There is already a glyph named " + q(name) + "."); return false; }
    std::vector<model::Contour> cs;
    std::set<int> seen;
    for (int i : contours)
        if (i >= 0 && i < static_cast<int>(src->contours.size()) && seen.insert(i).second &&
            src->contours[static_cast<std::size_t>(i)].closed)
            cs.push_back(src->contours[static_cast<std::size_t>(i)]);
    if (cs.empty()) { say(why, "Make part: pick the contour(s) to keep as a part (a node or segment of each)."); return false; }
    return j.run("Make part " + q(name), {name}, [&] {
        Glyph g;
        g.name = name;
        g.advance = src->advance;
        g.contours = std::move(cs);
        g.status = model::GlyphStatus::Drafted;
        g.skip_export = true;
        g.order = f.order_after("");
        f.put(name, Record{g});
    });
}

bool set_part(FontJournal& j, Font& f, const std::vector<std::string>& glyphs, bool part) {
    std::vector<std::string> ids;
    for (const auto& n : glyphs)
        if (const Glyph* g = f.glyph(n); g && g->skip_export != part) ids.push_back(n);
    if (ids.empty()) return false;
    const std::string label = (part ? "Make " : "Unmake ") +
                              (ids.size() == 1 ? "part " + q(ids[0]) : std::to_string(ids.size()) + " parts");
    return j.run(label, ids, [&] {
        for (const auto& n : ids) {
            Glyph g = glyph_copy(f, n);
            g.skip_export = part;
            f.put(n, Record{g});
        }
    });
}

bool place_part(FontJournal& j, Font& f, const std::string& glyph, const std::string& part, bool as_component,
                double dx, double dy, std::vector<int>* contours, int* component, std::string* why) {
    const Glyph* g0 = f.glyph(glyph);
    const Glyph* p = f.glyph(part);
    if (!g0) return false;
    if (!p) { say(why, "There is no glyph named " + q(part) + "."); return false; }
    if (!p->skip_export) { say(why, q(part) + " is not a part."); return false; }
    if (part == glyph) { say(why, "A part can't be placed into itself."); return false; }
    if (reaches(f, part, glyph)) { say(why, q(part) + " already uses " + q(glyph) + " -- that would be a cycle."); return false; }
    // What it draws (its contours, its components resolved), moved by (dx, dy).
    std::vector<model::Contour> cs = p->contours;
    for (const auto& k : p->components) {
        auto more = geom::component_contours(f, k);
        cs.insert(cs.end(), more.begin(), more.end());
    }
    if (cs.empty()) { say(why, q(part) + " draws nothing yet."); return false; }
    dx = std::round(dx);
    dy = std::round(dy);
    std::vector<int> at;
    int kat = -1;
    const bool ok = j.run("Place part " + q(part), {glyph}, [&] {
        Glyph g = glyph_copy(f, glyph);
        if (as_component) {
            model::Component k{part};
            k.dx = dx;
            k.dy = dy;
            g.components.push_back(k);
            kat = static_cast<int>(g.components.size()) - 1;
        } else {
            for (auto c : cs) {
                for (auto& n : c.nodes) {
                    n.x += dx; n.y += dy; n.cx1 += dx; n.cy1 += dy; n.cx2 += dx; n.cy2 += dy;
                }
                at.push_back(static_cast<int>(g.contours.size()));
                g.contours.push_back(std::move(c));
            }
            g.snap();
        }
        if (g.status == model::GlyphStatus::Empty) g.status = model::GlyphStatus::Drafted;
        f.put(glyph, Record{g});
    });
    if (ok && contours) *contours = at;
    if (ok && component) *component = kat;
    return ok;
}

bool add_shape(FontJournal& j, Font& f, const std::string& glyph, const model::Contour& c, const std::string& label,
               int* index, std::string* why) {
    auto refuse = [&](const char* r) { if (why) *why = r; return false; };
    if (!f.glyph(glyph)) return refuse("No glyph.");
    if (c.nodes.size() < 3 || !c.closed) return refuse("Drag to draw a shape.");
    Glyph probe;
    probe.contours = {c};
    probe.snap();
    const double area = geom::signed_area(probe.contours[0]);
    if (std::abs(area) < 1.0) return refuse("Too small: drag a larger shape.");
    if (area < 0) geom::reverse(probe.contours[0]);
    int at = -1;
    const bool ok = j.run(label, {glyph}, [&] {
        Glyph g = glyph_copy(f, glyph);
        g.contours.push_back(probe.contours[0]);
        at = static_cast<int>(g.contours.size()) - 1;
        if (g.status == model::GlyphStatus::Empty) g.status = model::GlyphStatus::Drafted;
        f.put(glyph, Record{g});
    });
    if (index) *index = at;
    return ok;
}

// s09: whole paths aligned / distributed as rigid shapes, by their exact
// bounds. Two or more: to the paths' joint bounds. One: to the glyph's box --
// 0 .. width across, baseline .. x-height up (cap height on a capital).
bool align_contours(FontJournal& j, Font& f, const std::string& glyph, const std::vector<int>& contours, Align how,
                    std::string* why) {
    auto refuse = [&](const std::string& r) { if (why) *why = r; return false; };
    const Glyph* was = f.glyph(glyph);
    if (!was) return refuse("No glyph.");
    std::vector<int> cs;
    for (int i : contours)
        if (i >= 0 && i < static_cast<int>(was->contours.size()) &&
            std::find(cs.begin(), cs.end(), i) == cs.end())
            cs.push_back(i);
    std::sort(cs.begin(), cs.end());
    if (cs.empty()) return refuse("Align: pick a path first.");
    const bool dist = how == Align::DistributeH || how == Align::DistributeV;
    if (dist && cs.size() < 3) return refuse("Distribute: pick three or more paths.");
    std::vector<model::Rect> box;
    for (int i : cs) {
        Glyph one;
        one.contours = {was->contours[static_cast<std::size_t>(i)]};
        const auto b = model::bounds(f, one);
        if (!b) return refuse("Align: a path draws nothing.");
        box.push_back(*b);
    }
    model::Rect to;   // what they align to
    if (cs.size() == 1) {
        const auto& m = f.metrics();
        bool cap = false;
        for (char32_t u : was->unicodes) cap = cap || unicode::category(u) == "Lu";
        to = {0, 0, was->advance, double(cap ? m.cap_height : m.x_height)};
    } else {
        to = box[0];
        for (const auto& b : box) {
            to.x0 = std::min(to.x0, b.x0); to.x1 = std::max(to.x1, b.x1);
            to.y0 = std::min(to.y0, b.y0); to.y1 = std::max(to.y1, b.y1);
        }
    }
    std::vector<geom::Vec2> d(cs.size());
    if (dist) {
        // Equal GAPS between neighbours (the outermost two stay put).
        const bool h = how == Align::DistributeH;
        std::vector<std::size_t> order(cs.size());
        for (std::size_t k = 0; k < order.size(); ++k) order[k] = k;
        std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            return h ? box[a].x0 + box[a].x1 < box[b].x0 + box[b].x1 : box[a].y0 + box[a].y1 < box[b].y0 + box[b].y1;
        });
        double total = 0;
        for (const auto& b : box) total += h ? b.x1 - b.x0 : b.y1 - b.y0;
        const double span = h ? to.x1 - to.x0 : to.y1 - to.y0;
        const double gap = (span - total) / static_cast<double>(cs.size() - 1);
        double at = h ? to.x0 : to.y0;
        for (std::size_t k : order) {
            const double lo = h ? box[k].x0 : box[k].y0;
            (h ? d[k].x : d[k].y) = at - lo;
            at += (h ? box[k].x1 - box[k].x0 : box[k].y1 - box[k].y0) + gap;
        }
    } else {
        for (std::size_t k = 0; k < cs.size(); ++k) {
            const auto& b = box[k];
            switch (how) {
                case Align::Left:    d[k].x = to.x0 - b.x0; break;
                case Align::HCenter: d[k].x = (to.x0 + to.x1) / 2 - (b.x0 + b.x1) / 2; break;
                case Align::Right:   d[k].x = to.x1 - b.x1; break;
                case Align::Top:     d[k].y = to.y1 - b.y1; break;
                case Align::VMiddle: d[k].y = (to.y0 + to.y1) / 2 - (b.y0 + b.y1) / 2; break;
                case Align::Bottom:  d[k].y = to.y0 - b.y0; break;
                default: break;
            }
        }
    }
    bool moves = false;
    for (auto& v : d) {
        v = {std::round(v.x), std::round(v.y)};   // whole units: the shapes stay on the grid
        moves = moves || v.x != 0 || v.y != 0;
    }
    if (!moves) return refuse("Already lined up.");
    static const char* names[] = {"Align left", "Align centre", "Align right", "Align top", "Align middle",
                                  "Align bottom", "Distribute across", "Distribute up"};
    return j.run(names[static_cast<int>(how)], {glyph}, [&] {
        Glyph g = glyph_copy(f, glyph);
        for (std::size_t k = 0; k < cs.size(); ++k)
            for (auto& n : g.contours[static_cast<std::size_t>(cs[k])].nodes) {
                n.x += d[k].x; n.y += d[k].y;
                n.cx1 += d[k].x; n.cy1 += d[k].y;
                n.cx2 += d[k].x; n.cy2 += d[k].y;
            }
        f.put(glyph, Record{g});
    });
}

// s09: copies of the picked paths, on top (appended, in their order), moved
// by (dx, dy) -- (0, 0) = in place, the copies picked so a drag places them.
bool duplicate_contours(FontJournal& j, Font& f, const std::string& glyph, const std::vector<int>& contours,
                        double dx, double dy, std::vector<int>* result, std::string* why) {
    auto refuse = [&](const char* r) { if (why) *why = r; return false; };
    const Glyph* was = f.glyph(glyph);
    if (!was) return refuse("No glyph.");
    std::set<int> pk;
    for (int i : contours)
        if (i >= 0 && i < static_cast<int>(was->contours.size())) pk.insert(i);
    if (pk.empty()) return refuse("Duplicate: pick a path first.");
    std::vector<int> at;
    const bool clone = dx != 0 || dy != 0;   // s09: an offset copy is a Clone
    const std::string label = std::string(clone ? "Clone" : "Duplicate") + (pk.size() == 1 ? " path" : " paths");
    const bool ok = j.run(label, {glyph}, [&] {
        Glyph g = glyph_copy(f, glyph);
        for (int i : pk) {
            model::Contour c = g.contours[static_cast<std::size_t>(i)];
            for (auto& n : c.nodes) {
                n.x += dx; n.y += dy;
                n.cx1 += dx; n.cy1 += dy;
                n.cx2 += dx; n.cy2 += dy;
            }
            at.push_back(static_cast<int>(g.contours.size()));
            g.contours.push_back(std::move(c));
        }
        g.snap();
        f.put(glyph, Record{g});
    });
    if (ok && result) *result = at;
    return ok;
}

bool make_first(FontJournal& j, Font& f, const std::string& glyph, int contour, int index, std::string* why) {
    auto no = [&](const char* w) { if (why) *why = w; return false; };
    const Glyph* was = f.glyph(glyph);
    if (!was || contour < 0 || contour >= static_cast<int>(was->contours.size()) || index < 0 ||
        index >= static_cast<int>(was->contours[contour].nodes.size()))
        return no("No such node");
    if (!was->contours[contour].closed) return no("An open contour starts at its loose end");
    if (index == 0) return no("That node is the start already");
    return j.run("Make first", {glyph}, [&] {
        Glyph g = glyph_copy(f, glyph);
        geom::set_start(g.contours[contour], index);
        f.put(glyph, Record{g});
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

// ── spacing (M6) ───────────────────────────────────────────────────────────

bool kern_pair(FontJournal& j, Font& f, const std::string& left, const std::string& right, int value,
               bool exception) {
    if (!f.glyph(left) || !f.glyph(right)) return false;
    const auto key = model::kern_target(f.kerning(), left, right, exception);
    const auto it = f.kerning().pairs.find(key);
    if ((it == f.kerning().pairs.end() && value == 0) || (it != f.kerning().pairs.end() && it->second == value))
        return false;
    return j.run("Kern " + left + " " + right, {Font::kKerning}, [&] {
        model::Kerning k = f.kerning();
        if (value == 0) k.pairs.erase(key);
        else            k.pairs[key] = value;
        f.put(Font::kKerning, Record{k});
    }, "kern:" + key.first + "|" + key.second);
}

bool kern_to_gap(FontJournal& j, Font& f, const std::string& left, const std::string& right, int* proposed) {
    const auto r = model::read_pair(f, left, right);
    if (!r.ok || !r.proposed) return false;
    if (proposed) *proposed = *r.proposed;
    return kern_pair(j, f, left, right, *r.proposed);
}

void kern_drag_begin(FontJournal& j, Font& f, const std::string& left, const std::string& right) {
    drag().names.clear();
    drag().at_press.clear();
    if (f.glyph(left) && f.glyph(right)) j.begin("Kern " + left + " " + right, {Font::kKerning});
}

bool set_metrics_key(FontJournal& j, Font& f, const std::string& glyph, const model::MetricsKeys::Key& key) {
    if (!f.glyph(glyph)) return false;
    const auto& keys = f.metrics_keys().keys;
    const bool empty = key.left.empty() && key.right.empty() && key.width.empty();
    const auto it = keys.find(glyph);
    if (it == keys.end() ? empty : it->second == key) return false;
    return j.run(empty ? "Clear metrics key" : "Metrics key", {Font::kMetricsKeys}, [&] {
        model::MetricsKeys k = f.metrics_keys();
        if (empty) k.keys.erase(glyph);
        else       k.keys[glyph] = key;
        f.put(Font::kMetricsKeys, Record{k});
    }, "mkey:" + glyph);
}

bool apply_metrics_keys(FontJournal& j, Font& f, const std::vector<std::string>& glyphs, std::size_t* changed) {
    const std::set<std::string> want(glyphs.begin(), glyphs.end());
    std::vector<std::string> ids;
    for (const auto& n : model::key_order(f))
        if (want.empty() || want.count(n)) ids.push_back(n);
    // Count first (from the font as it is), so the label can say how many.
    std::size_t n = 0;
    for (const auto& name : ids) if (model::check_key(f, name).off()) ++n;
    if (changed) *changed = n;
    if (n == 0) return false;
    const std::string label = ids.size() == 1 ? "Apply metrics key"
                                              : "Apply metrics keys (" + std::to_string(n) + " glyphs)";
    return j.run(label, ids, [&] {
        for (const auto& name : ids)   // referenced glyphs first: a chain sees the new values
            if (auto g = model::apply_key(f, name)) f.put(name, Record{*g});
    });
}

// ── checks (M7) ────────────────────────────────────────────────────────────

bool fix_findings(FontJournal& j, Font& f, const std::vector<model::Finding>& findings, std::size_t* fixed) {
    std::map<std::string, std::vector<model::Finding>> geo;   // glyph -> geometric fixes
    std::set<std::string> keyed;                               // glyphs whose key to apply
    std::size_t n = 0;
    model::CheckId first = model::CheckId::Direction;
    for (const auto& x : findings) {
        if (!x.fixable || !f.glyph(x.glyph)) continue;
        if (n == 0) first = x.check;
        if (x.check == model::CheckId::Keys) keyed.insert(x.glyph);
        else geo[x.glyph].push_back(x);
        ++n;
    }
    if (fixed) *fixed = n;
    if (n == 0) return false;
    std::vector<std::string> ids;
    for (const auto& [g, _] : geo) ids.push_back(g);
    for (const auto& g : keyed) if (!geo.count(g)) ids.push_back(g);
    const std::string label = n == 1 ? std::string("Fix: ") + model::check_info(first).name
                                     : "Fix " + std::to_string(n) + " findings";
    return j.run(label, ids, [&] {
        for (const auto& [name, xs] : geo)
            if (auto g = model::fix_glyph(f, name, xs)) f.put(name, Record{*g});
        for (const auto& name : model::key_order(f))   // referenced glyphs first
            if (keyed.count(name))
                if (auto g = model::apply_key(f, name)) f.put(name, Record{*g});
    });
}

bool skip_findings(FontJournal& j, Font& f, const std::vector<model::Finding>& findings, bool skip) {
    model::CheckSkips sk = f.skips();
    std::size_t n = 0;
    for (const auto& x : findings) {
        const auto k = x.key();
        if (skip ? sk.keys.insert(k).second : sk.keys.erase(k) > 0) ++n;
    }
    if (n == 0) return false;
    const std::string what = n == 1 ? "1 finding" : std::to_string(n) + " findings";
    return j.run((skip ? "Skip " : "Unskip ") + what, {Font::kSkips}, [&] { f.put(Font::kSkips, Record{sk}); });
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
    Glyph gh = add("H", U'H', 700, {box(80, 0, 170, 700), box(530, 0, 620, 700), box(170, 320, 530, 400)});
    // O's counter runs the WRONG way (counter-clockwise, like its outer) --
    // something for correct_directions to fix (M5d).
    add("O", U'O', 760, {box(60, -12, 700, 712), box(160, 88, 600, 612)});
    Glyph gn = add("n", U'n', 560, {box(70, 0, 150, 500), box(410, 0, 490, 500)});
    Glyph go = add("o", U'o', 540, {box(50, -10, 490, 510)});
    // Guides (M5c): something for the guide verbs to change.
    gh.guides = {{0, 400, 0, {}}};
    gn.guides = {{0, 100, 0, {}}};
    go.guides = {{0, 250, 0, {}}, {0, 100, 0, {}}};
    for (const Glyph* g : {&gh, &gn, &go}) f.put(g->name, Record{*g});
    // M13: a background image on "o", for the image verbs.
    go.image = model::GlyphImage{f.add_image(sample_png(), "scan"), 100, 0, 0, 100, 20, -150, {}, 0.4, false};
    f.put(go.name, Record{go});
    // M14c: a part (left out of the compile), for the part verbs.
    {
        Glyph ps = add("_part.serif", 0, 560, {box(40, 0, 180, 20)});
        ps.unicodes.clear();
        ps.skip_export = true;
        f.put(ps.name, Record{ps});
    }
    // M15b: an ellipse turned 45 degrees -- its nodes off the extremes, so
    // every quarter lacks an x and a y extreme -- for add_extremes.
    {
        model::Contour e = geom::shape_ellipse({{100, 100}, {300, 400}});
        const auto m = geom::make_transform(1, 1, 45, 0, 0, {200, 250});
        for (auto& p : e.nodes) {
            const auto a = m.apply({p.x, p.y}), h1 = m.apply({p.cx1, p.cy1}), h2 = m.apply({p.cx2, p.cy2});
            p.x = std::round(a.x); p.y = std::round(a.y);
            p.cx1 = std::round(h1.x); p.cy1 = std::round(h1.y);
            p.cx2 = std::round(h2.x); p.cy2 = std::round(h2.y);
        }
        Glyph lg = add("lens", 0, 400, {e});
        lg.unicodes.clear();
        f.put(lg.name, Record{lg});
    }
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
        {"delete_nodes", [](FontJournal& j, Font& f) {
             delete_points(j, f, "H", {{2, 1, geom::PointRef::Part::Node}, {0, 0, geom::PointRef::Part::In}}); }},
        {"insert_node",  [](FontJournal& j, Font& f) { insert_node(j, f, "o", 0, 1, 0.37); }},
        {"pen_new_path", [](FontJournal& j, Font& f) {
             const auto at = pen_press(j, f, "n", -1, {250.4, 600.2});
             pen_drag(j, f, "n", at, {30.2, 10.1}, false);
             pen_drag(j, f, "n", at, {60.4, 20.3}, false);
             drag_end(j, f); }},
        {"add_shape", [](FontJournal& j, Font& f) {
             add_shape(j, f, "n", geom::shape_ellipse({{600.3, 100}, {800, 299.6}}), "Ellipse"); }},
        {"boolean_contours", [](FontJournal& j, Font& f) { boolean_contours(j, f, "H", {0, 2}, geom::BoolOp::Union); }},
        {"add_extremes", [](FontJournal& j, Font& f) { add_extremes(j, f, "lens", {}); }},
        {"offset_contours", [](FontJournal& j, Font& f) { offset_contours(j, f, "n", {}, 10, geom::Join::Miter); }},
        {"expand_stroke", [](FontJournal& j, Font& f) { expand_stroke(j, f, "H", {2}, 20, geom::Join::Round, geom::Cap::Round); }},
        {"round_corners", [](FontJournal& j, Font& f) {
             round_corners(j, f, "H", {{0, 0, geom::PointRef::Part::Node}, {0, 1, geom::PointRef::Part::Node}}, 20); }},
        {"ink_traps", [](FontJournal& j, Font& f) {   // O's square counter: four crotches
             ink_traps(j, f, "O", geom::whole_contours(*f.glyph("O"), {{1, 0, geom::PointRef::Part::Node}}), 15, 25); }},
        {"transform_points", [](FontJournal& j, Font& f) {
             transform_points(j, f, "H", {}, geom::make_transform(1.2, 1, 10, 5, 0, {300, 350}), "Transform"); }},
        {"transform_from", [](FontJournal& j, Font& f) {
             transform_from(j, f, "H", *f.glyph("H"), {}, geom::make_transform(1, 1, 3, 0, 0, {300, 350}), "Rotate", "t"); }},
        {"align_contours", [](FontJournal& j, Font& f) { align_contours(j, f, "H", {0, 2}, Align::Left); }},
        {"duplicate_contours", [](FontJournal& j, Font& f) { duplicate_contours(j, f, "H", {0}, 0, 0); }},
        {"arrange_contours", [](FontJournal& j, Font& f) { arrange_contours(j, f, "H", {0}, Arrange::Front); }},
        {"make_part", [](FontJournal& j, Font& f) { make_part(j, f, "n", {0}, "_part.stem"); }},
        {"set_part", [](FontJournal& j, Font& f) { set_part(j, f, {"o"}, true); }},
        {"place_part", [](FontJournal& j, Font& f) { place_part(j, f, "H", "_part.serif", false, 0, -10); }},
        {"set_segments", [](FontJournal& j, Font& f) {
             set_segments(j, f, "n", {{0, 0, geom::PointRef::Part::Node}, {0, 1, geom::PointRef::Part::Node}}, true); }},
        {"set_position", [](FontJournal& j, Font& f) {
             set_position(j, f, "o", {0, 0, geom::PointRef::Part::Node}, 61, -9); }},
        {"add_default_anchors", [](FontJournal& j, Font& f) { add_default_anchors(j, f, {"H", "n", "o"}); }},
        {"drag_component", [](FontJournal& j, Font& f) {
             drag_begin(j, f, {"Odotbelow"}, "Move component");
             drag_points(j, f, {{-1, 0, geom::PointRef::Part::Component}}, 13.7, -4.2);
             drag_end(j, f); }},
        {"add_guide",    [](FontJournal& j, Font& f) { add_guide(j, f, "H", {0, 320, 0, "bar"}); }},
        {"add_font_guide", [](FontJournal& j, Font& f) { add_guide(j, f, "", {0, 0, 90, "centre"}); }},
        {"set_guide",    [](FontJournal& j, Font& f) { set_guide(j, f, "o", 0, {0, 260.4, 0, "middle"}); }},
        {"remove_guide", [](FontJournal& j, Font& f) { remove_guide(j, f, "n", 0); }},
        {"move_guide_owner", [](FontJournal& j, Font& f) { move_guide_owner(j, f, "H", true, 0); }},
        {"guide_drag", [](FontJournal& j, Font& f) {
             guide_drag_begin(j, f, "o", 1);
             set_guide(j, f, "o", 1, {0, 117.6, 0, {}});
             set_guide(j, f, "o", 1, {0, 121.3, 0, {}});
             drag_end(j, f); }},
        {"align_points", [](FontJournal& j, Font& f) {
             align_points(j, f, "H", {{0, 2, geom::PointRef::Part::Node}, {1, 0, geom::PointRef::Part::Node}},
                          Align::Top); }},
        {"reverse_contours", [](FontJournal& j, Font& f) { reverse_contours(j, f, "H", {0, 2}); }},
        {"correct_directions", [](FontJournal& j, Font& f) { correct_directions(j, f, {"H", "n", "o", "O"}); }},
        {"remove_overlaps", [](FontJournal& j, Font& f) { remove_overlaps(j, f, {"H", "n", "o", "O"}); }},
        {"make_first",   [](FontJournal& j, Font& f) { make_first(j, f, "o", 0, 2); }},
        {"place_image",  [](FontJournal& j, Font& f) {
             place_image(j, f, "n", image_fitted(f, {"scan.png"}, 4, 2)); }},
        {"replace_image", [](FontJournal& j, Font& f) {
             place_image(j, f, "o", {f.add_image(sample_png() + std::string("x"), "other"), 50, 0, 0, 50, 0, 0, {}, 0.6, true}); }},
        {"remove_image", [](FontJournal& j, Font& f) { remove_image(j, f, "o"); }},
        {"set_image_frame", [](FontJournal& j, Font& f) {
             set_image_frame(j, f, "o", {"", 120, 0, 0, 120, 31, -160}); }},
        {"set_image_opacity", [](FontJournal& j, Font& f) { set_image_opacity(j, f, "o", 0.75); }},
        {"set_image_locked", [](FontJournal& j, Font& f) { set_image_locked(j, f, "o", true); }},
        {"set_image_color", [](FontJournal& j, Font& f) { set_image_color(j, f, "o", ufo_color(0.2, 0.5, 1.0)); }},
        {"image_drag",   [](FontJournal& j, Font& f) {
             image_drag_begin(j, f, "o", "Move image");
             set_image_frame(j, f, "o", {"", 100, 0, 0, 100, 27.4, -141.2});
             set_image_frame(j, f, "o", {"", 112.5, 0, 0, 112.5, 33.6, -139.7});
             drag_end(j, f); }},
        {"set_family",   [](FontJournal& j, Font& f) { set_family(j, f, "Garamond Nova"); }},
        {"set_style",    [](FontJournal& j, Font& f) { set_style(j, f, "Italic"); }},
        {"set_units_per_em", [](FontJournal& j, Font& f) { set_units_per_em(j, f, 2048); }},
        {"set_metric",   [](FontJournal& j, Font& f) { set_metric(j, f, Metrics::Field::XHeight, 480); }},
        {"set_kern",     [](FontJournal& j, Font& f) { set_kern(j, f, "H", "n", -20); }},
        {"kern_pair",    [](FontJournal& j, Font& f) { kern_pair(j, f, "O", "H", -25); }},
        {"kern_drag",    [](FontJournal& j, Font& f) {
             kern_drag_begin(j, f, "n", "o");
             kern_pair(j, f, "n", "o", -3);
             kern_pair(j, f, "n", "o", -8);
             drag_end(j, f); }},
        {"kern_to_gap",  [](FontJournal& j, Font& f) { kern_to_gap(j, f, "n", "o"); }},
        {"kern_pair_exception", [](FontJournal& j, Font& f) { kern_pair(j, f, "O", "H", -5, true); }},
        {"set_metrics_key", [](FontJournal& j, Font& f) { set_metrics_key(j, f, "n", {"60", "60", ""}); }},
        {"fix_findings", [](FontJournal& j, Font& f) {
             std::vector<model::Finding> all;
             for (const auto& n : f.glyph_order())
                 for (auto& x : model::check_glyph(f, n)) all.push_back(x);
             fix_findings(j, f, all); }},
        {"skip_findings", [](FontJournal& j, Font& f) {
             skip_findings(j, f, model::check_glyph(f, "O"), true); }},
        {"apply_metrics_keys", [](FontJournal& j, Font& f) {
             // The fixture carries keys (o = |n ...) -- see make_fixture.
             apply_metrics_keys(j, f, {}); }},
    };
    return table;
}

}  // namespace granjon::command
