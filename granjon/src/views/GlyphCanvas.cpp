#include "views/GlyphCanvas.hpp"

#include "command/Verbs.hpp"
#include "model/Bounds.hpp"
#include "unicode/UcdTable.hpp"
#include "views/GlyphPaint.hpp"

#include <gtkmm/eventcontrollermotion.h>
#include <gtkmm/eventcontrollerscroll.h>
#include <pangomm/layout.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace granjon::views {

using geom::PointRef;
using geom::Vec2;
using Part = PointRef::Part;

namespace {

constexpr double kMinScale = 0.01, kMaxScale = 64.0;   // px per font unit
constexpr double kHitPx    = 6.0;                       // pick tolerance, screen px
constexpr double kDragPx   = 3.0;                       // movement before a press becomes a drag
constexpr double kPanStep  = 24.0;                      // px per scroll step

// Curvz's overlay palette (BezierPath::draw_editor_overlay), unchanged: it was
// tuned to read on both dark and light artboards.
constexpr double NF[3] = {0.96, 0.96, 0.96};         // node fill
constexpr double NS[3] = {0.20, 0.50, 1.00};         // selected
constexpr double NK[3] = {0.88, 0.88, 0.88};         // node stroke
constexpr double NB[4] = {0.00, 0.00, 0.00, 0.55};   // dark halo
constexpr double HL[4] = {0.55, 0.68, 1.00, 0.70};   // handle line
constexpr double HC[4] = {0.30, 0.60, 1.00, 0.95};   // handle dot
constexpr double AN[3] = {0.89, 0.30, 0.30};         // anchors (Granjon's own)

std::string fmt(double v) {
    std::ostringstream o;
    if (std::abs(v - std::round(v)) < 1e-6) o << static_cast<long long>(std::llround(v));
    else o << std::fixed << std::setprecision(1) << v;
    return o.str();
}

const char* type_name(model::Node::Type t) {
    switch (t) {
        case model::Node::Type::Smooth:    return "smooth";
        case model::Node::Type::Cusp:      return "cusp";
        case model::Node::Type::Symmetric: return "symmetric";
        default:                           return "corner";
    }
}

}  // namespace

