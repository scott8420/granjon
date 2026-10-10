#include "views/Inspector.hpp"

#include "command/Verbs.hpp"
#include "model/Bounds.hpp"
#include "model/Find.hpp"
#include "unicode/Blocks.hpp"
#include "unicode/UcdTable.hpp"
#include "views/GlyphPaint.hpp"

#include <gtkmm/eventcontrollerfocus.h>
#include <gtkmm/grid.h>
#include <gtkmm/label.h>
#include <gtkmm/stringlist.h>
#include <gtkmm/togglebutton.h>

#include <cmath>
#include <initializer_list>
#include <optional>
#include <set>
#include <functional>

namespace granjon::views {

namespace {

// A card: a rounded group with a small heading and label/value rows.
// "Lu" -> "Letter, uppercase (Lu)".
std::string category_label(std::string_view gc) {
    static const std::pair<const char*, const char*> k[] = {
        {"Lu", "Letter, uppercase"}, {"Ll", "Letter, lowercase"}, {"Lt", "Letter, titlecase"},
        {"Lm", "Letter, modifier"}, {"Lo", "Letter, other"}, {"Mn", "Mark, nonspacing"},
        {"Mc", "Mark, spacing"}, {"Me", "Mark, enclosing"}, {"Nd", "Number, decimal"},
        {"Nl", "Number, letter"}, {"No", "Number, other"}, {"Pc", "Punctuation, connector"},
        {"Pd", "Punctuation, dash"}, {"Ps", "Punctuation, open"}, {"Pe", "Punctuation, close"},
        {"Pi", "Punctuation, initial quote"}, {"Pf", "Punctuation, final quote"}, {"Po", "Punctuation, other"},
        {"Sm", "Symbol, math"}, {"Sc", "Symbol, currency"}, {"Sk", "Symbol, modifier"}, {"So", "Symbol, other"},
        {"Zs", "Separator, space"}, {"Zl", "Separator, line"}, {"Zp", "Separator, paragraph"},
        {"Cc", "Control"}, {"Cf", "Format"}, {"Cs", "Surrogate"}, {"Co", "Private use"}, {"Cn", "Unassigned"}};
    for (const auto& [code, label] : k)
        if (gc == code) return std::string(label) + " (" + code + ")";
    return std::string(gc);
}

struct Card {
    Gtk::Box*  box;
    Gtk::Grid* grid;
    int        row = 0;

