#include "model/Composite.hpp"

#include "model/Bounds.hpp"
#include "unicode/Blocks.hpp"
#include "unicode/UcdTable.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace granjon::model {

namespace {

// Combining mark -> its spacing twin (the accents fonts built for Latin-1
// carry even when they lack the combining set).
const std::map<char32_t, char32_t>& twins() {
    static const std::map<char32_t, char32_t> t = {
        {0x0300, 0x0060}, {0x0301, 0x00B4}, {0x0302, 0x02C6}, {0x0303, 0x02DC}, {0x0304, 0x00AF},
        {0x0306, 0x02D8}, {0x0307, 0x02D9}, {0x0308, 0x00A8}, {0x030A, 0x02DA}, {0x030B, 0x02DD},
        {0x030C, 0x02C7}, {0x0327, 0x00B8}, {0x0328, 0x02DB},
    };
    return t;
}

// Below the base; every other mark in U+0300..036F is treated as above.
bool is_below(char32_t cp) {
    switch (cp) {
        case 0x0316: case 0x0317: case 0x0318: case 0x0319: case 0x031C: case 0x031D: case 0x031E:
        case 0x031F: case 0x0320: case 0x0323: case 0x0324: case 0x0325: case 0x0326: case 0x0327:
        case 0x0328: case 0x0329: case 0x032A: case 0x032B: case 0x032C: case 0x032D: case 0x032E:
        case 0x032F: case 0x0330: case 0x0331: case 0x0332: case 0x0333: case 0x0339: case 0x033A:
        case 0x033B: case 0x033C: case 0x0347: case 0x0348: case 0x0349: case 0x034D: case 0x034E:
        case 0x0353: case 0x0354: case 0x0355: case 0x0356: case 0x0359: case 0x035A:
            return true;
        default:
            return false;
    }
}

const Anchor* find_anchor(const Glyph& g, const std::string& name) {
    for (const auto& a : g.anchors)
        if (a.name == name) return &a;
    return nullptr;
}

bool is_upper(char32_t cp) {
    const auto gc = unicode::category(cp);
    return gc == "Lu" || gc == "Lt";
}

// A glyph's mark code point: its own if it is a mark or a spacing twin, else
// the one its name suggests ("acute.cap" -> U+00B4 via "acute").
char32_t mark_cp(const Font& f, const Glyph& g) {
    for (char32_t cp : g.unicodes)
        if (mark_place(cp) != MarkPlace::None) return cp;
    const auto dot = g.name.find('.');
    if (dot != std::string::npos && dot > 0) {
        if (const Glyph* plain = f.glyph(g.name.substr(0, dot)))
            for (char32_t cp : plain->unicodes)
                if (mark_place(cp) != MarkPlace::None) return cp;
    }
    return 0;
}

bool is_case_form(const std::string& name) {
    return name.ends_with(".case") || name.ends_with(".cap");
}

}  // namespace

MarkPlace mark_place(char32_t cp) {
    if (cp >= 0x0300 && cp <= 0x036F) return is_below(cp) ? MarkPlace::Below : MarkPlace::Above;
    for (const auto& [comb, spacing] : twins())
        if (spacing == cp) return is_below(comb) ? MarkPlace::Below : MarkPlace::Above;
    return MarkPlace::None;
}

char32_t spacing_twin(char32_t combining) {
    const auto it = twins().find(combining);
    return it == twins().end() ? 0 : it->second;
}