GlyphCanvas::GlyphCanvas(ViewContext& ctx)
    : widgets::Box("editor", Gtk::Orientation::VERTICAL, 0),
      m_ctx(ctx),
      m_bar("editor.bar", Gtk::Orientation::HORIZONTAL, 8),
      m_title("editor.title"),
      m_context("editor.context"),
      m_fill("editor.fill"),
      m_fit_button("editor.fit"),
      m_zoom("editor.zoom"),
      m_area("editor.canvas") {
    add_css_class("editor");

    // ── toolbar ────────────────────────────────────────────────────────────
    m_bar.add_css_class("overview-bar");   // the same strip the overview wears
    m_bar.add_css_class("editor-bar");
    m_title.set_xalign(0.0f);
    m_title.set_hexpand(true);
    m_title.set_ellipsize(Pango::EllipsizeMode::END);
    m_context.set_label("Context");
    m_context.set_active(true);
    m_context.set_tooltip_text("Ghost a context glyph either side: H beside capitals and figures, n beside the rest");
    m_fill.set_label("Fill");
    m_fill.set_active(true);
    m_fill.set_tooltip_text("Shade the outline");
    m_fit_button.set_label("Fit");
    m_fit_button.set_tooltip_text("Fit the glyph (Ctrl+0)");
    m_zoom.add_css_class("dim");
    m_zoom.set_width_chars(6);
    m_zoom.set_xalign(1.0f);
    m_bar.append(m_title);
    m_bar.append(m_context);
    m_bar.append(m_fill);
    m_bar.append(m_fit_button);
    m_bar.append(m_zoom);
    append(m_bar);
    m_context.signal_toggled().connect([this] { m_area.queue_draw(); });
    m_fill.signal_toggled().connect([this] { m_area.queue_draw(); });
    m_fit_button.signal_clicked().connect([this] { fit(); m_area.grab_focus(); });

    // ── canvas ─────────────────────────────────────────────────────────────
    m_area.add_css_class("glyph-canvas");
    m_area.set_hexpand(true);
    m_area.set_vexpand(true);
    m_area.set_focusable(true);
    m_area.set_draw_func(sigc::mem_fun(*this, &GlyphCanvas::on_draw));
    append(m_area);

    m_drag = Gtk::GestureDrag::create();
    m_drag->set_button(GDK_BUTTON_PRIMARY);
    m_drag->signal_drag_begin().connect([this](double x, double y) { on_press(x, y); });
    m_drag->signal_drag_update().connect([this](double dx, double dy) { on_drag(dx, dy); });
    m_drag->signal_drag_end().connect([this](double dx, double dy) { on_release(dx, dy); });
    m_area.add_controller(m_drag);

    m_pan = Gtk::GestureDrag::create();
    m_pan->set_button(GDK_BUTTON_MIDDLE);
    m_pan->signal_drag_begin().connect([this](double, double) { m_press_ox = m_ox; m_press_oy = m_oy; });
    m_pan->signal_drag_update().connect([this](double dx, double dy) {
        m_ox = m_press_ox + dx;
        m_oy = m_press_oy + dy;
        m_area.queue_draw();
    });
    m_area.add_controller(m_pan);

    auto scroll = Gtk::EventControllerScroll::create();
    scroll->set_flags(Gtk::EventControllerScroll::Flags::BOTH_AXES);
    scroll->signal_scroll().connect([this, s = scroll.get()](double dx, double dy) {
        const auto st = s->get_current_event_state();
        if ((st & Gdk::ModifierType::CONTROL_MASK) == Gdk::ModifierType::CONTROL_MASK) {
            zoom_at(std::pow(1.1, -dy), m_mx, m_my);   // m_mx/m_my track the pointer
        } else {
            m_ox -= dx * kPanStep;
            m_oy -= dy * kPanStep;
            m_area.queue_draw();
        }
        return true;
    }, false);
    m_area.add_controller(scroll);

    auto motion = Gtk::EventControllerMotion::create();
    motion->signal_motion().connect(sigc::mem_fun(*this, &GlyphCanvas::on_motion));
    m_area.add_controller(motion);

    auto keys = Gtk::EventControllerKey::create();
    keys->signal_key_pressed().connect(sigc::mem_fun(*this, &GlyphCanvas::on_key), false);
    keys->signal_key_released().connect(sigc::mem_fun(*this, &GlyphCanvas::on_key_released));
    m_area.add_controller(keys);

    m_ctx.selection_changed.connect([this] { refresh(); });
    refresh();
}

void GlyphCanvas::focus_canvas() { m_area.grab_focus(); }

const model::Glyph* GlyphCanvas::glyph() const {
    return m_ctx.selected.empty() ? nullptr : m_ctx.doc.font.glyph(m_ctx.selected);
}

// ── refresh / fit / zoom ──────────────────────────────────────────────────

void GlyphCanvas::refresh() {
    const model::Glyph* g = glyph();
    if (m_ctx.selected != m_shown) {
        if (m_mode != Mode::None) cancel_drag();
        m_shown = m_ctx.selected;
        m_pick.clear();
        m_fitted = false;   // a new glyph: fit it on the next draw
    }
    if (g) {
        // Undo / redo may have taken points away: drop the stale ones.
        for (auto it = m_pick.begin(); it != m_pick.end();)
            it = geom::valid(*g, *it) ? std::next(it) : m_pick.erase(it);
        std::string t = g->name;
        if (!g->unicodes.empty()) {
            std::ostringstream o;
            o << "   U+" << std::uppercase << std::hex << std::setw(4) << std::setfill('0')
              << static_cast<unsigned>(g->unicodes[0]);
            t += o.str();
        }
        m_title.set_text(t);
    } else {
        m_title.set_text("No glyph");
    }
    m_area.queue_draw();
}

void GlyphCanvas::fit() {
    const int w = m_area.get_width(), h = m_area.get_height();
    const model::Glyph* g = glyph();
    if (w <= 0 || h <= 0) return;
    const auto& m = m_ctx.doc.font.metrics();
    const double em_h = std::max(1, m.ascender - m.descender);
    const double adv  = g ? std::max(g->advance, em_h * 0.3) : em_h * 0.6;
    m_s = std::clamp(std::min(w / (adv * 1.8), h / (em_h * 1.35)), kMinScale, kMaxScale);
    m_ox = (w - (g ? g->advance : adv) * m_s) / 2.0;
    m_oy = h / 2.0 + (m.ascender + m.descender) / 2.0 * m_s;
    m_fitted = true;
    update_zoom_label();
    m_area.queue_draw();
}

