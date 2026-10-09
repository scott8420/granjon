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

#include <cmath>

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
    void field(const char* label, Gtk::Widget& w) {
        auto l = Gtk::make_managed<Gtk::Label>(label);
        l->set_xalign(0.0f);
        l->add_css_class("field-label");
        w.set_hexpand(true);
        grid->attach(*l, 0, row);
        grid->attach(w, 1, row++);
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
      m_note("inspector.note") {
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
        m_preview.queue_draw();
    }
    m_painting = false;
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
