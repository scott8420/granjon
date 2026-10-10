#include "views/GlyphCanvas.hpp"
#include "geom/NodeEdit.hpp"

#include "command/Verbs.hpp"
#include "model/Bounds.hpp"
#include "unicode/Blocks.hpp"
#include "unicode/UcdTable.hpp"
#include "views/GlyphPaint.hpp"

#include <glibmm/main.h>
#include <gtkmm/eventcontrollermotion.h>
#include <gtkmm/eventcontrollerscroll.h>
#include <gtkmm/root.h>
#include <gtkmm/separator.h>
#include <pangomm/layout.h>

#include <algorithm>
#include <cstdio>
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
constexpr double kRuler    = 20.0;                      // ruler strip, px (top and left)
constexpr double GF[3] = {0.20, 0.75, 0.80};            // glyph guides (teal)
constexpr double GG[3] = {0.72, 0.45, 0.95};            // font guides (violet)

// Curvz's overlay palette (BezierPath::draw_editor_overlay), unchanged: it was
// tuned to read on both dark and light artboards.
constexpr double NF[3] = {0.96, 0.96, 0.96};         // node fill
constexpr double NS[3] = {0.20, 0.50, 1.00};         // selected
constexpr double NK[3] = {0.88, 0.88, 0.88};         // node stroke
constexpr double NB[4] = {0.00, 0.00, 0.00, 0.55};   // dark halo
constexpr double ST[3] = {0.98, 0.70, 0.12};         // start node: amber (Curvz tail)
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
      m_tab_scroll("editor.tab_scroll"),
      m_tabs("editor.tabs", Gtk::Orientation::HORIZONTAL, 2),
      m_bar("editor.bar", Gtk::Orientation::HORIZONTAL, 8),
      m_title("editor.title"),
      m_context("editor.context"),
      m_fill("editor.fill"),
      m_grid("editor.grid"),
      m_snap("editor.snap"),
      m_pen("editor.tool_pen"),
      m_tool_pick("editor.tool_pick"),
      m_tool_measure("editor.tool_measure"),
      m_tool_hand("editor.tool_hand"),
      m_tools("editor.tools", Gtk::Orientation::VERTICAL, 2),
      m_work("editor.work", Gtk::Orientation::HORIZONTAL, 0),
      m_fit_button("editor.fit"),
      m_anchors("editor.anchors"),
      m_readout("editor.readout"),
      m_zoom("editor.zoom"),
      m_area("editor.canvas"),
      m_pick(ctx.pick) {
    add_css_class("editor");

    // ── tabs ───────────────────────────────────────────────────────────────
    m_tabs.add_css_class("editor-tabs");
    m_tab_scroll.set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::NEVER);
    m_tab_scroll.set_child(m_tabs);
    m_tab_scroll.set_visible(false);
    append(m_tab_scroll);

    // ── toolbar ────────────────────────────────────────────────────────────
    m_bar.add_css_class("overview-bar");   // the same strip the overview wears
    m_bar.add_css_class("editor-bar");
    m_title.set_xalign(0.0f);
    m_title.set_hexpand(true);
    m_title.set_ellipsize(Pango::EllipsizeMode::END);
    m_title.set_width_chars(10);
    m_context.set_label("Context");
    m_context.set_active(m_ctx.settings.editor_context);
    m_context.set_tooltip_text("Ghost a context glyph either side: H beside capitals and figures, n beside the rest");
    m_fill.set_label("Fill");
    m_fill.set_active(m_ctx.settings.editor_fill);
    m_fill.set_tooltip_text("Shade the outline");
    m_fit_button.set_label("Fit");
    m_fit_button.set_tooltip_text("Fit the glyph (Ctrl+0)");
    m_zoom.add_css_class("dim");
    m_zoom.set_width_chars(16);
    m_zoom.set_xalign(1.0f);
    m_bar.append(m_title);
    m_anchors.set_label("Anchors");
    m_anchors.set_tooltip_text("Add default anchors to this glyph (top / bottom; _top / _bottom on a mark) -- one undo step");
    m_anchors.signal_clicked().connect([this] {
        if (!glyph()) return;
        std::string why;
        if (!command::add_default_anchors(m_ctx.doc.journal, m_ctx.doc.font, {m_shown}, &why) && m_ctx.say)
            m_ctx.say(why, false);
        m_area.grab_focus();
    });
    m_bar.append(m_anchors);
    m_grid.set_label("Grid");
    m_grid.set_active(m_ctx.settings.editor_grid);
    m_grid.set_tooltip_text("A grid that follows the zoom: tenths of a unit close in, whole units drawn stronger");
    m_snap.set_label("Snap");
    m_snap.set_active(m_ctx.settings.editor_snap);
    m_snap.set_tooltip_text("Drags snap to the grid (when shown), the metric lines, the sidebearings and the other nodes. "
                            "Ctrl (fine drag) never snaps.");
    m_bar.append(m_fit_button);
    m_readout.add_css_class("dim");
    m_readout.set_width_chars(11);
    m_readout.set_xalign(1.0f);
    m_bar.append(m_readout);
    m_bar.append(m_zoom);
    append(m_bar);
    // The toggles ARE the preferences: flipping one is remembered.
    auto remember = [this](bool& field, bool v) {
        m_area.queue_draw();
        if (field == v) return;
        field = v;
        if (m_ctx.save_settings) m_ctx.save_settings();
        m_ctx.settings_changed.emit();
    };
    m_context.signal_toggled().connect([this, remember] { remember(m_ctx.settings.editor_context, m_context.get_active()); });
    m_fill.signal_toggled().connect([this, remember] { remember(m_ctx.settings.editor_fill, m_fill.get_active()); });
    m_grid.signal_toggled().connect([this, remember] {
        remember(m_ctx.settings.editor_grid, m_grid.get_active());
        update_zoom_label();
    });
    m_snap.signal_toggled().connect([this, remember] { remember(m_ctx.settings.editor_snap, m_snap.get_active()); });
    m_ctx.settings_changed.connect([this] {
        if (m_context.get_active() != m_ctx.settings.editor_context) m_context.set_active(m_ctx.settings.editor_context);
        if (m_fill.get_active() != m_ctx.settings.editor_fill) m_fill.set_active(m_ctx.settings.editor_fill);
        if (m_grid.get_active() != m_ctx.settings.editor_grid) m_grid.set_active(m_ctx.settings.editor_grid);
        if (m_snap.get_active() != m_ctx.settings.editor_snap) m_snap.set_active(m_ctx.settings.editor_snap);
        m_area.queue_draw();   // context names may have changed
    });
    m_fit_button.signal_clicked().connect([this] { fit(); m_area.grab_focus(); });

    // ── canvas ─────────────────────────────────────────────────────────────
    m_area.add_css_class("glyph-canvas");
    m_area.set_hexpand(true);
    m_area.set_vexpand(true);
    m_area.set_focusable(true);
    m_area.set_draw_func(sigc::mem_fun(*this, &GlyphCanvas::on_draw));

    // ── the tool panel (M5h, Scott) ────────────────────────────────────────
    // Tools are one-at-a-time (a toggle group); the view toggles below keep
    // their own state. Icons are Granjon's own symbolic SVGs (resources/icons):
    // the stock themes don't carry drawing tools on every desktop.
    m_tools.add_css_class("tool-panel");
    auto icon_button = [](Gtk::ToggleButton& b, const char* icon, const std::string& tip) {
        b.set_icon_name(icon);
        b.set_tooltip_text(tip);
        b.add_css_class("flat");
        b.add_css_class("tool");
        b.set_focus_on_click(false);
    };
    icon_button(m_tool_pick, "granjon-tool-pick-symbolic",
                "Pick (V)\nClick, Shift+click or drag a box to pick nodes, handles, anchors, components; drag to move. "
                "Double-click a segment: add a node; a node: corner / smooth.");
    icon_button(m_pen, "granjon-tool-pen-symbolic",
                "Pen (P)\nClick: a corner node. Drag: a smooth node (Alt: the out-handle alone; Shift: 45 degrees). "
                "Click the first node: close. Click an open path's end: go on. Enter / Escape: end the path.");
    icon_button(m_tool_measure, "granjon-tool-measure-symbolic",
                "Measure (hold M)\nDrag: distance and angle. Click: the stems through that point.");
    icon_button(m_tool_hand, "granjon-tool-hand-symbolic",
                "Hand (hold Space)\nDrag to pan. Ctrl+scroll zooms; Ctrl+0 fits.");
    m_pen.set_group(m_tool_pick);
    m_tool_measure.set_group(m_tool_pick);
    m_tool_hand.set_group(m_tool_pick);
    m_tool_pick.set_active(true);
    for (auto* b : {&m_tool_pick, &m_pen, &m_tool_measure, &m_tool_hand}) {
        b->signal_toggled().connect([this, b] { if (b->get_active()) on_tool_changed(); });
        m_tools.append(*b);
    }
    auto rule = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL);
    rule->set_margin_top(6);
    rule->set_margin_bottom(6);
    m_tools.append(*rule);
    icon_button(m_fill, "granjon-view-fill-symbolic", "Fill\nShade the outline. Remembered.");
    icon_button(m_context, "granjon-view-context-symbolic",
                "Context\nGhost a context glyph either side: H beside capitals and figures, n beside the rest. Remembered.");
    icon_button(m_grid, "granjon-view-grid-symbolic",
                "Grid\nA grid that follows the zoom: tenths of a unit close in, whole units drawn stronger. Remembered.");
    icon_button(m_snap, "granjon-view-snap-symbolic",
                "Snap\nDrags snap to the grid (when shown), the metric lines, the sidebearings and the other nodes. "
                "Ctrl (fine drag) never snaps. Remembered.");
    for (auto* b : {&m_fill, &m_context, &m_grid, &m_snap}) m_tools.append(*b);
    m_work.append(m_tools);
    m_work.append(m_area);
    m_work.set_vexpand(true);
    append(m_work);

    m_drag = Gtk::GestureDrag::create();
    m_drag->set_button(GDK_BUTTON_PRIMARY);
    m_drag->signal_drag_begin().connect([this](double x, double y) { on_press(x, y); });
    m_drag->signal_drag_update().connect([this](double dx, double dy) { on_drag(dx, dy); });
    m_drag->signal_drag_end().connect([this](double dx, double dy) { on_release(dx, dy); });
    m_area.add_controller(m_drag);

    m_click = Gtk::GestureClick::create();
    m_click->set_button(GDK_BUTTON_PRIMARY);
    m_click->signal_pressed().connect([this](int n, double x, double y) {
        if (n != 2) return;
        const model::Glyph* g = glyph();
        if (!g) return;
        const geom::Hit hit = geom::hit_test(*g, to_font(x, y), kHitPx / m_s);
        if (hit.kind == geom::Hit::Kind::Segment) {   // M5f: a node on the segment, the shape unchanged
            int at = -1;
            if (command::insert_node(m_ctx.doc.journal, m_ctx.doc.font, m_shown, hit.contour, hit.index, hit.t, &at) && at >= 0) {
                m_pick = {{hit.contour, at, Part::Node}};
                say_pick();
                m_area.queue_draw();
            }
            return;
        }
        if (hit.kind != geom::Hit::Kind::Node) return;
        // Corner <-> smooth (any other type goes to corner).
        const auto t = g->contours[hit.contour].nodes[hit.index].type;
        const auto to = t == model::Node::Type::Corner ? model::Node::Type::Smooth : model::Node::Type::Corner;
        command::set_node_type(m_ctx.doc.journal, m_ctx.doc.font, m_shown, {{hit.contour, hit.index, Part::Node}}, to);
        say_pick();
    });
    m_area.add_controller(m_click);

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
    m_ctx.pick_changed.connect([this] { m_area.queue_draw(); });   // the inspector can pick too
    refresh();
}