void GlyphCanvas::update_zoom_label() {
    m_zoom.set_text(std::to_string(static_cast<int>(std::lround(m_s * 100))) + "%");
}

void GlyphCanvas::zoom_at(double factor, double sx, double sy) {
    const double s = std::clamp(m_s * factor, kMinScale, kMaxScale);
    const Vec2 at = to_font(sx, sy);   // keep this font point under the pointer
    m_s = s;
    m_ox = sx - at.x * m_s;
    m_oy = sy + at.y * m_s;
    update_zoom_label();
    m_area.queue_draw();
}

std::string GlyphCanvas::context_glyph(const model::Glyph& g) const {
    bool upper = false;
    if (!g.unicodes.empty()) {
        const auto gc = unicode::category(g.unicodes[0]);
        upper = gc == "Lu" || gc == "Lt" || gc.starts_with("N");
    } else if (!g.name.empty()) {
        upper = std::isupper(static_cast<unsigned char>(g.name[0])) != 0;
    }
    const auto& f = m_ctx.doc.font;
    const model::Glyph* c = f.glyph_for(upper ? U'H' : U'n');
    if (!c) c = f.glyph(upper ? "H" : "n");
    return c ? c->name : std::string{};
}

// ── drawing ───────────────────────────────────────────────────────────────

void GlyphCanvas::on_draw(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
    if (!m_fitted) fit();
    const auto fg = m_area.get_color();
    const double r = fg.get_red(), gr = fg.get_green(), b = fg.get_blue();
    const model::Glyph* g = glyph();
    if (!g) {
        auto layout = m_area.create_pango_layout("Pick a glyph -- double-click one in the overview, or find it (Ctrl+F)");
        int lw = 0, lh = 0;
        layout->get_pixel_size(lw, lh);
        cr->set_source_rgba(r, gr, b, 0.5);
        cr->move_to((w - lw) / 2.0, (h - lh) / 2.0);
        layout->show_in_cairo_context(cr);
        return;
    }
    const auto& font = m_ctx.doc.font;

    draw_guides(cr, w, h, *g);

    // A path in font units, built under the y-up matrix, stroked in pixels.
    auto font_path = [&](const model::Glyph& gg, double xoff) {
        cr->begin_new_path();
        cr->save();
        cr->translate(m_ox + xoff * m_s, m_oy);
        cr->scale(m_s, -m_s);
        append_glyph_path(cr, font, gg);
        cr->restore();
    };
    cr->set_fill_rule(Cairo::Context::FillRule::WINDING);

    // Context glyphs, ghosted.
    if (m_context.get_active()) {
        const std::string cn = context_glyph(*g);
        if (const model::Glyph* c = cn.empty() ? nullptr : font.glyph(cn)) {
            cr->set_source_rgba(r, gr, b, 0.14);
            font_path(*c, -c->advance);
            cr->fill();
            font_path(*c, g->advance);
            cr->fill();
        }
    }

    // Components: read-only, tinted.
    if (!g->components.empty()) {
        model::Glyph comps;
        comps.components = g->components;
        font_path(comps, 0);
        cr->set_source_rgba(r, gr, b, m_fill.get_active() ? 0.32 : 0.0);
        cr->fill_preserve();
        cr->set_source_rgba(r, gr, b, 0.45);
        cr->set_line_width(1.0);
        std::vector<double> dash{4.0, 3.0};
        cr->set_dash(dash, 0);
        cr->stroke();
        cr->unset_dash();
    }

    // The outline.
    model::Glyph own;
    own.contours = g->contours;
    font_path(own, 0);
    if (m_fill.get_active()) {
        cr->set_source_rgba(r, gr, b, 0.16);
        cr->fill_preserve();
    }
    cr->set_source_rgba(r, gr, b, 0.9);
    cr->set_line_width(1.0);
    cr->stroke();

    draw_overlay(cr, *g);
    draw_anchors(cr, *g);

    if (m_mode == Mode::Marquee) {
        const double x0 = std::min(m_press_x, m_mx), y0 = std::min(m_press_y, m_my);
        cr->rectangle(std::round(x0) + 0.5, std::round(y0) + 0.5, std::round(std::abs(m_mx - m_press_x)),
                      std::round(std::abs(m_my - m_press_y)));
        cr->set_source_rgba(NS[0], NS[1], NS[2], 0.10);
        cr->fill_preserve();
        cr->set_source_rgba(NS[0], NS[1], NS[2], 0.8);
        cr->set_line_width(1.0);
        cr->stroke();
    }
}

