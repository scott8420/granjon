#include "views/SpacingStrip.hpp"

#include "command/Verbs.hpp"
#include "model/Bounds.hpp"
#include "model/Profiles.hpp"
#include "model/Spacing.hpp"
#include "views/GlyphPaint.hpp"

#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/eventcontrollermotion.h>
#include <gtkmm/eventcontrollerscroll.h>
#include <gtkmm/gestureclick.h>
#include <gtkmm/root.h>
#include <pangomm/layout.h>

#include <algorithm>
#include <cmath>

namespace granjon::views {

namespace {
constexpr double kMargin   = 24;    // px, left / right / top
constexpr double kKernRow  = 16;    // px above the ascender: kern values
constexpr double kLineGap  = 14;    // px between one line's band and the next line
constexpr double kPairPx   = 7;     // px either side of a join that picks the pair
constexpr double ACC[3] = {0.20, 0.50, 1.00};   // the pick (Curvz's selected blue)
constexpr double NEG[3] = {0.25, 0.55, 1.00};   // a negative kern
constexpr double POS[3] = {0.95, 0.55, 0.15};   // a positive kern
constexpr double OFF[3] = {0.92, 0.28, 0.22};   // a metrics key out of step
constexpr double FLAG[3] = {0.95, 0.60, 0.10};  // the pair readouts disagree

std::string num(double v) {
    const double r = std::round(v);
    return std::to_string(static_cast<long>(r));
}
}  // namespace

SpacingStrip::SpacingStrip(ViewContext& ctx)
    : widgets::Box("spacing", Gtk::Orientation::VERTICAL, 0),
      m_ctx(ctx),
      m_bar("spacing.bar", Gtk::Orientation::HORIZONTAL, 8),
      m_text("spacing.text"),
      m_size("spacing.size"),
      m_size_label("spacing.size_label"),
      m_missing("spacing.missing"),
      m_area("spacing.strip") {
    add_css_class("spacing");

    // ── the bar: text, size ────────────────────────────────────────────────
    m_bar.add_css_class("overview-bar");
    m_text.set_hexpand(true);
    m_text.set_text(m_ctx.settings.spacing_text);
    m_text.set_placeholder_text("Text to space -- nnHOHnn, or /n/o/eacute by glyph name");
    m_text.set_tooltip_text("Characters map through the cmap; /name picks a glyph by name (a space or the next / "
                            "ends it); // is a slash. Remembered.");
    m_text.signal_changed().connect([this] {
        if (m_syncing) return;
        m_ctx.settings.spacing_text = m_text.get_text();
        if (m_ctx.save_settings) m_ctx.save_settings();
        drop_pair();
        m_sel = -1;
        reparse();
    });
    m_text.signal_activate().connect([this] { m_area.grab_focus(); });
    m_missing.add_css_class("dim");
    m_missing.set_ellipsize(Pango::EllipsizeMode::END);
    m_missing.set_max_width_chars(24);
    m_size.set_range(core::kSpacingSizeMin, core::kSpacingSizeMax);
    m_size.set_increments(4, 24);
    m_size.set_draw_value(false);
    m_size.set_size_request(150, -1);
    m_size.set_value(m_ctx.settings.spacing_size);
    m_size.set_tooltip_text("Size: pixels per em (Ctrl+scroll on the strip). Remembered.");
    m_size.signal_value_changed().connect([this] {
        if (m_syncing) return;
        set_size(static_cast<int>(std::lround(m_size.get_value())));
    });
    m_size_label.add_css_class("dim");
    m_size_label.set_width_chars(7);
    m_size_label.set_xalign(1.0f);
    m_size_label.set_text(std::to_string(m_ctx.settings.spacing_size) + " px");
    m_bar.append(m_text);
    m_bar.append(m_missing);
    m_bar.append(m_size);
    m_bar.append(m_size_label);
    append(m_bar);

    // ── the strip ──────────────────────────────────────────────────────────
    m_area.add_css_class("glyph-canvas");
    m_area.set_hexpand(true);
    m_area.set_vexpand(true);
    m_area.set_focusable(true);
    m_area.set_draw_func(sigc::mem_fun(*this, &SpacingStrip::on_draw));
    append(m_area);

    m_drag = Gtk::GestureDrag::create();
    m_drag->set_button(GDK_BUTTON_PRIMARY);
    m_drag->signal_drag_begin().connect([this](double x, double y) { on_press(x, y); });
    m_drag->signal_drag_update().connect([this](double dx, double dy) { on_drag(dx, dy); });
    m_drag->signal_drag_end().connect([this](double dx, double dy) { on_release(dx, dy); });
    m_area.add_controller(m_drag);

    auto motion = Gtk::EventControllerMotion::create();
    motion->signal_motion().connect(sigc::mem_fun(*this, &SpacingStrip::on_motion));
    m_area.add_controller(motion);

    auto scroll = Gtk::EventControllerScroll::create();
    scroll->set_flags(Gtk::EventControllerScroll::Flags::VERTICAL);
    scroll->signal_scroll().connect([this, s = scroll.get()](double, double dy) {
        const auto st = s->get_current_event_state();
        if ((st & Gdk::ModifierType::CONTROL_MASK) != Gdk::ModifierType::CONTROL_MASK) return false;
        set_size(static_cast<int>(std::lround(m_ctx.settings.spacing_size * std::pow(1.1, -dy))));
        return true;
    }, false);
    m_area.add_controller(scroll);

    auto keys = Gtk::EventControllerKey::create();
    keys->signal_key_pressed().connect(sigc::mem_fun(*this, &SpacingStrip::on_key), false);
    m_area.add_controller(keys);

    m_ctx.selection_changed.connect([this] {
        if (m_sel >= 0 && (m_sel >= static_cast<int>(m_cells.size()) || m_cells[m_sel].name != m_ctx.selected))
            m_sel = -1;
        m_area.queue_draw();
    });
    m_ctx.kern_pick_changed.connect([this] {   // dropped elsewhere (another view came to the front)
        if (!m_ctx.kern_pick && m_pair >= 0) { m_pair = -1; m_area.queue_draw(); }
    });
    m_ctx.settings_changed.connect([this] {
        m_syncing = true;
        if (std::string(m_text.get_text()) != m_ctx.settings.spacing_text) m_text.set_text(m_ctx.settings.spacing_text);
        if (std::lround(m_size.get_value()) != m_ctx.settings.spacing_size) m_size.set_value(m_ctx.settings.spacing_size);
        m_syncing = false;
        reparse();
    });
    reparse();
}

void SpacingStrip::refresh() {
    reparse();
}

void SpacingStrip::focus_strip() {
    m_area.grab_focus();
}

double SpacingStrip::scale() const {
    const int upm = std::max(16, m_ctx.doc.font.info().units_per_em);
    return m_ctx.settings.spacing_size / static_cast<double>(upm);
}

void SpacingStrip::set_size(int px) {
    px = std::clamp(px, core::kSpacingSizeMin, core::kSpacingSizeMax);
    m_size_label.set_text(std::to_string(px) + " px");
    if (px == m_ctx.settings.spacing_size) return;
    m_ctx.settings.spacing_size = px;
    m_syncing = true;
    if (std::lround(m_size.get_value()) != px) m_size.set_value(px);
    m_syncing = false;
    if (m_ctx.save_settings) m_ctx.save_settings();
    m_laid_w = -1;
    m_area.queue_draw();
}

void SpacingStrip::reparse() {
    std::vector<std::string> missing;
    m_names = model::parse_strip(m_ctx.doc.font, m_ctx.settings.spacing_text, &missing);
    if (missing.empty()) {
        m_missing.set_text("");
        m_missing.set_tooltip_text("");
    } else {
        std::string s;
        for (const auto& m : missing) s += (s.empty() ? "" : " ") + m;
        m_missing.set_text("Not in the font: " + s);
        m_missing.set_tooltip_text("Not in the font: " + s);
    }
    // A picked pair survives a model change while its cells still hold it.
    if (m_pair > 0) {
        relayout(m_area.get_width());
        if (m_pair >= static_cast<int>(m_cells.size()) || m_cells[m_pair].line_start ||
            !m_ctx.kern_pick || m_cells[m_pair - 1].name != m_ctx.kern_pick->left ||
            m_cells[m_pair].name != m_ctx.kern_pick->right)
            drop_pair();
    }
    m_laid_w = -1;
    m_area.queue_draw();
}

// ── layout ────────────────────────────────────────────────────────────────
// Lines wrap at the width; a kern applies only between glyphs on one line.
void SpacingStrip::relayout(int w) {
    m_cells.clear();
    m_laid_w = w;
    const auto& f = m_ctx.doc.font;
    const auto& m = f.metrics();
    const double s = scale();
    const double asc = std::max({m.ascender, m.cap_height, m.x_height}) * s;
    const double desc = -std::min(m.descender, 0) * s;
    const double line_h = kKernRow + asc + desc + 4 + m_band_h + kLineGap;
    const double right = std::max(kMargin + 1.0, w - kMargin);
    double x = kMargin;
    int line = 0;
    bool start = true;
    for (std::size_t i = 0; i < m_names.size(); ++i) {
        const model::Glyph* g = f.glyph(m_names[i]);
        if (!g) continue;
        const double adv = g->advance * s;
        int kern = 0;
        if (!start) kern = model::kern_lookup(f.kerning(), m_cells.back().name, m_names[i]).value;
        if (!start && x + kern * s + adv > right) {   // wrap
            ++line;
            x = kMargin;
            kern = 0;
            start = true;
        }
        Cell c;
        c.name = m_names[i];
        c.index = static_cast<int>(i);
        c.line = line;
        c.kern = kern;
        c.line_start = start;
        c.x = x + kern * s;
        c.base = kMargin + line * line_h + kKernRow + asc;
        c.adv = adv;
        m_cells.push_back(c);
        x = c.x + adv;
        start = false;
    }
}

SpacingStrip::Hit SpacingStrip::hit_at(double x, double y) const {
    const auto& m = m_ctx.doc.font.metrics();
    const double s = scale();
    const double asc = std::max({m.ascender, m.cap_height, m.x_height}) * s;
    const double desc = -std::min(m.descender, 0) * s;
    for (int i = 0; i < static_cast<int>(m_cells.size()); ++i) {
        const Cell& c = m_cells[i];
        const double top = c.base - asc - kKernRow;
        const double band_top = c.base + desc + 4;
        const double band_bot = band_top + m_band_h;
        if (y < top || y > band_bot) continue;
        // The join with the previous glyph on this line picks the pair.
        if (y < band_top && !c.line_start) {
            const Cell& p = m_cells[i - 1];
            const double join = (p.x + p.adv + c.x) / 2;
            if (std::abs(x - join) <= kPairPx) return {Zone::Pair, i};
        }
        if (x < c.x || x >= c.x + std::max(c.adv, 1.0)) continue;
        if (y >= band_top) return {x < c.x + c.adv / 2 ? Zone::Left : Zone::Right, i};
        return {Zone::Glyph, i};
    }
    return {};
}

// ── drawing ───────────────────────────────────────────────────────────────

void SpacingStrip::on_draw(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
    relayout(w);
    const auto fg = m_area.get_color();
    const double r = fg.get_red(), gr = fg.get_green(), b = fg.get_blue();
    const auto& f = m_ctx.doc.font;
    const auto& m = f.metrics();
    const double s = scale();
    if (m_cells.empty()) {
        auto layout = m_area.create_pango_layout(m_names.empty() && !m_ctx.settings.spacing_text.empty()
                                                     ? "None of that text is in the font"
                                                     : "Type some text above -- nnHOHnn, or /n/o/eacute");
        int lw = 0, lh = 0;
        layout->get_pixel_size(lw, lh);
        cr->set_source_rgba(r, gr, b, 0.5);
        cr->move_to((w - lw) / 2.0, (h - lh) / 2.0);
        layout->show_in_cairo_context(cr);
        return;
    }
    Pango::FontDescription small = m_area.get_pango_context()->get_font_description();
    small.set_size(static_cast<int>(small.get_size() * 0.8));
    auto text_at = [&](const std::string& t, double cx, double y, int align) {   // align -1 left, 0 centre, 1 right
        auto layout = m_area.create_pango_layout(t);
        layout->set_font_description(small);
        int lw = 0, lh = 0;
        layout->get_pixel_size(lw, lh);
        const double x = align < 0 ? cx : align > 0 ? cx - lw : cx - lw / 2.0;
        cr->move_to(x, y);
        layout->show_in_cairo_context(cr);
        return lw;
    };

    const double desc_px = -std::min(m.descender, 0) * s;
    // Metric lines behind each line of text, the width of the strip.
    {
        int last = -1;
        for (const Cell& c : m_cells) {
            if (c.line == last) continue;
            last = c.line;
            cr->set_line_width(1.0);
            for (int v : {m.ascender, m.cap_height, m.x_height, m.descender}) {
                const double y = std::round(c.base - v * s) + 0.5;
                cr->set_source_rgba(r, gr, b, 0.10);
                cr->move_to(kMargin / 2, y);
                cr->line_to(w - kMargin / 2, y);
                cr->stroke();
            }
            cr->set_source_rgba(r, gr, b, 0.28);   // the baseline, stronger
            cr->move_to(kMargin / 2, std::round(c.base) + 0.5);
            cr->line_to(w - kMargin / 2, std::round(c.base) + 0.5);
            cr->stroke();
        }
    }

    for (int i = 0; i < static_cast<int>(m_cells.size()); ++i) {
        const Cell& c = m_cells[i];
        const model::Glyph* g = f.glyph(c.name);
        if (!g) continue;
        const double top = c.base - std::max({m.ascender, m.cap_height, m.x_height}) * s;
        const double bot = c.base + desc_px;
        const bool sel = c.name == m_ctx.selected && !m_ctx.selected.empty();
        // The selection: every occurrence tinted, the clicked one stronger.
        if (sel) {
            cr->set_source_rgba(ACC[0], ACC[1], ACC[2], i == m_sel ? 0.16 : 0.07);
            cr->rectangle(c.x, top, c.adv, bot - top);
            cr->fill();
        }
        // The advance box edges, faint.
        cr->set_source_rgba(r, gr, b, sel ? 0.35 : 0.14);
        cr->set_line_width(1.0);
        for (double ex : {c.x, c.x + c.adv}) {
            cr->move_to(std::round(ex) + 0.5, top);
            cr->line_to(std::round(ex) + 0.5, bot);
        }
        cr->stroke();
        // The glyph.
        cr->begin_new_path();
        cr->save();
        cr->translate(c.x, c.base);
        cr->scale(s, -s);
        append_glyph_path(cr, f, *g);
        cr->restore();
        cr->set_fill_rule(Cairo::Context::FillRule::WINDING);
        cr->set_source_rgba(r, gr, b, 0.92);
        cr->fill();

        // The band: LSB on the left half, RSB on the right; a key out of step is red.
        const double band_top = bot + 4;
        const auto sb = model::sidebearings(f, *g);
        const auto key = model::check_key(f, c.name);
        if (sb && (c.adv >= 34 || sel)) {
            auto colour = [&](bool off) {
                if (off) cr->set_source_rgb(OFF[0], OFF[1], OFF[2]);
                else     cr->set_source_rgba(r, gr, b, sel ? 0.85 : 0.55);
            };
            colour(key.left_off);
            text_at(num(sb->left), c.x + 2, band_top + 3, -1);
            colour(key.right_off);
            text_at(num(sb->right), c.x + c.adv - 2, band_top + 3, 1);
        }
        if (key.off()) {   // a marker under the glyph: its key no longer matches
            cr->set_source_rgb(OFF[0], OFF[1], OFF[2]);
            const double mx = c.x + c.adv / 2, my = band_top + m_band_h - 5;
            cr->move_to(mx, my - 4);
            cr->line_to(mx + 4, my + 2);
            cr->line_to(mx - 4, my + 2);
            cr->close_path();
            cr->fill();
        } else if (key.has_key) {   // keyed and in step: a faint dot
            cr->set_source_rgba(r, gr, b, 0.35);
            cr->arc(c.x + c.adv / 2, band_top + m_band_h - 4, 1.6, 0, 2 * M_PI);
            cr->fill();
        }
        // The kern with the previous glyph, above the line.
        if (!c.line_start && (c.kern != 0 || i == m_pair)) {
            const Cell& p = m_cells[i - 1];
            const double join = (p.x + p.adv + c.x) / 2;
            const double* col = c.kern < 0 ? NEG : POS;
            if (i == m_pair) {
                cr->set_source_rgba(ACC[0], ACC[1], ACC[2], 0.9);
                cr->set_line_width(2.0);
                cr->move_to(std::round(join) + 0.5, top - 2);
                cr->line_to(std::round(join) + 0.5, bot);
                cr->stroke();
                cr->set_source_rgb(ACC[0], ACC[1], ACC[2]);
            } else {
                cr->set_source_rgb(col[0], col[1], col[2]);
            }
            text_at(c.kern > 0 ? "+" + std::to_string(c.kern) : std::to_string(c.kern), join, top - kKernRow, 0);
        }
    }
    draw_pair_readout(cr);
}

// ── the picked pair's readouts (M8) ───────────────────────────────────────
// The optical area shaded between the clamped edges in the band; the contact
// gap as a line between the touching points, labelled. Amber when the two
// readouts disagree (an open shape: contact alone would misjudge it).
void SpacingStrip::draw_pair_readout(const Cairo::RefPtr<Cairo::Context>& cr) {
    if (m_pair <= 0 || m_pair >= static_cast<int>(m_cells.size())) return;
    const Cell& a = m_cells[m_pair - 1];
    const auto r = model::read_pair(m_ctx.doc.font, a.name, m_cells[m_pair].name);
    if (!r.ok) return;
    const double s = scale();
    auto X = [&](double fx) { return a.x + fx * s; };
    auto Y = [&](double fy) { return a.base - fy * s; };
    const double* col = r.disagree ? FLAG : ACC;
    if (r.ys.size() >= 2) {
        cr->begin_new_path();
        for (std::size_t i = 0; i < r.ys.size(); ++i) {
            if (i == 0) cr->move_to(X(r.lx[i]), Y(r.ys[i]));
            else        cr->line_to(X(r.lx[i]), Y(r.ys[i]));
        }
        for (std::size_t i = r.ys.size(); i-- > 0;) cr->line_to(X(r.rx[i]), Y(r.ys[i]));
        cr->close_path();
        cr->set_source_rgba(col[0], col[1], col[2], 0.13);
        cr->fill();
    }
    cr->begin_new_path();
    cr->set_source_rgba(col[0], col[1], col[2], 0.95);
    cr->set_line_width(1.5);
    cr->move_to(X(r.contact_lx), Y(r.contact_ly));
    cr->line_to(X(r.contact_rx), Y(r.contact_ry));
    cr->stroke();
    for (auto [px, py] : {std::pair{r.contact_lx, r.contact_ly}, {r.contact_rx, r.contact_ry}}) {
        cr->arc(X(px), Y(py), 2.5, 0, 2 * M_PI);
        cr->fill();
    }
    auto layout = m_area.create_pango_layout(std::to_string(static_cast<long>(std::lround(r.contact))));
    Pango::FontDescription small = m_area.get_pango_context()->get_font_description();
    small.set_size(static_cast<int>(small.get_size() * 0.8));
    layout->set_font_description(small);
    int lw = 0, lh = 0;
    layout->get_pixel_size(lw, lh);
    cr->move_to((X(r.contact_lx) + X(r.contact_rx)) / 2 - lw / 2.0, std::min(Y(r.contact_ly), Y(r.contact_ry)) - lh - 2);
    layout->show_in_cairo_context(cr);
}

// ── gestures ──────────────────────────────────────────────────────────────

void SpacingStrip::on_press(double x, double y) {
    m_area.grab_focus();
    relayout(m_area.get_width());
    const Hit hit = hit_at(x, y);
    const auto st = m_drag->get_current_event_state();
    m_alt_press = (st & Gdk::ModifierType::ALT_MASK) == Gdk::ModifierType::ALT_MASK;
    m_fine = (st & Gdk::ModifierType::CONTROL_MASK) == Gdk::ModifierType::CONTROL_MASK;
    m_mode = Zone::None;
    m_drag_cell = hit.cell;
    auto& j = m_ctx.doc.journal;
    auto& f = m_ctx.doc.font;
    switch (hit.zone) {
        case Zone::Pair: {
            pick_pair(hit.cell);
            const auto& l = m_cells[hit.cell - 1].name;
            const auto& r = m_cells[hit.cell].name;
            m_exception = m_alt_press;
            m_kern_press = model::kern_lookup(f.kerning(), l, r).value;
            if (m_exception) {   // an exception starts from the value in effect
                const auto it = f.kerning().pairs.find({l, r});
                if (it != f.kerning().pairs.end()) m_kern_press = it->second;
            }
            command::kern_drag_begin(j, f, l, r);
            m_mode = Zone::Pair;
            break;
        }
        case Zone::Glyph:
            drop_pair();
            select_cell(hit.cell);
            break;
        case Zone::Left:
        case Zone::Right:
            drop_pair();
            select_cell(hit.cell);
            command::drag_begin(j, f, {m_cells[hit.cell].name},
                                hit.zone == Zone::Left ? "Left sidebearing" : "Right sidebearing");
            m_mode = hit.zone;
            break;
        default:
            drop_pair();
            break;
    }
}

void SpacingStrip::on_drag(double dx, double) {
    if (m_mode == Zone::None || m_drag_cell < 0 || m_drag_cell >= static_cast<int>(m_cells.size())) return;
    auto& j = m_ctx.doc.journal;
    auto& f = m_ctx.doc.font;
    const double units = dx / scale() * (m_fine ? 0.1 : 1.0);
    if (m_mode == Zone::Pair) {
        const int v = m_kern_press + static_cast<int>(std::lround(units));
        command::kern_pair(j, f, m_cells[m_drag_cell - 1].name, m_cells[m_drag_cell].name, v, m_exception);
        say_pair();
    } else {
        // The band's left half: dragging right narrows the LSB (the origin
        // moves under the drawing); Alt slides the drawing instead.
        command::drag_sidebearing(j, f, m_mode == Zone::Left ? command::Side::Left : command::Side::Right, units,
                                  m_mode == Zone::Left && m_alt_press);
        if (const model::Glyph* g = f.glyph(m_cells[m_drag_cell].name))
            if (const auto sb = model::sidebearings(f, *g); sb && m_ctx.say)
                m_ctx.say(g->name + "   LSB " + num(sb->left) + "   RSB " + num(sb->right) + "   width " +
                          num(g->advance), false);
    }
}

void SpacingStrip::on_release(double, double) {
    if (m_mode == Zone::None) return;
    command::drag_end(m_ctx.doc.journal, m_ctx.doc.font);
    m_mode = Zone::None;
    if (m_pair > 0) say_pair();
}

void SpacingStrip::on_motion(double x, double y) {
    if (m_mode != Zone::None) return;
    if (m_laid_w != m_area.get_width()) relayout(m_area.get_width());
    const Hit hit = hit_at(x, y);
    switch (hit.zone) {
        case Zone::Pair:  m_area.set_cursor("col-resize"); break;
        case Zone::Left:
        case Zone::Right: m_area.set_cursor("ew-resize"); break;
        default:          m_area.set_cursor("default"); break;
    }
}

bool SpacingStrip::on_key(guint keyval, guint, Gdk::ModifierType state) {
    const bool ctrl  = (state & Gdk::ModifierType::CONTROL_MASK) == Gdk::ModifierType::CONTROL_MASK;
    const bool shift = (state & Gdk::ModifierType::SHIFT_MASK) == Gdk::ModifierType::SHIFT_MASK;
    const bool alt   = (state & Gdk::ModifierType::ALT_MASK) == Gdk::ModifierType::ALT_MASK;
    if (keyval == GDK_KEY_Escape) {
        if (m_mode != Zone::None) {
            command::drag_cancel(m_ctx.doc.journal);
            m_mode = Zone::None;
        } else if (m_pair > 0) {
            drop_pair();
        } else if (auto* root = dynamic_cast<Gtk::Widget*>(m_area.get_root())) {
            root->child_focus(Gtk::DirectionType::TAB_FORWARD);
        }
        return true;
    }
    if (ctrl) return false;
    int dir = 0;
    switch (keyval) {
        case GDK_KEY_Left:  case GDK_KEY_KP_Left:  dir = -1; break;
        case GDK_KEY_Right: case GDK_KEY_KP_Right: dir = 1;  break;
        default: return false;
    }
    if (m_mode != Zone::None) return true;
    if (m_pair > 0) {
        kern_by(dir * (shift ? 10 : 1), alt);
        return true;
    }
    // No pair: the selection steps along the strip.
    if (m_cells.empty()) return true;
    int at = m_sel;
    if (at < 0)
        for (int i = 0; i < static_cast<int>(m_cells.size()); ++i)
            if (m_cells[i].name == m_ctx.selected) { at = i; break; }
    at = at < 0 ? (dir > 0 ? 0 : static_cast<int>(m_cells.size()) - 1)
                : std::clamp(at + dir, 0, static_cast<int>(m_cells.size()) - 1);
    select_cell(at);
    return true;
}

// ── the pick ──────────────────────────────────────────────────────────────

void SpacingStrip::select_cell(int cell) {
    if (cell < 0 || cell >= static_cast<int>(m_cells.size())) return;
    m_sel = cell;
    m_ctx.select(m_cells[cell].name);
    m_area.queue_draw();
    if (m_ctx.say) {
        const auto& f = m_ctx.doc.font;
        if (const model::Glyph* g = f.glyph(m_cells[cell].name)) {
            std::string s = g->name + "   width " + num(g->advance);
            if (const auto sb = model::sidebearings(f, *g)) s += "   LSB " + num(sb->left) + "   RSB " + num(sb->right);
            if (model::check_key(f, g->name).off()) s += "   -- its metrics key no longer matches";
            m_ctx.say(s, false);
        }
    }
}

void SpacingStrip::pick_pair(int right_cell) {
    if (right_cell <= 0 || right_cell >= static_cast<int>(m_cells.size()) || m_cells[right_cell].line_start) return;
    m_pair = right_cell;
    m_sel = -1;
    m_ctx.set_kern_pick(ViewContext::KernPick{m_cells[right_cell - 1].name, m_cells[right_cell].name});
    say_pair();
    m_area.queue_draw();
}

void SpacingStrip::drop_pair() {
    if (m_pair < 0 && !m_ctx.kern_pick) return;
    m_pair = -1;
    m_ctx.set_kern_pick(std::nullopt);
    m_area.queue_draw();
}

void SpacingStrip::kern_by(int delta, bool exception) {
    if (!m_ctx.kern_pick) return;
    const auto& f = m_ctx.doc.font;
    const auto& l = m_ctx.kern_pick->left;
    const auto& r = m_ctx.kern_pick->right;
    int now = model::kern_lookup(f.kerning(), l, r).value;
    if (exception) {   // an exception moves from its own value, or from the value in effect
        const auto it = f.kerning().pairs.find({l, r});
        if (it != f.kerning().pairs.end()) now = it->second;
    }
    command::kern_pair(m_ctx.doc.journal, m_ctx.doc.font, l, r, now + delta, exception);
    say_pair();
}

void SpacingStrip::say_pair() {
    if (!m_ctx.kern_pick || !m_ctx.say) return;
    const auto& k = m_ctx.doc.font.kerning();
    const auto& l = m_ctx.kern_pick->left;
    const auto& r = m_ctx.kern_pick->right;
    const auto hit = model::kern_lookup(k, l, r);
    const auto key = model::kern_target(k, l, r, false);
    std::string s = "Pair " + l + " " + r + "   kern " + std::to_string(hit.value) + "   (" +
                    model::kern_kind(key) + (hit.found ? "" : ", none yet") + ")";
    if (const auto rd = model::read_pair(m_ctx.doc.font, l, r); rd.ok) {
        s += "   contact " + std::to_string(std::lround(rd.contact)) + "   optical " + std::to_string(std::lround(rd.optical));
        if (rd.proposed && *rd.proposed != hit.value) s += "   to gap: " + std::to_string(*rd.proposed);
        if (rd.disagree) s += "   -- the readouts disagree";
    }
    m_ctx.say(s, false);
}

}  // namespace granjon::views
