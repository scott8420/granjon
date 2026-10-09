#include "views/GlyphPaint.hpp"

#include <algorithm>

namespace granjon::views {

void append_glyph_path(const Cairo::RefPtr<Cairo::Context>& cr, const model::Font& f,
                       const model::Glyph& g, int depth) {
    for (const auto& c : g.contours) {
        const auto& ns = c.nodes;
        if (ns.empty()) continue;
        cr->move_to(ns[0].x, ns[0].y);
        const std::size_t n = ns.size();
        const std::size_t segs = c.closed ? n : n - 1;
        for (std::size_t i = 0; i < segs; ++i) {
            const auto& a = ns[i];
            const auto& b = ns[(i + 1) % n];
            if (!a.has_out() && !b.has_in()) cr->line_to(b.x, b.y);
            else cr->curve_to(a.cx2, a.cy2, b.cx1, b.cy1, b.x, b.y);
        }
        if (c.closed) cr->close_path();
    }
    if (depth >= 8) return;
    for (const auto& k : g.components) {
        const model::Glyph* base = f.glyph(k.base);
        if (!base) continue;
        cr->save();
        Cairo::Matrix m(k.xx, k.xy, k.yx, k.yy, k.dx, k.dy);
        cr->transform(m);
        append_glyph_path(cr, f, *base, depth + 1);
        cr->restore();
    }
}

void paint_glyph_fitted(const Cairo::RefPtr<Cairo::Context>& cr, const model::Font& f,
                        const model::Glyph& g, double w, double h, double pad) {
    const auto& m = f.metrics();
    const double em_h  = std::max(1, m.ascender - m.descender);
    const double em_w  = std::max({1.0, g.advance, static_cast<double>(f.info().units_per_em) * 0.5});
    const double scale = std::min((w - 2 * pad) / em_w, (h - 2 * pad) / em_h);
    if (scale <= 0) return;
    cr->save();
    // Centre the advance box horizontally; baseline sits where the descender says.
    cr->translate((w - g.advance * scale) / 2.0, pad + m.ascender * scale + ((h - 2 * pad) - em_h * scale) / 2.0);
    cr->scale(scale, -scale);   // y-up
    cr->set_fill_rule(Cairo::Context::FillRule::WINDING);
    append_glyph_path(cr, f, g);
    cr->fill();
    cr->restore();
}

}  // namespace granjon::views