void GlyphCanvas::draw_guides(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h, const model::Glyph& g) {
    const auto fg = m_area.get_color();
    const double r = fg.get_red(), gr = fg.get_green(), b = fg.get_blue();
    const auto& m = m_ctx.doc.font.metrics();
    const double os = m.overshoot;

    struct Guide { const char* name; double y, z0, z1; double alpha; };
    const Guide guides[] = {
        {"ascender",   static_cast<double>(m.ascender),   static_cast<double>(m.ascender),       m.ascender + os,  0.22},
        {"cap height", static_cast<double>(m.cap_height), static_cast<double>(m.cap_height),     m.cap_height + os, 0.22},
        {"x-height",   static_cast<double>(m.x_height),   static_cast<double>(m.x_height),       m.x_height + os,  0.22},
        {"baseline",   0.0,                               -os,                                   0.0,              0.45},
        {"descender",  static_cast<double>(m.descender),  m.descender - os,                      static_cast<double>(m.descender), 0.22},
    };
    auto crisp = [](double v) { return std::round(v) + 0.5; };

    // Overshoot zones: tinted bands.
    for (const auto& gd : guides) {
        if (os <= 0) break;
        const double y0 = to_screen({0, gd.z1}).y, y1 = to_screen({0, gd.z0}).y;
        cr->rectangle(0, y0, w, y1 - y0);
        cr->set_source_rgba(NS[0], NS[1], NS[2], 0.09);
        cr->fill();
    }
    // Lines + labels.
    Pango::FontDescription small;
    small.set_size(8 * PANGO_SCALE);
    for (const auto& gd : guides) {
        const double y = crisp(to_screen({0, gd.y}).y);
        cr->move_to(0, y);
        cr->line_to(w, y);
        cr->set_source_rgba(r, gr, b, gd.alpha);
        cr->set_line_width(1.0);
        cr->stroke();
        auto layout = m_area.create_pango_layout(std::string(gd.name) + "  " + fmt(gd.y));
        layout->set_font_description(small);
        int lw = 0, lh = 0;
        layout->get_pixel_size(lw, lh);
        cr->set_source_rgba(r, gr, b, 0.5);
        cr->move_to(8, y - lh - 1);
        layout->show_in_cairo_context(cr);
    }

    // Sidebearing lines: 0 and the advance.
    for (int side = 0; side < 2; ++side) {
        const double x = crisp(to_screen({side == 0 ? 0.0 : g.advance, 0}).x);
        const bool hot = (m_mode == Mode::Sidebearing && m_side == side) ||
                         (m_mode == Mode::None && m_hover_side == side);
        cr->move_to(x, 0);
        cr->line_to(x, h);
        if (hot) { cr->set_source_rgba(NS[0], NS[1], NS[2], 0.95); cr->set_line_width(2.0); }
        else     { cr->set_source_rgba(r, gr, b, 0.35);            cr->set_line_width(1.0); }
        cr->stroke();
    }

    // LSB / width / RSB, under the descender zone.
    const auto sb = model::sidebearings(m_ctx.doc.font, g);
    const double ty = to_screen({0, m.descender - os}).y + 6;
    auto label = [&](const std::string& s, double cx, double alpha) {
        auto layout = m_area.create_pango_layout(s);
        layout->set_font_description(small);
        int lw = 0, lh = 0;
        layout->get_pixel_size(lw, lh);
        cr->set_source_rgba(r, gr, b, alpha);
        cr->move_to(cx - lw / 2.0, ty);
        layout->show_in_cairo_context(cr);
    };
    const double x0 = to_screen({0, 0}).x, x1 = to_screen({g.advance, 0}).x;
    label(fmt(g.advance), (x0 + x1) / 2.0, 0.7);
    if (sb) {
        label("LSB " + fmt(sb->left), x0 + 34, 0.55);
        label("RSB " + fmt(sb->right), x1 - 34, 0.55);
    }
}

