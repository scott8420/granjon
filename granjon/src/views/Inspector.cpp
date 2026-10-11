#include "views/Inspector.hpp"
#include "model/Presets.hpp"

#include "model/Profiles.hpp"
#include "model/Spacing.hpp"

#include "command/Verbs.hpp"
#include "model/Bounds.hpp"
#include "model/Find.hpp"
#include "unicode/TypeIt.hpp"
#include "unicode/Blocks.hpp"
#include "unicode/UcdTable.hpp"
#include "views/GlyphPaint.hpp"
#include "views/ImagePlace.hpp"
#include "export/Codes.hpp"

#include <gtkmm/eventcontrollerfocus.h>
#include <gtkmm/grid.h>
#include <gtkmm/label.h>
#include <gtkmm/stringlist.h>
#include <gtkmm/togglebutton.h>

#include <cmath>
#include <cstdio>
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
      m_key("inspector.metrics_key"),
      m_key_state("inspector.metrics_key_state"),
      m_key_apply("inspector.metrics_key_apply"),
      m_kern_title("inspector.kern_title"),
      m_kern_preview("inspector.kern_preview"),
      m_kern_left("inspector.kern_left"),
      m_kern_right("inspector.kern_right"),
      m_kern_entry("inspector.kern_entry"),
      m_kern_value("inspector.kern_value"),
      m_kern_exception("inspector.kern_exception"),
      m_kern_remove("inspector.kern_remove"),
      m_kern_contact("inspector.kern_contact"),
      m_kern_optical("inspector.kern_optical"),
      m_kern_control("inspector.kern_control"),
      m_kern_flag("inspector.kern_flag"),
      m_kern_to_gap("inspector.kern_to_gap"),
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
      m_is_part("inspector.is_part"),
      m_type_it("inspector.type_it"),
      m_part_list("inspector.part_list"),
      m_place_linked("inspector.place_part_linked"),
      m_place_contours("inspector.place_part_contours"),
      m_part_name("inspector.part_name"),
      m_make_part("inspector.make_part"),
      m_anchor_list("inspector.anchors", Gtk::Orientation::VERTICAL, 2),
      m_anchor_add("inspector.anchor_add"),
      m_default_anchors("inspector.default_anchors"),
      m_outline_info("inspector.outline_info"),
      m_correct("inspector.correct_directions"),
      m_overlaps("inspector.remove_overlaps"),
      m_union("inspector.union"),
      m_subtract("inspector.subtract"),
      m_intersect("inspector.intersect"),
      m_reverse("inspector.reverse_contours"),
      m_make_first("inspector.make_first"),
      m_code_fmt("inspector.code_format"),
      m_code_scroll("inspector.code_scroll"),
      m_code("inspector.code"),
      m_code_summary("inspector.code_summary"),
      m_code_copy("inspector.code_copy") {
    add_css_class("inspector");
    set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    set_size_request(290, -1);
    m_pages.set_transition_type(Gtk::StackTransitionType::CROSSFADE);
    m_pages.set_transition_duration(120);
    m_pages.set_vhomogeneous(false);
    build_font_page();
    build_glyph_page();
    build_kern_page();
    set_child(m_pages);
    m_ctx.kern_pick_changed.connect([this] { refresh(); });
    m_ctx.selection_changed.connect([this] { refresh(); });
    m_ctx.pick_changed.connect([this] { refresh_pick(); refresh_guide(); refresh_booleans(); });
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

    // M12: what the font was started from -- a record of intent (the stems
    // it set out with), shown only for a preset font.
    Card origin(*page, "STARTED FROM");
    m_origin.set_xalign(0.0f);
    m_origin.set_wrap(true);
    origin.wide(m_origin);
    m_origin_card = origin.box;

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
    m_is_part.set_label("Part -- not exported");
    m_is_part.set_tooltip_text("A piece to build glyphs from (a serif, a bowl, a stem): left out of the OTF / TTF / WOFF2 "
                               "(UFO public.skipExportGlyphs) and listed under Parts in the navigator -- one undo step");
    m_is_part.signal_toggled().connect([this] {
        if (m_painting || m_ctx.selected.empty()) return;
        command::set_part(m_ctx.doc.journal, m_ctx.doc.font, {m_ctx.selected}, m_is_part.get_active());
    });
    glyph.wide(m_is_part);
    // s08 (Scott): how a user types this character in a text editor -- shown, never acted on.
    m_type_it.set_xalign(0.0f);
    m_type_it.set_wrap(true);
    m_type_it.set_selectable(true);
    m_type_it.set_focusable(false);
    m_type_it.add_css_class("hint");
    glyph.field("Type it", m_type_it);

    Card metrics(*page, "METRICS");
    spin_setup(m_width, 0, 32767);
    m_width.signal_value_changed().connect([this] { on_width(); });
    metrics.field("Width", m_width);
    m_lsb.set_xalign(0.0f);
    m_rsb.set_xalign(0.0f);
    metrics.field("Left side", m_lsb);
    metrics.field("Right side", m_rsb);
    // The metrics key (M6): one rule line -- L=R=60, =n, L=n R=|o, W=500, =n+10.
    m_key.set_placeholder_text("Key: L=R=60, =n, L=n R=|o");
    m_key.set_tooltip_text("Metrics key -- a number, or another glyph's side: =n (both sides from n), |n (n's "
                           "opposite side), n.L / n.R, W=n (width), =n+10. Enter commits; empty removes it.");
    on_commit(m_key, [this] { on_key_rule(); });
    metrics.field("Key", m_key);
    m_key_state.set_xalign(0.0f);
    m_key_state.set_wrap(true);
    m_key_state.add_css_class("hint");
    metrics.wide(m_key_state);
    m_key_apply.set_label("Apply key");
    m_key_apply.set_halign(Gtk::Align::START);
    m_key_apply.set_tooltip_text("Set the sidebearings / width the key asks for -- one undo step");
    m_key_apply.signal_clicked().connect([this] {
        if (command::apply_metrics_keys(m_ctx.doc.journal, m_ctx.doc.font, {m_ctx.selected}) && m_ctx.say)
            m_ctx.say(m_ctx.doc.journal.undo_label() + ".", false);
    });
    metrics.wide(m_key_apply);

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
    // Make first (M5e): one picked node becomes its contour's start.
    m_make_first.set_label("Make first");
    m_make_first.set_halign(Gtk::Align::START);
    m_make_first.set_tooltip_text("Make this node the contour's start (F in the editor) -- one undo step");
    m_make_first.signal_clicked().connect([this] {
        if (m_ctx.pick.size() != 1) return;
        const auto p = *m_ctx.pick.begin();
        std::string why;
        if (command::make_first(m_ctx.doc.journal, m_ctx.doc.font, m_ctx.selected, p.contour, p.index, &why)) {
            m_ctx.pick = {{p.contour, 0, geom::PointRef::Part::Node}};   // the same node, now first
            m_ctx.pick_changed.emit();
        } else if (!why.empty() && m_ctx.say) {
            m_ctx.say(why, true);
        }
    });
    pick.wide(m_make_first);
    m_ctx.settings_changed.connect([this] {
        if (m_relative.get_active() != m_ctx.settings.handles_relative) m_relative.set_active(m_ctx.settings.handles_relative);
        // The IMAGE card's colour button shows the app tint for images without their own.
        if (const model::Glyph* g = m_ctx.doc.font.glyph(m_ctx.selected); g && g->image) {
            m_painting = true;
            refresh_image(*g);
            m_painting = false;
        }
    });
    on_commit(m_anchor_name, [this] { on_anchor_name(); });
    m_anchor_name_label = pick.field("Name", m_anchor_name);

    // ── OUTLINE: contours and their direction (M5d) ─────────────────────────
    Card outline(*page, "OUTLINE");
    m_outline_info.set_xalign(0.0f);
    m_outline_info.set_wrap(true);
    m_outline_info.add_css_class("hint");
    outline.wide(m_outline_info);
    m_correct.set_label("Correct directions");
    m_correct.set_tooltip_text("Outer contours counter-clockwise, counters clockwise (CFF / UFO) -- one undo step");
    m_correct.signal_clicked().connect([this] {
        std::size_t n = 0;
        if (command::correct_directions(m_ctx.doc.journal, m_ctx.doc.font, {m_ctx.selected}, &n))
            m_ctx.say(m_ctx.doc.journal.undo_label() + ".", false);
    });
    m_reverse.set_label("Reverse picked");
    m_reverse.set_tooltip_text("Reverse every contour with a picked node or handle (the start node stays)");
    m_reverse.signal_clicked().connect([this] {
        std::set<int> cs;
        for (const auto& p : m_ctx.pick)
            if (p.contour >= 0) cs.insert(p.contour);
        command::reverse_contours(m_ctx.doc.journal, m_ctx.doc.font, m_ctx.selected, {cs.begin(), cs.end()});
    });
    m_overlaps.set_label("Remove overlaps");
    m_overlaps.set_tooltip_text("Merge overlapping contours into one outline, the curves kept exact "
                                "(components untouched) -- one undo step. Export does this on its own.");
    m_overlaps.signal_clicked().connect([this] {
        std::size_t n = 0, fb = 0, ww = 0;
        if (command::remove_overlaps(m_ctx.doc.journal, m_ctx.doc.font, {m_ctx.selected}, &n, &fb, &ww))
            m_ctx.say(m_ctx.doc.journal.undo_label() + ".", false);
        else if (ww)
            m_ctx.say("A contour runs the wrong way -- correct directions first (the union would fill that counter).", true);
        else if (fb)
            m_ctx.say("The overlaps could not be removed cleanly; the outline is as drawn.", true);
        else
            m_ctx.say("No overlaps to remove.", false);
    });
    // M14b: the booleans on the picked contours (a node or segment of each).
    auto boolean = [this](geom::BoolOp op) {
        std::set<int> cs;
        for (const auto& p : m_ctx.pick)
            if (p.contour >= 0) cs.insert(p.contour);
        std::vector<int> at;
        std::string why;
        if (command::boolean_contours(m_ctx.doc.journal, m_ctx.doc.font, m_ctx.selected, {cs.begin(), cs.end()}, op, &at, &why)) {
            // The result is picked, so the next drag / op acts on it.
            m_ctx.pick.clear();
            if (const model::Glyph* g = m_ctx.doc.font.glyph(m_ctx.selected))
                for (int ci : at)
                    for (int i = 0; i < static_cast<int>(g->contours[static_cast<std::size_t>(ci)].nodes.size()); ++i)
                        m_ctx.pick.insert({ci, i, geom::PointRef::Part::Node});
            m_ctx.pick_changed.emit();
            m_ctx.say(m_ctx.doc.journal.undo_label() + ": " + std::to_string(at.size()) +
                          (at.size() == 1 ? " contour." : " contours."), false);
        } else {
            m_ctx.say(why, true);
        }
    };
    m_union.set_label("Union");
    m_union.set_tooltip_text("The picked contours become one outline (pick a node or segment of each) -- one undo step");
    m_union.signal_clicked().connect([boolean] { boolean(geom::BoolOp::Union); });
    m_subtract.set_label("Subtract");
    m_subtract.set_tooltip_text("The picked contours cut the rest of the glyph, then go: draw a shape (it comes picked), "
                                "Subtract -- one undo step");
    m_subtract.signal_clicked().connect([boolean] { boolean(geom::BoolOp::Subtract); });
    m_intersect.set_label("Intersect");
    m_intersect.set_tooltip_text("The rest of the glyph keeps only what the picked contours cover; the picked ones go -- "
                                 "one undo step");
    m_intersect.signal_clicked().connect([boolean] { boolean(geom::BoolOp::Intersect); });
    {
        auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        row->append(m_correct);
        row->append(m_reverse);
        outline.wide(*row);
        auto row2 = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        row2->append(m_overlaps);
        outline.wide(*row2);
        auto row3 = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        row3->set_homogeneous(true);
        row3->append(m_union);
        row3->append(m_subtract);
        row3->append(m_intersect);
        outline.wide(*row3);
    }

    // ── IMAGE: the background image (M13) ───────────────────────────────────
    Card image(*page, "IMAGE");
    m_image_info.set_xalign(0.0f);
    m_image_info.set_wrap(true);
    m_image_info.add_css_class("hint");
    image.wide(m_image_info);
    m_image_place.set_label("Place…");
    m_image_place.set_tooltip_text("Put a scan or sketch (PNG / JPEG) behind this glyph to trace -- one undo step. "
                                   "Dropping a file on the editor canvas does the same.");
    m_image_place.signal_clicked().connect([this] {
        if (m_ctx.selected.empty()) return;
        choose_image_file(m_ctx, dynamic_cast<Gtk::Window*>(get_root()), m_ctx.selected);
    });
    m_image_fit.set_label("Fit");
    m_image_fit.set_tooltip_text("Size the image to span descender to ascender, its left edge on the origin -- one undo step");
    m_image_fit.signal_clicked().connect([this] {
        const model::Glyph* g = m_ctx.doc.font.glyph(m_ctx.selected);
        int w = 0, h = 0;
        if (!g || !g->image) return;
        const auto bytes = m_ctx.doc.font.image(g->image->file);
        if (!bytes || !model::png_size(*bytes, w, h)) return;
        command::set_image_frame(m_ctx.doc.journal, m_ctx.doc.font, m_ctx.selected,
                                 command::image_fitted(m_ctx.doc.font, *g->image, w, h));
    });
    m_image_remove.set_label("Remove");
    m_image_remove.set_tooltip_text("Take the image away from this glyph -- one undo step");
    m_image_remove.signal_clicked().connect([this] {
        if (command::remove_image(m_ctx.doc.journal, m_ctx.doc.font, m_ctx.selected))
            m_ctx.say(m_ctx.doc.journal.undo_label() + " -- Ctrl+Z brings it back.", false);
    });
    {
        auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        row->append(m_image_place);
        row->append(m_image_fit);
        row->append(m_image_remove);
        image.wide(*row);
    }
    // The fields sit in their own grid so they can hide while there is no image.
    m_image_rows = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
    {
        auto grid = Gtk::make_managed<Gtk::Grid>();
        grid->set_row_spacing(6);
        grid->set_column_spacing(10);
        int row = 0;
        auto field = [&](const char* label, Gtk::Widget& w, const char* tip) {
            auto l = Gtk::make_managed<Gtk::Label>(label);
            l->set_xalign(0.0f);
            l->add_css_class("field-label");
            w.set_hexpand(true);
            w.set_tooltip_text(tip);
            grid->attach(*l, 0, row);
            grid->attach(w, 1, row++);
        };
        spin_setup(m_image_x, -32768, 32767);
        spin_setup(m_image_y, -32768, 32767);
        spin_setup(m_image_h, 1, 65535);
        for (auto* sb : {&m_image_x, &m_image_y, &m_image_h})
            sb->signal_value_changed().connect([this] { on_image_frame(); });
        field("X", m_image_x, "The image's left edge, font units");
        field("Y", m_image_y, "The image's bottom edge, font units");
        field("Height", m_image_h, "The image's height in font units -- the width follows (the aspect is kept)");
        m_image_opacity.set_range(5, 100);
        m_image_opacity.set_increments(5, 10);
        m_image_opacity.set_digits(0);
        m_image_opacity.set_draw_value(true);
        m_image_opacity.set_value_pos(Gtk::PositionType::RIGHT);
        m_image_opacity.signal_value_changed().connect([this] {
            if (m_painting || m_ctx.selected.empty()) return;
            command::set_image_opacity(m_ctx.doc.journal, m_ctx.doc.font, m_ctx.selected,
                                       m_image_opacity.get_value() / 100.0);
        });
        field("Opacity", m_image_opacity, "How strongly the image shows behind the outline (one undo step per drag)");
        {   // Colour: the image's own ink colour (UFO color); Default = the app's tint.
            auto dlg = Gtk::ColorDialog::create();
            dlg->set_with_alpha(false);   // how strong it shows is the Opacity slider
            dlg->set_title("Image colour");
            m_image_color.set_dialog(dlg);
            m_image_color.set_hexpand(false);
            m_image_color.property_rgba().signal_changed().connect([this] {
                if (m_painting || m_ctx.selected.empty()) return;
                const Gdk::RGBA c = m_image_color.get_rgba();
                command::set_image_color(m_ctx.doc.journal, m_ctx.doc.font, m_ctx.selected,
                                         command::ufo_color(c.get_red(), c.get_green(), c.get_blue()));
            });
            m_image_color_default.set_label("Default");
            m_image_color_default.set_tooltip_text("Back to the app's colour (Preferences -> Background image colour)");
            m_image_color_default.signal_clicked().connect([this] {
                command::set_image_color(m_ctx.doc.journal, m_ctx.doc.font, m_ctx.selected, "");
            });
            auto crow = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
            crow->append(m_image_color);
            crow->append(m_image_color_default);
            field("Colour", *crow, "The colour this image's ink is drawn in (its paper is never drawn) -- one undo step");
        }
        m_image_lock.set_label("Locked");
        m_image_lock.set_halign(Gtk::Align::START);
        m_image_lock.set_hexpand(false);
        m_image_lock.signal_toggled().connect([this] {
            if (m_painting || m_ctx.selected.empty()) return;
            command::set_image_locked(m_ctx.doc.journal, m_ctx.doc.font, m_ctx.selected, m_image_lock.get_active());
        });
        field("Lock", m_image_lock, "Locked: the mouse goes through the image -- trace without moving it. "
                                    "Unlocked: drag it, or its corners, on the canvas.");
        m_image_rows->append(*grid);
    }
    image.wide(*m_image_rows);

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

    // ── PARTS (M14c) ────────────────────────────────────────────────────────
    Card partc(*page, "PARTS");
    {
        auto hint = Gtk::make_managed<Gtk::Label>(
            "Pieces to build from -- left out of the compile. Linked: edit the part and every glyph using it follows.");
        hint->set_xalign(0.0f);
        hint->set_wrap(true);
        hint->add_css_class("hint");
        partc.wide(*hint);
    }
    m_part_list.set_tooltip_text("The font's parts (the navigator's Parts section)");
    partc.wide(m_part_list);
    m_place_linked.set_label("Place linked");
    m_place_linked.set_tooltip_text("Add the part as a component -- edits to the part show here; decomposed on export. "
                                    "One undo step; it comes picked, drag it into place.");
    m_place_linked.signal_clicked().connect([this] { place_selected_part(true); });
    m_place_contours.set_label("Place as contours");
    m_place_contours.set_tooltip_text("Copy the part in as plain contours to edit freely. One undo step; they come picked.");
    m_place_contours.signal_clicked().connect([this] { place_selected_part(false); });
    {
        auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        row->set_homogeneous(true);
        row->append(m_place_linked);
        row->append(m_place_contours);
        partc.wide(*row);
    }
    m_part_name.set_tooltip_text("The new part's name (a glyph name; parts start with an underscore by convention)");
    m_make_part.set_label("Make part");
    m_make_part.set_tooltip_text("Copy the picked contours into a new part (where they sit) -- this glyph is unchanged. "
                                 "One undo step.");
    auto make = [this] {
        std::set<int> cs;
        for (const auto& p : m_ctx.pick)
            if (p.contour >= 0) cs.insert(p.contour);
        std::string name = m_part_name.get_text();
        if (name.empty()) name = m_part_name.get_placeholder_text();
        std::string why;
        if (command::make_part(m_ctx.doc.journal, m_ctx.doc.font, m_ctx.selected, {cs.begin(), cs.end()}, name, &why)) {
            m_part_name.set_text("");
            m_ctx.say(m_ctx.doc.journal.undo_label() + " -- under Parts in the navigator.", false);
        } else {
            m_ctx.say(why, true);
        }
    };
    m_make_part.signal_clicked().connect(make);
    // Enter only -- not on_commit's focus-leave: clicking away must not make a part.
    m_part_name.signal_activate().connect(make);
    {
        auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        m_part_name.set_hexpand(true);
        row->append(m_part_name);
        row->append(m_make_part);
        partc.wide(*row);
    }

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

    // CODE (M9c): the drawing as the files carry it. Read-only; follows edits.
    Card code(*page, "CODE");
    m_code_fmt.set_model(Gtk::StringList::create({"CFF (OTF)", "TrueType (TTF)", "SVG path"}));
    m_code_fmt.set_tooltip_text("CFF: the charstring the OTF holds, disassembled. TrueType: the points after the "
                                "quadratic conversion (ttx's <pt> lines). SVG: the path Export SVG writes. Remembered.");
    m_code_fmt.property_selected().signal_changed().connect([this] {
        if (m_painting) return;
        static const char* ids[] = {"cff", "truetype", "svg"};
        const auto i = std::min<guint>(m_code_fmt.get_selected(), 2);
        m_ctx.settings.code_format = ids[i];
        if (m_ctx.save_settings) m_ctx.save_settings();
        refresh_code();
    });
    code.field("Format", m_code_fmt);
    m_code.set_editable(false);
    m_code.set_cursor_visible(false);
    m_code.set_monospace(true);
    m_code.set_wrap_mode(Gtk::WrapMode::WORD_CHAR);   // long lines wrap (Scott, s05)
    m_code.set_left_margin(6);
    m_code.set_top_margin(4);
    m_code.add_css_class("code");
    m_code_scroll.set_child(m_code);
    m_code_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_code_scroll.set_size_request(-1, 220);
    m_code_scroll.set_has_frame(true);
    code.wide(m_code_scroll);
    auto code_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    m_code_summary.add_css_class("dim");
    m_code_summary.set_xalign(0.0f);
    m_code_summary.set_hexpand(true);
    m_code_summary.set_ellipsize(Pango::EllipsizeMode::END);
    m_code_copy.set_label("Copy");
    m_code_copy.set_tooltip_text("Copy the listing");
    m_code_copy.signal_clicked().connect([this] {
        m_code.get_clipboard()->set_text(m_code.get_buffer()->get_text());
        if (m_ctx.say) m_ctx.say("Copied the " + std::string(m_code_fmt.get_selected() == 0 ? "CFF" : m_code_fmt.get_selected() == 1 ? "TrueType" : "SVG") + " listing.", false);
    });
    code_row->append(m_code_summary);
    code_row->append(m_code_copy);
    code.wide(*code_row);

    m_pages.add(*page, "glyph");
}