void GlyphCanvas::focus_canvas() {
    // Coming to the front (Ctrl+2, a double-click): the glyph in hand gets a tab.
    if (glyph() && std::find(m_tab_names.begin(), m_tab_names.end(), m_ctx.selected) == m_tab_names.end()) {
        m_tab_names.push_back(m_ctx.selected);
        rebuild_tabs();
    }
    m_area.grab_focus();
}

void GlyphCanvas::open_tabs(const std::vector<std::string>& names, const std::string& active) {
    for (const auto& n : names)
        if (m_ctx.doc.font.glyph(n) && std::find(m_tab_names.begin(), m_tab_names.end(), n) == m_tab_names.end())
            m_tab_names.push_back(n);
    rebuild_tabs();
    if (!active.empty()) m_ctx.select(active);
}

void GlyphCanvas::clear_tabs() {
    m_tab_names.clear();
    rebuild_tabs();
}

void GlyphCanvas::close_tab(const std::string& name) {
    const auto it = std::find(m_tab_names.begin(), m_tab_names.end(), name);
    if (it == m_tab_names.end()) return;
    const std::size_t at = static_cast<std::size_t>(it - m_tab_names.begin());
    m_tab_names.erase(it);
    rebuild_tabs();
    if (name == m_ctx.selected) {   // the front tab closed: its neighbour comes forward
        if (m_tab_names.empty()) m_ctx.select({});
        else m_ctx.select(m_tab_names[std::min(at, m_tab_names.size() - 1)]);
    }
}

void GlyphCanvas::rebuild_tabs() {
    while (auto* c = m_tabs.get_first_child()) m_tabs.remove(*c);
    for (const auto& n : m_tab_names) {
        auto tab = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
        tab->add_css_class("editor-tab");
        if (n == m_ctx.selected) tab->add_css_class("active");
        auto label = Gtk::make_managed<Gtk::Button>(n);
        label->set_has_frame(false);
        label->add_css_class("tab-label");
        label->signal_clicked().connect([this, n] { m_ctx.select(n); focus_canvas(); });
        auto close = Gtk::make_managed<Gtk::Button>("\u00D7");
        close->set_has_frame(false);
        close->add_css_class("tab-close");
        close->set_tooltip_text("Close this tab");
        close->signal_clicked().connect([this, n] { close_tab(n); });
        tab->append(*label);
        tab->append(*close);
        m_tabs.append(*tab);
    }
    m_tab_scroll.set_visible(!m_tab_names.empty());
}

const model::Glyph* GlyphCanvas::glyph() const {
    return m_ctx.selected.empty() ? nullptr : m_ctx.doc.font.glyph(m_ctx.selected);
}

// ── refresh / fit / zoom ──────────────────────────────────────────────────