CompositePlan plan_composite(const Font& f, char32_t cp) {
    CompositePlan plan;
    bool compat = false;
    const auto parts = unicode::decomposition(cp, &compat);
    if (parts.size() < 2 || compat) return plan;
    for (std::size_t i = 1; i < parts.size(); ++i)
        if (mark_place(parts[i]) == MarkPlace::None) return plan;   // not base + marks

    // ── the base ───────────────────────────────────────────────────────────
    char32_t base_cp = parts[0];
    const bool any_above = std::any_of(parts.begin() + 1, parts.end(),
                                       [](char32_t m) { return mark_place(m) == MarkPlace::Above; });
    const Glyph* base = nullptr;
    if (any_above && (base_cp == U'i' || base_cp == U'j'))
        base = f.glyph_for(base_cp == U'i' ? char32_t(0x0131) : char32_t(0x0237));   // dotless
    if (!base) base = f.glyph_for(base_cp);
    if (!base) {
        plan.missing.push_back(unicode::format_cp(base_cp) + " " + unicode::utf8(base_cp) + " (the base)");
        return plan;
    }
    const bool upper = is_upper(cp);
    plan.components.push_back({base->name});
    plan.advance = base->advance;
    const auto& m = f.metrics();
    const auto base_box = bounds(f, *base);
    const double base_cx = base_box ? (base_box->x0 + base_box->x1) / 2.0 : base->advance / 2.0;

    // The attachment points so far: where the next above / below mark goes.
    // Start at the base's anchors; a placed mark's own "top" / "bottom" (or,
    // lacking one, its drawing's edge) becomes the next one's.
    std::optional<std::pair<double, double>> at_top, at_bottom;
    if (const Anchor* a = find_anchor(*base, "top"))    at_top    = {{a->x, a->y}};
    if (const Anchor* a = find_anchor(*base, "bottom")) at_bottom = {{a->x, a->y}};
    std::optional<double> stack_top, stack_bottom;   // bbox edges of placed marks (no-anchor stacking)

    // ── the marks ──────────────────────────────────────────────────────────
    for (std::size_t i = 1; i < parts.size(); ++i) {
        const char32_t mc = parts[i];
        const MarkPlace place = mark_place(mc);
        const Glyph* mark = f.glyph_for(mc);
        if (!mark)
            if (char32_t t = spacing_twin(mc)) mark = f.glyph_for(t);
        if (!mark) {
            plan.missing.push_back(unicode::format_cp(mc) + " (a mark for it)");
            continue;
        }
        bool cased = false;
        if (upper && place == MarkPlace::Above) {
            for (const char* suffix : {".case", ".cap"})
                if (const Glyph* c = f.glyph(mark->name + suffix)) { mark = c; cased = true; break; }
        }
        Component k{mark->name};
        const auto mbox = bounds(f, *mark);
        const bool above = place == MarkPlace::Above;
        const auto& at = above ? at_top : at_bottom;
        const Anchor* ma = find_anchor(*mark, above ? "_top" : "_bottom");
        if (at && ma) {
            k.dx = at->first - ma->x;
            k.dy = at->second - ma->y;
        } else {
            // No anchors: centre on the base's drawing; stack on marks already placed.
            const double mcx = mbox ? (mbox->x0 + mbox->x1) / 2.0 : mark->advance / 2.0;
            k.dx = base_cx - mcx;
            if (mc == 0x0328 && base_box && mbox) k.dx = base_box->x1 - mbox->x1;   // ogonek: right foot
            if (above) {
                if (upper && !cased) k.dy = m.cap_height - m.x_height;
                if (stack_top && mbox) k.dy = std::max(k.dy, *stack_top + m.overshoot * 2 - mbox->y0);
            } else if (stack_bottom && mbox) {
                k.dy = std::min(0.0, *stack_bottom - m.overshoot * 2 - mbox->y1);
            }
        }
        k.dx = std::round(k.dx);
        k.dy = std::round(k.dy);
        // What the next mark on this side attaches to.
        if (const Anchor* next = find_anchor(*mark, above ? "top" : "bottom")) {
            (above ? at_top : at_bottom) = {{next->x + k.dx, next->y + k.dy}};
        } else {
            (above ? at_top : at_bottom).reset();
            if (mbox) (above ? stack_top : stack_bottom) = above ? mbox->y1 + k.dy : mbox->y0 + k.dy;
        }
        plan.components.push_back(k);
    }
    if (!plan.missing.empty()) plan.components.clear();
    return plan;
}

std::size_t plan_composites(const Font& f, std::vector<NewGlyph>& glyphs) {
    std::size_t n = 0;
    for (auto& g : glyphs) {
        if (g.unicodes.empty()) continue;
        const auto plan = plan_composite(f, g.unicodes.front());
        if (!plan.ok()) continue;
        g.components = plan.components;
        g.advance = plan.advance;
        ++n;
    }
    return n;
}

std::vector<Anchor> default_anchors(const Font& f, const Glyph& g) {
    std::vector<Anchor> out;
    const auto box = bounds(f, g);
    if (!box) return out;
    const double cx = std::round((box->x0 + box->x1) / 2.0);
    const auto& m = f.metrics();
    auto add = [&](const char* name, double x, double y) {
        if (!find_anchor(g, name)) out.push_back({name, x, std::round(y)});
    };
    if (const char32_t mc = mark_cp(f, g)) {
        // A mark: "_top" where it meets a lowercase letter's top (x-height;
        // cap height for a case form), and "top" over it for the next mark.
        const bool cased = is_case_form(g.name);
        if (mark_place(mc) == MarkPlace::Above) {
            add("_top", cx, cased ? m.cap_height : m.x_height);
            add("top", cx, box->y1 + m.overshoot);
        } else {
            add("_bottom", cx, 0);
            add("bottom", cx, box->y0 - m.overshoot);
        }
        return out;
    }
    const char32_t cp = g.unicodes.empty() ? 0 : g.unicodes.front();
    const auto gc = cp ? unicode::category(cp) : std::string_view{};
    if (!gc.starts_with("L")) return out;   // letters only
    add("top", cx, box->y1);
    add("bottom", cx, 0);
    return out;
}

}  // namespace granjon::model
