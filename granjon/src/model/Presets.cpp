#include "model/Presets.hpp"

#include "geom/Edit.hpp"
#include "geom/Overlap.hpp"
#include "model/Composite.hpp"
#include "model/Coverage.hpp"
#include "model/Spacing.hpp"
#include "unicode/CharSet.hpp"
#include "unicode/Embedded.hpp"
#include "unicode/UcdTable.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>

namespace granjon::model {

const std::vector<std::string>& control_glyphs() {
    static const std::vector<std::string> v{"n", "o", "H", "O", "p", "v"};
    return v;
}

// ── the file ───────────────────────────────────────────────────────────────

namespace {

std::string trim(std::string_view s) {
    const auto a = s.find_first_not_of(" \t\r");
    if (a == std::string_view::npos) return {};
    const auto b = s.find_last_not_of(" \t\r");
    return std::string(s.substr(a, b - a + 1));
}

bool to_int(const std::string& s, int* out) {
    try {
        std::size_t used = 0;
        const int v = std::stoi(s, &used);
        if (used != s.size()) return false;
        *out = v;
        return true;
    } catch (...) { return false; }
}

}  // namespace

PresetFile parse_presets(std::string_view text, std::vector<std::string>* errors) {
    PresetFile out;
    Preset* cur = nullptr;
    bool in_keys = false;
    int line_no = 0;
    const auto err = [&](const std::string& m) {
        if (errors) errors->push_back("line " + std::to_string(line_no) + ": " + m);
    };
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const auto nl = text.find('\n', pos);
        const std::string line = trim(text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos));
        pos = nl == std::string_view::npos ? text.size() + 1 : nl + 1;
        ++line_no;
        if (line.empty() || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            const std::string id = line.substr(1, line.size() - 2);
            in_keys = id == "keys";
            cur = nullptr;
            if (!in_keys) {
                out.presets.push_back({});
                cur = &out.presets.back();
                cur->id = id;
                cur->name = id;
            }
            continue;
        }
        const auto colon = line.find(':');
        if (colon == std::string::npos) { err("expected 'key: value'"); continue; }
        const std::string k = trim(std::string_view(line).substr(0, colon));
        const std::string v = trim(std::string_view(line).substr(colon + 1));
        if (in_keys) {
            if (k != "key") { err("only 'key:' lines in [keys]"); continue; }
            const auto sp = v.find(' ');
            const std::string glyph = sp == std::string::npos ? v : v.substr(0, sp);
            const std::string rule  = sp == std::string::npos ? "" : trim(std::string_view(v).substr(sp + 1));
            MetricsKeys::Key key;
            std::string why;
            if (!parse_key_rule(rule, glyph, &key, &why) || (key.left.empty() && key.right.empty() && key.width.empty())) {
                err("bad key for " + glyph + (why.empty() ? "" : ": " + why));
                continue;
            }
            out.keys.emplace_back(glyph, key);
            continue;
        }
        if (!cur) { err("a value before any [preset]"); continue; }
        if (k == "name")    { cur->name = v; continue; }
        if (k == "blurb")   { cur->blurb = v; continue; }
        if (k == "charset") { cur->charset = v; continue; }
        if (k == "glyph" || k == "guide") {
            std::istringstream is(v);
            std::string name;
            int a = 0, b = 0, c = 0;
            if (k == "glyph") {
                if (!(is >> name >> a >> b >> c)) { err("glyph: NAME width left right"); continue; }
                cur->shapes[name] = {a, b, c};
            } else {
                if (!(is >> name >> a)) { err("guide: NAME y"); continue; }
                Guide g;
                g.y = a;
                g.angle = 0;
                g.name = name;
                cur->guides.push_back(g);
            }
            continue;
        }
        int n = 0;
        if (!to_int(v, &n)) { err("'" + k + "' wants a whole number"); continue; }
        Metrics& m = cur->metrics;
        if      (k == "upm")        cur->units_per_em = n;
        else if (k == "ascender")   m.ascender = n;
        else if (k == "descender")  m.descender = n;
        else if (k == "x_height")   m.x_height = n;
        else if (k == "cap_height") m.cap_height = n;
        else if (k == "overshoot")  m.overshoot = n;
        else if (k == "stem_v")     cur->stem_v = n;
        else if (k == "stem_h")     cur->stem_h = n;
        else if (k == "cap_stem_v") cur->cap_stem_v = n;
        else if (k == "cap_stem_h") cur->cap_stem_h = n;
        else if (k == "serif")      cur->serif = n;
        else if (k == "serif_t")    cur->serif_t = n;
        else if (k == "mono")       cur->mono = n;
        else if (k == "space")      cur->space = n;
        else err("unknown key '" + k + "'");
    }
    return out;
}