    Card(Gtk::Box& parent, const char* title) {
        box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
        box->add_css_class("card");
        auto t = Gtk::make_managed<Gtk::Label>(title);
        t->set_xalign(0.0f);
        t->add_css_class("card-title");
        box->append(*t);
        grid = Gtk::make_managed<Gtk::Grid>();
        grid->set_row_spacing(6);
        grid->set_column_spacing(10);
        box->append(*grid);
        parent.append(*box);
    }
    Gtk::Label* field(const char* label, Gtk::Widget& w) {
        auto l = Gtk::make_managed<Gtk::Label>(label);
        l->set_xalign(0.0f);
        l->add_css_class("field-label");
        w.set_hexpand(true);
        grid->attach(*l, 0, row);
        grid->attach(w, 1, row++);
        return l;
    }
    void wide(Gtk::Widget& w) { w.set_hexpand(true); grid->attach(w, 0, row++, 2, 1); }
};

// Commit a text field on Enter and on leaving it.
template <class F>
void on_commit(Gtk::Entry& e, F f) {
    e.signal_activate().connect(f);
    auto leave = Gtk::EventControllerFocus::create();
    leave->signal_leave().connect(f);
    e.add_controller(leave);
}

void spin_setup(Gtk::SpinButton& s, double lo, double hi) {
    s.set_range(lo, hi);
    s.set_increments(1, 10);
    s.set_numeric(true);
    s.set_width_chars(6);
    // A spin's own text undo is no use (it would only take back keystrokes in
    // a number); with it off, Ctrl+Z after the +/- buttons reaches the
    // journal. Shell::focus_is_text treats a spin as not-a-text-box to match.
    s.set_enable_undo(false);
}

std::string fmt_units(double v) {
    const double r = std::round(v);
    return std::to_string(static_cast<long>(r));
}

const model::GlyphStatus kStatuses[] = {model::GlyphStatus::Empty, model::GlyphStatus::Drafted,
                                        model::GlyphStatus::Done, model::GlyphStatus::Review};

}  // namespace

Inspector::Inspector(ViewContext& ctx)
    : widgets::ScrolledWindow("inspector"),
      m_ctx(ctx),
      m_pages("inspector.pages"),
      m_family("inspector.family"),
      m_style("inspector.style"),
      m_upm("inspector.upm"),
      m_preview("inspector.preview"),
      m_title("inspector.title"),
      m_name("inspector.name"),
      m_status("inspector.status"),
      m_width("inspector.width"),
      m_lsb("inspector.lsb"),
      m_rsb("inspector.rsb"),
      m_unicodes("inspector.unicodes"),
      m_section("inspector.section"),
      m_uname("inspector.uname"),
      m_category("inspector.category"),
      m_case("inspector.case"),
      m_note("inspector.note"),
      m_pick_what("inspector.pick_what"),
      m_px("inspector.pick_x"),
      m_py("inspector.pick_y"),
      m_anchor_name("inspector.anchor_name"),
      m_types("inspector.node_types", Gtk::Orientation::HORIZONTAL, 0),
      m_relative("inspector.handles_relative"),
      m_inx("inspector.in_x"),
      m_iny("inspector.in_y"),
      m_outx("inspector.out_x"),
      m_outy("inspector.out_y"),
      m_guide_what("inspector.guide_what"),
      m_gx("inspector.guide_x"),
      m_gy("inspector.guide_y"),
      m_gangle("inspector.guide_angle"),
      m_gname("inspector.guide_name"),
      m_gfont("inspector.guide_font"),
      m_gdelete("inspector.guide_delete"),
      m_comp_list("inspector.components", Gtk::Orientation::VERTICAL, 2),
      m_comp_add("inspector.component_add"),
      m_decompose("inspector.decompose"),
      m_anchor_list("inspector.anchors", Gtk::Orientation::VERTICAL, 2),
      m_anchor_add("inspector.anchor_add"),
      m_default_anchors("inspector.default_anchors") {
    add_css_class("inspector");
    set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    set_size_request(290, -1);
    m_pages.set_transition_type(Gtk::StackTransitionType::CROSSFADE);
    m_pages.set_transition_duration(120);
    m_pages.set_vhomogeneous(false);
    build_font_page();
    build_glyph_page();
    set_child(m_pages);
    m_ctx.selection_changed.connect([this] { refresh(); });
    m_ctx.pick_changed.connect([this] { refresh_pick(); refresh_guide(); });
}

void Inspector::build_font_page() {
    auto page = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
    auto head = Gtk::make_managed<Gtk::Label>("Font");
    head->set_xalign(0.0f);
    head->add_css_class("inspector-title");
    page->append(*head);

    Card names(*page, "NAMES");
    on_commit(m_family, [this] { on_family(); });
    on_commit(m_style, [this] { on_style(); });
    names.field("Family", m_family);
    names.field("Style", m_style);

    Card grid(*page, "GRID");
    spin_setup(m_upm, 16, 16384);
    m_upm.set_increments(1, 100);
    m_upm.signal_value_changed().connect([this] { on_upm(); });
    grid.field("Units per em", m_upm);

    Card vm(*page, "VERTICAL METRICS");
    using F = model::Metrics::Field;
    for (F f : {F::Ascender, F::CapHeight, F::XHeight, F::Descender, F::Overshoot}) {
        auto* s = Gtk::make_managed<widgets::SpinButton>(std::string("inspector.metric.") + model::field_name(f));
        spin_setup(*s, -16384, 16384);
        s->signal_value_changed().connect([this, f] { on_metric(f); });
        m_metric[static_cast<std::size_t>(f)] = s;
        vm.field(model::field_name(f), *s);
    }
    auto hint = Gtk::make_managed<Gtk::Label>("Font units, y-up from the baseline.");
    hint->set_xalign(0.0f);
    hint->set_wrap(true);
    hint->add_css_class("hint");
    vm.wide(*hint);

    m_pages.add(*page, "font");
}

void Inspector::build_glyph_page() {
    auto page = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
    m_title.set_xalign(0.0f);
    m_title.add_css_class("inspector-title");
    page->append(m_title);

    Card glyph(*page, "GLYPH");
    m_preview.set_content_height(150);
    m_preview.add_css_class("preview");
    m_preview.set_draw_func([this](const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
        const model::Glyph* g = m_ctx.doc.font.glyph(m_ctx.selected);
        if (!g) return;
        const auto fg = m_preview.get_color();
        const auto& f = m_ctx.doc.font;
        const auto& m = f.metrics();
        // Baseline and advance box, faint; then the drawing.
        const double em_h = std::max(1, m.ascender - m.descender);
        const double em_w = std::max({1.0, g->advance, f.info().units_per_em * 0.5});
        const double pad = 12;
        const double sc = std::min((w - 2 * pad) / em_w, (h - 2 * pad) / em_h);
        const double ox = (w - g->advance * sc) / 2.0;
        const double base = pad + m.ascender * sc + ((h - 2 * pad) - em_h * sc) / 2.0;
        cr->set_source_rgba(fg.get_red(), fg.get_green(), fg.get_blue(), 0.18);
        cr->set_line_width(1);
        cr->move_to(0, std::round(base) + 0.5);
        cr->line_to(w, std::round(base) + 0.5);
        cr->stroke();
        cr->rectangle(std::round(ox) + 0.5, std::round(base - m.ascender * sc) + 0.5,
                      std::round(g->advance * sc), std::round(em_h * sc));
        cr->stroke();
        cr->set_source_rgba(fg.get_red(), fg.get_green(), fg.get_blue(), 0.92);
        paint_glyph_fitted(cr, f, *g, w, h, pad);
    });
    glyph.wide(m_preview);
    on_commit(m_name, [this] { on_name(); });
    glyph.field("Name", m_name);
    auto list = Gtk::StringList::create({"Empty", "Drafted", "Done", "Review"});
    m_status.set_model(list);
    m_status.property_selected().signal_changed().connect([this] { on_status(); });
    glyph.field("Status", m_status);

    Card metrics(*page, "METRICS");
    spin_setup(m_width, 0, 32767);
    m_width.signal_value_changed().connect([this] { on_width(); });
    metrics.field("Width", m_width);
    m_lsb.set_xalign(0.0f);
    m_rsb.set_xalign(0.0f);
    metrics.field("Left side", m_lsb);
    metrics.field("Right side", m_rsb);

    // ── PICK: one picked point, typed coordinates ───────────────────────────
    Card pick(*page, "PICK");
    m_pick_card = pick.box;
    m_pick_what.set_xalign(0.0f);
    m_pick_what.add_css_class("hint");
    pick.wide(m_pick_what);
    spin_setup(m_px, -32768, 32767);
    spin_setup(m_py, -32768, 32767);
    m_px.signal_value_changed().connect([this] { on_pick_xy(); });
    m_py.signal_value_changed().connect([this] { on_pick_xy(); });
    // Node type: a linked row, applied to every picked node.
    {
        static const char* names[] = {"Corner", "Smooth", "Sym.", "Cusp"};
        static const char* tips[]  = {"Corner: handles move freely",
                                      "Smooth: one handle drags the other into line, lengths kept",
                                      "Symmetric: the handles mirror",
                                      "Cusp: handles locked to the arriving tangent, length only"};
        m_types.add_css_class("linked");
        m_types.add_css_class("node-types");
        m_types.set_homogeneous(true);
        for (int i = 0; i < 4; ++i) {
            auto b = Gtk::make_managed<Gtk::ToggleButton>(names[i]);
            b->set_tooltip_text(tips[i]);
            b->signal_clicked().connect([this, i] { on_node_type(i); });
            m_type_buttons[i] = b;
            m_types.append(*b);
        }
        pick.wide(m_types);   // full width: four buttons beside a label overflow the panel
    }
    m_x_label = pick.field("X", m_px);
    m_y_label = pick.field("Y", m_py);
    // A picked node's handles: two spins a row, relative to the node by default.
    auto pair_row = [](Gtk::Entry& a, Gtk::Entry& b) {
        auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        a.set_hexpand(true);
        b.set_hexpand(true);
        row->append(a);
        row->append(b);
        return row;
    };
    for (auto* e : {&m_inx, &m_iny, &m_outx, &m_outy}) {
        e->set_width_chars(5);
        e->set_input_purpose(Gtk::InputPurpose::NUMBER);
    }
    m_inx.set_placeholder_text("x");  m_iny.set_placeholder_text("y");
    m_outx.set_placeholder_text("x"); m_outy.set_placeholder_text("y");
    on_commit(m_inx, [this] { on_handle_xy(false); });
    on_commit(m_iny, [this] { on_handle_xy(false); });
    on_commit(m_outx, [this] { on_handle_xy(true); });
    on_commit(m_outy, [this] { on_handle_xy(true); });
    m_in_row = pair_row(m_inx, m_iny);
    m_out_row = pair_row(m_outx, m_outy);
    m_in_label = pick.field("In", *m_in_row);
    m_out_label = pick.field("Out", *m_out_row);
    m_relative.set_label("Relative to the node");
    m_relative.set_active(m_ctx.settings.handles_relative);
    m_relative.set_halign(Gtk::Align::START);
    m_relative.set_tooltip_text("Handles as offsets from their node (on) or font coordinates (off). Remembered.");
    m_relative.signal_toggled().connect([this] {
        if (m_ctx.settings.handles_relative != m_relative.get_active()) {
            m_ctx.settings.handles_relative = m_relative.get_active();
            if (m_ctx.save_settings) m_ctx.save_settings();
            m_ctx.settings_changed.emit();
        }
        refresh_pick();
    });
    pick.wide(m_relative);
    // Align / distribute: two picked nodes or anchors, or more.
    {
        static const char* names[] = {"Left", "Centre", "Right", "\u2194", "Top", "Middle", "Bottom", "\u2195"};
        static const command::Align ops[] = {command::Align::Left, command::Align::HCenter, command::Align::Right,
                                             command::Align::DistributeH, command::Align::Top, command::Align::VMiddle,
                                             command::Align::Bottom, command::Align::DistributeV};
        auto rows = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
        for (int r = 0; r < 2; ++r) {
            auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
            row->add_css_class("linked");
            row->add_css_class("node-types");
            row->set_homogeneous(true);
            for (int k = r * 4; k < r * 4 + 4; ++k) {
                auto b = Gtk::make_managed<Gtk::Button>(names[k]);
                b->set_tooltip_text(k == 3 ? "Space them evenly across, between the outermost two"
                                   : k == 7 ? "Space them evenly up, between the outermost two"
                                            : "Align to the pick's edge / centre -- one undo step");
                const auto op = ops[k];
                b->signal_clicked().connect([this, op] {
                    command::align_points(m_ctx.doc.journal, m_ctx.doc.font, m_ctx.selected, m_ctx.pick, op);
                });
                row->append(*b);
            }
            rows->append(*row);
        }
        m_align_rows = rows;
        pick.wide(*rows);
    }
    m_ctx.settings_changed.connect([this] {
        if (m_relative.get_active() != m_ctx.settings.handles_relative) m_relative.set_active(m_ctx.settings.handles_relative);
    });
    on_commit(m_anchor_name, [this] { on_anchor_name(); });
    m_anchor_name_label = pick.field("Name", m_anchor_name);

    // ── GUIDE: a picked guide ───────────────────────────────────────────────
    Card guide(*page, "GUIDE");
    m_guide_card = guide.box;
    m_guide_what.set_xalign(0.0f);
    m_guide_what.add_css_class("hint");
    guide.wide(m_guide_what);
    spin_setup(m_gx, -32768, 32767);
    spin_setup(m_gy, -32768, 32767);
    spin_setup(m_gangle, 0, 359);
    for (auto* s : {&m_gx, &m_gy, &m_gangle}) s->signal_value_changed().connect([this] { on_guide_field(); });
    m_gx_label = guide.field("X", m_gx);
    m_gy_label = guide.field("Y", m_gy);
    guide.field("Angle", m_gangle);
    on_commit(m_gname, [this] { on_guide_field(); });
    m_gname.set_placeholder_text("optional");
    guide.field("Name", m_gname);
    m_gfont.set_label("Whole font");
    m_gfont.set_tooltip_text("On: every glyph shows this guide (font-wide). Off: this glyph only.");
    m_gfont.signal_toggled().connect([this] {
        if (m_painting || !m_ctx.guide_pick) return;
        const bool want_font = m_gfont.get_active();
        const bool is_font = m_ctx.guide_pick->owner.empty();
        if (want_font == is_font || m_ctx.selected.empty()) return;
        int at = -1;
        if (command::move_guide_owner(m_ctx.doc.journal, m_ctx.doc.font, m_ctx.selected, want_font,
                                      m_ctx.guide_pick->index, &at))
            m_ctx.guide_pick = ViewContext::GuideRef{want_font ? std::string{} : m_ctx.selected, at};
        m_ctx.pick_changed.emit();
    });
    m_gdelete.set_label("Delete");
    m_gdelete.signal_clicked().connect([this] {
        if (!m_ctx.guide_pick) return;
        command::remove_guide(m_ctx.doc.journal, m_ctx.doc.font, m_ctx.guide_pick->owner, m_ctx.guide_pick->index);
        m_ctx.guide_pick.reset();
        m_ctx.pick_changed.emit();
    });
    {
        auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        row->append(m_gfont);
        row->append(m_gdelete);
        guide.wide(*row);
    }

    // ── COMPONENTS ──────────────────────────────────────────────────────────
    Card comps(*page, "COMPONENTS");
    comps.wide(m_comp_list);
    m_comp_add.set_placeholder_text("Add a component: glyph name");
    on_commit(m_comp_add, [this] {
        const std::string base = m_comp_add.get_text();
        if (base.empty() || m_ctx.selected.empty()) return;
        std::string why;
        if (command::add_component(m_ctx.doc.journal, m_ctx.doc.font, m_ctx.selected, base, 0, 0, &why))
            m_comp_add.set_text("");
        else if (!why.empty())
            m_ctx.say(why, true);
    });
    comps.wide(m_comp_add);
    m_decompose.set_label("Decompose");
    m_decompose.set_halign(Gtk::Align::START);
    m_decompose.set_tooltip_text("Turn the components into plain outlines -- one undo step");
    m_decompose.signal_clicked().connect([this] {
        std::string why;
        if (!command::decompose(m_ctx.doc.journal, m_ctx.doc.font, m_ctx.selected, &why) && !why.empty())
            m_ctx.say(why, false);
    });
    comps.wide(m_decompose);

    // ── ANCHORS ─────────────────────────────────────────────────────────────
    Card anchors(*page, "ANCHORS");
    anchors.wide(m_anchor_list);
    m_anchor_add.set_placeholder_text("Add an anchor: name");
    on_commit(m_anchor_add, [this] {
        const std::string name = m_anchor_add.get_text();
        const model::Glyph* g = m_ctx.doc.font.glyph(m_ctx.selected);
        if (name.empty() || !g) return;
        // Where to put it: over the drawing's centre, at the top (or the
        // baseline for a "bottom"-ish name); the designer drags it from there.
        const auto box = model::bounds(m_ctx.doc.font, *g);
        const double x = box ? (box->x0 + box->x1) / 2.0 : g->advance / 2.0;
        const bool low = name.find("bottom") != std::string::npos || name.find("ogonek") != std::string::npos;
        const double y = low ? 0.0 : (box ? box->y1 : m_ctx.doc.font.metrics().x_height);
        std::string why;
        if (command::add_anchor(m_ctx.doc.journal, m_ctx.doc.font, m_ctx.selected, name, x, y, &why))
            m_anchor_add.set_text("");
        else if (!why.empty())
            m_ctx.say(why, true);
    });
    anchors.wide(m_anchor_add);
    m_default_anchors.set_label("Add default anchors");
    m_default_anchors.set_halign(Gtk::Align::START);
    m_default_anchors.set_tooltip_text("top / bottom on a letter, _top / _bottom on a mark -- one undo step");
    m_default_anchors.signal_clicked().connect([this] {
        std::string why;
        if (!command::add_default_anchors(m_ctx.doc.journal, m_ctx.doc.font, {m_ctx.selected}, &why) && !why.empty())
            m_ctx.say(why, false);
    });
    anchors.wide(m_default_anchors);

    Card uni(*page, "UNICODE");
    m_unicodes.set_placeholder_text("U+0041  or  A");
    on_commit(m_unicodes, [this] { on_unicodes(); });
    uni.field("Codepoints", m_unicodes);
    m_section.set_xalign(0.0f);
    m_section.add_css_class("hint");
    uni.field("Block", m_section);
    m_uname.set_xalign(0.0f);
    m_uname.set_wrap(true);
    m_uname.set_max_width_chars(24);
    m_uname.add_css_class("hint");
    uni.field("Name", m_uname);
    m_category.set_xalign(0.0f);
    m_category.add_css_class("hint");
    uni.field("Category", m_category);
    m_case.set_halign(Gtk::Align::START);
    m_case.set_tooltip_text("Go to the other case (Alt+C)");
    m_case.signal_clicked().connect([this] {
        if (auto* w = dynamic_cast<Gtk::Widget*>(this)) w->activate_action("win.other-case");
    });
    uni.field("Case", m_case);

    Card note(*page, "NOTE");
    m_note.set_placeholder_text("A note to yourself");
    on_commit(m_note, [this] { on_note(); });
    note.wide(m_note);

    m_pages.add(*page, "glyph");
}

void Inspector::refresh() {
    m_painting = true;
    const auto& f = m_ctx.doc.font;
    const model::Glyph* g = m_ctx.selected.empty() ? nullptr : f.glyph(m_ctx.selected);
    if (!g) {
        m_pages.set_visible_child("font");
        if (!m_family.has_focus()) m_family.set_text(f.info().family);
        if (!m_style.has_focus())  m_style.set_text(f.info().style);
        m_upm.set_value(f.info().units_per_em);
        for (std::size_t i = 0; i < m_metric.size(); ++i)
            if (m_metric[i]) m_metric[i]->set_value(f.metrics().get(static_cast<model::Metrics::Field>(i)));
    } else {
        m_pages.set_visible_child("glyph");
        m_title.set_text(g->name);
        if (!m_name.has_focus()) m_name.set_text(g->name);
        for (unsigned i = 0; i < 4; ++i) if (kStatuses[i] == g->status) m_status.set_selected(i);
        m_width.set_value(g->advance);
        if (const auto sb = model::sidebearings(f, *g)) {
            m_lsb.set_text(fmt_units(sb->left));
            m_rsb.set_text(fmt_units(sb->right));
        } else {
            m_lsb.set_text("—");
            m_rsb.set_text("—");
        }
        if (!m_unicodes.has_focus()) {
            std::string t;
            for (char32_t cp : g->unicodes) t += (t.empty() ? "" : " ") + unicode::format_cp(cp);
            m_unicodes.set_text(t);
        }
        m_section.set_text(unicode::section_of(g->unicodes));
        const char32_t first = g->unicodes.empty() ? 0 : g->unicodes.front();
        m_uname.set_text(first ? unicode::name_of(first) : std::string("\u2014"));
        m_category.set_text(first ? category_label(unicode::category(first)) : std::string("\u2014"));
        char32_t other = 0;
        const std::string to = model::other_case_glyph(f, g->name, &other);
        m_case.set_sensitive(other != 0);
        m_case.set_label(!other ? std::string("No case")
                         : !to.empty() ? "Go to " + to
                                       : unicode::utf8(other) + "  " + unicode::format_cp(other) + "  (none yet)");
        if (!m_note.has_focus()) m_note.set_text(g->note);
        refresh_parts(*g);
        m_preview.queue_draw();
    }
    m_painting = false;
    refresh_pick();
    refresh_guide();
}

namespace {
std::string xy(double x, double y) { return fmt_units(x) + ", " + fmt_units(y); }
// A flat list row: clicking it picks that item on the canvas.
Gtk::Button* part_row(const std::string& text, std::function<void()> pick) {
    auto b = Gtk::make_managed<Gtk::Button>();
    auto l = Gtk::make_managed<Gtk::Label>(text);
    l->set_xalign(0.0f);
    b->set_child(*l);
    b->add_css_class("flat");
    b->add_css_class("part-row");
    b->signal_clicked().connect(std::move(pick));
    return b;
}
}  // namespace

void Inspector::refresh_parts(const model::Glyph& g) {
    using Part = geom::PointRef::Part;
    auto pick = [this](geom::PointRef r) {
        return [this, r] {
            m_ctx.pick = {r};
            m_ctx.pick_changed.emit();
            if (m_ctx.show_view) m_ctx.show_view("editor");
        };
    };
    while (auto* c = m_comp_list.get_first_child()) m_comp_list.remove(*c);
    for (int i = 0; i < static_cast<int>(g.components.size()); ++i) {
        const auto& k = g.components[i];
        m_comp_list.append(*part_row(k.base + "    " + xy(k.dx, k.dy), pick({-1, i, Part::Component})));
    }
    if (g.components.empty()) {
        auto none = Gtk::make_managed<Gtk::Label>("None");
        none->set_xalign(0.0f);
        none->add_css_class("hint");
        m_comp_list.append(*none);
    }
    m_decompose.set_sensitive(!g.components.empty());
    while (auto* c = m_anchor_list.get_first_child()) m_anchor_list.remove(*c);
    for (int i = 0; i < static_cast<int>(g.anchors.size()); ++i) {
        const auto& a = g.anchors[i];
        m_anchor_list.append(*part_row(a.name + "    " + xy(a.x, a.y), pick({-1, i, Part::Anchor})));
    }
    if (g.anchors.empty()) {
        auto none = Gtk::make_managed<Gtk::Label>("None");
        none->set_xalign(0.0f);
        none->add_css_class("hint");
        m_anchor_list.append(*none);
    }
}

void Inspector::refresh_pick() {
    if (!m_pick_card) return;
    using Part = geom::PointRef::Part;
    const model::Glyph* g = m_ctx.selected.empty() ? nullptr : m_ctx.doc.font.glyph(m_ctx.selected);
    // The nodes in the pick (a picked handle counts its node) -- the Type row.
    std::set<std::pair<int, int>> nodes;
    if (g)
        for (const auto& p : m_ctx.pick)
            if (geom::valid(*g, p) && (p.part == Part::Node || p.part == Part::In || p.part == Part::Out))
                nodes.insert({p.contour, p.index});
    const bool one = g && m_ctx.pick.size() == 1 && geom::valid(*g, *m_ctx.pick.begin());
    std::size_t anchors_picked = 0;
    if (g) for (const auto& q : m_ctx.pick) if (geom::valid(*g, q) && q.part == Part::Anchor) ++anchors_picked;
    m_pick_card->set_visible(one || !nodes.empty() || anchors_picked >= 2);
    if (!g || (!one && nodes.empty() && anchors_picked < 2)) return;
    const bool was = m_painting;
    m_painting = true;

    // Type: the shared type lights its button; a mix lights none.
    std::optional<model::Node::Type> shared;
    bool mixed = false;
    for (const auto& [c, i] : nodes) {
        const auto t = g->contours[c].nodes[i].type;
        if (!shared) shared = t;
        else if (*shared != t) mixed = true;
    }
    static const model::Node::Type order[] = {model::Node::Type::Corner, model::Node::Type::Smooth,
                                              model::Node::Type::Symmetric, model::Node::Type::Cusp};
    for (int k = 0; k < 4; ++k) m_type_buttons[k]->set_active(!mixed && shared && *shared == order[k]);
    m_types.set_visible(!nodes.empty());
    if (m_types_label) m_types_label->set_visible(!nodes.empty());

    const bool rel = m_relative.get_active();
    const auto& p = *m_ctx.pick.begin();
    const bool node = one && p.part == Part::Node;
    const bool handle = one && (p.part == Part::In || p.part == Part::Out);
    for (auto* w : std::initializer_list<Gtk::Widget*>{&m_px, &m_py, m_x_label, m_y_label}) w->set_visible(one);
    for (auto* w : std::initializer_list<Gtk::Widget*>{m_in_row, m_out_row, m_in_label, m_out_label}) w->set_visible(node);
    m_relative.set_visible(node || handle);
    std::size_t alignable = 0;
    for (const auto& q : m_ctx.pick)
        if (geom::valid(*g, q) && (q.part == Part::Node || q.part == Part::Anchor)) ++alignable;
    if (m_align_rows) m_align_rows->set_visible(alignable >= 2);

    std::string what = std::to_string(nodes.size()) + " nodes";
    if (one) {
        geom::Vec2 v = geom::position(*g, p);
        if (handle && rel) {
            const auto& n = g->contours[p.contour].nodes[p.index];
            v = v - geom::Vec2{n.x, n.y};
        }
        if (!m_px.has_focus()) m_px.set_value(v.x);
        if (!m_py.has_focus()) m_py.set_value(v.y);
        switch (p.part) {
            case Part::Node:      what = "Node " + std::to_string(p.index) + " of contour " + std::to_string(p.contour); break;
            case Part::In:        what = std::string("In-handle of node ") + std::to_string(p.index) + (rel ? " (from the node)" : ""); break;
            case Part::Out:       what = std::string("Out-handle of node ") + std::to_string(p.index) + (rel ? " (from the node)" : ""); break;
            case Part::Anchor:    what = "Anchor"; break;
            case Part::Component: what = "Component on " + g->components[p.index].base + " (its offset)"; break;
        }
    }
    if (node) {
        const auto& n = g->contours[p.contour].nodes[p.index];
        const double ox = rel ? n.x : 0, oy = rel ? n.y : 0;
        auto put = [](Gtk::Entry& e, double v) { if (!e.has_focus()) e.set_text(fmt_units(v)); };
        put(m_inx, n.cx1 - ox);
        put(m_iny, n.cy1 - oy);
        put(m_outx, n.cx2 - ox);
        put(m_outy, n.cy2 - oy);
    }
    m_pick_what.set_text(what);
    const bool anchor = one && p.part == Part::Anchor;
    m_anchor_name.set_visible(anchor);
    if (m_anchor_name_label) m_anchor_name_label->set_visible(anchor);
    if (anchor && !m_anchor_name.has_focus()) m_anchor_name.set_text(g->anchors[p.index].name);
    m_painting = was;
}

void Inspector::refresh_guide() {
    if (!m_guide_card) return;
    const model::Guide* gd = nullptr;
    if (m_ctx.guide_pick) {
        const auto& r = *m_ctx.guide_pick;
        const auto* gs = r.owner.empty() ? &m_ctx.doc.font.info().guides
                         : m_ctx.doc.font.glyph(r.owner) ? &m_ctx.doc.font.glyph(r.owner)->guides : nullptr;
        if (gs && r.index >= 0 && r.index < static_cast<int>(gs->size())) gd = &(*gs)[r.index];
    }
    m_guide_card->set_visible(gd != nullptr);
    if (!gd) return;
    const bool was = m_painting;
    m_painting = true;
    const bool font = m_ctx.guide_pick->owner.empty();
    m_guide_what.set_text(std::string(gd->horizontal() ? "Horizontal" : gd->vertical() ? "Vertical" : "Angled") +
                          (font ? " guide, whole font" : " guide, this glyph"));
    m_gx.set_visible(!gd->horizontal());
    m_gx_label->set_visible(!gd->horizontal());
    m_gy.set_visible(!gd->vertical());
    m_gy_label->set_visible(!gd->vertical());
    if (!m_gx.has_focus()) m_gx.set_value(gd->x);
    if (!m_gy.has_focus()) m_gy.set_value(gd->y);
    if (!m_gangle.has_focus()) m_gangle.set_value(gd->angle);
    if (!m_gname.has_focus()) m_gname.set_text(gd->name);
    m_gfont.set_active(font);
    m_painting = was;
}

void Inspector::on_guide_field() {
    if (m_painting || !m_ctx.guide_pick) return;
    model::Guide gd;
    gd.x = m_gx.get_value();
    gd.y = m_gy.get_value();
    gd.angle = m_gangle.get_value();
    gd.name = m_gname.get_text();
    command::set_guide(m_ctx.doc.journal, m_ctx.doc.font, m_ctx.guide_pick->owner, m_ctx.guide_pick->index, gd);
}

void Inspector::on_pick_xy() {
    if (m_painting || m_ctx.pick.size() != 1) return;
    using Part = geom::PointRef::Part;
    const auto& p = *m_ctx.pick.begin();
    double x = m_px.get_value(), y = m_py.get_value();
    const model::Glyph* g = m_ctx.doc.font.glyph(m_ctx.selected);
    if (g && geom::valid(*g, p) && (p.part == Part::In || p.part == Part::Out) && m_relative.get_active()) {
        const auto& n = g->contours[p.contour].nodes[p.index];
        x += n.x;
        y += n.y;
    }
    command::set_position(m_ctx.doc.journal, m_ctx.doc.font, m_ctx.selected, p, x, y);
}

void Inspector::on_handle_xy(bool out) {
    if (m_painting || m_ctx.pick.size() != 1) return;
    const auto& p = *m_ctx.pick.begin();
    const model::Glyph* g = m_ctx.doc.font.glyph(m_ctx.selected);
    if (!g || !geom::valid(*g, p) || p.part != geom::PointRef::Part::Node) return;
    const auto& n = g->contours[p.contour].nodes[p.index];
    const double ox = m_relative.get_active() ? n.x : 0, oy = m_relative.get_active() ? n.y : 0;
    const geom::PointRef h{p.contour, p.index, out ? geom::PointRef::Part::Out : geom::PointRef::Part::In};
    // A field that does not read as a number puts the old value back.
    auto num = [](Gtk::Entry& e, double& v) {
        try { std::size_t used = 0; v = std::stod(std::string(e.get_text()), &used); return used > 0; }
        catch (...) { return false; }
    };
    double fx = 0, fy = 0;
    if (!num(out ? m_outx : m_inx, fx) || !num(out ? m_outy : m_iny, fy)) { refresh_pick(); return; }
    const double x = fx + ox;
    const double y = fy + oy;
    command::set_position(m_ctx.doc.journal, m_ctx.doc.font, m_ctx.selected, h, x, y);
}

void Inspector::on_node_type(int k) {
    if (m_painting) return;
    static const model::Node::Type order[] = {model::Node::Type::Corner, model::Node::Type::Smooth,
                                              model::Node::Type::Symmetric, model::Node::Type::Cusp};
    if (!command::set_node_type(m_ctx.doc.journal, m_ctx.doc.font, m_ctx.selected, m_ctx.pick, order[k]))
        refresh_pick();   // nothing changed: put the buttons back
}

void Inspector::on_anchor_name() {
    if (m_painting || m_ctx.pick.size() != 1) return;
    const auto& p = *m_ctx.pick.begin();
    if (p.part != geom::PointRef::Part::Anchor) return;
    std::string why;
    if (!command::rename_anchor(m_ctx.doc.journal, m_ctx.doc.font, m_ctx.selected, p.index, m_anchor_name.get_text(), &why) &&
        !why.empty()) {
        m_ctx.say(why, true);
        refresh_pick();
    }
}

void Inspector::focus_name() {
    m_name.grab_focus();
    m_name.select_region(0, -1);
}

// ── font handlers ──────────────────────────────────────────────────────────
void Inspector::on_family() {
    if (m_painting) return;
    const std::string v = m_family.get_text();
    if (v != m_ctx.doc.font.info().family) command::set_family(m_ctx.doc.journal, m_ctx.doc.font, v);
}
void Inspector::on_style() {
    if (m_painting) return;
    const std::string v = m_style.get_text();
    if (v != m_ctx.doc.font.info().style) command::set_style(m_ctx.doc.journal, m_ctx.doc.font, v);
}
void Inspector::on_upm() {
    if (m_painting) return;
    std::string why;
    if (!command::set_units_per_em(m_ctx.doc.journal, m_ctx.doc.font, m_upm.get_value_as_int(), &why) && !why.empty())
        m_ctx.say(why, true);
}
void Inspector::on_metric(model::Metrics::Field f) {
    if (m_painting) return;
    command::set_metric(m_ctx.doc.journal, m_ctx.doc.font, f,
                        m_metric[static_cast<std::size_t>(f)]->get_value_as_int());
}

// ── glyph handlers ─────────────────────────────────────────────────────────
void Inspector::on_name() {
    if (m_painting || m_ctx.selected.empty()) return;
    const std::string from = m_ctx.selected, to = m_name.get_text();
    if (to == from) return;
    std::string why;
    if (command::rename_glyph(m_ctx.doc.journal, m_ctx.doc.font, from, to, &why)) {
        m_ctx.select(to);
        m_ctx.doc.journal.reselect({to});
    } else {
        if (!why.empty()) m_ctx.say(why, true);
        m_name.set_text(from);
    }
}
void Inspector::on_status() {
    if (m_painting || m_ctx.selected.empty()) return;
    const unsigned i = m_status.get_selected();
    if (i < 4) command::set_status(m_ctx.doc.journal, m_ctx.doc.font, m_ctx.selected, kStatuses[i]);
}
void Inspector::on_width() {
    if (m_painting || m_ctx.selected.empty()) return;
    command::set_advance(m_ctx.doc.journal, m_ctx.doc.font, m_ctx.selected, m_width.get_value());
}
void Inspector::on_unicodes() {
    if (m_painting || m_ctx.selected.empty()) return;
    std::vector<char32_t> cps;
    if (!unicode::parse_cps(m_unicodes.get_text(), cps)) {
        m_ctx.say("Codepoints are written U+0041 (or 0041, or the character itself), separated by spaces.", true);
        refresh();
        return;
    }
    const model::Glyph* g = m_ctx.doc.font.glyph(m_ctx.selected);
    if (g && cps != g->unicodes) command::set_unicodes(m_ctx.doc.journal, m_ctx.doc.font, m_ctx.selected, cps);
}
void Inspector::on_note() {
    if (m_painting || m_ctx.selected.empty()) return;
    const model::Glyph* g = m_ctx.doc.font.glyph(m_ctx.selected);
    const std::string v = m_note.get_text();
    if (g && v != g->note) command::set_note(m_ctx.doc.journal, m_ctx.doc.font, m_ctx.selected, v);
}

}  // namespace granjon::views
