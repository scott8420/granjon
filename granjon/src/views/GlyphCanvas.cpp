#include "views/GlyphCanvas.hpp"
#include "views/PathOps.hpp"
#include "Log.hpp"
#include <giomm/menu.h>
#include <giomm/menuitem.h>
#include "geom/Shapes.hpp"
#include "geom/Corners.hpp"
#include "geom/NodeEdit.hpp"

#include "command/Verbs.hpp"
#include "model/Bounds.hpp"
#include "unicode/Blocks.hpp"
#include "unicode/UcdTable.hpp"
#include "views/GlyphPaint.hpp"
#include "views/ImagePlace.hpp"

#include <gdk/gdk.h>
#include <glibmm/main.h>
#include <gtkmm/droptarget.h>
#include <gtkmm/eventcontrollermotion.h>
#include <gtkmm/eventcontrollerscroll.h>
#include <gtkmm/root.h>
#include <gtkmm/separator.h>
#include <pangomm/layout.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <set>
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

// M13: a scan as an ink mask. Each pixel's darkness (over white, so a
// transparent PNG reads as paper) becomes alpha through a levels ramp: paper
// and its speckle (darkness < 40 of 255) go fully clear, ink (> 200) fully
// opaque. The canvas fills it with a pale tint, so the drawing shows the
// same on a light or a dark desktop -- never a white sheet.
Cairo::RefPtr<Cairo::ImageSurface> ink_mask(const Cairo::RefPtr<Cairo::ImageSurface>& src) {
    if (!src) return {};
    src->flush();
    const int w = src->get_width(), h = src->get_height();
    auto out = Cairo::ImageSurface::create(Cairo::Surface::Format::A8, w, h);
    out->flush();
    const auto fmt = src->get_format();
    const int ss = src->get_stride(), os = out->get_stride();
    const unsigned char* sp = src->get_data();
    unsigned char* op = out->get_data();
    constexpr int kLo = 40, kHi = 200;   // the levels ramp, darkness 0..255
    for (int y = 0; y < h; ++y) {
        const auto* row = reinterpret_cast<const std::uint32_t*>(sp + y * ss);
        unsigned char* orow = op + y * os;
        for (int x = 0; x < w; ++x) {
            const std::uint32_t p = row[x];   // premultiplied ARGB, native endian
            const int a = fmt == Cairo::Surface::Format::RGB24 ? 255 : static_cast<int>(p >> 24);
            const int r = static_cast<int>((p >> 16) & 0xFF) + (255 - a);   // over white
            const int g = static_cast<int>((p >> 8) & 0xFF) + (255 - a);
            const int b = static_cast<int>(p & 0xFF) + (255 - a);
            const int lum = (299 * std::min(r, 255) + 587 * std::min(g, 255) + 114 * std::min(b, 255)) / 1000;
            const int dark = 255 - lum;
            orow[x] = static_cast<unsigned char>(std::clamp((dark - kLo) * 255 / (kHi - kLo), 0, 255));
        }
    }
    out->mark_dirty();
    return out;
}

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
      m_align("editor.align"),
      m_image("editor.image"),
      m_pen("editor.tool_pen"),
      m_tool_select("editor.tool_select"),
      m_tool_pick("editor.tool_pick"),
      m_tool_measure("editor.tool_measure"),
      m_tool_hand("editor.tool_hand"),
      m_tool_rect("editor.tool_rect"),
      m_tool_ellipse("editor.tool_ellipse"),
      m_tool_line("editor.tool_line"),
      m_stroke_label("editor.stroke_label"),
      m_stroke("editor.stroke"),
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
    // M14a: the Line tool's stroke width -- shown only with that tool.
    m_stroke_label.set_text("Stroke");
    m_stroke_label.add_css_class("dim");
    m_stroke.set_range(1, 2000);
    m_stroke.set_increments(1, 10);
    m_stroke.set_numeric(true);
    m_stroke.set_width_chars(5);
    m_stroke.set_enable_undo(false);
    m_stroke.set_tooltip_text("The Line tool's stroke width, font units. Remembered; until set, the font's stem "
                              "(a preset font's lowercase stem, else 8 % of the em).");
    m_stroke_label.set_visible(false);
    m_stroke.set_visible(false);
    m_stroke.signal_value_changed().connect([this] {
        const int v = static_cast<int>(m_stroke.get_value());
        if (v == static_cast<int>(std::lround(stroke_width()))) return;
        m_ctx.settings.line_width = v;
        if (m_ctx.save_settings) m_ctx.save_settings();
    });
    m_bar.append(m_stroke_label);
    m_bar.append(m_stroke);
    m_grid.set_label("Grid");
    m_grid.set_active(m_ctx.settings.editor_grid);
    m_grid.set_tooltip_text("A grid that follows the zoom: tenths of a unit close in, whole units drawn stronger");
    m_snap.set_label("Snap");
    m_snap.set_active(m_ctx.settings.editor_snap);
    m_snap.set_tooltip_text("Drags snap to the grid (when shown), the metric lines, the sidebearings and the other nodes. "
                            "Ctrl (fine drag) never snaps.");
    m_bar.append(m_fit_button);
    m_readout.add_css_class("dim");
    m_readout.set_width_chars(16);   // M15a: room for "134.9 x 120.0 %" and "Rotate -30.6°"
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
    m_align.set_active(m_ctx.settings.editor_align);
    m_align.signal_toggled().connect([this, remember] { remember(m_ctx.settings.editor_align, m_align.get_active()); });
    m_image.set_active(m_ctx.settings.editor_image);
    m_image.signal_toggled().connect([this, remember] {
        if (!m_image.get_active()) m_image_picked = false;
        remember(m_ctx.settings.editor_image, m_image.get_active());
    });
    m_ctx.settings_changed.connect([this] {
        if (m_context.get_active() != m_ctx.settings.editor_context) m_context.set_active(m_ctx.settings.editor_context);
        if (m_fill.get_active() != m_ctx.settings.editor_fill) m_fill.set_active(m_ctx.settings.editor_fill);
        if (m_grid.get_active() != m_ctx.settings.editor_grid) m_grid.set_active(m_ctx.settings.editor_grid);
        if (m_snap.get_active() != m_ctx.settings.editor_snap) m_snap.set_active(m_ctx.settings.editor_snap);
        if (m_align.get_active() != m_ctx.settings.editor_align) m_align.set_active(m_ctx.settings.editor_align);
        if (m_image.get_active() != m_ctx.settings.editor_image) m_image.set_active(m_ctx.settings.editor_image);
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
    icon_button(m_tool_select, "granjon-tool-select-symbolic",
                "Select (V)\nWhole paths: click a path's outline or inside it, Shift+click adds / removes, drag a box "
                "to take every path it touches; drag to move. Double-click: into Pick on that path.");
    icon_button(m_tool_pick, "granjon-tool-pick-symbolic",
                "Pick (A)\nNodes and handles: click, Shift+click or drag a box to pick nodes, handles, anchors, "
                "components; drag to move. Double-click a segment: add a node; a node: corner / smooth.");
    icon_button(m_pen, "granjon-tool-pen-symbolic",
                "Pen (P)\nClick: a corner node. Drag: a smooth node (Alt: the out-handle alone; Shift: 45 degrees). "
                "Click the first node: close. Click an open path's end: go on. Enter / Escape: end the path.");
    icon_button(m_tool_measure, "granjon-tool-measure-symbolic",
                "Measure (hold M)\nDrag: distance and angle. Click: the stems through that point.");
    icon_button(m_tool_hand, "granjon-tool-hand-symbolic",
                "Hand (hold Space)\nDrag to pan. Ctrl+scroll zooms; Ctrl+0 fits.");
    icon_button(m_tool_rect, "granjon-tool-rect-symbolic",
                "Rectangle (R)\nDrag a box. Shift: a square; Alt: from the centre. One closed contour, picked -- "
                "one undo step.");
    icon_button(m_tool_ellipse, "granjon-tool-ellipse-symbolic",
                "Ellipse (E)\nDrag its box. Shift: a circle; Alt: from the centre. Four smooth nodes at the extremes.");
    icon_button(m_tool_line, "granjon-tool-line-symbolic",
                "Line (\\)\nDrag a bar of the Stroke width (the field in the bar above). Shift: 45 degrees; "
                "Alt: from the centre.");
    m_tool_select.set_group(m_tool_pick);
    m_pen.set_group(m_tool_pick);
    m_tool_rect.set_group(m_tool_pick);
    m_tool_ellipse.set_group(m_tool_pick);
    m_tool_line.set_group(m_tool_pick);
    m_tool_measure.set_group(m_tool_pick);
    m_tool_hand.set_group(m_tool_pick);
    m_tool_select.set_active(true);   // s09: the app opens on Select (V)
    for (auto* b : {&m_tool_select, &m_tool_pick, &m_pen, &m_tool_rect, &m_tool_ellipse, &m_tool_line, &m_tool_measure, &m_tool_hand}) {
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
    icon_button(m_align, "granjon-view-align-symbolic",
                "Alignment guides\nWhile a node is dragged: dashed lines to the nearest node above, below, left and right "
                "on any path, with the distances (the Measure tool's tags); solid accent when the node lines up. Remembered.");
    icon_button(m_image, "granjon-view-image-symbolic",
                "Image\nShow the background image -- a scan or sketch to trace (drop a PNG / JPEG on the canvas, "
                "or the inspector's IMAGE card). Unlocked, drag it or its corners; locked, clicks go through. "
                "Remembered.");
    for (auto* b : {&m_fill, &m_context, &m_grid, &m_snap, &m_align, &m_image}) m_tools.append(*b);
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
        if (tool() == Tool::Select) {   // the solid arrow: drill into the path with the hollow one
            set_tool(Tool::Pick);
            return;
        }
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
    build_context_menu();   // s09: right-click
    m_ctx.tf_origin_changed.connect([this] { m_area.queue_draw(); });   // M15a r1: the pivot moved

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

    // M13: drop an image file on the canvas -> it goes behind the glyph shown.
    auto drop = Gtk::DropTarget::create(GDK_TYPE_FILE_LIST, Gdk::DragAction::COPY);
    drop->signal_drop().connect([this](const Glib::ValueBase& value, double, double) {
        if (!glyph() || !G_VALUE_HOLDS(value.gobj(), GDK_TYPE_FILE_LIST)) return false;
        auto* list = static_cast<GSList*>(g_value_get_boxed(value.gobj()));
        if (!list || !list->data) return false;
        auto file = Glib::wrap(G_FILE(list->data), true);   // take a reference
        const bool ok = place_image_file(m_ctx, m_shown, file);
        if (ok) {
            if (!m_image.get_active()) m_image.set_active(true);
            m_image_picked = true;
            m_pick.clear();
            m_ctx.pick_changed.emit();
        }
        m_area.grab_focus();
        m_area.queue_draw();
        return ok;
    }, false);
    m_area.add_controller(drop);

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
        m_image_picked = false;
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
        if (m_image_picked && !image_live(*g)) m_image_picked = false;   // removed, locked, hidden
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

    if (g->image && m_image.get_active()) draw_image(cr, *g);   // behind everything
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
    {   // s09: with two or more paths picked, the TOP one (the booleans'
        // operand -- it cuts / keeps) gets an accent outline.
        const std::set<int> cs = picked_contours(m_ctx);
        if (cs.size() >= 2) {
            model::Glyph top;
            top.contours = {g->contours[static_cast<std::size_t>(*cs.rbegin())]};
            font_path(top, 0);
            cr->set_source_rgba(NS[0], NS[1], NS[2], 0.95);
            cr->set_line_width(2.0);
            cr->stroke();
        }
    }

    draw_directions(cr, *g, w);
    draw_overlay(cr, *g);
    draw_anchors(cr, *g);

    // What the lead point snapped to, while dragging: a dashed accent line.
    if ((m_mode == Mode::Points || m_mode == Mode::Shape) && (m_snap_x || m_snap_y)) {
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

    if (m_mode == Mode::Points && m_align.get_active()) draw_neighbours(cr, *g);
    if (transform_box_shown()) draw_transform_box(cr);
    draw_measure(cr, *g);
    if (m_pen.get_active()) draw_pen(cr, *g);
    if (m_mode == Mode::Shape) draw_shape(cr);
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
    // s10: overlaps -- the shared area tinted amber and outlined; a path
    // crossing itself gets an amber dashed outline.
    const double AM[3] = {0.96, 0.62, 0.12};
    const auto& ov = overlaps_of(g);
    for (const auto& o : ov) {
        if (o.a == o.b) {
            model::Glyph one;
            one.contours = {g.contours[static_cast<std::size_t>(o.a)]};
            cr->begin_new_path();
            cr->save();
            cr->translate(m_ox, m_oy);
            cr->scale(m_s, -m_s);
            append_glyph_path(cr, m_ctx.doc.font, one);
            cr->restore();
            std::vector<double> dash{5.0, 3.0};
            cr->set_dash(dash, 0);
            cr->set_source_rgba(AM[0], AM[1], AM[2], 0.95);
            cr->set_line_width(2.0);
            cr->stroke();
            cr->unset_dash();
            continue;
        }
        cr->begin_new_path();
        for (const auto& poly : o.region) {
            for (std::size_t i = 0; i < poly.size(); ++i) {
                const geom::Vec2 p = to_screen(poly[i]);
                if (i == 0) cr->move_to(p.x, p.y);
                else cr->line_to(p.x, p.y);
            }
            cr->close_path();
        }
        cr->set_fill_rule(Cairo::Context::FillRule::WINDING);
        cr->set_source_rgba(AM[0], AM[1], AM[2], 0.38);
        cr->fill_preserve();
        cr->set_source_rgba(AM[0], AM[1], AM[2], 0.95);
        cr->set_line_width(1.5);
        cr->stroke();
    }

    std::vector<std::pair<std::string, const double*>> notes;
    if (!wrong.empty())
        notes.push_back({std::to_string(wrong.size()) + (wrong.size() == 1 ? " contour runs" : " contours run") +
                             " the wrong way -- Correct directions in the inspector",
                         WR});
    if (!ov.empty())
        notes.push_back({std::to_string(ov.size()) + (ov.size() == 1 ? " overlap" : " overlaps") +
                             " (amber) -- Remove overlaps, or a boolean",
                         AM});
    double y = kRuler + 8;
    for (const auto& [text, rgb] : notes) {
        Pango::FontDescription small;
        small.set_size(9 * PANGO_SCALE);
        auto layout = m_area.create_pango_layout(text);
        layout->set_font_description(small);
        int lw = 0, lh = 0;
        layout->get_pixel_size(lw, lh);
        const double x = std::max(kRuler + 8, (w - lw) / 2.0);
        cr->set_source_rgba(0, 0, 0, 0.55);
        cr->rectangle(x - 6, y - 3, lw + 12, lh + 6);
        cr->fill();
        cr->set_source_rgb(rgb[0], rgb[1], rgb[2]);
        cr->move_to(x, y);
        layout->show_in_cairo_context(cr);
        y += lh + 10;
    }
}

const std::vector<geom::OverlapPair>& GlyphCanvas::overlaps_of(const model::Glyph& g) {
    if (g.contours != m_ov_for) {
        m_ov_for = g.contours;
        m_ov = geom::overlapping(g.contours, true);
    }
    return m_ov;
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

// s09 (Scott: "when moving a node a temp line up to adjacent nodes left/right
// up/down ... the measure tool kind of info"; then: "take into account all the
// paths in the view"). While a node is dragged, four guides to nodes on ANY
// path that stay put: UP / DOWN go to the node above / below that is nearest
// to plumb (smallest dx), LEFT / RIGHT to the node left / right nearest to
// level (smallest dy). Each is a dashed amber L leaving the node along its
// direction -- the long leg labelled with the distance, the short one with the
// offset still to close; lined up (offset 0) it is one solid accent line. The
// Alignment guides toggle in the tool panel hides them.
void GlyphCanvas::draw_neighbours(const Cairo::RefPtr<Cairo::Context>& cr, const model::Glyph& g) {
    if (m_lead.part != Part::Node || !geom::valid(g, m_lead) || !m_pick.count(m_lead)) return;
    const auto& ln = g.contours[static_cast<std::size_t>(m_lead.contour)].nodes[static_cast<std::size_t>(m_lead.index)];
    const geom::Vec2 L{ln.x, ln.y};
    // up, down, left, right: the best node so far and its score (offset, then distance).
    std::optional<geom::Vec2> best[4];
    double score[4][2];
    for (int ci = 0; ci < static_cast<int>(g.contours.size()); ++ci) {
        const auto& ns = g.contours[static_cast<std::size_t>(ci)].nodes;
        for (int ni = 0; ni < static_cast<int>(ns.size()); ++ni) {
            if (m_pick.count({ci, ni, Part::Node})) continue;   // it moves too: nothing to line up with
            const geom::Vec2 N{ns[static_cast<std::size_t>(ni)].x, ns[static_cast<std::size_t>(ni)].y};
            const double dx = N.x - L.x, dy = N.y - L.y;
            const auto offer = [&](int d, double off, double dist) {
                if (!best[d] || off < score[d][0] - 1e-9 || (std::abs(off - score[d][0]) < 1e-9 && dist < score[d][1])) {
                    best[d] = N;
                    score[d][0] = off;
                    score[d][1] = dist;
                }
            };
            if (dy > 0.5)  offer(0, std::abs(dx), dy);
            if (dy < -0.5) offer(1, std::abs(dx), -dy);
            if (dx < -0.5) offer(2, std::abs(dy), -dx);
            if (dx > 0.5)  offer(3, std::abs(dy), dx);
        }
    }
    Pango::FontDescription small;
    small.set_size(8 * PANGO_SCALE);
    small.set_weight(Pango::Weight::BOLD);
    const double MC[3] = {0.98, 0.62, 0.20};   // the Measure tool's amber
    auto tag = [&](const std::string& s, geom::Vec2 at) {
        auto layout = m_area.create_pango_layout(s);
        layout->set_font_description(small);
        int lw = 0, lh = 0;
        layout->get_pixel_size(lw, lh);
        at = at - geom::Vec2{lw / 2.0, lh / 2.0};
        cr->set_source_rgba(0, 0, 0, 0.7);
        cr->rectangle(at.x - 3, at.y - 2, lw + 6, lh + 4);
        cr->fill();
        cr->set_source_rgb(MC[0], MC[1], MC[2]);
        cr->move_to(at.x, at.y);
        layout->show_in_cairo_context(cr);
    };
    auto leg = [&](geom::Vec2 a, geom::Vec2 b, bool lined_up) {
        const auto sa = to_screen(a), sb = to_screen(b);
        if (lined_up) {
            cr->set_source_rgba(NS[0], NS[1], NS[2], 0.95);
            cr->set_line_width(1.5);
        } else {
            std::vector<double> dash{3.0, 3.0};
            cr->set_dash(dash, 0);
            cr->set_source_rgba(MC[0], MC[1], MC[2], 0.9);
            cr->set_line_width(1.0);
        }
        cr->move_to(sa.x, sa.y);
        cr->line_to(sb.x, sb.y);
        cr->stroke();
        cr->unset_dash();
    };
    std::vector<geom::Vec2> done;
    for (int d = 0; d < 4; ++d) {
        if (!best[d]) continue;
        const geom::Vec2 N = *best[d];
        bool seen = false;
        for (const auto& v : done) seen = seen || (v.x == N.x && v.y == N.y);
        if (seen) continue;   // one node answering two directions: one guide
        done.push_back(N);
        const bool vertical = d < 2;
        const double off = vertical ? N.x - L.x : N.y - L.y;
        const bool lined_up = std::abs(off) < 0.5;
        // The long leg leaves the node along the direction; the short leg closes the offset.
        const geom::Vec2 corner = vertical ? geom::Vec2{L.x, N.y} : geom::Vec2{N.x, L.y};
        const geom::Vec2 end = lined_up ? N : corner;
        leg(L, end, lined_up);
        if (!lined_up) leg(corner, N, false);
        const double dist = vertical ? std::abs(N.y - L.y) : std::abs(N.x - L.x);
        const geom::Vec2 mid = to_screen({(L.x + end.x) / 2, (L.y + end.y) / 2});
        tag(fmt(std::round(dist)), mid + (vertical ? geom::Vec2{-18, 0} : geom::Vec2{0, -12}));
        if (!lined_up) {
            const geom::Vec2 sm = to_screen({(corner.x + N.x) / 2, (corner.y + N.y) / 2});
            tag(fmt(std::round(std::abs(off))), sm + (vertical ? geom::Vec2{0, d == 0 ? -12.0 : 12.0} : geom::Vec2{d == 2 ? -18.0 : 18.0, 0}));
        }
    }
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

    // M14a: a primitive -- press .. release draws it; nothing is written
    // until the release (one step).
    if (const Tool t = tool(); t == Tool::Rect || t == Tool::Ellipse || t == Tool::Line) {
        const auto pst = m_drag->get_current_event_state();
        const bool ctrl = (pst & Gdk::ModifierType::CONTROL_MASK) == Gdk::ModifierType::CONTROL_MASK;
        m_mode = Mode::Shape;
        m_shape_a = m_shape_b = snap_free(to_font(x, y), !ctrl);
        m_shape_shift = (pst & Gdk::ModifierType::SHIFT_MASK) == Gdk::ModifierType::SHIFT_MASK;
        m_shape_alt = (pst & Gdk::ModifierType::ALT_MASK) == Gdk::ModifierType::ALT_MASK;
        m_snap_x.reset();
        m_snap_y.reset();
        m_area.queue_draw();
        return;
    }

    const auto st = m_drag->get_current_event_state();
    const bool shift = (st & Gdk::ModifierType::SHIFT_MASK) == Gdk::ModifierType::SHIFT_MASK;
    // M13: a picked image's corner handles come first (they sit on its frame).
    if (m_image_picked && image_live(*g)) {
        if (const int hc = image_handle_at(*g, x, y); hc >= 0) {
            m_image_handle = hc;
            m_image_press = *g->image;
            m_mode = Mode::Image;
            return;
        }
    }
    // M15a: the transform box's handles (Select tool) come before the paths.
    if (transform_box_shown()) {
        if (const int th = transform_handle_at(x, y); th >= 0) {
            m_tf_handle = th;
            m_tf_box = *pick_box(m_ctx);
            m_tf_pick = m_pick;
            m_mode = Mode::Transform;
            return;
        }
    }
    const geom::Hit hit = geom::hit_test(*g, to_font(x, y), kHitPx / m_s);
    m_press_glyph = *g;
    m_image_picked = false;   // re-set below when the press lands on the image
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
    // The Select tool (the solid arrow): whole paths. A press on a path's
    // outline, or inside it (the smallest path there -- a counter inside its
    // bowl), takes the whole path. Handles don't catch; anchors, components,
    // guides, the image, sidebearings and the marquee act as in Pick.
    m_unpick_contour.reset();
    if (tool() == Tool::Select) {
        const geom::Vec2 fp = to_font(x, y);
        const geom::Hit sh = geom::hit_test(*g, fp, kHitPx / m_s, false);
        int c = -1, lead = 0;
        if (sh.kind == K::Node || sh.kind == K::Segment) {
            c = sh.contour;
            lead = sh.index;
        } else if (sh.kind == K::None && sidebearing_at(x, y) < 0 && !guide_at(x, y) &&
                   geom::component_at(m_ctx.doc.font, *g, fp) < 0) {
            c = geom::contour_at(*g, fp);
            if (c >= 0) {   // the lead (what snaps) is the node nearest the press
                double best = 1e18;
                const auto& ns = g->contours[static_cast<std::size_t>(c)].nodes;
                for (int i = 0; i < static_cast<int>(ns.size()); ++i)
                    if (const double d = (geom::Vec2{ns[static_cast<std::size_t>(i)].x, ns[static_cast<std::size_t>(i)].y} - fp).length();
                        d < best) { best = d; lead = i; }
            }
        }
        // s09: a press on / inside a path that is already picked keeps the
        // pick, even where another path lies there too -- a duplicate sits
        // exactly on its original, and the drag must take the copy.
        if (c >= 0 && !shift) {
            const auto whole = [&](int ci) {
                const geom::Points pth = geom::whole_contours(*g, {{ci, 0, Part::Node}});
                return std::all_of(pth.begin(), pth.end(), [&](const PointRef& r) { return m_pick.count(r) > 0; });
            };
            if (!whole(c)) {
                std::set<int> picked;
                for (const auto& p : m_pick) if (p.contour >= 0) picked.insert(p.contour);
                for (auto it = picked.rbegin(); it != picked.rend(); ++it) {   // the top one first
                    if (*it >= static_cast<int>(g->contours.size()) || !whole(*it)) continue;
                    model::Glyph one;
                    one.contours = {g->contours[static_cast<std::size_t>(*it)]};
                    const geom::Hit oh = geom::hit_test(one, fp, kHitPx / m_s, false);
                    if (oh.kind == K::Node || oh.kind == K::Segment || geom::contour_at(one, fp) == 0) {
                        c = *it;
                        double best = 1e18;
                        const auto& ns = one.contours[0].nodes;
                        for (int i = 0; i < static_cast<int>(ns.size()); ++i)
                            if (const double d = (geom::Vec2{ns[static_cast<std::size_t>(i)].x, ns[static_cast<std::size_t>(i)].y} - fp).length();
                                d < best) { best = d; lead = i; }
                        break;
                    }
                }
            }
        }
        if (c >= 0) {
            m_ctx.guide_pick.reset();
            const geom::Points path = geom::whole_contours(*g, {{c, 0, Part::Node}});
            const bool all = std::all_of(path.begin(), path.end(), [&](const PointRef& r) { return m_pick.count(r) > 0; });
            if (shift) {
                if (all) m_unpick_contour = c;   // a Shift-CLICK drops it; a Shift-drag moves
                else m_pick.insert(path.begin(), path.end());
            } else if (!all) {
                m_pick = path;
            }
            m_lead = {c, lead, Part::Node};
            m_mode = Mode::Points;
            say_pick();
            m_area.queue_draw();
            return;
        }
    }
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
            // M13: on an unlocked image: pick it -- a drag moves it.
            if (image_live(*g) && image_inside(*g, x, y)) {
                m_pick.clear();
                m_ctx.guide_pick.reset();
                m_image_picked = true;
                m_image_handle = -1;
                m_image_press = *g->image;
                m_mode = Mode::Image;
                m_ctx.pick_changed.emit();
                if (m_ctx.say)
                    m_ctx.say("Image " + g->image->file + ": drag to move, a corner to scale (Shift: from the centre); "
                              "Delete removes it; lock it (inspector) to trace.", false);
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
    if (m_mode == Mode::Transform) { transform_drag(dx, dy); return; }
    if (m_mode == Mode::Shape) {
        const auto st = m_drag->get_current_event_state();
        const bool ctrl = (st & Gdk::ModifierType::CONTROL_MASK) == Gdk::ModifierType::CONTROL_MASK;
        m_shape_shift = (st & Gdk::ModifierType::SHIFT_MASK) == Gdk::ModifierType::SHIFT_MASK;
        m_shape_alt = (st & Gdk::ModifierType::ALT_MASK) == Gdk::ModifierType::ALT_MASK;
        m_mx = m_press_x + dx; m_my = m_press_y + dy;
        // Shift constrains the shape itself, so only a free drag snaps.
        m_shape_b = snap_free(to_font(m_mx, m_my), !ctrl && !m_shape_shift);
        const model::Contour c = shape_now();
        std::ostringstream o;
        if (tool() == Tool::Line) {
            geom::Vec2 a, b;
            geom::drag_line(m_shape_a, m_shape_b, m_shape_shift, m_shape_alt, a, b);
            o << fmt(std::round((b - a).length())) << " long";
        } else {
            const auto bx = geom::drag_box(m_shape_a, m_shape_b, m_shape_shift, m_shape_alt);
            o << fmt(std::round(bx.hi.x - bx.lo.x)) << " \u00d7 " << fmt(std::round(bx.hi.y - bx.lo.y));
        }
        m_readout.set_text(c.nodes.empty() ? "" : o.str());
        m_area.queue_draw();
        return;
    }
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
    if (m_mode == Mode::Image) {
        const model::Glyph* g = glyph();
        if (!g || !g->image) return;
        auto& doc = m_ctx.doc;
        if (!m_began) {
            if (std::hypot(dx, dy) < kDragPx) return;
            command::image_drag_begin(doc.journal, doc.font, m_shown, m_image_handle < 0 ? "Move image" : "Scale image");
            m_began = doc.journal.in_gesture();
            if (!m_began) return;
        }
        model::GlyphImage im = m_image_press;
        if (m_image_handle < 0) {
            im.dx += dx / m_s;
            im.dy -= dy / m_s;
            m_readout.set_text(fmt(im.dx - m_image_press.dx) + ", " + fmt(im.dy - m_image_press.dy));
        } else {
            int pw = 0, ph = 0;
            if (!image_px(m_image_press, pw, ph)) return;
            const auto st = m_drag->get_current_event_state();
            const bool centre = (st & Gdk::ModifierType::SHIFT_MASK) == Gdk::ModifierType::SHIFT_MASK;
            const geom::Vec2 c0 = image_corner(m_image_press, pw, ph, m_image_handle);
            const geom::Vec2 a = centre ? (c0 + image_corner(m_image_press, pw, ph, (m_image_handle + 2) % 4)) * 0.5
                                        : image_corner(m_image_press, pw, ph, (m_image_handle + 2) % 4);
            const geom::Vec2 p = to_font(m_press_x + dx, m_press_y + dy);
            const geom::Vec2 diag = c0 - a;
            const double len2 = diag.x * diag.x + diag.y * diag.y;
            if (len2 <= 0) return;
            const double k = std::max(0.02, ((p.x - a.x) * diag.x + (p.y - a.y) * diag.y) / len2);
            // Scale about `a`: the matrix by k, the offset pulled toward a.
            im.xx *= k; im.xy *= k; im.yx *= k; im.yy *= k;
            im.dx = a.x + (m_image_press.dx - a.x) * k;
            im.dy = a.y + (m_image_press.dy - a.y) * k;
            m_readout.set_text(fmt(k * 100) + " %");
        }
        command::set_image_frame(doc.journal, doc.font, m_shown, im);
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
                if (tool() == Tool::Select) m_pick = geom::whole_contours(*g, m_pick);   // every path it touches
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
    if (was == Mode::Transform) {   // M15a
        const bool began_was = m_began;
        const int handle_was = m_tf_handle;
        if (m_began) command::drag_end(m_ctx.doc.journal, m_ctx.doc.font);
        m_began = false;
        m_tf_handle = -1;
        m_readout.set_text("");
        m_ctx.pick_changed.emit();   // same refs, new places: the inspector follows
        if (m_ctx.say && !m_tf_label.empty() && began_was) m_ctx.say(m_tf_label + " -- one undo step.", false);
        else if (m_ctx.say && handle_was == 20) m_ctx.say("Origin set -- rotate, skew and scale now work about it. "
                                                               "Reset on the TRANSFORM card clears it.", false);
        m_area.queue_draw();
        return;
    }
    if (was == Mode::Shape) {
        const model::Contour c = shape_now();
        const Tool t = tool();
        const std::string label = t == Tool::Rect ? "Rectangle" : t == Tool::Ellipse ? "Ellipse" : "Line";
        int at = -1;
        std::string why;
        if (!c.nodes.empty() &&
            command::add_shape(m_ctx.doc.journal, m_ctx.doc.font, m_shown, c, label, &at, &why) && at >= 0) {
            m_pick.clear();
            for (int i = 0; i < static_cast<int>(c.nodes.size()); ++i) m_pick.insert({at, i, Part::Node});
            m_ctx.pick_changed.emit();
            if (m_ctx.say) m_ctx.say(label + " drawn -- picked; drag or nudge it, or draw another. V to select.", false);
        } else if (m_ctx.say) {
            m_ctx.say(why.empty() ? std::string("Drag to draw a ") + (t == Tool::Rect ? "rectangle." : t == Tool::Ellipse ? "ellipse." : "line.") : why, false);
        }
        m_snap_x.reset();
        m_snap_y.reset();
        m_readout.set_text("");
        m_area.queue_draw();
        return;
    }
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
    if (was == Mode::Image) {
        if (m_began) {
            command::drag_end(m_ctx.doc.journal, m_ctx.doc.font);   // the offset snaps; one step
            if (m_ctx.say) m_ctx.say(m_ctx.doc.journal.undo_label() + ".", false);
        }
        m_began = false;
        m_image_handle = -1;
        m_readout.set_text("");
        m_area.queue_draw();
        return;
    }
    if (m_unpick && !m_began) m_pick.erase(*m_unpick);   // it was a Shift-click, not a drag
    if (m_unpick_contour && !m_began)
        if (const model::Glyph* g = glyph())
            for (const auto& r : geom::whole_contours(*g, {{*m_unpick_contour, 0, Part::Node}})) m_pick.erase(r);
    m_unpick_contour.reset();
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

// ── the transform box (M15a) ───────────────────────────────────────────────
// Scott: "One tool missing so far is scale/skew/rotate". With the Select tool
// and something picked, the pick's box shows 8 handles: a corner scales both
// ways (Shift: uniform), an edge one way (Shift: uniform); Alt: about the
// centre instead of the opposite side; Ctrl on an edge: skew. Just outside a
// corner: rotate about the centre (Shift: 15 degree steps). Live, one undo
// step at the release (drag_begin .. drag_transform .. drag_end).

bool GlyphCanvas::transform_box_shown() const {
    if (tool() != Tool::Select || m_pick.empty() || m_pen.get_active()) return false;
    if (m_mode != Mode::None && m_mode != Mode::Transform) return false;
    const auto b = pick_box(m_ctx);
    return b && (b->x1 - b->x0 > 0.5 || b->y1 - b->y0 > 0.5);
}

int GlyphCanvas::transform_handle_at(double sx, double sy) const {
    const auto b = pick_box(m_ctx);
    if (!b) return -1;
    // r1: the pivot (the origin) first -- it may sit on a handle.
    if (const geom::Vec2 o = to_screen(transform_origin(m_ctx, *b)); std::hypot(sx - o.x, sy - o.y) <= 7) return 20;
    const geom::Vec2 lo = to_screen({b->x0, b->y1}), hi = to_screen({b->x1, b->y0});   // screen: top-left, bottom-right
    const double mx = (lo.x + hi.x) / 2, my = (lo.y + hi.y) / 2;
    const geom::Vec2 pts[8] = {{lo.x, lo.y}, {mx, lo.y}, {hi.x, lo.y}, {hi.x, my},
                               {hi.x, hi.y}, {mx, hi.y}, {lo.x, hi.y}, {lo.x, my}};
    const double r = 6;
    for (int k = 0; k < 8; ++k)
        if (std::abs(sx - pts[k].x) <= r && std::abs(sy - pts[k].y) <= r) return k;
    // Rotate: a band just outside each corner.
    const bool outside = sx < lo.x - 2 || sx > hi.x + 2 || sy < lo.y - 2 || sy > hi.y + 2;
    if (outside)
        for (int c = 0; c < 4; ++c) {
            const geom::Vec2 p = pts[c * 2];
            const double d = std::hypot(sx - p.x, sy - p.y);
            if (d >= 4 && d <= 22) return 8 + c;
        }
    return -1;
}

void GlyphCanvas::draw_transform_box(const Cairo::RefPtr<Cairo::Context>& cr) {
    const auto b = pick_box(m_ctx);
    if (!b) return;
    const geom::Vec2 lo = to_screen({b->x0, b->y1}), hi = to_screen({b->x1, b->y0});
    const double x0 = std::round(lo.x) + 0.5, y0 = std::round(lo.y) + 0.5, x1 = std::round(hi.x) + 0.5, y1 = std::round(hi.y) + 0.5;
    std::vector<double> dash{4.0, 3.0};
    cr->set_dash(dash, 0);
    cr->set_source_rgba(NS[0], NS[1], NS[2], 0.7);
    cr->set_line_width(1.0);
    cr->rectangle(x0, y0, x1 - x0, y1 - y0);
    cr->stroke();
    cr->unset_dash();
    const double mx = std::round((x0 + x1) / 2) + 0.5, my = std::round((y0 + y1) / 2) + 0.5;
    const geom::Vec2 pts[8] = {{x0, y0}, {mx, y0}, {x1, y0}, {x1, my}, {x1, y1}, {mx, y1}, {x0, y1}, {x0, my}};
    for (const auto& p : pts) {
        cr->rectangle(p.x - 3.5, p.y - 3.5, 7, 7);
        cr->set_source_rgb(1, 1, 1);
        cr->fill_preserve();
        cr->set_source_rgba(NS[0], NS[1], NS[2], 1.0);
        cr->set_line_width(1.0);
        cr->stroke();
    }
    // r1: the pivot -- a ring with a cross; filled once an origin is chosen
    // (dragged here, typed, or picked on the card's grid).
    const geom::Vec2 o = to_screen(transform_origin(m_ctx, *b));
    const double ox = std::round(o.x) + 0.5, oy = std::round(o.y) + 0.5;
    // White under accent, so it reads over nodes, outlines and the fill.
    const auto arms = [&] {
        cr->move_to(ox - 14, oy); cr->line_to(ox - 6, oy);
        cr->move_to(ox + 6, oy);  cr->line_to(ox + 14, oy);
        cr->move_to(ox, oy - 14); cr->line_to(ox, oy - 6);
        cr->move_to(ox, oy + 6);  cr->line_to(ox, oy + 14);
    };
    cr->set_source_rgb(1, 1, 1);
    cr->set_line_width(3.0);
    arms();
    cr->stroke();
    cr->set_source_rgba(NS[0], NS[1], NS[2], 1.0);
    cr->set_line_width(1.0);
    arms();
    cr->stroke();
    cr->arc(ox, oy, 6, 0, 2 * M_PI);
    cr->set_source_rgb(1, 1, 1);
    cr->fill_preserve();
    cr->set_source_rgba(NS[0], NS[1], NS[2], 1.0);
    cr->set_line_width(1.5);
    cr->stroke();
    if (origin_chosen(m_ctx)) {   // chosen: a dot in the ring
        cr->arc(ox, oy, 2.5, 0, 2 * M_PI);
        cr->fill();
    }
}

void GlyphCanvas::transform_drag(double dx, double dy) {
    auto& doc = m_ctx.doc;
    m_mx = m_press_x + dx; m_my = m_press_y + dy;
    if (m_tf_handle == 20) {
        // r1: drag the pivot. It snaps to the box's 9 points (then it is
        // that grid point -- it follows the box), else to a node, else to
        // whole units. Not an edit: nothing goes in the journal.
        const auto b = pick_box(m_ctx);
        const model::Glyph* g = glyph();
        if (!b || !g) return;
        int best = -1;
        double bd = 8;
        for (int k = 0; k < 9; ++k) {
            const geom::Vec2 sp = to_screen(box_point(*b, k));
            if (const double d = std::hypot(sp.x - m_mx, sp.y - m_my); d <= bd) { bd = d; best = k; }
        }
        if (best >= 0) {
            set_origin_point(m_ctx, best);
        } else {
            geom::Vec2 p = to_font(m_mx, m_my);
            const geom::Hit h = geom::hit_test(*g, p, kHitPx / m_s);
            if (h.kind == geom::Hit::Kind::Node) {
                const auto& n = g->contours[static_cast<std::size_t>(h.contour)].nodes[static_cast<std::size_t>(h.index)];
                p = {n.x, n.y};
            } else {
                p = {std::round(p.x), std::round(p.y)};
            }
            set_custom_origin(m_ctx, p);
        }
        const geom::Vec2 o = transform_origin(m_ctx, *b);
        m_readout.set_text("Origin " + fmt(std::round(o.x * 10) / 10) + ", " + fmt(std::round(o.y * 10) / 10));
        m_tf_label.clear();
        m_area.queue_draw();
        return;
    }
    const bool edge = m_tf_handle < 8 && m_tf_handle % 2 == 1;
    if (!m_began) {
        if (std::hypot(dx, dy) < kDragPx) return;   // still a click
        const auto st0 = m_drag->get_current_event_state();
        const bool ctrl0 = (st0 & Gdk::ModifierType::CONTROL_MASK) == Gdk::ModifierType::CONTROL_MASK;
        command::drag_begin(doc.journal, doc.font, {m_shown},
                            m_tf_handle >= 8 ? "Rotate" : (edge && !ctrl0) ? "Skew" : "Scale");
        m_began = doc.journal.in_gesture();
        if (!m_began) return;
    }
    const auto st = m_drag->get_current_event_state();
    const bool shift = (st & Gdk::ModifierType::SHIFT_MASK) == Gdk::ModifierType::SHIFT_MASK;
    const bool alt = (st & Gdk::ModifierType::ALT_MASK) == Gdk::ModifierType::ALT_MASK;
    const bool ctrl = (st & Gdk::ModifierType::CONTROL_MASK) == Gdk::ModifierType::CONTROL_MASK;
    const model::Rect& b = m_tf_box;
    const geom::Vec2 P = to_font(m_mx, m_my), P0 = to_font(m_press_x, m_press_y);
    const geom::Vec2 C{(b.x0 + b.x1) / 2, (b.y0 + b.y1) / 2};
    // r1: a chosen origin is where everything starts from (Curvz); Alt = the centre.
    const bool chosen = origin_chosen(m_ctx);
    const geom::Vec2 O = transform_origin(m_ctx, b);
    // The handles in font units (y up): 0 top-left .. clockwise.
    const geom::Vec2 H[8] = {{b.x0, b.y1}, {C.x, b.y1}, {b.x1, b.y1}, {b.x1, C.y},
                             {b.x1, b.y0}, {C.x, b.y0}, {b.x0, b.y0}, {b.x0, C.y}};
    geom::Affine m;
    std::ostringstream o;
    if (m_tf_handle >= 8) {
        const geom::Vec2 R = alt ? C : chosen ? O : C;
        double a = (std::atan2(P.y - R.y, P.x - R.x) - std::atan2(P0.y - R.y, P0.x - R.x)) * 180.0 / M_PI;
        while (a > 180) a -= 360;
        while (a < -180) a += 360;
        if (shift) a = std::round(a / 15.0) * 15.0;
        m = geom::make_transform(1, 1, a, 0, 0, R);
        o << "Rotate " << fmt(std::round(a * 100) / 100) << "°";
        m_tf_label = "Rotate";
    } else {
        const int h = m_tf_handle;
        const geom::Vec2 hp = H[h];
        const geom::Vec2 A = alt ? C : chosen ? O : H[(h + 4) % 8];   // the fixed point
        const bool horizontal_edge = h == 1 || h == 5;
        if (edge && !ctrl) {
            // Skew (Curvz: the edge middles skew): a top / bottom edge slides
            // across, a side edge slides up.
            double deg;
            if (horizontal_edge) {
                const double span = hp.y - A.y;
                deg = std::atan((P.x - P0.x) / (std::abs(span) < 1e-9 ? 1.0 : span)) * 180.0 / M_PI;
                if (shift) deg = std::round(deg / 5.0) * 5.0;
                m = geom::make_transform(1, 1, 0, deg, 0, A);
            } else {
                const double span = hp.x - A.x;
                deg = std::atan((P.y - P0.y) / (std::abs(span) < 1e-9 ? 1.0 : span)) * 180.0 / M_PI;
                if (shift) deg = std::round(deg / 5.0) * 5.0;
                m = geom::make_transform(1, 1, 0, 0, deg, A);
            }
            o << "Skew " << fmt(std::round(deg * 100) / 100) << "°";
            m_tf_label = "Skew";
        } else {
            // Scale: a corner both ways (Shift uniform); Ctrl on an edge, one way.
            const double ux = hp.x - A.x, uy = hp.y - A.y;
            double sx = std::abs(ux) < 1e-9 ? 1.0 : (P.x - A.x) / ux;
            double sy = std::abs(uy) < 1e-9 ? 1.0 : (P.y - A.y) / uy;
            if (edge) (horizontal_edge ? sx : sy) = 1.0;
            if (shift) {   // uniform: the axis that moved most leads
                const double sc = edge ? (horizontal_edge ? sy : sx) : (std::abs(sx - 1) > std::abs(sy - 1) ? sx : sy);
                sx = sy = sc;
            }
            m = geom::make_transform(sx, sy, 0, 0, 0, A);
            o << fmt(std::round(sx * 1000) / 10) << " % × " << fmt(std::round(sy * 1000) / 10) << " %";
            m_tf_label = "Scale";
        }
    }
    command::drag_transform(doc.journal, doc.font, m_tf_pick, m);
    m_readout.set_text(o.str());
    m_area.queue_draw();
}

// ── the context menu (s09) ─────────────────────────────────────────────────
// Scott: "can some of these modifier tools be in a context menu and easy to
// find". Right-click on the canvas: the path tools, grouped, each with its key
// shown. Everything calls the same views/PathOps helpers as the inspector and
// the keys. A right-click on a path that isn't picked picks it first (the file
// manager's rule), so "right-click a stem -> Duplicate" just works.

GlyphCanvas::~GlyphCanvas() {
    if (m_menu_root && m_menu_outside) m_menu_root->remove_controller(m_menu_outside);
    if (m_menu) m_menu->unparent();
}

void GlyphCanvas::build_context_menu() {
    m_actions = Gio::SimpleActionGroup::create();
    const auto act = [this](const std::string& name, std::function<void()> f) {
        m_action[name] = m_actions->add_action(name, [this, f] {
            f();
            m_area.queue_draw();
        });
    };
    using A = command::Align;
    act("duplicate", [this] { duplicate_picked(m_ctx); });
    act("clone", [this] { clone_picked(m_ctx); });
    act("front", [this] { arrange_picked(m_ctx, command::Arrange::Front); });
    act("forward", [this] { arrange_picked(m_ctx, command::Arrange::Forward); });
    act("backward", [this] { arrange_picked(m_ctx, command::Arrange::Backward); });
    act("back", [this] { arrange_picked(m_ctx, command::Arrange::Back); });
    act("align-left", [this] { align_picked(m_ctx, A::Left); });
    act("align-centre", [this] { align_picked(m_ctx, A::HCenter); });
    act("align-right", [this] { align_picked(m_ctx, A::Right); });
    act("align-top", [this] { align_picked(m_ctx, A::Top); });
    act("align-middle", [this] { align_picked(m_ctx, A::VMiddle); });
    act("align-bottom", [this] { align_picked(m_ctx, A::Bottom); });
    act("distribute-h", [this] { align_picked(m_ctx, A::DistributeH); });
    act("distribute-v", [this] { align_picked(m_ctx, A::DistributeV); });
    act("flip-h", [this] { flip_picked(m_ctx, true); });
    act("flip-v", [this] { flip_picked(m_ctx, false); });
    act("rotate-left", [this] { rotate_picked(m_ctx, 90); });
    act("rotate-right", [this] { rotate_picked(m_ctx, -90); });
    act("rotate-180", [this] { rotate_picked(m_ctx, 180); });
    act("union", [this] { boolean_picked(m_ctx, geom::BoolOp::Union); });
    act("subtract", [this] { boolean_picked(m_ctx, geom::BoolOp::Subtract); });
    act("intersect", [this] { boolean_picked(m_ctx, geom::BoolOp::Intersect); });
    act("overlaps", [this] { remove_overlaps_here(m_ctx); });
    act("reverse", [this] { reverse_picked(m_ctx); });
    act("directions", [this] { correct_directions_here(m_ctx); });
    act("round-corners", [this] { round_picked(m_ctx); });          // M15b
    act("ink-trap", [this] { ink_trap_picked(m_ctx); });
    act("extremes", [this] { extremes_picked(m_ctx); });
    act("bolder", [this] { offset_picked(m_ctx, true); });
    act("lighter", [this] { offset_picked(m_ctx, false); });
    act("stroke", [this] { stroke_picked(m_ctx); });
    const auto segs = [this](bool curve) {
        std::string why;
        if (command::set_segments(m_ctx.doc.journal, m_ctx.doc.font, m_shown, m_pick, curve, &why)) say_pick();
        else if (m_ctx.say) m_ctx.say(why.empty() ? std::string("Pick two neighbouring nodes.") : why, false);
    };
    act("lines", [segs] { segs(false); });
    act("curves", [segs] { segs(true); });
    const auto type = [this](model::Node::Type t) {
        command::set_node_type(m_ctx.doc.journal, m_ctx.doc.font, m_shown, m_pick, t);
        m_ctx.pick_changed.emit();
    };
    act("corner", [type] { type(model::Node::Type::Corner); });
    act("smooth", [type] { type(model::Node::Type::Smooth); });
    act("symmetric", [type] { type(model::Node::Type::Symmetric); });
    act("cusp", [type] { type(model::Node::Type::Cusp); });
    act("select-all", [this] { on_key(GDK_KEY_a, 0, Gdk::ModifierType::CONTROL_MASK); });
    m_area.insert_action_group("canvas", m_actions);

    const auto item = [](const Glib::RefPtr<Gio::Menu>& m, const char* label, const char* action, const char* accel = nullptr) {
        auto it = Gio::MenuItem::create(label, std::string("canvas.") + action);
        if (accel) it->set_attribute_value("accel", Glib::Variant<Glib::ustring>::create(accel));
        m->append_item(it);
    };
    auto menu = Gio::Menu::create();
    {
        auto s = Gio::Menu::create();
        item(s, "Duplicate", "duplicate", "<Control>d");
        item(s, "Clone (offset copy)", "clone", "<Alt>d");
        menu->append_section(s);
    }
    {
        auto s = Gio::Menu::create();
        auto arrange = Gio::Menu::create();
        item(arrange, "Bring to Front", "front", "<Control><Shift>bracketright");
        item(arrange, "Bring Forward", "forward", "<Control>bracketright");
        item(arrange, "Send Backward", "backward", "<Control>bracketleft");
        item(arrange, "Send to Back", "back", "<Control><Shift>bracketleft");
        s->append_submenu("Arrange", arrange);
        auto align = Gio::Menu::create();
        auto a1 = Gio::Menu::create();
        item(a1, "Left", "align-left");
        item(a1, "Centre", "align-centre");
        item(a1, "Right", "align-right");
        align->append_section(a1);
        auto a2 = Gio::Menu::create();
        item(a2, "Top", "align-top");
        item(a2, "Middle", "align-middle");
        item(a2, "Bottom", "align-bottom");
        align->append_section(a2);
        auto a3 = Gio::Menu::create();
        item(a3, "Distribute Across", "distribute-h");
        item(a3, "Distribute Up", "distribute-v");
        align->append_section(a3);
        s->append_submenu("Align", align);
        auto tfm = Gio::Menu::create();
        auto t1 = Gio::Menu::create();
        item(t1, "Flip Horizontal", "flip-h");
        item(t1, "Flip Vertical", "flip-v");
        tfm->append_section(t1);
        auto t2 = Gio::Menu::create();
        item(t2, "Rotate 90\u00B0 Left", "rotate-left");
        item(t2, "Rotate 90\u00B0 Right", "rotate-right");
        item(t2, "Rotate 180\u00B0", "rotate-180");
        tfm->append_section(t2);
        s->append_submenu("Transform", tfm);
        auto comb = Gio::Menu::create();
        auto c1 = Gio::Menu::create();
        item(c1, "Union", "union");
        item(c1, "Subtract (top path cuts)", "subtract");
        item(c1, "Intersect (top path keeps)", "intersect");
        comb->append_section(c1);
        auto c2 = Gio::Menu::create();
        item(c2, "Remove Overlaps (whole glyph)", "overlaps");
        comb->append_section(c2);
        s->append_submenu("Combine", comb);
        menu->append_section(s);
    }
    {
        auto s = Gio::Menu::create();
        auto nodes = Gio::Menu::create();
        auto n1 = Gio::Menu::create();
        item(n1, "Make Lines", "lines", "l");
        item(n1, "Make Curves", "curves", "c");
        nodes->append_section(n1);
        auto n2 = Gio::Menu::create();
        item(n2, "Corner", "corner");
        item(n2, "Smooth", "smooth");
        item(n2, "Symmetric", "symmetric");
        item(n2, "Cusp", "cusp");
        nodes->append_section(n2);
        s->append_submenu("Nodes", nodes);
        auto shp = Gio::Menu::create();   // M15b
        item(shp, "Round Corners", "round-corners");
        item(shp, "Ink Trap", "ink-trap");
        item(shp, "Add Extreme Points", "extremes");
        auto shp2 = Gio::Menu::create();
        item(shp2, "Offset Bolder", "bolder");
        item(shp2, "Offset Lighter", "lighter");
        item(shp2, "Expand Stroke", "stroke");
        shp->append_section(shp2);
        s->append_submenu("Shape", shp);
        auto dir = Gio::Menu::create();
        item(dir, "Reverse Picked Paths", "reverse");
        item(dir, "Correct Directions (whole glyph)", "directions");
        s->append_submenu("Direction", dir);
        menu->append_section(s);
    }
    {
        auto s = Gio::Menu::create();
        item(s, "Pick All", "select-all", "<Control>a");
        menu->append_section(s);
    }
    m_menu = Gtk::make_managed<Gtk::PopoverMenu>(menu, Gtk::PopoverMenu::Flags::NESTED);
    m_menu->set_parent(m_area);
    m_menu->set_has_arrow(false);
    m_menu->set_halign(Gtk::Align::START);
    m_menu->set_autohide(true);
    m_menu->signal_closed().connect([this] {
        // Off the window after this event is done (it may be the one firing).
        Glib::signal_idle().connect_once([this] {
            if (m_menu && m_menu->get_visible()) return;   // shown again meanwhile
            if (m_menu_root && m_menu_outside) m_menu_root->remove_controller(m_menu_outside);
            m_menu_root = nullptr;
        });
        focus_canvas();   // keys go back to the canvas
    });
    // The fallback: a capture-phase click on the window (any button) while
    // the menu shows pops it down. Clicks on the menu itself never reach it
    // (the menu is its own surface).
    m_menu_outside = Gtk::GestureClick::create();
    m_menu_outside->set_button(0);
    m_menu_outside->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    m_menu_outside->signal_pressed().connect([this](int, double, double) {
        if (!m_menu || !m_menu->get_visible()) return;
        m_menu->popdown();
        m_menu_outside->set_state(Gtk::EventSequenceState::CLAIMED);   // the click only closes the menu
    });

    m_menu_click = Gtk::GestureClick::create();
    m_menu_click->set_button(GDK_BUTTON_SECONDARY);
    m_menu_click->signal_pressed().connect([this](int, double x, double y) {
        // Claim the press: no other gesture holds the sequence while the
        // menu takes its grab (a held sequence can keep a popup from closing).
        m_menu_click->set_state(Gtk::EventSequenceState::CLAIMED);
        show_context_menu(x, y);
    });
    m_area.add_controller(m_menu_click);
}

void GlyphCanvas::show_context_menu(double x, double y) {
    const model::Glyph* g = glyph();
    if (!g || m_mode != Mode::None) return;
    // A path under the pointer that has nothing picked: pick it whole.
    const geom::Vec2 fp = to_font(x, y);
    const geom::Hit h = geom::hit_test(*g, fp, kHitPx / m_s, false);
    int c = (h.kind == geom::Hit::Kind::Node || h.kind == geom::Hit::Kind::Segment) ? h.contour : -1;
    if (c < 0) c = geom::contour_at(*g, fp);
    if (c >= 0 && !picked_contours(m_ctx).count(c)) {
        m_ctx.guide_pick.reset();
        m_pick = geom::whole_contours(*g, {{c, 0, Part::Node}});
        m_ctx.pick_changed.emit();
        say_pick();
    }
    const std::set<int> cs = picked_contours(m_ctx);
    const int n = static_cast<int>(g->contours.size());
    const bool any = !cs.empty(), two = cs.size() >= 2;
    std::size_t nodes = 0;
    for (const auto& p : m_pick) nodes += p.part == Part::Node;
    const bool top_block = any && *cs.begin() == n - static_cast<int>(cs.size());
    const bool bottom_block = any && *cs.rbegin() == static_cast<int>(cs.size()) - 1;
    const bool whole = whole_paths_picked(m_ctx);
    const auto on = [this](const char* a, bool v) { m_action[a]->set_enabled(v); };
    on("duplicate", any);
    on("clone", any);
    on("front", any && !top_block);
    on("forward", any && !top_block);
    on("backward", any && !bottom_block);
    on("back", any && !bottom_block);
    const bool can_align = whole ? any : nodes >= 2;
    for (const char* a : {"align-left", "align-centre", "align-right", "align-top", "align-middle", "align-bottom"}) on(a, can_align);
    const bool can_dist = whole ? cs.size() >= 3 : nodes >= 3;
    on("distribute-h", can_dist);
    on("distribute-v", can_dist);
    const bool drawn = n > 0 || !g->components.empty();   // nothing picked: the whole glyph
    for (const char* a : {"flip-h", "flip-v", "rotate-left", "rotate-right", "rotate-180"}) on(a, drawn);
    on("union", two);
    on("subtract", two);
    on("intersect", two);
    on("overlaps", n >= 1);   // s10: one path can cross itself
    {   // M15b: corners / crotches among the picked nodes
        int corners = 0, crotches = 0;
        const auto wrong = geom::wrong_way(g->contours);
        for (const auto& p : m_pick) {
            if (p.part != Part::Node || p.contour < 0 || p.contour >= n) continue;
            const auto& con = g->contours[static_cast<std::size_t>(p.contour)];
            corners += geom::is_corner(con, p.index);
            crotches += geom::is_crotch(con, p.index, std::find(wrong.begin(), wrong.end(), p.contour) == wrong.end());
        }
        on("round-corners", corners > 0);
        on("ink-trap", crotches > 0);
        on("extremes", n >= 1);
        on("bolder", n >= 1);
        on("lighter", n >= 1);
        on("stroke", n >= 1);
    }
    on("reverse", any);
    on("directions", n >= 1);
    on("lines", nodes >= 2);
    on("curves", nodes >= 2);
    for (const char* a : {"corner", "smooth", "symmetric", "cusp"}) on(a, nodes >= 1);
    on("select-all", n > 0 || !g->anchors.empty() || !g->components.empty());
    Gdk::Rectangle r(static_cast<int>(x), static_cast<int>(y), 1, 1);
    m_menu->set_pointing_to(r);
    if (!m_menu_root) {
        if (auto* root = dynamic_cast<Gtk::Widget*>(get_root())) {
            m_menu_root = root;
            root->add_controller(m_menu_outside);
        }
    }
    m_menu->popup();
}

bool GlyphCanvas::on_key(guint keyval, guint, Gdk::ModifierType state) {
    const bool ctrl  = (state & Gdk::ModifierType::CONTROL_MASK) == Gdk::ModifierType::CONTROL_MASK;
    const bool shift = (state & Gdk::ModifierType::SHIFT_MASK) == Gdk::ModifierType::SHIFT_MASK;
    const model::Glyph* g = glyph();

    // s09: Clone -- an offset copy (Alt+D).
    if ((state & Gdk::ModifierType::ALT_MASK) == Gdk::ModifierType::ALT_MASK && !ctrl && m_mode == Mode::None &&
        (keyval == GDK_KEY_d || keyval == GDK_KEY_D)) {
        clone_picked(m_ctx);
        return true;
    }
    // s09: Duplicate the picked paths in place (Ctrl+D).
    if (ctrl && !shift && m_mode == Mode::None && (keyval == GDK_KEY_d || keyval == GDK_KEY_D)) {
        duplicate_picked(m_ctx);
        return true;
    }
    // s09: arrange the picked paths (Illustrator's keys). Shift+] arrives as
    // braceright on a US layout.
    if (ctrl && m_mode == Mode::None &&
        (keyval == GDK_KEY_bracketright || keyval == GDK_KEY_braceright ||
         keyval == GDK_KEY_bracketleft || keyval == GDK_KEY_braceleft)) {
        const bool up = keyval == GDK_KEY_bracketright || keyval == GDK_KEY_braceright;
        arrange_picked(m_ctx, up ? (shift ? command::Arrange::Front : command::Arrange::Forward)
                                 : (shift ? command::Arrange::Back : command::Arrange::Backward));
        return true;
    }

    if ((keyval == GDK_KEY_p || keyval == GDK_KEY_P) && !ctrl && m_mode == Mode::None) {
        set_pen(!m_pen.get_active());
        return true;
    }
    if (!ctrl && m_mode == Mode::None &&
        (keyval == GDK_KEY_r || keyval == GDK_KEY_R || keyval == GDK_KEY_e || keyval == GDK_KEY_E || keyval == GDK_KEY_backslash)) {
        set_tool(keyval == GDK_KEY_backslash ? Tool::Line : (keyval == GDK_KEY_r || keyval == GDK_KEY_R) ? Tool::Rect : Tool::Ellipse);
        return true;
    }
    if ((keyval == GDK_KEY_a || keyval == GDK_KEY_A) && !ctrl && m_mode == Mode::None) {
        set_tool(Tool::Pick);
        return true;
    }
    if ((keyval == GDK_KEY_v || keyval == GDK_KEY_V) && !ctrl && m_mode == Mode::None) {
        set_tool(Tool::Select);
        return true;
    }
    if (keyval == GDK_KEY_Escape && m_mode == Mode::None && (tool() == Tool::Measure || tool() == Tool::Hand || tool() == Tool::Rect ||
                                                          tool() == Tool::Ellipse || tool() == Tool::Line)) {
        set_tool(Tool::Select);
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
        else if (m_image_picked) { m_image_picked = false; m_area.queue_draw(); }
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
        m_image_picked && m_mode == Mode::None) {
        if (command::remove_image(m_ctx.doc.journal, m_ctx.doc.font, m_shown) && m_ctx.say)
            m_ctx.say(m_ctx.doc.journal.undo_label() + " -- Ctrl+Z brings it back.", false);
        m_image_picked = false;
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
    if (m_mode == Mode::Transform) return;
    if (m_mode == Mode::None && !m_space && transform_box_shown()) {   // M15a: say what a press here does
        // Corners scale; an edge's middle skews (it slides along the edge).
        static const char* cursors[] = {"nwse-resize", "ew-resize", "nesw-resize", "ns-resize",
                                        "nwse-resize", "ew-resize", "nesw-resize", "ns-resize"};
        if (const int th = transform_handle_at(x, y); th >= 0) {
            m_area.set_cursor(th == 20 ? "move" : th < 8 ? cursors[th] : "grab");
            return;
        }
    }
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
    if (tool() == Tool::Select) {   // whole paths: count paths (and whatever else rides along)
        std::set<int> paths;
        std::size_t other = 0;
        for (const auto& p : m_pick) (p.contour >= 0 ? (void)paths.insert(p.contour) : (void)++other);
        std::string t = std::to_string(paths.size()) + (paths.size() == 1 ? " path" : " paths");
        if (other) t += " + " + std::to_string(other) + (other == 1 ? " anchor / component" : " anchors / components");
        m_ctx.say(t + " picked" + (paths.size() == 1 ? ", contour " + std::to_string(*paths.begin() + 1) + " of " +
                                                         std::to_string(g->contours.size()) : std::string()), false);
        return;
    }
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
    if (m_tool_select.get_active()) return Tool::Select;
    if (m_pen.get_active()) return Tool::Pen;
    if (m_tool_rect.get_active()) return Tool::Rect;
    if (m_tool_ellipse.get_active()) return Tool::Ellipse;
    if (m_tool_line.get_active()) return Tool::Line;
    if (m_tool_measure.get_active()) return Tool::Measure;
    if (m_tool_hand.get_active()) return Tool::Hand;
    return Tool::Pick;
}

bool GlyphCanvas::use_tool(const std::string& name) {
    if (name == "select")       set_tool(Tool::Select);
    else if (name == "pick")    set_tool(Tool::Pick);
    else if (name == "pen")     set_tool(Tool::Pen);
    else if (name == "rectangle") set_tool(Tool::Rect);
    else if (name == "ellipse") set_tool(Tool::Ellipse);
    else if (name == "line")    set_tool(Tool::Line);
    else if (name == "measure") set_tool(Tool::Measure);
    else if (name == "hand")    set_tool(Tool::Hand);
    else return false;
    return true;
}

void GlyphCanvas::set_tool(Tool t) {
    Gtk::ToggleButton& b = t == Tool::Select ? static_cast<Gtk::ToggleButton&>(m_tool_select)
                         : t == Tool::Pen ? static_cast<Gtk::ToggleButton&>(m_pen)
                         : t == Tool::Rect ? static_cast<Gtk::ToggleButton&>(m_tool_rect)
                         : t == Tool::Ellipse ? static_cast<Gtk::ToggleButton&>(m_tool_ellipse)
                         : t == Tool::Line ? static_cast<Gtk::ToggleButton&>(m_tool_line)
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
    const bool shape = t == Tool::Rect || t == Tool::Ellipse || t == Tool::Line;
    m_area.set_cursor(t == Tool::Pen || t == Tool::Measure || shape ? "crosshair" : t == Tool::Hand ? "grab" : "default");
    m_stroke_label.set_visible(t == Tool::Line);
    m_stroke.set_visible(t == Tool::Line);
    if (t == Tool::Line) m_stroke.set_value(stroke_width());
    if (m_ctx.say) {
        switch (t) {
            case Tool::Pen:     m_ctx.say("Pen: click for a corner, drag for a curve; A or Escape for picking.", false); break;
            case Tool::Rect:    m_ctx.say("Rectangle: drag a box (Shift square, Alt from the centre); V to select.", false); break;
            case Tool::Ellipse: m_ctx.say("Ellipse: drag its box (Shift circle, Alt from the centre); V to select.", false); break;
            case Tool::Line:    m_ctx.say("Line: drag a bar of the Stroke width (Shift 45 degrees, Alt from the centre); V to select.", false); break;
            case Tool::Measure: m_ctx.say("Measure: drag a distance, click for the stems; V to select.", false); break;
            case Tool::Hand:    m_ctx.say("Hand: drag to pan; V to select.", false); break;
            case Tool::Pick:    m_ctx.say("Pick: nodes and handles (A). V for whole paths.", false); break;
            case Tool::Select:  m_ctx.say("Select: whole paths (V). A for nodes and handles; double-click a path to go in.", false); break;
        }
    }
    m_area.queue_draw();
    m_area.grab_focus();
}

// ── the primitives (M14a) ─────────────────────────────────────────────────

double GlyphCanvas::stroke_width() const { return views::stroke_width(m_ctx); }   // M15b: shared with Expand stroke

model::Contour GlyphCanvas::shape_now() const {
    const Tool t = tool();
    if (t == Tool::Line) {
        geom::Vec2 a, b;
        geom::drag_line(m_shape_a, m_shape_b, m_shape_shift, m_shape_alt, a, b);
        if ((b - a).length() < 1.0) return {};
        return geom::shape_bar(a, b, stroke_width());
    }
    const geom::Box bx = geom::drag_box(m_shape_a, m_shape_b, m_shape_shift, m_shape_alt);
    if (geom::degenerate(bx)) return {};
    return t == Tool::Ellipse ? geom::shape_ellipse(bx) : geom::shape_rect(bx);
}

// The pointer, snapped as a dragged node would be: the metric lines, the
// sidebearings, the glyph's nodes, guides, the grid (when shown).
geom::Vec2 GlyphCanvas::snap_free(geom::Vec2 p, bool on) {
    m_snap_x.reset();
    m_snap_y.reset();
    const model::Glyph* g = glyph();
    if (!on || !g || !m_snap.get_active()) return p;
    const auto& m = m_ctx.doc.font.metrics();
    const double os = m.overshoot;
    std::vector<double> xs{0.0, g->advance};
    std::vector<double> ys{0.0, double(m.x_height), double(m.cap_height), double(m.ascender),
                           double(m.descender), -os, m.x_height + os, m.cap_height + os};
    for (const auto& c : g->contours)
        for (const auto& n : c.nodes) { xs.push_back(n.x); ys.push_back(n.y); }
    for (const std::vector<model::Guide>* gs : {&m_ctx.doc.font.info().guides, &g->guides})
        for (const auto& gd : *gs) {
            if (gd.horizontal()) ys.push_back(gd.y);
            else if (gd.vertical()) xs.push_back(gd.x);
        }
    const double grid = m_grid.get_active() ? std::max(1.0, geom::grid_step(m_s)) : 0.0;
    const double tol = kHitPx / m_s;
    p.x = geom::snap_axis(p.x, xs, grid, tol, &m_snap_x);
    p.y = geom::snap_axis(p.y, ys, grid, tol, &m_snap_y);
    return p;
}

void GlyphCanvas::draw_shape(const Cairo::RefPtr<Cairo::Context>& cr) {
    const model::Contour c = shape_now();
    if (c.nodes.empty()) return;
    cr->save();
    cr->begin_new_path();
    const int n = static_cast<int>(c.nodes.size());
    const geom::Vec2 p0 = to_screen({c.nodes[0].x, c.nodes[0].y});
    cr->move_to(p0.x, p0.y);
    for (int i = 0; i < n; ++i) {
        const auto& a = c.nodes[static_cast<std::size_t>(i)];
        const auto& b = c.nodes[static_cast<std::size_t>((i + 1) % n)];
        const geom::Vec2 h1 = to_screen({a.cx2, a.cy2}), h2 = to_screen({b.cx1, b.cy1}), q = to_screen({b.x, b.y});
        cr->curve_to(h1.x, h1.y, h2.x, h2.y, q.x, q.y);
    }
    cr->close_path();
    cr->set_source_rgba(NS[0], NS[1], NS[2], 0.12);
    cr->fill_preserve();
    cr->set_source_rgba(NS[0], NS[1], NS[2], 0.9);
    cr->set_line_width(1.0);
    std::vector<double> dash{5.0, 3.0};
    cr->set_dash(dash, 0);
    cr->stroke();
    cr->restore();
}

// ── the background image (M13) ───────────────────────────────────────────

bool GlyphCanvas::image_px(const model::GlyphImage& im, int& w, int& h) const {
    const auto bytes = m_ctx.doc.font.image(im.file);
    return bytes && model::png_size(*bytes, w, h);
}

geom::Vec2 GlyphCanvas::image_corner(const model::GlyphImage& im, int w, int h, int i) const {
    const double u = (i == 1 || i == 2) ? w : 0, v = (i >= 2) ? h : 0;   // ccw from bottom-left, y up
    return {im.xx * u + im.yx * v + im.dx, im.xy * u + im.yy * v + im.dy};
}

bool GlyphCanvas::image_live(const model::Glyph& g) const {
    int w = 0, h = 0;
    return g.image && !g.image->locked && m_image.get_active() && image_px(*g.image, w, h);
}

int GlyphCanvas::image_handle_at(const model::Glyph& g, double sx, double sy) const {
    int w = 0, h = 0;
    if (!g.image || !image_px(*g.image, w, h)) return -1;
    for (int i = 0; i < 4; ++i) {
        const geom::Vec2 c = to_screen(image_corner(*g.image, w, h, i));
        if (std::abs(c.x - sx) <= kHitPx + 1 && std::abs(c.y - sy) <= kHitPx + 1) return i;
    }
    return -1;
}

bool GlyphCanvas::image_inside(const model::Glyph& g, double sx, double sy) const {
    int w = 0, h = 0;
    if (!g.image || !image_px(*g.image, w, h)) return false;
    const auto& im = *g.image;
    const double det = im.xx * im.yy - im.xy * im.yx;
    if (std::abs(det) < 1e-12) return false;
    const geom::Vec2 p = to_font(sx, sy);
    const double px = p.x - im.dx, py = p.y - im.dy;
    const double u = ( im.yy * px - im.yx * py) / det;
    const double v = (-im.xy * px + im.xx * py) / det;
    return u >= 0 && u <= w && v >= 0 && v <= h;
}

void GlyphCanvas::draw_image(const Cairo::RefPtr<Cairo::Context>& cr, const model::Glyph& g) {
    const auto& im = *g.image;
    const auto bytes = m_ctx.doc.font.image(im.file);
    int w = 0, h = 0;
    if (!bytes || !model::png_size(*bytes, w, h)) return;
    // Decode once per pool entry (drop entries whose bytes the pool let go).
    std::erase_if(m_surfaces, [](const auto& e) { return e.second.first.use_count() <= 1; });
    auto it = m_surfaces.find(bytes.get());
    if (it == m_surfaces.end()) {
        Cairo::RefPtr<Cairo::ImageSurface> mask;
        try {
            std::size_t at = 0;
            auto rgb = Cairo::ImageSurface::create_from_png_stream([&](unsigned char* data, unsigned int len) {
                if (at + len > bytes->size()) return CAIRO_STATUS_READ_ERROR;
                std::memcpy(data, bytes->data() + at, len);
                at += len;
                return CAIRO_STATUS_SUCCESS;
            });
            mask = ink_mask(rgb);
        } catch (...) {
            mask.reset();
        }
        it = m_surfaces.emplace(bytes.get(), std::make_pair(bytes, mask)).first;
    }
    const auto& mask = it->second.second;
    if (!mask || mask->get_width() <= 0) return;

    // The tint: the image's own UFO colour when it carries one, else the
    // app's (Preferences -> Background image colour).
    double tr = 0.88, tg = 0.44, tb = 0.24;
    {
        Gdk::RGBA pref;
        if (pref.set(m_ctx.settings.image_tint)) { tr = pref.get_red(); tg = pref.get_green(); tb = pref.get_blue(); }
    }
    if (!im.color.empty()) {
        double c[4] = {0, 0, 0, 1};
        if (std::sscanf(im.color.c_str(), "%lf,%lf,%lf,%lf", &c[0], &c[1], &c[2], &c[3]) >= 3) {
            tr = std::clamp(c[0], 0.0, 1.0); tg = std::clamp(c[1], 0.0, 1.0); tb = std::clamp(c[2], 0.0, 1.0);
        }
    }

    cr->save();
    cr->translate(m_ox, m_oy);
    cr->scale(m_s, -m_s);                                       // font units, y up
    cr->transform(Cairo::Matrix(im.xx, im.xy, im.yx, im.yy, im.dx, im.dy));   // image px (y up) -> font
    cr->translate(0, h);
    cr->scale(1, -1);                                           // the PNG's rows run down
    cr->set_source_rgba(tr, tg, tb, std::clamp(im.opacity, 0.0, 1.0));
    auto pat = Cairo::SurfacePattern::create(mask);
    // Close in, show the pixels as pixels (what a tracer wants to see).
    const double px_on_screen = m_s * std::sqrt(std::abs(im.xx * im.yy - im.xy * im.yx));
    pat->set_filter(px_on_screen > 3 ? Cairo::SurfacePattern::Filter::NEAREST : Cairo::SurfacePattern::Filter::GOOD);
    cr->mask(pat);
    cr->restore();

    if (im.locked) return;
    // Unlocked: a thin frame says "this moves"; picked, accent with handles.
    cr->begin_new_path();
    for (int i = 0; i < 4; ++i) {
        const geom::Vec2 c = to_screen(image_corner(im, w, h, i));
        if (i == 0) cr->move_to(c.x, c.y); else cr->line_to(c.x, c.y);
    }
    cr->close_path();
    if (m_image_picked) {
        cr->set_source_rgba(NS[0], NS[1], NS[2], 0.95);
        cr->set_line_width(1.5);
        cr->stroke();
        for (int i = 0; i < 4; ++i) {
            const geom::Vec2 c = to_screen(image_corner(im, w, h, i));
            cr->rectangle(std::round(c.x) - 4.5, std::round(c.y) - 4.5, 9, 9);
            cr->set_source_rgba(NF[0], NF[1], NF[2], 1.0);
            cr->fill_preserve();
            cr->set_source_rgba(NS[0], NS[1], NS[2], 1.0);
            cr->set_line_width(1.5);
            cr->stroke();
        }
    } else {
        std::vector<double> dash{3.0, 3.0};
        cr->set_dash(dash, 0);
        cr->set_source_rgba(NS[0], NS[1], NS[2], 0.55);
        cr->set_line_width(1.0);
        cr->stroke();
        cr->unset_dash();
    }
}

}  // namespace granjon::views