namespace {
const PresetFile& bundled() {
    static const PresetFile f = parse_presets(unicode::embedded::get("presets/presets.txt"));
    return f;
}
}  // namespace

const std::vector<Preset>& presets() { return bundled().presets; }
const std::vector<std::pair<std::string, MetricsKeys::Key>>& preset_keys() { return bundled().keys; }

const Preset* preset(const std::string& id) {
    for (const auto& p : presets())
        if (p.id == id) return &p;
    return nullptr;
}

// ── the drawing ────────────────────────────────────────────────────────────
// Each glyph is built from overlapping parts, every part counter-clockwise
// (y-up, CFF's outer direction) except a counter, which runs clockwise; the
// union (nonzero) is taken by remove_overlaps, which keeps the curves and
// turns the crossings into corners. Parts never share an edge (coincident
// edges are the overlap rebuild's weak spot): a stem stops half-way into its
// serif, a bar ends half-way into a stem.

namespace {

constexpr double kRound = 0.56;   // handle length of a quarter ellipse, as a fraction of its radius
constexpr double kRoundStem = 1.06;   // a round's side is drawn this much heavier than a straight stem

Node smooth(double x, double y, double ix, double iy, double ox, double oy) {
    return {x, y, ix, iy, ox, oy, Node::Type::Smooth};
}

Contour rect(double x0, double y0, double x1, double y1) {
    Contour c;
    for (auto [x, y] : {std::pair{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}) c.nodes.push_back(Node::corner(x, y));
    return c;
}

Contour poly(std::initializer_list<std::pair<double, double>> pts) {
    Contour c;
    for (auto [x, y] : pts) c.nodes.push_back(Node::corner(x, y));
    return c;
}

// Counter-clockwise from the bottom; `hole` reverses it (a counter).
Contour ellipse(double cx, double cy, double rx, double ry, bool hole) {
    const double kx = kRound * rx, ky = kRound * ry;
    Contour c;
    c.nodes = {
        smooth(cx, cy - ry, cx - kx, cy - ry, cx + kx, cy - ry),
        smooth(cx + rx, cy, cx + rx, cy - ky, cx + rx, cy + ky),
        smooth(cx, cy + ry, cx + kx, cy + ry, cx - kx, cy + ry),
        smooth(cx - rx, cy, cx - rx, cy + ky, cx - rx, cy - ky),
    };
    if (hole) geom::reverse(c);
    return c;
}

// Where the line through a, along da, meets the line through b, along db.
// A plain struct, not std::pair: GCC on aarch64 notes pair's C++17 ABI change
// (-Wpsabi) on a by-value pair return -- as Overlap.cpp's solve_cross (s05).
struct XY { double x, y; };
XY meet(double ax, double ay, double dax, double day,
                               double bx, double by, double dbx, double dby) {
    const double den = dax * dby - day * dbx;
    if (std::abs(den) < 1e-9) return {(ax + bx) / 2, (ay + by) / 2};
    const double t = ((bx - ax) * dby - (by - ay) * dbx) / den;
    return {ax + t * dax, ay + t * day};
}

struct Kit {
    const Preset& p;
    double X, C, D, o;
    double sl, st;   // serif length / thickness
    bool serif() const { return sl > 0 && st > 0; }

    // A lowercase stem from y0 to y1 at [0, V]: a head serif (a wedge out to
    // the left, the stem's top cut flat) when `head`, a foot slab when `foot`.
    void stem(std::vector<Contour>& out, double V, double y0, double y1, bool head, bool foot) const {
        if (!serif()) { out.push_back(rect(0, y0, V, y1)); return; }
        const double b = foot ? y0 + st / 2 : y0;
        if (head)
            out.push_back(poly({{0, b}, {V, b}, {V, y1}, {-sl, y1 - st}, {-sl, y1 - 2 * st}, {0, y1 - 2.6 * st}}));
        else
            out.push_back(rect(0, b, V, y1));
        if (foot) out.push_back(rect(-sl, y0, V + sl, y0 + st));
    }
    // A capital stem at [x, x + V], 0 to C, slabs at both ends when serifed.
    void cap_stem(std::vector<Contour>& out, double x, double V) const {
        if (!serif()) { out.push_back(rect(x, 0, x + V, C)); return; }
        out.push_back(rect(x, st / 2, x + V, C - st / 2));
        out.push_back(rect(x - sl, 0, x + V + sl, st));
        out.push_back(rect(x - sl, C - st, x + V + sl, C));
    }

    std::vector<Contour> n(double W) const {
        const double V = p.stem_v, Hh = p.stem_h;
        std::vector<Contour> parts;
        stem(parts, V, 0, X, true, true);
        // The right stem and the arch, one contour; its left end tucks into
        // the left stem, so the union cuts the two crotches.
        const double ya = 0.55 * X;            // where the right side turns up into the arch
        const double top = X + o, in_top = X + o - Hh;
        const double cxi = W / 2;              // the counter's top: centred on the counter
        const double cx = W / 2 + 0.05 * V;    // the outside's top, a touch right (the arch leans out of the stem)
        const double jx = V / 2;               // inside the left stem
        const double jy = X - 0.25 * X;        // the outer curve's end (it crosses the stem's edge higher)
        const double ky = 0.45 * X;            // the inner curve's start (crosses lower: the deep crotch)
        const double bot = serif() ? st / 2 : 0;
        Contour a;
        a.nodes.push_back(Node::corner(W - V, bot));
        a.nodes.push_back(Node::corner(W, bot));
        a.nodes.push_back({W, ya, W, ya, W, ya + kRound * (top - ya), Node::Type::Corner});
        a.nodes.push_back(smooth(cx, top, cx + kRound * (W - cx), top, cx - kRound * (cx - jx), top));
        a.nodes.push_back({jx, jy, jx, jy + kRound * (top - jy), jx, jy, Node::Type::Corner});
        a.nodes.push_back({jx, ky, jx, ky, jx, ky + kRound * (in_top - ky), Node::Type::Corner});
        a.nodes.push_back(smooth(cxi, in_top, cxi - kRound * (cxi - jx), in_top, cxi + kRound * (W - V - cxi), in_top));
        a.nodes.push_back({W - V, ya, W - V, ya + kRound * (in_top - ya), W - V, ya, Node::Type::Corner});
        parts.push_back(a);
        if (serif()) parts.push_back(rect(W - V - sl, 0, W + sl, st));
        return parts;
    }

    std::vector<Contour> ring(double W, double height, double V, double Hh) const {
        const double ry = height / 2 + o;
        return {ellipse(W / 2, height / 2, W / 2, ry, false),
                ellipse(W / 2, height / 2, W / 2 - V * kRoundStem, ry - Hh, true)};
    }

    std::vector<Contour> H(double W) const {
        const double V = p.cap_stem_v, Hh = p.cap_stem_h;
        std::vector<Contour> parts;
        cap_stem(parts, 0, V);
        cap_stem(parts, W - V, V);
        const double bar = 0.52 * C;   // the optical centre: a bar at half height looks low
        parts.push_back(rect(V / 2, bar - Hh / 2, W - V / 2, bar + Hh / 2));
        return parts;
    }

    std::vector<Contour> pp(double W) const {
        const double V = p.stem_v, Hh = p.stem_h;
        std::vector<Contour> parts;
        stem(parts, V, D, X, true, true);
        const double rx = (W - V / 2) / 2, ry = X / 2 + o;
        parts.push_back(ellipse(V / 2 + rx, X / 2, rx, ry, false));
        const double l = 0.7 * V, r = W - V * kRoundStem;
        parts.push_back(ellipse((l + r) / 2, X / 2, (r - l) / 2, ry - Hh, true));
        return parts;
    }

    std::vector<Contour> v(double W) const {
        // The left arm carries the weight (a down-stroke), the right is
        // thinner by the preset's contrast.
        const double dl = p.stem_v * 1.1;
        const double dr = std::max(p.stem_h * 1.3, std::min(dl, p.stem_v * 0.9 * (p.stem_h / double(p.stem_v)) + p.stem_h * 0.4));
        const double Xt = serif() ? X - st / 2 : X;
        const double b = 0.4 * (dl + dr) / 2;   // the flat at the foot
        const double bx0 = W / 2 - b / 2, bx1 = W / 2 + b / 2;
        // Inner edges parallel to the outer ones.
        const auto apex = meet(dl, Xt, bx0 - 0, 0 - Xt, W - dr, Xt, bx1 - W, 0 - Xt);
        std::vector<Contour> parts;
        parts.push_back(poly({{bx0, 0}, {bx1, 0}, {W, Xt}, {W - dr, Xt}, {apex.x, apex.y}, {dl, Xt}, {0, Xt}}));
        if (serif()) {
            parts.push_back(rect(-sl, X - st, dl + sl, X));
            parts.push_back(rect(W - dr - sl, X - st, W + sl, X));
        }
        return parts;
    }
};

Glyph finish(const Preset& p, const std::string& name, char32_t cp, std::vector<Contour> parts,
             std::vector<std::string>* notes) {
    Glyph g;
    g.name = name;
    g.unicodes = {cp};
    g.status = GlyphStatus::Drafted;
    const auto merged = geom::remove_overlaps(parts);
    if (merged.fallback && notes) notes->push_back(name + ": parts kept as drawn (" + merged.why + ")");
    g.contours = merged.contours;
    g.snap();
    double x0 = 1e9, x1 = -1e9;
    for (const auto& c : g.contours)
        for (const auto& n : c.nodes) { x0 = std::min(x0, n.x); x1 = std::max(x1, n.x); }
    const auto it = p.shapes.find(name);
    const Preset::Shape s = it != p.shapes.end() ? it->second : Preset::Shape{};
    double shift = 0;
    if (p.mono > 0) {
        shift = std::round((p.mono - (x1 - x0)) / 2 - x0);
        g.advance = p.mono;
    } else {
        shift = s.left - x0;
        g.advance = std::round(x1 - x0 + s.left + s.right);
    }
    geom::shift_x(g, shift);
    g.snap();
    return g;
}

}  // namespace

std::vector<Glyph> starter_glyphs(const Preset& p, std::vector<std::string>* notes) {
    const Metrics& m = p.metrics;
    Kit k{p, double(m.x_height), double(m.cap_height), double(m.descender), double(m.overshoot),
          double(p.serif), double(p.serif_t)};
    const auto w = [&](const std::string& n) {
        const auto it = p.shapes.find(n);
        return double(it != p.shapes.end() && it->second.width > 0 ? it->second.width : 500);
    };
    std::vector<Glyph> out;
    out.push_back(finish(p, "n", U'n', k.n(w("n")), notes));
    out.push_back(finish(p, "o", U'o', k.ring(w("o"), m.x_height, p.stem_v, p.stem_h), notes));
    out.push_back(finish(p, "H", U'H', k.H(w("H")), notes));
    out.push_back(finish(p, "O", U'O', k.ring(w("O"), m.cap_height, p.cap_stem_v, p.cap_stem_h), notes));
    out.push_back(finish(p, "p", U'p', k.pp(w("p")), notes));
    out.push_back(finish(p, "v", U'v', k.v(w("v")), notes));
    return out;
}

// ── the font ───────────────────────────────────────────────────────────────

void make_preset_font(Font& f, const NewFontOptions& o, NewFontReport* rep) {
    NewFontReport local;
    NewFontReport& r = rep ? *rep : local;
    make_new_font(f);
    const Preset* p = preset(o.preset);
    Font::Loading load(f);
    if (!p) {
        if (!o.family.empty()) {
            FontInfo info = f.info();
            info.family = o.family;
            f.put(Font::kInfo, Record{info});
        }
        return;
    }

    FontInfo info;
    info.family = o.family.empty() ? FontInfo{}.family : o.family;
    info.units_per_em = p->units_per_em;
    info.guides = p->guides;
    info.origin = {p->id, p->stem_v, p->stem_h, p->cap_stem_v, p->cap_stem_h};
    f.put(Font::kInfo, Record{info});
    f.put(Font::kMetrics, Record{p->metrics});

    // The set: its order is the glyph order (.notdef first).
    const std::string set_id = o.charset.empty() ? p->charset : o.charset;
    const unicode::CharSet* cs = set_id == "@none" ? nullptr : unicode::charset(set_id);
    std::vector<char32_t> cps;
    if (cs) cps = cs->cps;
    const auto order_of = [&](char32_t cp) {
        const auto it = std::find(cps.begin(), cps.end(), cp);
        return it == cps.end() ? double(cps.size() + 10 + cp) : double(it - cps.begin()) + 2;
    };

    Glyph space = *f.glyph("space");
    space.advance = p->mono > 0 ? p->mono : p->space;
    space.order = order_of(U' ');
    f.put("space", Record{space});
    if (p->mono > 0) {
        Glyph nd = *f.glyph(".notdef");
        geom::shift_x(nd, (p->mono - nd.advance) / 2);
        nd.advance = p->mono;
        nd.snap();
        f.put(".notdef", Record{nd});
    }

    double n_adv = 500, H_adv = 600;
    for (Glyph g : starter_glyphs(*p, &r.notes)) {
        g.order = order_of(g.unicodes.front());
        if (g.name == "n") n_adv = g.advance;
        if (g.name == "H") H_adv = g.advance;
        f.put(g.name, Record{g});
        ++r.drawn;
    }

    // The rest of the set: plain glyphs first, so the accented ones can be
    // planned against bases and marks that are now in the font.
    if (cs && o.create_set) {
        const auto missing = missing_from(f, cps);
        std::vector<char32_t> plain, built;
        for (char32_t cp : missing) {
            const auto plan = plan_composite(f, cp);
            (plan.components.empty() && plan.missing.empty() ? plain : built).push_back(cp);
        }
        const auto add = [&](const std::vector<NewGlyph>& gs) {
            for (const auto& ng : gs) {
                Glyph g;
                g.name = ng.name;
                g.unicodes = ng.unicodes;
                g.components = ng.components;
                const char32_t cp = ng.unicodes.empty() ? 0 : ng.unicodes.front();
                const bool upper = unicode::category(cp) == "Lu";
                g.advance = p->mono > 0 ? p->mono : ng.advance >= 0 ? ng.advance : upper ? H_adv : n_adv;
                if (!g.components.empty()) { g.status = GlyphStatus::Drafted; ++r.composites; }
                g.order = order_of(cp);
                f.put(g.name, Record{g});
                ++r.added;
            }
        };
        add(glyphs_for(f, plain));
        auto rest = glyphs_for(f, built);
        plan_composites(f, rest);
        add(rest);
    }

    // Spacing keys: the control glyphs from their numbers, the rest from the
    // shared table (only glyphs in the font); mono: one width for all.
    MetricsKeys keys;
    if (p->mono > 0) {
        for (const auto& name : f.glyph_order())
            if (name != ".notdef") keys.keys[name].width = std::to_string(p->mono);
    } else {
        for (const auto& [name, s] : p->shapes) {
            if (name == "p") { keys.keys[name] = {"n", "o", ""}; continue; }
            keys.keys[name] = {std::to_string(s.left), std::to_string(s.right), ""};
        }
        for (const auto& [name, key] : preset_keys())
            if (f.glyph(name) && !keys.keys.count(name)) keys.keys[name] = key;
    }
    r.keys = static_cast<int>(keys.keys.size());
    f.put(Font::kMetricsKeys, Record{keys});
}

}  // namespace granjon::model