void GlyphCanvas::draw_overlay(const Cairo::RefPtr<Cairo::Context>& cr, const model::Glyph& g) {
    // Curvz's sizes, in screen pixels (no zoom scaling): 5 px nodes, 7 px picked.
    const double nr = 2.5, nr_sel = 3.5, hr = 2.5;

    auto draw_handle = [&](Vec2 a, Vec2 h, bool picked) {
        if (h.dist(a) < 0.4) return;
        cr->set_line_width(0.8);
        cr->set_source_rgba(HL[0], HL[1], HL[2], HL[3]);
        cr->move_to(a.x, a.y);
        cr->line_to(h.x, h.y);
        cr->stroke();
        cr->arc(h.x, h.y, picked ? hr + 1 : hr, 0, 2 * M_PI);
        if (picked) cr->set_source_rgba(NS[0], NS[1], NS[2], 1.0);
        else        cr->set_source_rgba(HC[0], HC[1], HC[2], HC[3]);
        cr->fill_preserve();
        cr->set_source_rgb(1.0, 1.0, 1.0);
        cr->set_line_width(0.5);
        cr->stroke();
    };
    // Curvz shapes: symmetric = filled circle, smooth = hollow circle,
    // cusp = triangle, corner = square; the first node of a contour is a
    // diamond (Curvz's "tail") so the start point reads.
    auto build = [&](Vec2 p, int shape, double rr) {
        switch (shape) {
            case 0: case 1: cr->arc(p.x, p.y, rr, 0, 2 * M_PI); break;
            case 2: {
                const double h3 = rr * 1.732;
                cr->move_to(p.x, p.y - rr * 1.2);
                cr->line_to(p.x - h3 * 0.6, p.y + rr * 0.6);
                cr->line_to(p.x + h3 * 0.6, p.y + rr * 0.6);
                cr->close_path();
                break;
            }
            case 3: cr->rectangle(p.x - rr, p.y - rr, rr * 2, rr * 2); break;
            default:
                cr->move_to(p.x, p.y - rr * 1.3);
                cr->line_to(p.x + rr * 1.3, p.y);
                cr->line_to(p.x, p.y + rr * 1.3);
                cr->line_to(p.x - rr * 1.3, p.y);
                cr->close_path();
                break;
        }
    };
    auto draw_node = [&](Vec2 p, int shape, bool picked) {
        const double rr = picked ? nr_sel : nr;
        if (picked) cr->set_source_rgba(1.0, 1.0, 1.0, 0.9);
        else        cr->set_source_rgba(NB[0], NB[1], NB[2], NB[3]);
        build(p, shape, rr + (picked ? 1.5 : 1.2));
        cr->fill();
        if (picked)          cr->set_source_rgb(NS[0], NS[1], NS[2]);
        else if (shape == 1) cr->set_source_rgba(NF[0], NF[1], NF[2], 0.0);
        else                 cr->set_source_rgb(NF[0], NF[1], NF[2]);
        build(p, shape, rr);
        cr->fill_preserve();
        if (picked) cr->set_source_rgba(NS[0], NS[1], NS[2], 0.8);
        else        cr->set_source_rgb(NK[0], NK[1], NK[2]);
        cr->set_line_width(shape == 1 ? 2.0 : 1.5);
        cr->stroke();
    };

    for (int c = 0; c < static_cast<int>(g.contours.size()); ++c) {
        const auto& nodes = g.contours[c].nodes;
        for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
            const auto& n = nodes[i];
            const Vec2 a = to_screen({n.x, n.y});
            if (n.has_in())  draw_handle(a, to_screen({n.cx1, n.cy1}), m_pick.count({c, i, Part::In}) > 0);
            if (n.has_out()) draw_handle(a, to_screen({n.cx2, n.cy2}), m_pick.count({c, i, Part::Out}) > 0);
        }
    }
    for (int c = 0; c < static_cast<int>(g.contours.size()); ++c) {
        const auto& nodes = g.contours[c].nodes;
        for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
            const auto& n = nodes[i];
            int shape = 3;
            switch (n.type) {
                case model::Node::Type::Symmetric: shape = 0; break;
                case model::Node::Type::Smooth:    shape = 1; break;
                case model::Node::Type::Cusp:      shape = 2; break;
                default:                           shape = 3; break;
            }
            if (i == 0) shape = 4;
            draw_node(to_screen({n.x, n.y}), shape, m_pick.count({c, i, Part::Node}) > 0);
        }
    }
}