void Inspector::refresh_code() {
    const auto& s = m_ctx.settings.code_format;
    const guint want = s == "truetype" ? 1u : s == "svg" ? 2u : 0u;
    const bool was = m_painting;
    m_painting = true;
    if (m_code_fmt.get_selected() != want) m_code_fmt.set_selected(want);
    m_painting = was;
    if (m_ctx.selected.empty() || !m_ctx.doc.font.glyph(m_ctx.selected)) return;
    const auto fmt = want == 1 ? otf::CodeFormat::TrueType : want == 2 ? otf::CodeFormat::Svg : otf::CodeFormat::Cff;
    const auto code = otf::glyph_code(m_ctx.doc.font, m_ctx.selected, fmt);
    m_code.get_buffer()->set_text(code.text);
    m_code_summary.set_text(code.summary);
}

void Inspector::refresh() {
    m_painting = true;
    const auto& f = m_ctx.doc.font;
    const model::Glyph* g = m_ctx.selected.empty() ? nullptr : f.glyph(m_ctx.selected);
    if (m_ctx.kern_pick && f.glyph(m_ctx.kern_pick->left) && f.glyph(m_ctx.kern_pick->right)) {
        m_pages.set_visible_child("kern");
        refresh_kern();
    } else if (!g) {
        m_pages.set_visible_child("font");
        if (!m_family.has_focus()) m_family.set_text(f.info().family);
        if (!m_style.has_focus())  m_style.set_text(f.info().style);
        m_upm.set_value(f.info().units_per_em);
        for (std::size_t i = 0; i < m_metric.size(); ++i)
            if (m_metric[i]) m_metric[i]->set_value(f.metrics().get(static_cast<model::Metrics::Field>(i)));
        const auto& o = f.info().origin;
        const model::Preset* p = model::preset(o.preset);
        m_origin_card->set_visible(!o.preset.empty());
        if (!o.preset.empty())
            m_origin.set_text((p ? p->name : o.preset) + " preset.\nStems: lowercase " + std::to_string(o.stem_v) + " / " +
                              std::to_string(o.stem_h) + ", capitals " + std::to_string(o.cap_stem_v) + " / " +
                              std::to_string(o.cap_stem_h) + " (vertical / horizontal).\n"
                              "Measure (M) a stem to compare; the Primer (F1) says why they differ.");
    } else {
        m_pages.set_visible_child("glyph");
        m_title.set_text(g->name);
        if (!m_name.has_focus()) m_name.set_text(g->name);
        for (unsigned i = 0; i < 4; ++i) if (kStatuses[i] == g->status) m_status.set_selected(i);
        {
            const bool was = m_painting;
            m_painting = true;
            m_is_part.set_active(g->skip_export);
            m_painting = was;
        }
        if (g->skip_export) {
            m_type_it.set_text("A part -- left out of the font, so never typed.");
            m_type_it.set_tooltip_text("");
        } else if (g->unicodes.empty()) {
            m_type_it.set_text("Not typed directly: no code point. Give it one (UNICODE card), "
                               "or it is reached through a feature (a ligature, a small cap ...).");
            m_type_it.set_tooltip_text("");
        } else {
            const auto t = unicode::type_it(g->unicodes.front());
            std::string s;
            if (!t.key.empty()) {
                s = "Key: " + t.key;   // ASCII: the key is all anyone needs
            } else {
                if (!t.compose.empty()) s += t.compose + "\n";
                s += t.hex;
            }
            m_type_it.set_text(s);
            std::string tip = "How to type it in a text editor once the font is installed (US keyboard).\n"
                              "Any GNOME / GTK app: " + t.hex + " (type the hex, then Space or Enter).\n"
                              "Compose needs a Compose key (GNOME Tweaks -> Keyboard & Mouse).";
            if (!t.windows.empty()) tip += "\nWindows: " + t.windows;
            if (!t.mac.empty()) tip += "\nmacOS: " + t.mac;
            m_type_it.set_tooltip_text(tip);
        }
        m_width.set_value(g->advance);
        if (const auto sb = model::sidebearings(f, *g)) {
            m_lsb.set_text(fmt_units(sb->left));
            m_rsb.set_text(fmt_units(sb->right));
        } else {
            m_lsb.set_text("—");
            m_rsb.set_text("—");
        }
        {
            const auto& keys = f.metrics_keys().keys;
            const auto it = keys.find(g->name);
            if (!m_key.has_focus()) m_key.set_text(it == keys.end() ? "" : model::format_key_rule(it->second));
            const auto c = model::check_key(f, g->name);
            std::string st;
            if (!c.has_key) {
                st = "No key.";
            } else if (!c.off()) {
                st = "In step with its key.";
            } else {
                st = "Off its key --";
                const auto sb = model::sidebearings(f, *g);
                if (c.left_off && sb)  st += " L " + fmt_units(*c.left) + " (is " + fmt_units(sb->left) + ")";
                if (c.right_off && sb) st += " R " + fmt_units(*c.right) + " (is " + fmt_units(sb->right) + ")";
                if (c.width_off)       st += " W " + fmt_units(*c.width) + " (is " + fmt_units(g->advance) + ")";
            }
            if (c.has_key && ((!it->second.left.empty() && !c.left) || (!it->second.right.empty() && !c.right) ||
                              (!it->second.width.empty() && !c.width)))
                st += " A reference is missing or has no outline.";
            m_key_state.set_text(st);
            m_key_apply.set_sensitive(c.off());
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
        refresh_image(*g);
        m_preview.queue_draw();
        refresh_code();
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

// M14c: the PARTS card -- the list (without this glyph), the place buttons,
// a free name for Make part, which wants a picked contour.
void Inspector::refresh_parts_card(const model::Glyph& g) {
    const auto& f = m_ctx.doc.font;
    std::vector<std::string> names;
    for (const auto& n : command::parts(f))
        if (n != g.name) names.push_back(n);
    if (names != m_part_names) {
        const std::string keep = m_part_list.get_selected() < m_part_names.size() ? m_part_names[m_part_list.get_selected()] : "";
        m_part_names = names;
        std::vector<Glib::ustring> labels(names.begin(), names.end());
        if (labels.empty()) labels.push_back("No parts yet -- pick contours, Make part");
        m_part_list.set_model(Gtk::StringList::create(labels));
        for (unsigned i = 0; i < names.size(); ++i)
            if (names[i] == keep) m_part_list.set_selected(i);
    }
    m_part_list.set_sensitive(!m_part_names.empty());
    m_place_linked.set_sensitive(!m_part_names.empty());
    m_place_contours.set_sensitive(!m_part_names.empty());
    m_part_name.set_placeholder_text(command::free_part_name(f, g.name));
    bool picked = false;
    for (const auto& p : m_ctx.pick) picked = picked || p.contour >= 0;
    m_make_part.set_sensitive(picked);
}

void Inspector::place_selected_part(bool linked) {
    const unsigned i = m_part_list.get_selected();
    if (i >= m_part_names.size() || m_ctx.selected.empty()) return;
    std::vector<int> at;
    int k = -1;
    std::string why;
    if (!command::place_part(m_ctx.doc.journal, m_ctx.doc.font, m_ctx.selected, m_part_names[i], linked, 0, 0, &at, &k, &why)) {
        m_ctx.say(why, true);
        return;
    }
    // What was placed comes picked: a drag or the arrows put it in place.
    m_ctx.pick.clear();
    if (linked) {
        m_ctx.pick.insert({-1, k, geom::PointRef::Part::Component});
    } else if (const model::Glyph* g = m_ctx.doc.font.glyph(m_ctx.selected)) {
        for (int ci : at)
            for (int n = 0; n < static_cast<int>(g->contours[static_cast<std::size_t>(ci)].nodes.size()); ++n)
                m_ctx.pick.insert({ci, n, geom::PointRef::Part::Node});
    }
    m_ctx.pick_changed.emit();
    m_ctx.say(m_ctx.doc.journal.undo_label() + (linked ? " (linked) -- drag it into place." : " -- drag it into place."), false);
}

// M14b: the boolean buttons follow the pick (contours with a picked point).
void Inspector::refresh_booleans() {
    const model::Glyph* g = m_ctx.doc.font.glyph(m_ctx.selected);
    std::set<int> picked;
    if (g)
        for (const auto& p : m_ctx.pick)
            if (p.contour >= 0 && p.contour < static_cast<int>(g->contours.size())) picked.insert(p.contour);
    m_union.set_sensitive(picked.size() >= 2);
    const bool cut = g && !picked.empty() && picked.size() < g->contours.size();
    m_subtract.set_sensitive(cut);
    m_intersect.set_sensitive(cut);
    if (g) refresh_parts_card(*g);
}

void Inspector::refresh_parts(const model::Glyph& g) {
    {   // OUTLINE
        const auto wrong = geom::wrong_way(g.contours);
        std::string t = std::to_string(g.contours.size()) + (g.contours.size() == 1 ? " contour" : " contours");
        if (!g.contours.empty())
            t += wrong.empty() ? ", all running the right way"
                               : ", " + std::to_string(wrong.size()) + " the wrong way (red on the canvas)";
        m_outline_info.set_text(t);
        m_correct.set_sensitive(!wrong.empty());
        m_overlaps.set_sensitive(g.contours.size() > 0);
        refresh_booleans();
    }
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

// ── the background image (M13) ──────────────────────────────────────────────

void Inspector::refresh_image(const model::Glyph& g) {
    const auto& f = m_ctx.doc.font;
    m_image_place.set_label(g.image ? "Replace…" : "Place…");
    m_image_fit.set_visible(g.image.has_value());
    m_image_remove.set_visible(g.image.has_value());
    m_image_rows->set_visible(g.image.has_value());
    if (!g.image) {
        m_image_info.set_text("None. Drop a scan or sketch (PNG / JPEG) on the editor canvas, or Place… -- "
                              "something to trace. Never exported.");
        return;
    }
    const auto& im = *g.image;
    int w = 0, h = 0;
    const auto bytes = f.image(im.file);
    const bool have = bytes && model::png_size(*bytes, w, h);
    std::string t = im.file;
    if (have) t += "   " + std::to_string(w) + " x " + std::to_string(h) + " px";
    else      t += " -- the file is missing from the font's images/ folder";
    if (im.xy != 0 || im.yx != 0) t += "\nRotated or skewed (from another tool): the fields show its offset only.";
    m_image_info.set_text(t);
    if (!m_image_x.has_focus()) m_image_x.set_value(std::round(im.dx));
    if (!m_image_y.has_focus()) m_image_y.set_value(std::round(im.dy));
    if (!m_image_h.has_focus()) m_image_h.set_value(have ? std::round(std::abs(im.yy) * h) : 0);
    m_image_h.set_sensitive(have && im.xy == 0 && im.yx == 0);
    m_image_fit.set_sensitive(have);
    m_image_opacity.set_value(std::round(im.opacity * 100));
    m_image_lock.set_active(im.locked);
    {   // the colour button shows what draws: the image's own, else the app's
        Gdk::RGBA c;
        double v[4] = {0, 0, 0, 1};
        if (!im.color.empty() && std::sscanf(im.color.c_str(), "%lf,%lf,%lf,%lf", &v[0], &v[1], &v[2], &v[3]) >= 3)
            c.set_rgba(v[0], v[1], v[2], 1.0);
        else if (!c.set(m_ctx.settings.image_tint))
            c.set("#e0703c");
        if (m_image_color.get_rgba() != c) m_image_color.set_rgba(c);
        m_image_color_default.set_sensitive(!im.color.empty());
    }
}

void Inspector::on_image_frame() {
    if (m_painting || m_ctx.selected.empty()) return;
    const model::Glyph* g = m_ctx.doc.font.glyph(m_ctx.selected);
    if (!g || !g->image) return;
    model::GlyphImage im = *g->image;
    im.dx = m_image_x.get_value();
    im.dy = m_image_y.get_value();
    int w = 0, h = 0;
    const auto bytes = m_ctx.doc.font.image(im.file);
    if (bytes && model::png_size(*bytes, w, h) && im.xy == 0 && im.yx == 0 && im.yy != 0) {
        const double want = m_image_h.get_value();
        const double k = want / (std::abs(im.yy) * h);
        if (std::abs(k - 1) > 1e-9) { im.xx *= k; im.yy *= k; }
    }
    command::set_image_frame(m_ctx.doc.journal, m_ctx.doc.font, m_ctx.selected, im);
}

// ── the pair (M6) ───────────────────────────────────────────────────────────

void Inspector::build_kern_page() {
    auto page = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
    m_kern_title.set_xalign(0.0f);
    m_kern_title.add_css_class("inspector-title");
    page->append(m_kern_title);

    Card pair(*page, "KERN");
    m_kern_preview.set_content_height(120);
    m_kern_preview.add_css_class("preview");
    m_kern_preview.set_draw_func([this](const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
        if (!m_ctx.kern_pick) return;
        const auto& f = m_ctx.doc.font;
        const model::Glyph* a = f.glyph(m_ctx.kern_pick->left);
        const model::Glyph* b = f.glyph(m_ctx.kern_pick->right);
        if (!a || !b) return;
        const auto& m = f.metrics();
        const int kern = model::kern_lookup(f.kerning(), a->name, b->name).value;
        const double total = std::max(1.0, a->advance + kern + b->advance);
        const double em_h = std::max(1, std::max(m.ascender, m.cap_height) - m.descender);
        const double pad = 10;
        const double sc = std::min((w - 2 * pad) / total, (h - 2 * pad) / em_h);
        const double ox = (w - total * sc) / 2.0;
        const double base = pad + std::max(m.ascender, m.cap_height) * sc;
        const auto fg = m_kern_preview.get_color();
        cr->set_source_rgba(fg.get_red(), fg.get_green(), fg.get_blue(), 0.18);
        cr->set_line_width(1);
        cr->move_to(0, std::round(base) + 0.5);
        cr->line_to(w, std::round(base) + 0.5);
        cr->stroke();
        cr->set_source_rgba(0.20, 0.50, 1.00, 0.8);   // the join
        const double jx = std::round(ox + (a->advance + kern / 2.0) * sc) + 0.5;
        cr->move_to(jx, pad / 2);
        cr->line_to(jx, h - pad / 2);
        cr->stroke();
        cr->set_source_rgba(fg.get_red(), fg.get_green(), fg.get_blue(), 0.92);
        for (auto [g, x] : {std::pair{a, 0.0}, std::pair{b, a->advance + kern}}) {
            cr->save();
            cr->translate(ox + x * sc, base);
            cr->scale(sc, -sc);
            append_glyph_path(cr, f, *g);
            cr->restore();
        }
        cr->set_fill_rule(Cairo::Context::FillRule::WINDING);
        cr->fill();
    });
    pair.wide(m_kern_preview);
    for (auto* l : {&m_kern_left, &m_kern_right, &m_kern_entry}) {
        l->set_xalign(0.0f);
        l->set_wrap(true);
    }
    pair.field("Left", m_kern_left);
    pair.field("Right", m_kern_right);
    pair.field("Entry", m_kern_entry);
    spin_setup(m_kern_value, -4000, 4000);
    m_kern_value.set_tooltip_text("The kern in effect; a change writes that entry (or the groups' pair when "
                                  "there is none). 0 removes it.");
    m_kern_value.signal_value_changed().connect([this] {
        if (m_painting || !m_ctx.kern_pick) return;
        command::kern_pair(m_ctx.doc.journal, m_ctx.doc.font, m_ctx.kern_pick->left, m_ctx.kern_pick->right,
                           static_cast<int>(std::lround(m_kern_value.get_value())));
    });
    pair.field("Value", m_kern_value);
    m_kern_exception.set_label("Make exception");
    m_kern_exception.set_tooltip_text("Kern just these two glyphs (glyph / glyph), starting from the value in "
                                      "effect -- the group pair stays. Alt+arrows in the strip do the same.");
    m_kern_exception.signal_clicked().connect([this] {
        if (!m_ctx.kern_pick) return;
        const auto& f = m_ctx.doc.font;
        const int v = model::kern_lookup(f.kerning(), m_ctx.kern_pick->left, m_ctx.kern_pick->right).value;
        command::kern_pair(m_ctx.doc.journal, m_ctx.doc.font, m_ctx.kern_pick->left, m_ctx.kern_pick->right, v,
                           true);
    });
    m_kern_remove.set_label("Remove");
    m_kern_remove.set_tooltip_text("Remove the entry in effect -- the next one down (group pair) shows through");
    m_kern_remove.signal_clicked().connect([this] {
        if (!m_ctx.kern_pick) return;
        command::kern_pair(m_ctx.doc.journal, m_ctx.doc.font, m_ctx.kern_pick->left, m_ctx.kern_pick->right, 0);
    });
    auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    row->append(m_kern_exception);
    row->append(m_kern_remove);
    pair.wide(*row);
    // ── the readouts (M8) ────────────────────────────────────────────────────
    Card gap(*page, "GAP");
    for (auto* l : {&m_kern_contact, &m_kern_optical, &m_kern_control, &m_kern_flag}) {
        l->set_xalign(0.0f);
        l->set_wrap(true);
    }
    m_kern_contact.set_tooltip_text("The closest approach between the two outlines (the line in the strip)");
    m_kern_optical.set_tooltip_text("The mean whitespace between the two profiles inside the band, deep overhangs "
                                    "clamped (the shaded area in the strip)");
    m_kern_control.set_tooltip_text("The optical gap of the band's control pair: n n for the x-height band, H H for the "
                                    "cap band");
    gap.field("Contact", m_kern_contact);
    gap.field("Optical", m_kern_optical);
    gap.field("Control", m_kern_control);
    m_kern_flag.add_css_class("hint");
    gap.wide(m_kern_flag);
    m_kern_to_gap.set_halign(Gtk::Align::START);
    m_kern_to_gap.set_tooltip_text("Kern the pair so its optical gap matches the control pair's -- one undo step");
    m_kern_to_gap.signal_clicked().connect([this] {
        if (!m_ctx.kern_pick) return;
        int v = 0;
        if (command::kern_to_gap(m_ctx.doc.journal, m_ctx.doc.font, m_ctx.kern_pick->left, m_ctx.kern_pick->right, &v) &&
            m_ctx.say)
            m_ctx.say("Kern to gap: " + std::to_string(v) + ".", false);
    });
    gap.wide(m_kern_to_gap);

    auto hint = Gtk::make_managed<Gtk::Label>("In the strip: Left / Right kern, Shift x10, Alt writes an exception. "
                                              "Escape drops the pair.");
    hint->set_xalign(0.0f);
    hint->set_wrap(true);
    hint->add_css_class("hint");
    pair.wide(*hint);
    m_pages.add(*page, "kern");
}

void Inspector::refresh_kern() {
    if (!m_ctx.kern_pick) return;
    const auto& k = m_ctx.doc.font.kerning();
    const auto& l = m_ctx.kern_pick->left;
    const auto& r = m_ctx.kern_pick->right;
    const bool was = m_painting;
    m_painting = true;
    m_kern_title.set_text(l + "  " + r);
    const std::string g1 = model::kern_group(k, l, 1), g2 = model::kern_group(k, r, 2);
    auto short_group = [](const std::string& g, const char* prefix) {
        return g.empty() ? std::string("no group") : "group @" + g.substr(std::string(prefix).size());
    };
    m_kern_left.set_text(l + "  (" + short_group(g1, "public.kern1.") + ")");
    m_kern_right.set_text(r + "  (" + short_group(g2, "public.kern2.") + ")");
    const auto hit = model::kern_lookup(k, l, r);
    const auto target = model::kern_target(k, l, r, false);
    if (hit.found) {
        m_kern_entry.set_text(model::kern_kind(hit.key) + (hit.key == model::KernKey{l, r} && (!g1.empty() || !g2.empty())
                                                               ? " (an exception)" : ""));
    } else {
        m_kern_entry.set_text("none yet -- a kern writes " + model::kern_kind(target));
    }
    if (!m_kern_value.has_focus() || std::lround(m_kern_value.get_value()) != hit.value) m_kern_value.set_value(hit.value);
    m_kern_exception.set_sensitive(!(hit.found && hit.key == model::KernKey{l, r}) && (!g1.empty() || !g2.empty()));
    m_kern_remove.set_sensitive(hit.found);
    {
        const auto rd = model::read_pair(m_ctx.doc.font, l, r);
        auto n = [](double v) { return std::to_string(std::lround(v)); };
        if (!rd.ok) {
            m_kern_contact.set_text("\u2014");
            m_kern_optical.set_text("\u2014");
            m_kern_control.set_text("\u2014");
            m_kern_flag.set_text("One of the two has no outline.");
            m_kern_to_gap.set_label("Kern to gap");
            m_kern_to_gap.set_sensitive(false);
        } else {
            m_kern_contact.set_text(rd.contact < 0 ? n(rd.contact) + "  (they collide)" : n(rd.contact));
            m_kern_optical.set_text(n(rd.optical) + (rd.band == 'x' ? "  (x-height band)" : "  (cap band)"));
            m_kern_control.set_text(rd.control.empty() ? std::string("none -- the font has no ") + (rd.band == 'x' ? "n" : "H")
                                                       : rd.control + ":  " + n(rd.control_optical));
            m_kern_flag.set_text(rd.disagree ? "The readouts disagree: an open shape, where the contact gap alone "
                                               "would kern too tight (To, LT). Trust the optical gap, then your eye."
                                             : "");
            m_kern_flag.set_visible(rd.disagree);
            const bool can = rd.proposed && *rd.proposed != hit.value;
            m_kern_to_gap.set_label(rd.proposed ? "Kern to gap: " + std::to_string(*rd.proposed) : std::string("Kern to gap"));
            m_kern_to_gap.set_sensitive(can);
        }
    }
    m_kern_preview.queue_draw();
    m_painting = was;
}

void Inspector::on_key_rule() {
    if (m_painting || m_ctx.selected.empty()) return;
    model::MetricsKeys::Key k;
    std::string why;
    if (!model::parse_key_rule(m_key.get_text(), m_ctx.selected, &k, &why)) {
        if (m_ctx.say) m_ctx.say("Metrics key: " + why, true);
        return;
    }
    command::set_metrics_key(m_ctx.doc.journal, m_ctx.doc.font, m_ctx.selected, k);
    refresh();
}

void Inspector::refresh_pick() {
    if (!m_pick_card) return;
    {
        bool any = false;
        for (const auto& p : m_ctx.pick) if (p.contour >= 0) any = true;
        m_reverse.set_sensitive(any);
    }
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
    m_make_first.set_visible(node);
    if (node) m_make_first.set_sensitive(p.index != 0 && g->contours[p.contour].closed);
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
            case Part::Node:
                what = "Node " + std::to_string(p.index) + (p.index == 0 ? " (start)" : "") + " of contour " +
                       std::to_string(p.contour);
                break;
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
        // A named list, not a braced temporary: GCC 16 at -O2 reports a false
        // -Wfree-nonheap-object on the temporary's destructor (s03).
        const std::vector<std::string> reselected{to};
        m_ctx.doc.journal.reselect(reselected);
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