void GlyphCanvas::refresh() {
    const model::Glyph* g = glyph();
    // Tabs: drop the ones whose glyph is gone (undo of an add, a rename).
    {
        const auto before = m_tab_names.size();
        std::erase_if(m_tab_names, [&](const std::string& n) { return !m_ctx.doc.font.glyph(n); });
        // Selected elsewhere WHILE the editor is in front: it joins the tabs.
        // (Browsing the overview or navigator does not collect tabs.)
        const bool joined = g && get_mapped() &&
                            std::find(m_tab_names.begin(), m_tab_names.end(), g->name) == m_tab_names.end();
        if (joined) m_tab_names.push_back(g->name);
        if (joined || before != m_tab_names.size() || m_ctx.selected != m_shown) rebuild_tabs();
    }
    if (m_ctx.selected != m_shown) {
        if (m_mode != Mode::None) cancel_drag();
        m_shown = m_ctx.selected;
        m_pick.clear();
        if (m_ctx.guide_pick && !m_ctx.guide_pick->owner.empty()) m_ctx.guide_pick.reset();
        m_ctx.pick_changed.emit();
        m_fitted = false;   // a new glyph: fit it on the next draw
    }
    if (g) {
        // Undo / redo may have taken points away: drop the stale ones.
        const std::size_t had = m_pick.size();
        for (auto it = m_pick.begin(); it != m_pick.end();)
            it = geom::valid(*g, *it) ? std::next(it) : m_pick.erase(it);
        if (m_ctx.guide_pick && !picked_guide()) { m_ctx.guide_pick.reset(); m_ctx.pick_changed.emit(); }
        if (m_pick.size() != had) m_ctx.pick_changed.emit();
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
    // fit() can run inside the draw (first draw after a glyph change), and a
    // label changed mid-snapshot doesn't relayout -- set it on the next idle.
    Glib::signal_idle().connect_once([this] {
        std::string t = std::to_string(static_cast<int>(std::lround(m_s * 100))) + "%";
        if (m_grid.get_active()) t += "  \u00B7  grid " + fmt(geom::grid_step(m_s));
        m_zoom.set_text(t);
    });
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
    // The preference is a glyph name ("H", "n.ss01") or a character ("Ж").
    const std::string& want = upper ? m_ctx.settings.context_upper : m_ctx.settings.context_lower;
    const auto& f = m_ctx.doc.font;
    const model::Glyph* c = f.glyph(want);
    if (!c) {
        std::vector<char32_t> cps;   // "Ж", "U+0416"
        if (unicode::parse_cps(want, cps) && cps.size() == 1) c = f.glyph_for(cps[0]);
    }
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

    if (m_grid.get_active()) draw_grid(cr, w, h);
    draw_guides(cr, w, h, *g);
    draw_user_guides(cr, w, h, *g);

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
        // Picked components: an accent outline.
        for (int i = 0; i < static_cast<int>(g->components.size()); ++i) {
            if (!m_pick.count({-1, i, Part::Component})) continue;
            model::Glyph one;
            one.components = {g->components[i]};
            font_path(one, 0);
            cr->set_source_rgba(NS[0], NS[1], NS[2], 0.18);
            cr->fill_preserve();
            cr->set_source_rgba(NS[0], NS[1], NS[2], 0.95);
            cr->set_line_width(2.0);
            cr->stroke();
        }
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

    draw_directions(cr, *g, w);
    draw_overlay(cr, *g);
    draw_anchors(cr, *g);

    // What the lead point snapped to, while dragging: a dashed accent line.
    if (m_mode == Mode::Points && (m_snap_x || m_snap_y)) {
        std::vector<double> dash{4.0, 3.0};
        cr->set_dash(dash, 0);
        cr->set_source_rgba(NS[0], NS[1], NS[2], 0.9);
        cr->set_line_width(1.0);
        if (m_snap_x) {
            const double x = std::round(to_screen({*m_snap_x, 0}).x) + 0.5;
            cr->move_to(x, 0);
            cr->line_to(x, h);
        }
        if (m_snap_y) {
            const double y = std::round(to_screen({0, *m_snap_y}).y) + 0.5;
            cr->move_to(0, y);
            cr->line_to(w, y);
        }
        cr->stroke();
        cr->unset_dash();
    }

    draw_measure(cr, *g);
    if (m_pen.get_active()) draw_pen(cr, *g);
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
    draw_rulers(cr, w, h);
}

// ── direction (M5d) ───────────────────────────────────────────────────────
// An arrowhead on every contour (on its first segment, and on any segment
// long enough on screen to carry one), pointing the way the contour runs.
// Contours running the wrong way for CFF (outers counter-clockwise, counters
// clockwise) are redrawn in red, with a note at the top of the canvas.
void GlyphCanvas::draw_directions(const Cairo::RefPtr<Cairo::Context>& cr, const model::Glyph& g, int w) {
    const auto fg = m_area.get_color();
    const auto wrong = geom::wrong_way(g.contours);
    const double WR[3] = {0.93, 0.33, 0.30};
    auto is_wrong = [&](int i) { return std::find(wrong.begin(), wrong.end(), i) != wrong.end(); };
    for (int ci = 0; ci < static_cast<int>(g.contours.size()); ++ci) {
        const auto& c = g.contours[ci];
        const int n = geom::segment_count(c);
        const bool bad = is_wrong(ci);
        if (bad) {   // the contour itself, red
            model::Glyph one;
            one.contours = {c};
            cr->begin_new_path();
            cr->save();
            cr->translate(m_ox, m_oy);
            cr->scale(m_s, -m_s);
            append_glyph_path(cr, m_ctx.doc.font, one);
            cr->restore();
            cr->set_source_rgba(WR[0], WR[1], WR[2], 0.95);
            cr->set_line_width(2.0);
            cr->stroke();
        }
        for (int s = 0; s < n; ++s) {
            const auto seg = geom::segment(c, s);
            const double chord = (to_screen(seg.p3) - to_screen(seg.p0)).length();
            if (s != 0 && chord < 90) continue;   // one per long segment, always one on the first
            if (chord < 14) continue;             // too small to read
            const double t = s == 0 ? 0.35 : 0.5;
            const geom::Vec2 p = to_screen(seg.at(t));
            geom::Vec2 d = seg.tangent(t);
            d = geom::Vec2{d.x, -d.y}.normalised();   // y flips on screen
            if (d.length() < 1e-9) continue;
            const geom::Vec2 nrm = d.perp();
            const double L = 7, W = 3.5;
            cr->move_to(p.x + d.x * L / 2, p.y + d.y * L / 2);
            cr->line_to(p.x - d.x * L / 2 + nrm.x * W, p.y - d.y * L / 2 + nrm.y * W);
            cr->line_to(p.x - d.x * L / 2 - nrm.x * W, p.y - d.y * L / 2 - nrm.y * W);
            cr->close_path();
            if (bad) cr->set_source_rgba(WR[0], WR[1], WR[2], 1.0);
            else     cr->set_source_rgba(fg.get_red(), fg.get_green(), fg.get_blue(), 0.7);
            cr->fill();
        }
    }
    if (!wrong.empty()) {
        Pango::FontDescription small;
        small.set_size(9 * PANGO_SCALE);
        auto layout = m_area.create_pango_layout(
            std::to_string(wrong.size()) + (wrong.size() == 1 ? " contour runs" : " contours run") +
            " the wrong way -- Correct directions in the inspector");
        layout->set_font_description(small);
        int lw = 0, lh = 0;
        layout->get_pixel_size(lw, lh);
        const double x = std::max(kRuler + 8, (w - lw) / 2.0), y = kRuler + 8;
        cr->set_source_rgba(0, 0, 0, 0.55);
        cr->rectangle(x - 6, y - 3, lw + 12, lh + 6);
        cr->fill();
        cr->set_source_rgb(WR[0], WR[1], WR[2]);
        cr->move_to(x, y);
        layout->show_in_cairo_context(cr);
    }
}

// ── guides, rulers, measure (M5c) ─────────────────────────────────────────

const model::Guide* GlyphCanvas::picked_guide() const {
    if (!m_ctx.guide_pick) return nullptr;
    const auto& r = *m_ctx.guide_pick;
    const std::vector<model::Guide>* gs = nullptr;
    if (r.owner.empty()) gs = &m_ctx.doc.font.info().guides;
    else if (const model::Glyph* g = m_ctx.doc.font.glyph(r.owner)) gs = &g->guides;
    if (!gs || r.index < 0 || r.index >= static_cast<int>(gs->size())) return nullptr;
    return &(*gs)[r.index];
}

std::optional<ViewContext::GuideRef> GlyphCanvas::guide_at(double sx, double sy) const {
    const model::Glyph* g = glyph();
    if (!g) return std::nullopt;
    const geom::Vec2 p = to_font(sx, sy);
    const double tol = 4.0 / m_s;
    std::optional<ViewContext::GuideRef> best;
    double best_d = tol;
    auto scan = [&](const std::vector<model::Guide>& gs, const std::string& owner) {
        for (int i = 0; i < static_cast<int>(gs.size()); ++i) {
            const double d = geom::guide_distance(gs[i], p);
            if (d <= best_d) { best_d = d; best = ViewContext::GuideRef{owner, i}; }
        }
    };
    scan(m_ctx.doc.font.info().guides, {});
    scan(g->guides, g->name);   // the glyph's own win a tie (drawn on top)
    return best;
}

geom::Vec2 GlyphCanvas::snap_to_node(geom::Vec2 p) const {
    const model::Glyph* g = glyph();
    if (!g) return p;
    const double tol = kHitPx / m_s;
    geom::Vec2 best = p;
    double best_d = tol;
    for (const auto& c : geom::all_contours(m_ctx.doc.font, *g))
        for (const auto& n : c.nodes) {
            const double d = geom::Vec2{n.x, n.y}.dist(p);
            if (d < best_d) { best_d = d; best = {n.x, n.y}; }
        }
    return best;
}

void GlyphCanvas::draw_user_guides(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h, const model::Glyph& g) {
    const model::Guide* picked = picked_guide();
    Pango::FontDescription small;
    small.set_size(8 * PANGO_SCALE);
    auto line = [&](const model::Guide& gd, const double* col, bool hot) {
        cr->set_source_rgba(col[0], col[1], col[2], hot ? 0.95 : 0.6);
        cr->set_line_width(hot ? 2.0 : 1.0);
        if (gd.horizontal()) {
            const double y = std::round(to_screen({0, gd.y}).y) + 0.5;
            cr->move_to(0, y);
            cr->line_to(w, y);
        } else if (gd.vertical()) {
            const double x = std::round(to_screen({gd.x, 0}).x) + 0.5;
            cr->move_to(x, 0);
            cr->line_to(x, h);
        } else {   // angled: through (x, y), long enough to cross the view
            const double a = gd.angle * M_PI / 180.0;
            const geom::Vec2 dir{std::cos(a), std::sin(a)};
            const double far = (w + h) / m_s;
            const geom::Vec2 p0 = to_screen(geom::Vec2{gd.x, gd.y} - dir * far);
            const geom::Vec2 p1 = to_screen(geom::Vec2{gd.x, gd.y} + dir * far);
            cr->move_to(p0.x, p0.y);
            cr->line_to(p1.x, p1.y);
        }
        cr->stroke();
        if (!gd.name.empty()) {
            auto layout = m_area.create_pango_layout(gd.name);
            layout->set_font_description(small);
            cr->set_source_rgba(col[0], col[1], col[2], 0.9);
            if (gd.vertical()) cr->move_to(to_screen({gd.x, 0}).x + 4, kRuler + 4);
            else               cr->move_to(w - 8 - 7.0 * gd.name.size(), to_screen({gd.x, gd.y}).y - 13);
            layout->show_in_cairo_context(cr);
        }
    };
    for (const auto& gd : m_ctx.doc.font.info().guides) line(gd, GG, &gd == picked);
    for (const auto& gd : g.guides) line(gd, GF, &gd == picked);
    if (m_mode == Mode::Guide && m_guide_new) {   // the one being dragged out of a ruler
        model::Guide gd;
        if (m_guide_h) gd.y = m_guide_preview.y;
        else { gd.x = m_guide_preview.x; gd.angle = 90; }
        line(gd, GF, true);
    }
}

void GlyphCanvas::draw_rulers(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
    const auto fg = m_area.get_color();
    const double r = fg.get_red(), gr = fg.get_green(), b = fg.get_blue();
    // The strips: the canvas colour darkened a touch by the foreground.
    cr->set_source_rgba(r, gr, b, 0.07);
    cr->rectangle(0, 0, w, kRuler);
    cr->rectangle(0, kRuler, kRuler, h - kRuler);
    cr->fill();
    cr->set_source_rgba(r, gr, b, 0.25);
    cr->set_line_width(1.0);
    cr->move_to(0, kRuler - 0.5); cr->line_to(w, kRuler - 0.5);
    cr->move_to(kRuler - 0.5, kRuler); cr->line_to(kRuler - 0.5, h);
    cr->stroke();
    // Ticks: the grid's step, labels where they fit (>= 60 px apart).
    const double step = geom::grid_step(m_s, 6.0);
    const double label = geom::grid_step(m_s, 60.0);
    Pango::FontDescription small;
    small.set_size(7 * PANGO_SCALE);
    auto ticks = [&](bool top) {
        const geom::Vec2 lo = to_font(kRuler, h), hi = to_font(w, kRuler);
        const double from = top ? lo.x : lo.y, to = top ? hi.x : hi.y;
        const long k0 = static_cast<long>(std::floor(from / step)), k1 = static_cast<long>(std::ceil(to / step));
        if (k1 - k0 > 3000) return;
        for (long k = k0; k <= k1; ++k) {
            const double v = k * step;
            const bool lab = std::abs(v / label - std::round(v / label)) < 1e-6;
            const bool mid = !lab && std::abs(v / (label / 2) - std::round(v / (label / 2))) < 1e-6;
            const double len = lab ? 9 : mid ? 6 : 3;
            if (top) {
                const double x = std::round(to_screen({v, 0}).x) + 0.5;
                if (x < kRuler) continue;
                cr->move_to(x, kRuler);
                cr->line_to(x, kRuler - len);
            } else {
                const double y = std::round(to_screen({0, v}).y) + 0.5;
                if (y < kRuler) continue;
                cr->move_to(kRuler, y);
                cr->line_to(kRuler - len, y);
            }
            cr->set_source_rgba(r, gr, b, 0.45);
            cr->stroke();
            if (lab) {
                auto layout = m_area.create_pango_layout(fmt(v));
                layout->set_font_description(small);
                cr->set_source_rgba(r, gr, b, 0.65);
                if (top) {
                    cr->move_to(to_screen({v, 0}).x + 2, 1);
                    layout->show_in_cairo_context(cr);
                } else {
                    int lw = 0, lh = 0;
                    layout->get_pixel_size(lw, lh);
                    cr->save();
                    cr->move_to(1, to_screen({0, v}).y - 2);
                    cr->rotate(-M_PI / 2);
                    layout->show_in_cairo_context(cr);
                    cr->restore();
                }
            }
        }
    };
    ticks(true);
    ticks(false);
}

void GlyphCanvas::draw_measure(const Cairo::RefPtr<Cairo::Context>& cr, const model::Glyph&) {
    if (!m_measure) return;
    Pango::FontDescription small;
    small.set_size(8 * PANGO_SCALE);
    small.set_weight(Pango::Weight::BOLD);
    const double MC[3] = {0.98, 0.62, 0.20};   // measure: amber
    auto tag = [&](const std::string& s, geom::Vec2 at) {
        auto layout = m_area.create_pango_layout(s);
        layout->set_font_description(small);
        int lw = 0, lh = 0;
        layout->get_pixel_size(lw, lh);
        cr->set_source_rgba(0, 0, 0, 0.7);
        cr->rectangle(at.x - 3, at.y - 2, lw + 6, lh + 4);
        cr->fill();
        cr->set_source_rgb(MC[0], MC[1], MC[2]);
        cr->move_to(at.x, at.y);
        layout->show_in_cairo_context(cr);
    };
    auto seg = [&](geom::Vec2 a, geom::Vec2 b) {
        const auto sa = to_screen(a), sb = to_screen(b);
        cr->set_source_rgb(MC[0], MC[1], MC[2]);
        cr->set_line_width(1.5);
        cr->move_to(sa.x, sa.y);
        cr->line_to(sb.x, sb.y);
        cr->stroke();
        for (const auto& e : {sa, sb}) { cr->arc(e.x, e.y, 3, 0, 2 * M_PI); cr->fill(); }
    };
    if (m_meas_dragged) {
        const geom::Vec2 d = m_meas_b - m_meas_a;
        seg(m_meas_a, m_meas_b);
        std::ostringstream o;
        o << fmt(std::round(d.length() * 10) / 10) << "   " << fmt(std::round(std::atan2(d.y, d.x) * 1800 / M_PI) / 10)
          << "\u00B0   \u0394 " << fmt(std::round(d.x)) << ", " << fmt(std::round(d.y));
        tag(o.str(), to_screen(m_meas_b) + geom::Vec2{10, -18});
    }
    if (m_stem_h) {
        seg({m_stem_h->first, m_stem_at.y}, {m_stem_h->second, m_stem_at.y});
        tag(fmt(std::round((m_stem_h->second - m_stem_h->first) * 10) / 10),
            to_screen({(m_stem_h->first + m_stem_h->second) / 2, m_stem_at.y}) + geom::Vec2{-10, -20});
    }
    if (m_stem_v) {
        seg({m_stem_at.x, m_stem_v->first}, {m_stem_at.x, m_stem_v->second});
        // At the top end, right of the line -- clear of the horizontal stem's tag.
        tag(fmt(std::round((m_stem_v->second - m_stem_v->first) * 10) / 10),
            to_screen({m_stem_at.x, m_stem_v->second}) + geom::Vec2{8, -4});
    }
}

// The grid follows the zoom: tenths of a unit close in, 20s / 50s far out.
// Every tenth line is stronger; when the step is under a unit, whole units are
// stronger still -- the "obvious way to tell whole values" (Scott).
void GlyphCanvas::draw_grid(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
    const auto fg = m_area.get_color();
    const double r = fg.get_red(), gr = fg.get_green(), b = fg.get_blue();
    const double step = geom::grid_step(m_s);
    const geom::Vec2 lo = to_font(0, h), hi = to_font(w, 0);
    const long x0 = static_cast<long>(std::floor(lo.x / step)), x1 = static_cast<long>(std::ceil(hi.x / step));
    const long y0 = static_cast<long>(std::floor(lo.y / step)), y1 = static_cast<long>(std::ceil(hi.y / step));
    if ((x1 - x0) + (y1 - y0) > 4000) return;
    auto alpha = [&](long k) {
        if (k % 10 == 0) return 0.13;                                  // major
        const double v = k * step;
        if (step < 1 && std::abs(v - std::round(v)) < 1e-9) return 0.10;   // a whole unit
        return 0.045;
    };
    cr->set_line_width(1.0);
    for (long k = x0; k <= x1; ++k) {
        const double x = std::round(to_screen({k * step, 0}).x) + 0.5;
        cr->set_source_rgba(r, gr, b, alpha(k));
        cr->move_to(x, 0);
        cr->line_to(x, h);
        cr->stroke();
    }
    for (long k = y0; k <= y1; ++k) {
        const double y = std::round(to_screen({0, k * step}).y) + 0.5;
        cr->set_source_rgba(r, gr, b, alpha(k));
        cr->move_to(0, y);
        cr->line_to(w, y);
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

    // The start node (M5e): an amber diamond, a touch larger, with a faint
    // ring -- Curvz's tail colour, so the start reads in a dense outline.
    auto draw_start = [&](Vec2 p, bool picked) {
        const double rr = (picked ? nr_sel : nr) * 1.3;
        cr->set_source_rgba(ST[0], ST[1], ST[2], 0.35);
        cr->set_line_width(1.0);
        cr->begin_new_path();   // an arc joins any current point (the canvas note leaves one)
        cr->arc(p.x, p.y, rr * 2.4, 0, 2 * M_PI);
        cr->stroke();
        cr->set_source_rgba(NB[0], NB[1], NB[2], NB[3]);
        build(p, 4, rr + 1.2);
        cr->fill();
        cr->set_source_rgb(ST[0], ST[1], ST[2]);
        build(p, 4, rr);
        cr->fill_preserve();
        if (picked) { cr->set_source_rgb(NS[0], NS[1], NS[2]); cr->set_line_width(2.0); }
        else        { cr->set_source_rgb(NK[0], NK[1], NK[2]); cr->set_line_width(1.2); }
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
            const bool picked = m_pick.count({c, i, Part::Node}) > 0;
            if (i == 0) draw_start(to_screen({n.x, n.y}), picked);
            else        draw_node(to_screen({n.x, n.y}), shape, picked);
        }
        // An open contour's END: a short bar across the path -- a loose end.
        if (!g.contours[c].closed && nodes.size() >= 2) {
            const auto& e = nodes.back();
            const auto& p = nodes[nodes.size() - 2];
            const Vec2 a = to_screen({e.x, e.y});
            Vec2 from = e.has_in() ? to_screen({e.cx1, e.cy1}) : to_screen({p.x, p.y});
            Vec2 t{a.x - from.x, a.y - from.y};
            const double len = std::hypot(t.x, t.y);
            if (len > 1e-6) {
                t = {t.x / len, t.y / len};
                const Vec2 nrm{-t.y, t.x};
                const double half = 6.0;
                cr->set_line_width(2.0);
                cr->set_source_rgba(ST[0], ST[1], ST[2], 1.0);
                cr->move_to(a.x + t.x * 5 - nrm.x * half, a.y + t.y * 5 - nrm.y * half);
                cr->line_to(a.x + t.x * 5 + nrm.x * half, a.y + t.y * 5 + nrm.y * half);
                cr->stroke();
            }
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

    // Measure (hold M): measures, never edits.
    if (m_measure) {
        m_mode = Mode::Measure;
        m_meas_a = m_meas_b = snap_to_node(to_font(x, y));
        m_meas_dragged = false;
        m_stem_h.reset();
        m_stem_v.reset();
        m_area.queue_draw();
        return;
    }
    // The rulers: drag a new guide out (top: horizontal, left: vertical).
    if (y < kRuler || x < kRuler) {
        if (y < kRuler && x < kRuler) { m_mode = Mode::None; return; }   // the corner
        m_mode = Mode::Guide;
        m_guide_new = true;
        m_guide_h = y < kRuler;
        m_guide_preview = to_font(x, y);
        m_area.queue_draw();
        return;
    }
    m_guide_new = false;

    // The pen (M5g): every press puts a node in (or closes / picks up a path).
    if (m_pen.get_active()) {
        const auto pst = m_drag->get_current_event_state();
        const bool pshift = (pst & Gdk::ModifierType::SHIFT_MASK) == Gdk::ModifierType::SHIFT_MASK;
        geom::Vec2 p = to_font(x, y);
        const double tol = kHitPx / m_s;
        auto& doc = m_ctx.doc;
        if (pen_path_live()) {
            const auto& c = g->contours[static_cast<std::size_t>(m_pen_contour)];
            const auto& first = m_pen_from_start ? c.nodes.back() : c.nodes.front();
            const auto& last = m_pen_from_start ? c.nodes.front() : c.nodes.back();
            const bool on_first = c.nodes.size() >= 2 && (geom::Vec2{first.x, first.y} - p).length() <= tol;
            if (on_first && !m_pen_from_start) {
                const auto at = command::pen_press(doc.journal, doc.font, m_shown, m_pen_contour, p, true);
                m_pen_at_contour = at.contour; m_pen_at_node = at.node; m_pen_at_closed = at.closed;
            } else {
                if (pshift) p = geom::Vec2{last.x, last.y} + geom::constrain45(p - geom::Vec2{last.x, last.y});
                const auto at = command::pen_press(doc.journal, doc.font, m_shown, m_pen_contour, p, false, m_pen_from_start);
                m_pen_from_start = false;
                m_pen_at_contour = at.contour; m_pen_at_node = at.node; m_pen_at_closed = at.closed;
            }
        } else {
            // On an open path's end: pick it up and go on from there (no node yet).
            const geom::Hit hit = geom::hit_test(*g, p, tol, false);
            if (hit.kind == geom::Hit::Kind::Node && !g->contours[static_cast<std::size_t>(hit.contour)].closed) {
                const int n = static_cast<int>(g->contours[static_cast<std::size_t>(hit.contour)].nodes.size());
                if (hit.index == n - 1 || hit.index == 0) {
                    m_pen_glyph = m_shown;
                    m_pen_contour = hit.contour;
                    m_pen_from_start = hit.index == 0 && n > 1;
                    m_pick = {{hit.contour, hit.index, Part::Node}};
                    m_mode = Mode::None;
                    if (m_ctx.say) m_ctx.say("Pen: going on from this end.", false);
                    say_pick();
                    m_area.queue_draw();
                    return;
                }
            }
            const auto at = command::pen_press(doc.journal, doc.font, m_shown, -1, p);
            m_pen_at_contour = at.contour; m_pen_at_node = at.node; m_pen_at_closed = at.closed;
        }
        m_began = doc.journal.in_gesture();
        if (!m_began) { m_mode = Mode::None; return; }
        m_mode = Mode::Pen;
        m_pen_glyph = m_shown;
        m_pen_contour = m_pen_at_closed ? -1 : m_pen_at_contour;
        m_pick = {{m_pen_at_contour, m_pen_at_node, Part::Node}};
        m_area.queue_draw();
        return;
    }

    const auto st = m_drag->get_current_event_state();
    const bool shift = (st & Gdk::ModifierType::SHIFT_MASK) == Gdk::ModifierType::SHIFT_MASK;
    const geom::Hit hit = geom::hit_test(*g, to_font(x, y), kHitPx / m_s);
    m_press_glyph = *g;
    m_lead = {};   // invalid: nothing snaps unless a point was grabbed
    m_unpick.reset();

    auto pick_one = [&](PointRef ref) {
        m_ctx.guide_pick.reset();
        if (ref.part != Part::Component) m_lead = ref;   // a component's offset does not snap
        if (shift) {
            // Shift on a picked one: a Shift-DRAG constrains the move; only a
            // Shift-CLICK (no drag by the release) drops it from the pick.
            if (m_pick.count(ref)) m_unpick = ref;
            else m_pick.insert(ref);
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
    } else if (hit.kind == K::Segment || hit.kind == K::None) {
        if (hit.kind == K::None) {
            // A guide under the pointer: pick it (drag moves it; drop it on a ruler to delete).
            if (const auto gr = guide_at(x, y)) {
                m_pick.clear();
                m_ctx.guide_pick = gr;
                if (const model::Guide* gd = picked_guide()) m_guide_press = *gd;
                m_mode = Mode::Guide;
                say_pick();
                m_area.queue_draw();
                return;
            }
            // Inside a component's drawing: pick the whole component.
            const int ci = geom::component_at(m_ctx.doc.font, *g, to_font(x, y));
            if (ci >= 0) {
                pick_one({-1, ci, Part::Component});
                say_pick();
                m_area.queue_draw();
                return;
            }
            m_mode = Mode::Marquee;
            m_press_pick = shift ? m_pick : geom::Points{};
            if (!shift) m_pick.clear();
            m_ctx.guide_pick.reset();
            say_pick();
            m_area.queue_draw();
            return;
        }
        const int n = static_cast<int>(g->contours[hit.contour].nodes.size());
        const PointRef a{hit.contour, hit.index, Part::Node};
        const PointRef b{hit.contour, (hit.index + 1) % n, Part::Node};
        if (!shift && !(m_pick.count(a) && m_pick.count(b))) m_pick.clear();
        m_pick.insert(a);
        m_pick.insert(b);
        m_lead = a;
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
    if (m_mode == Mode::Pen) {
        const auto st = m_drag->get_current_event_state();
        const bool shift = (st & Gdk::ModifierType::SHIFT_MASK) == Gdk::ModifierType::SHIFT_MASK;
        const bool alt = (st & Gdk::ModifierType::ALT_MASK) == Gdk::ModifierType::ALT_MASK;
        geom::Vec2 d{dx / m_s, -dy / m_s};
        if (std::hypot(dx, dy) < kDragPx) d = {0, 0};
        else if (shift) d = geom::constrain45(d);
        command::pen_drag(m_ctx.doc.journal, m_ctx.doc.font, m_shown,
                          {m_pen_at_contour, m_pen_at_node, m_pen_at_closed}, d, alt);
        m_mx = m_press_x + dx; m_my = m_press_y + dy;
        return;
    }
    switch (m_mode) {
        case Mode::Measure:
            m_meas_b = snap_to_node(to_font(m_press_x + dx, m_press_y + dy));
            m_meas_dragged = m_meas_dragged || std::hypot(dx, dy) >= kDragPx;
            m_area.queue_draw();
            return;
        case Mode::Guide: {
            if (m_guide_new) {
                m_guide_preview = to_font(m_press_x + dx, m_press_y + dy);
                m_area.queue_draw();
                return;
            }
            auto& doc = m_ctx.doc;
            if (!m_ctx.guide_pick) return;
            if (!m_began) {
                if (std::hypot(dx, dy) < kDragPx) return;
                command::guide_drag_begin(doc.journal, doc.font, m_ctx.guide_pick->owner, m_ctx.guide_pick->index);
                m_began = doc.journal.in_gesture();
                if (!m_began) return;
            }
            model::Guide gd = m_guide_press;
            const geom::Vec2 d{dx / m_s, -dy / m_s};
            if (gd.horizontal())    gd.y += d.y;
            else if (gd.vertical()) gd.x += d.x;
            else { gd.x += d.x; gd.y += d.y; }
            // Snap a straight guide onto a node / metric line / the grid.
            if (m_snap.get_active() && (gd.horizontal() || gd.vertical())) {
                std::vector<double> t;
                const auto& m = doc.font.metrics();
                if (gd.horizontal()) t = {0.0, double(m.x_height), double(m.cap_height), double(m.ascender), double(m.descender)};
                else                 t = {0.0, m_press_glyph.advance};
                if (const model::Glyph* g = glyph())
                    for (const auto& c : g->contours)
                        for (const auto& n : c.nodes) t.push_back(gd.horizontal() ? n.y : n.x);
                const double grid = m_grid.get_active() ? std::max(1.0, geom::grid_step(m_s)) : 0.0;
                double& v = gd.horizontal() ? gd.y : gd.x;
                v = geom::snap_axis(v, t, grid, kHitPx / m_s);
            }
            command::set_guide(doc.journal, doc.font, m_ctx.guide_pick->owner, m_ctx.guide_pick->index, gd);
            m_readout.set_text(gd.horizontal() ? "y " + fmt(std::round(gd.y)) : gd.vertical() ? "x " + fmt(std::round(gd.x))
                                                 : fmt(std::round(gd.x)) + ", " + fmt(std::round(gd.y)));
            return;
        }
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
                const auto st = m_drag->get_current_event_state();
                auto has = [&](Gdk::ModifierType m) { return (st & m) == m; };
                const bool shift = has(Gdk::ModifierType::SHIFT_MASK);
                const bool alt   = has(Gdk::ModifierType::ALT_MASK);
                const bool ctrl  = has(Gdk::ModifierType::CONTROL_MASK);
                bool handles_only = !m_pick.empty();
                for (const auto& p : m_pick)
                    if (p.part != Part::In && p.part != Part::Out) handles_only = false;
                geom::Vec2 d{fdx, fdy};
                if (ctrl) d = d * 0.1;   // fine drag
                bool lock_x = false, lock_y = false;   // the axis Shift holds still
                if (shift) {
                    if (handles_only && m_pick.size() == 1 && geom::valid(m_press_glyph, *m_pick.begin())) {
                        // A handle locks to horizontal / vertical FROM ITS NODE (Curvz).
                        const auto& p = *m_pick.begin();
                        const auto& n = m_press_glyph.contours[p.contour].nodes[p.index];
                        const geom::Vec2 a{n.x, n.y}, h0 = geom::position(m_press_glyph, p);
                        const geom::Vec2 v = geom::axis_lock(h0 + d - a);
                        d = a + v - h0;
                        lock_x = v.x == 0;
                        lock_y = v.y == 0;
                    } else {
                        d = geom::axis_lock(d);
                        lock_x = d.x == 0;
                        lock_y = d.y == 0;
                    }
                }
                // Snap the lead point (the one that was under the pointer).
                m_snap_x.reset();
                m_snap_y.reset();
                if (m_snap.get_active() && !ctrl && geom::valid(m_press_glyph, m_lead)) {
                    const auto& m = doc.font.metrics();
                    const double os = m.overshoot;
                    std::vector<double> xs{0.0, m_press_glyph.advance};
                    std::vector<double> ys{0.0, double(m.x_height), double(m.cap_height), double(m.ascender),
                                           double(m.descender), -os, m.x_height + os, m.cap_height + os};
                    for (int ci = 0; ci < static_cast<int>(m_press_glyph.contours.size()); ++ci)
                        for (int ni = 0; ni < static_cast<int>(m_press_glyph.contours[ci].nodes.size()); ++ni) {
                            if (m_pick.count({ci, ni, Part::Node})) continue;   // moving with us
                            const auto& n = m_press_glyph.contours[ci].nodes[ni];
                            xs.push_back(n.x);
                            ys.push_back(n.y);
                        }
                    for (const std::vector<model::Guide>* gs : std::initializer_list<const std::vector<model::Guide>*>{&doc.font.info().guides, &m_press_glyph.guides})
                        for (const auto& gd : *gs) {
                            if (gd.horizontal()) ys.push_back(gd.y);
                            else if (gd.vertical()) xs.push_back(gd.x);
                        }
                    const double grid = m_grid.get_active() ? std::max(1.0, geom::grid_step(m_s)) : 0.0;
                    const double tol = kHitPx / m_s;
                    const geom::Vec2 p0 = geom::position(m_press_glyph, m_lead);
                    geom::Vec2 p = p0 + d;
                    if (!lock_x) p.x = geom::snap_axis(p.x, xs, grid, tol, &m_snap_x);
                    if (!lock_y) p.y = geom::snap_axis(p.y, ys, grid, tol, &m_snap_y);
                    d = p - p0;
                }
                command::drag_points(doc.journal, doc.font, m_pick, d.x, d.y, alt && handles_only);
                std::ostringstream o;
                o << "\u0394 " << fmt(std::round(d.x)) << ", " << fmt(std::round(d.y));
                m_readout.set_text(o.str());
                m_area.queue_draw();
            } else {
                const auto st = m_drag->get_current_event_state();
                const auto& mod = m_ctx.settings.sidebearing_mod;
                const auto mask = mod == "ctrl"    ? Gdk::ModifierType::CONTROL_MASK
                                  : mod == "shift" ? Gdk::ModifierType::SHIFT_MASK
                                                   : Gdk::ModifierType::ALT_MASK;
                const bool alt = (st & mask) == mask;   // "slide the drawing" modifier (a preference)
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

void GlyphCanvas::on_release(double dx, double dy) {
    const Mode was = m_mode;
    m_mode = Mode::None;
    if (was == Mode::Pen) {
        command::drag_end(m_ctx.doc.journal, m_ctx.doc.font);
        m_began = false;
        if (m_ctx.say) {
            if (m_pen_at_closed) m_ctx.say("Path closed. Click to start another; P or Escape leaves the pen.", false);
            else m_ctx.say("Pen: node " + std::to_string(m_pen_at_node + 1) +
                           " -- click the first node to close, Enter to end the path.", false);
        }
        m_area.queue_draw();
        return;
    }
    if (was == Mode::Measure) {
        if (!m_meas_dragged) {   // a click: the stems through that point
            if (const model::Glyph* g = glyph()) {
                const auto cs = geom::all_contours(m_ctx.doc.font, *g);
                m_stem_at = to_font(m_press_x, m_press_y);
                m_stem_h = geom::stem_at(cs, m_stem_at, true);
                m_stem_v = geom::stem_at(cs, m_stem_at, false);
            }
        }
        m_area.queue_draw();
        return;
    }
    if (was == Mode::Guide) {
        auto& doc = m_ctx.doc;
        const double rx = m_press_x + dx, ry = m_press_y + dy;
        const bool on_ruler = rx < kRuler || ry < kRuler;
        if (m_guide_new) {
            m_guide_new = false;
            if (!on_ruler && glyph()) {
                const geom::Vec2 p = to_font(rx, ry);
                model::Guide gd;
                if (m_guide_h) { gd.y = p.y; gd.angle = 0; }
                else           { gd.x = p.x; gd.angle = 90; }
                if (command::add_guide(doc.journal, doc.font, m_shown, gd)) {
                    m_pick.clear();
                    m_ctx.guide_pick = ViewContext::GuideRef{m_shown, static_cast<int>(glyph()->guides.size()) - 1};
                }
            }
        } else if (m_began) {
            if (on_ruler && m_ctx.guide_pick) {   // dropped back on a ruler: deleted
                command::drag_cancel(doc.journal);
                command::remove_guide(doc.journal, doc.font, m_ctx.guide_pick->owner, m_ctx.guide_pick->index);
                m_ctx.guide_pick.reset();
            } else {
                command::drag_end(doc.journal, doc.font);
            }
        }
        m_began = false;
        m_readout.set_text("");
        say_pick();
        m_area.queue_draw();
        return;
    }
    if (m_unpick && !m_began) m_pick.erase(*m_unpick);   // it was a Shift-click, not a drag
    m_unpick.reset();
    if ((was == Mode::Points || was == Mode::Sidebearing) && m_began) {
        auto& doc = m_ctx.doc;
        command::drag_end(doc.journal, doc.font);   // snaps, then one step
        if (const model::Glyph* g = glyph(); g && was == Mode::Sidebearing && m_side == 0)
            m_ox = m_press_ox + (m_press_advance - g->advance) * m_s;
    }
    m_began = false;
    m_side = -1;
    m_snap_x.reset();
    m_snap_y.reset();
    m_readout.set_text("");
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

    if ((keyval == GDK_KEY_p || keyval == GDK_KEY_P) && !ctrl && m_mode == Mode::None) {
        set_pen(!m_pen.get_active());
        return true;
    }
    if ((keyval == GDK_KEY_v || keyval == GDK_KEY_V) && !ctrl && m_mode == Mode::None) {
        set_tool(Tool::Pick);
        return true;
    }
    if (keyval == GDK_KEY_Escape && m_mode == Mode::None && (tool() == Tool::Measure || tool() == Tool::Hand)) {
        set_tool(Tool::Pick);
        return true;
    }
    if ((keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) && m_pen.get_active() && m_mode == Mode::None) {
        end_path();
        return true;
    }
    if (keyval == GDK_KEY_Escape && m_pen.get_active() && m_mode == Mode::None) {
        if (pen_path_live()) end_path();
        else set_pen(false);
        return true;
    }
    if (keyval == GDK_KEY_Escape) {
        if (m_mode != Mode::None) cancel_drag();
        else if (m_pick.empty() && !m_ctx.guide_pick) {
            // Nothing left to drop: give the focus back (the canvas keeps Tab).
            if (auto* root = dynamic_cast<Gtk::Widget*>(m_area.get_root()))
                root->child_focus(Gtk::DirectionType::TAB_FORWARD);
        }
        else { m_pick.clear(); m_ctx.guide_pick.reset(); say_pick(); m_area.queue_draw(); }
        return true;
    }
    // M5e traversal: Tab / Shift+Tab walk the nodes, Ctrl jumps contours;
    // ` / Shift+` walk nodes AND handles (Alt+Tab belongs to the desktop).
    {
        const bool tab   = keyval == GDK_KEY_Tab || keyval == GDK_KEY_KP_Tab || keyval == GDK_KEY_ISO_Left_Tab;
        const bool grave = keyval == GDK_KEY_grave || keyval == GDK_KEY_asciitilde || keyval == GDK_KEY_dead_grave;
        if ((tab || grave) && m_mode == Mode::None) {
            const int dir = (shift || keyval == GDK_KEY_ISO_Left_Tab || keyval == GDK_KEY_asciitilde) ? -1 : 1;
            step_pick(dir, tab && ctrl, grave);
            return true;
        }
    }
    if ((keyval == GDK_KEY_f || keyval == GDK_KEY_F) && !ctrl && !shift && m_mode == Mode::None) {
        make_first();
        return true;
    }
    if ((keyval == GDK_KEY_m || keyval == GDK_KEY_M) && !ctrl) {   // hold M: measure
        if (!m_measure) { m_measure = true; m_area.set_cursor("crosshair"); m_area.queue_draw(); }
        return true;
    }
    if ((keyval == GDK_KEY_Delete || keyval == GDK_KEY_KP_Delete || keyval == GDK_KEY_BackSpace) && !ctrl &&
        m_ctx.guide_pick && m_mode == Mode::None) {
        command::remove_guide(m_ctx.doc.journal, m_ctx.doc.font, m_ctx.guide_pick->owner, m_ctx.guide_pick->index);
        m_ctx.guide_pick.reset();
        say_pick();
        return true;
    }
    if ((keyval == GDK_KEY_Delete || keyval == GDK_KEY_KP_Delete || keyval == GDK_KEY_BackSpace) && !ctrl) {
        if (!g || m_pick.empty() || m_mode != Mode::None) return true;
        std::string why;
        if (command::delete_points(m_ctx.doc.journal, m_ctx.doc.font, m_shown, m_pick, &why)) {
            m_pick.clear();
            say_pick();
            if (m_ctx.say) m_ctx.say(m_ctx.doc.journal.undo_label() + " -- Ctrl+Z brings it back.", false);
        } else if (!why.empty() && m_ctx.say) {
            m_ctx.say(why, true);
        }
        return true;
    }
    // M5f: the segments between picked nodes -> lines (L) / curves (C).
    if ((keyval == GDK_KEY_l || keyval == GDK_KEY_L || keyval == GDK_KEY_c || keyval == GDK_KEY_C) && !ctrl &&
        (state & Gdk::ModifierType::ALT_MASK) != Gdk::ModifierType::ALT_MASK && m_mode == Mode::None) {
        if (!g) return true;
        const bool curve = keyval == GDK_KEY_c || keyval == GDK_KEY_C;
        std::string why;
        if (command::set_segments(m_ctx.doc.journal, m_ctx.doc.font, m_shown, m_pick, curve, &why)) {
            say_pick();
            if (m_ctx.say) m_ctx.say(m_ctx.doc.journal.undo_label() + ".", false);
        }
        else if (!why.empty() && m_ctx.say) m_ctx.say(why, true);
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
                for (int i = 0; i < static_cast<int>(g->components.size()); ++i)
                    m_pick.insert({-1, i, Part::Component});
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
    if (keyval == GDK_KEY_m || keyval == GDK_KEY_M) {
        if (tool() == Tool::Measure) return;   // the Measure tool keeps measuring
        m_measure = false;
        if (m_mode == Mode::Measure) m_mode = Mode::None;
        m_meas_dragged = false;
        m_meas_a = m_meas_b = {};
        m_stem_h.reset();
        m_stem_v.reset();
        m_area.set_cursor("default");
        m_area.queue_draw();
        return;
    }
    if (keyval == GDK_KEY_space) {
        m_space = tool() == Tool::Hand;
        on_motion(m_mx, m_my);   // put the right cursor back
    }
}

void GlyphCanvas::update_readout(double sx, double sy) {
    if (!glyph()) { m_readout.set_text(""); return; }
    const geom::Vec2 p = to_font(sx, sy);
    m_readout.set_text(fmt(std::round(p.x)) + ", " + fmt(std::round(p.y)));
}

void GlyphCanvas::on_motion(double x, double y) {
    if (m_mode == Mode::None) { m_mx = x; m_my = y; update_readout(x, y); }
    if (m_pen.get_active() && !m_space) {
        m_area.set_cursor("crosshair");
        if (pen_path_live() || m_mode == Mode::Pen) m_area.queue_draw();   // the rubber band follows
        return;
    }
    if (m_mode == Mode::Marquee || m_mode == Mode::Points) return;
    if (m_measure && !m_space) { m_area.set_cursor("crosshair"); return; }
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

// ── node traversal (M5e) ──────────────────────────────────────────────────
void GlyphCanvas::step_pick(int dir, bool contour, bool handles) {
    const model::Glyph* g = glyph();
    if (!g) return;
    std::vector<int> live;   // contours with nodes
    for (int c = 0; c < static_cast<int>(g->contours.size()); ++c)
        if (!g->contours[c].nodes.empty()) live.push_back(c);
    if (live.empty()) return;
    // Where we stand: the pick's first outline point (a handle counts).
    std::optional<PointRef> at;
    for (const auto& p : m_pick)
        if (p.contour >= 0 && geom::valid(*g, p) &&
            (p.part == Part::Node || p.part == Part::In || p.part == Part::Out)) { at = p; break; }
    PointRef next{};
    if (!at) {
        // Nothing picked: forward starts at the first contour's start; back
        // ends at the last contour (its start on a contour jump, else its last node).
        const int c = dir > 0 ? live.front() : live.back();
        const int n = static_cast<int>(g->contours[c].nodes.size());
        next = {c, (dir > 0 || contour) ? 0 : n - 1, Part::Node};
    } else if (contour) {
        auto it = std::find(live.begin(), live.end(), at->contour);
        int k = static_cast<int>(it - live.begin());
        k = (k + dir + static_cast<int>(live.size())) % static_cast<int>(live.size());
        next = {live[k], 0, Part::Node};
    } else {
        // The contour's walk, in its direction: in-handle, node, out-handle
        // per node (handles only with `handles`, and only where they exist).
        const auto& nodes = g->contours[at->contour].nodes;
        std::vector<PointRef> walk;
        for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
            if (handles && nodes[i].has_in())  walk.push_back({at->contour, i, Part::In});
            walk.push_back({at->contour, i, Part::Node});
            if (handles && nodes[i].has_out()) walk.push_back({at->contour, i, Part::Out});
        }
        PointRef here = *at;
        if (!handles) here.part = Part::Node;   // from a handle, Tab walks on from its node
        auto it = std::find(walk.begin(), walk.end(), here);
        if (it == walk.end()) it = std::find(walk.begin(), walk.end(), PointRef{at->contour, at->index, Part::Node});
        const int sz = static_cast<int>(walk.size());
        int k = static_cast<int>(it - walk.begin());
        if (here.part != at->part) {
            // From a handle, Tab goes to the node on that side: an in-handle
            // leads to its own node forward, an out-handle back.
            const bool own = (at->part == Part::In) == (dir > 0);
            if (!own) k = (k + dir + sz) % sz;
        } else {
            k = (k + dir + sz) % sz;
        }
        next = walk[k];
    }
    m_pick.clear();
    m_pick.insert(next);
    m_ctx.guide_pick.reset();
    keep_in_view(geom::position(*g, next));
    say_pick();
    m_area.queue_draw();
}

void GlyphCanvas::keep_in_view(geom::Vec2 font_pt) {
    const double w = m_area.get_width(), h = m_area.get_height();
    if (w <= 0 || h <= 0) return;
    const double margin = 48;   // clear of the rulers, with room to see around it
    const Vec2 p = to_screen(font_pt);
    if (p.x < margin)          m_ox += margin - p.x;
    else if (p.x > w - margin) m_ox -= p.x - (w - margin);
    if (p.y < margin)          m_oy += margin - p.y;
    else if (p.y > h - margin) m_oy -= p.y - (h - margin);
}

void GlyphCanvas::make_first() {
    const model::Glyph* g = glyph();
    if (!g || m_pick.size() != 1) {
        if (m_ctx.say) m_ctx.say("Make first: pick one node", true);
        return;
    }
    const PointRef p = *m_pick.begin();
    if (p.contour < 0 || p.part != Part::Node) {
        if (m_ctx.say) m_ctx.say("Make first: pick one node", true);
        return;
    }
    std::string why;
    if (command::make_first(m_ctx.doc.journal, m_ctx.doc.font, m_shown, p.contour, p.index, &why)) {
        m_pick.clear();
        m_pick.insert({p.contour, 0, Part::Node});   // the same node, now index 0
        say_pick();
        m_area.queue_draw();
    } else if (!why.empty() && m_ctx.say) {
        m_ctx.say(why, true);
    }
}

// The status line speaks for the pick: one point's coordinates, else a count.
void GlyphCanvas::say_pick() {
    m_ctx.pick_changed.emit();
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
            case Part::Component: what = "Component “" + g->components[p.index].base + "”, offset"; break;
            case Part::In:     what = "In-handle"; break;
            case Part::Out:    what = "Out-handle"; break;
            default:
                what = std::string("Node (") + type_name(g->contours[p.contour].nodes[p.index].type) + ")";
                break;
        }
        std::string where;
        if (p.contour >= 0) {
            const auto& c = g->contours[p.contour];
            where = "   node " + std::to_string(p.index + 1) + " of " + std::to_string(c.nodes.size()) +
                    (p.index == 0 ? " (start)" : "") + ", contour " + std::to_string(p.contour + 1) + " of " +
                    std::to_string(g->contours.size());
        }
        m_ctx.say(what + "   " + fmt(v.x) + ", " + fmt(v.y) + where, false);
        return;
    }
    m_ctx.say(std::to_string(m_pick.size()) + " points picked", false);
}

// ── the pen (M5g) ──────────────────────────────────────────────────────────

void GlyphCanvas::set_pen(bool on) {
    set_tool(on ? Tool::Pen : Tool::Pick);   // the group's handler does the rest
}

bool GlyphCanvas::pen_path_live() const {
    const model::Glyph* g = glyph();
    if (!g || m_pen_contour < 0 || m_pen_glyph != m_shown) return false;
    if (m_pen_contour >= static_cast<int>(g->contours.size())) return false;
    const auto& c = g->contours[static_cast<std::size_t>(m_pen_contour)];
    return !c.closed && !c.nodes.empty();
}

void GlyphCanvas::end_path(bool say_it) {
    const bool was = pen_path_live();
    m_pen_contour = -1;
    m_pen_from_start = false;
    if (was && say_it && m_ctx.say) m_ctx.say("Path ended (open). Click to start another; Escape leaves the pen.", false);
    m_area.queue_draw();
}

void GlyphCanvas::draw_pen(const Cairo::RefPtr<Cairo::Context>& cr, const model::Glyph& g) {
    if (!pen_path_live() || m_mode == Mode::Pen) return;
    const auto& c = g.contours[static_cast<std::size_t>(m_pen_contour)];
    const auto& last = m_pen_from_start ? c.nodes.front() : c.nodes.back();
    const auto& first = m_pen_from_start ? c.nodes.back() : c.nodes.front();
    const geom::Vec2 a = to_screen({last.x, last.y});
    // The segment the next click would make: a straight rubber band (or the
    // curve the last node's handle starts).
    cr->save();
    cr->set_line_width(1.0);
    cr->set_source_rgba(NS[0], NS[1], NS[2], 0.8);
    std::vector<double> dash{5.0, 3.0};
    cr->set_dash(dash, 0);
    const bool handle = m_pen_from_start ? last.has_in() : last.has_out();
    cr->move_to(a.x, a.y);
    if (handle) {
        const geom::Vec2 h = to_screen(m_pen_from_start ? geom::Vec2{last.cx1, last.cy1} : geom::Vec2{last.cx2, last.cy2});
        cr->curve_to(h.x, h.y, m_mx, m_my, m_mx, m_my);
    } else {
        cr->line_to(m_mx, m_my);
    }
    cr->stroke();
    cr->unset_dash();
    // The first node rings when a click would close the path.
    const geom::Vec2 f = to_screen({first.x, first.y});
    if (!m_pen_from_start && c.nodes.size() >= 2 && std::hypot(f.x - m_mx, f.y - m_my) <= kHitPx) {
        cr->set_line_width(2.0);
        cr->arc(f.x, f.y, 8.0, 0, 2 * 3.14159265358979);
        cr->stroke();
    }
    cr->restore();
}

// ── the tool panel (M5h) ──────────────────────────────────────────────────

GlyphCanvas::Tool GlyphCanvas::tool() const {
    if (m_pen.get_active()) return Tool::Pen;
    if (m_tool_measure.get_active()) return Tool::Measure;
    if (m_tool_hand.get_active()) return Tool::Hand;
    return Tool::Pick;
}

void GlyphCanvas::set_tool(Tool t) {
    Gtk::ToggleButton& b = t == Tool::Pen ? static_cast<Gtk::ToggleButton&>(m_pen)
                         : t == Tool::Measure ? static_cast<Gtk::ToggleButton&>(m_tool_measure)
                         : t == Tool::Hand ? static_cast<Gtk::ToggleButton&>(m_tool_hand)
                                           : static_cast<Gtk::ToggleButton&>(m_tool_pick);
    if (!b.get_active()) b.set_active(true);
}

void GlyphCanvas::on_tool_changed() {
    const Tool t = tool();
    if (t != Tool::Pen) end_path(false);
    m_measure = t == Tool::Measure;
    m_space = t == Tool::Hand;
    if (t != Tool::Measure) { m_meas_a = m_meas_b = {}; m_stem_h.reset(); m_stem_v.reset(); }
    m_area.set_cursor(t == Tool::Pen || t == Tool::Measure ? "crosshair" : t == Tool::Hand ? "grab" : "default");
    if (m_ctx.say) {
        switch (t) {
            case Tool::Pen:     m_ctx.say("Pen: click for a corner, drag for a curve; V or Escape for picking.", false); break;
            case Tool::Measure: m_ctx.say("Measure: drag a distance, click for the stems; V for picking.", false); break;
            case Tool::Hand:    m_ctx.say("Hand: drag to pan; V for picking.", false); break;
            case Tool::Pick:    m_ctx.say("Pick.", false); break;
        }
    }
    m_area.queue_draw();
    m_area.grab_focus();
}

}  // namespace granjon::views