void GlyphCanvas::draw_anchors(const Cairo::RefPtr<Cairo::Context>& cr, const model::Glyph& g) {
    Pango::FontDescription small;
    small.set_size(8 * PANGO_SCALE);
    for (int i = 0; i < static_cast<int>(g.anchors.size()); ++i) {
        const auto& a = g.anchors[i];
        const Vec2 p = to_screen({a.x, a.y});
        const bool picked = m_pick.count({-1, i, Part::Anchor}) > 0;
        const double rr = picked ? 5.0 : 4.0;
        cr->move_to(p.x, p.y - rr);
        cr->line_to(p.x + rr, p.y);
        cr->line_to(p.x, p.y + rr);
        cr->line_to(p.x - rr, p.y);
        cr->close_path();
        if (picked) cr->set_source_rgb(NS[0], NS[1], NS[2]);
        else        cr->set_source_rgb(AN[0], AN[1], AN[2]);
        cr->fill_preserve();
        cr->set_source_rgba(1.0, 1.0, 1.0, 0.9);
        cr->set_line_width(1.0);
        cr->stroke();
        auto layout = m_area.create_pango_layout(a.name);
        layout->set_font_description(small);
        cr->set_source_rgb(AN[0], AN[1], AN[2]);
        cr->move_to(p.x + rr + 3, p.y - rr - 10);
        layout->show_in_cairo_context(cr);
    }
}

// ── input ─────────────────────────────────────────────────────────────────

int GlyphCanvas::sidebearing_at(double sx, double /*sy*/) const {
    const model::Glyph* g = glyph();
    if (!g) return -1;
    const double x0 = to_screen({0, 0}).x, x1 = to_screen({g->advance, 0}).x;
    const double d0 = std::abs(sx - x0), d1 = std::abs(sx - x1);
    if (d1 <= kHitPx && d1 <= d0) return 1;   // a zero-width glyph: the right line wins
    if (d0 <= kHitPx) return 0;
    return -1;
}

void GlyphCanvas::on_press(double x, double y) {
    m_area.grab_focus();
    m_press_x = x;  m_press_y = y;
    m_mx = x;       m_my = y;
    m_press_ox = m_ox;
    m_press_oy = m_oy;
    m_began = false;
    const model::Glyph* g = glyph();
    if (!g) { m_mode = Mode::None; return; }
    if (m_space) { m_mode = Mode::Pan; return; }

    const auto st = m_drag->get_current_event_state();
    const bool shift = (st & Gdk::ModifierType::SHIFT_MASK) == Gdk::ModifierType::SHIFT_MASK;
    const geom::Hit hit = geom::hit_test(*g, to_font(x, y), kHitPx / m_s);

    auto pick_one = [&](PointRef ref) {
        if (shift) {
            if (m_pick.erase(ref)) { m_mode = Mode::None; return; }   // Shift on a picked one: drop it
            m_pick.insert(ref);
        } else if (!m_pick.count(ref)) {
            m_pick = {ref};
        }
        m_mode = Mode::Points;
    };

    using K = geom::Hit::Kind;
    int side = -1;
    if (hit.kind == K::Node)           pick_one({hit.contour, hit.index, Part::Node});
    else if (hit.kind == K::HandleIn)  pick_one({hit.contour, hit.index, Part::In});
    else if (hit.kind == K::HandleOut) pick_one({hit.contour, hit.index, Part::Out});
    else if (hit.kind == K::Anchor)    pick_one({-1, hit.index, Part::Anchor});
    else if ((side = sidebearing_at(x, y)) >= 0) {
        m_mode = Mode::Sidebearing;
        m_side = side;
        m_press_advance = g->advance;
    } else if (hit.kind == K::Segment) {
        const int n = static_cast<int>(g->contours[hit.contour].nodes.size());
        const PointRef a{hit.contour, hit.index, Part::Node};
        const PointRef b{hit.contour, (hit.index + 1) % n, Part::Node};
        if (!shift && !(m_pick.count(a) && m_pick.count(b))) m_pick.clear();
        m_pick.insert(a);
        m_pick.insert(b);
        m_mode = Mode::Points;
    } else {
        m_mode = Mode::Marquee;
        m_press_pick = shift ? m_pick : geom::Points{};
        if (!shift) m_pick.clear();
    }
    say_pick();
    m_area.queue_draw();
}

void GlyphCanvas::on_drag(double dx, double dy) {
    switch (m_mode) {
        case Mode::Pan:
            m_ox = m_press_ox + dx;
            m_oy = m_press_oy + dy;
            m_area.queue_draw();
            return;
        case Mode::Marquee: {
            m_mx = m_press_x + dx;
            m_my = m_press_y + dy;
            if (const model::Glyph* g = glyph()) {
                m_pick = m_press_pick;
                for (const auto& p : geom::points_in(*g, to_font(m_press_x, m_press_y), to_font(m_mx, m_my)))
                    m_pick.insert(p);
            }
            m_area.queue_draw();
            return;
        }
        case Mode::Points:
        case Mode::Sidebearing: {
            auto& doc = m_ctx.doc;
            if (!m_began) {
                if (std::hypot(dx, dy) < kDragPx) return;   // still a click
                const char* label = m_mode == Mode::Points ? "Move points"
                                    : m_side == 0          ? "Left sidebearing"
                                                           : "Width";
                command::drag_begin(doc.journal, doc.font, {m_shown}, label);
                m_began = doc.journal.in_gesture();
                if (!m_began) return;
            }
            const double fdx = dx / m_s, fdy = -dy / m_s;
            if (m_mode == Mode::Points) {
                command::drag_points(doc.journal, doc.font, m_pick, fdx, fdy);
            } else {
                const auto st = m_drag->get_current_event_state();
                const bool alt = (st & Gdk::ModifierType::ALT_MASK) == Gdk::ModifierType::ALT_MASK;
                command::drag_sidebearing(doc.journal, doc.font,
                                          m_side == 0 ? command::Side::Left : command::Side::Right, fdx, alt);
                // Left edge, no modifier: the origin moved, the drawing must not --
                // the view follows the change in advance.
                if (const model::Glyph* g = glyph(); g && m_side == 0)
                    m_ox = m_press_ox + (m_press_advance - g->advance) * m_s;
            }
            say_pick();
            return;
        }
        default:
            return;
    }
}

void GlyphCanvas::on_release(double /*dx*/, double /*dy*/) {
    const Mode was = m_mode;
    m_mode = Mode::None;
    if ((was == Mode::Points || was == Mode::Sidebearing) && m_began) {
        auto& doc = m_ctx.doc;
        command::drag_end(doc.journal, doc.font);   // snaps, then one step
        if (const model::Glyph* g = glyph(); g && was == Mode::Sidebearing && m_side == 0)
            m_ox = m_press_ox + (m_press_advance - g->advance) * m_s;
    }
    m_began = false;
    m_side = -1;
    say_pick();
    m_area.queue_draw();
}

void GlyphCanvas::cancel_drag() {
    const Mode was = m_mode;
    m_mode = Mode::None;
    if (m_began) command::drag_cancel(m_ctx.doc.journal);
    m_began = false;
    if (was == Mode::Sidebearing || was == Mode::Pan) { m_ox = m_press_ox; m_oy = m_press_oy; }
    if (was == Mode::Marquee) m_pick = m_press_pick;
    m_side = -1;
    m_drag->reset();
    m_area.queue_draw();
}

bool GlyphCanvas::on_key(guint keyval, guint, Gdk::ModifierType state) {
    const bool ctrl  = (state & Gdk::ModifierType::CONTROL_MASK) == Gdk::ModifierType::CONTROL_MASK;
    const bool shift = (state & Gdk::ModifierType::SHIFT_MASK) == Gdk::ModifierType::SHIFT_MASK;
    const model::Glyph* g = glyph();

    if (keyval == GDK_KEY_Escape) {
        if (m_mode != Mode::None) cancel_drag();
        else { m_pick.clear(); say_pick(); m_area.queue_draw(); }
        return true;
    }
    if (keyval == GDK_KEY_space && !ctrl) {
        if (!m_space) { m_space = true; m_area.set_cursor("grab"); }
        return true;
    }
    if (ctrl) {
        switch (keyval) {
            case GDK_KEY_0: case GDK_KEY_KP_0:
                fit();
                return true;
            case GDK_KEY_equal: case GDK_KEY_plus: case GDK_KEY_KP_Add:
                zoom_at(1.25, m_area.get_width() / 2.0, m_area.get_height() / 2.0);
                return true;
            case GDK_KEY_minus: case GDK_KEY_KP_Subtract:
                zoom_at(0.8, m_area.get_width() / 2.0, m_area.get_height() / 2.0);
                return true;
            case GDK_KEY_a: case GDK_KEY_A:
                if (!g) return false;
                m_pick.clear();
                for (int c = 0; c < static_cast<int>(g->contours.size()); ++c)
                    for (int i = 0; i < static_cast<int>(g->contours[c].nodes.size()); ++i)
                        m_pick.insert({c, i, Part::Node});
                for (int i = 0; i < static_cast<int>(g->anchors.size()); ++i) m_pick.insert({-1, i, Part::Anchor});
                say_pick();
                m_area.queue_draw();
                return true;
            default:
                return false;
        }
    }
    double dx = 0, dy = 0;
    switch (keyval) {
        case GDK_KEY_Left:  case GDK_KEY_KP_Left:  dx = -1; break;
        case GDK_KEY_Right: case GDK_KEY_KP_Right: dx = 1;  break;
        case GDK_KEY_Up:    case GDK_KEY_KP_Up:    dy = 1;  break;
        case GDK_KEY_Down:  case GDK_KEY_KP_Down:  dy = -1; break;
        default: return false;
    }
    if (!g || m_pick.empty() || m_mode != Mode::None) return true;   // arrows stay on the canvas
    const double step = shift ? 10 : 1;
    command::nudge(m_ctx.doc.journal, m_ctx.doc.font, m_shown, m_pick, dx * step, dy * step);
    say_pick();
    return true;
}

void GlyphCanvas::on_key_released(guint keyval, guint, Gdk::ModifierType) {
    if (keyval == GDK_KEY_space) {
        m_space = false;
        on_motion(m_mx, m_my);   // put the right cursor back
    }
}

void GlyphCanvas::on_motion(double x, double y) {
    if (m_mode == Mode::None) { m_mx = x; m_my = y; }
    if (m_mode == Mode::Marquee || m_mode == Mode::Points) return;
    const int side = m_mode == Mode::None ? sidebearing_at(x, y) : m_side;
    if (m_space) {
        m_area.set_cursor(m_mode == Mode::Pan ? "grabbing" : "grab");
    } else if (side >= 0) {
        // A node on the line still wins the press -- don't promise a resize then.
        const model::Glyph* g = glyph();
        const bool on_point = g && geom::hit_test(*g, to_font(x, y), kHitPx / m_s).kind != geom::Hit::Kind::None &&
                              geom::hit_test(*g, to_font(x, y), kHitPx / m_s).kind != geom::Hit::Kind::Segment;
        m_area.set_cursor(on_point ? "default" : "ew-resize");
    } else {
        m_area.set_cursor("default");
    }
    if (side != m_hover_side) {
        m_hover_side = side;
        m_area.queue_draw();
    }
}

// The status line speaks for the pick: one point's coordinates, else a count.
void GlyphCanvas::say_pick() {
    if (!m_ctx.say) return;
    const model::Glyph* g = glyph();
    if (!g) return;
    if (m_mode == Mode::Sidebearing) {
        const auto sb = model::sidebearings(m_ctx.doc.font, *g);
        std::string s = "Width " + fmt(g->advance);
        if (sb) s += "   LSB " + fmt(sb->left) + "   RSB " + fmt(sb->right);
        m_ctx.say(s, false);
        return;
    }
    if (m_pick.empty()) return;
    if (m_pick.size() == 1) {
        const PointRef& p = *m_pick.begin();
        if (!geom::valid(*g, p)) return;
        const Vec2 v = geom::position(*g, p);
        std::string what;
        switch (p.part) {
            case Part::Anchor: what = "Anchor “" + g->anchors[p.index].name + "”"; break;
            case Part::In:     what = "In-handle"; break;
            case Part::Out:    what = "Out-handle"; break;
            default:
                what = std::string("Node (") + type_name(g->contours[p.contour].nodes[p.index].type) + ")";
                break;
        }
        m_ctx.say(what + "   " + fmt(v.x) + ", " + fmt(v.y), false);
        return;
    }
    m_ctx.say(std::to_string(m_pick.size()) + " points picked", false);
}

}  // namespace granjon::views
