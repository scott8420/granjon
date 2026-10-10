#include "views/OverviewGrid.hpp"

#include "command/Verbs.hpp"
#include "model/Composite.hpp"
#include "model/Coverage.hpp"
#include "model/Find.hpp"
#include "unicode/Agl.hpp"
#include "unicode/Blocks.hpp"
#include "unicode/CharSet.hpp"
#include "unicode/UcdTable.hpp"
#include "views/GlyphPaint.hpp"

#include <gtkmm/flowbox.h>
#include <gtkmm/image.h>
#include <pangomm/layout.h>

#include <algorithm>
#include <cctype>
#include <set>

namespace granjon::views {

namespace {

// Gap cells carry "@gap:<hex>" as their widget name; glyph cells their glyph
// name (never starting with '@').
constexpr const char* kGapPrefix = "@gap:";
constexpr int kMinCell = core::kOverviewCellMin, kMaxCell = core::kOverviewCellMax;

std::string gap_id(char32_t cp) { return kGapPrefix + std::to_string(static_cast<unsigned>(cp)); }
bool is_gap(const std::string& id) { return id.rfind(kGapPrefix, 0) == 0; }
char32_t gap_cp(const std::string& id) { return static_cast<char32_t>(std::stoul(id.substr(5))); }

Gtk::Widget* section_header(const std::string& title, std::size_t count, std::size_t gaps) {
    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    box->add_css_class("section-header");
    std::string upper = title;   // small caps by hand: GTK CSS has no text-transform
    for (auto& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    auto l = Gtk::make_managed<Gtk::Label>(upper);
    l->set_xalign(0.0f);
    box->append(*l);
    auto b = Gtk::make_managed<Gtk::Label>(std::to_string(count));
    b->add_css_class("badge");
    box->append(*b);
    if (gaps > 0) {
        auto m = Gtk::make_managed<Gtk::Label>(std::to_string(gaps) + " missing");
        m->add_css_class("badge");
        m->add_css_class("missing");
        box->append(*m);
    }
    return box;
}

// What a gap shows for its character: the character itself, a combining mark
// on a dotted circle, nothing for spaces and controls.
std::string gap_text(char32_t cp) {
    const auto gc = unicode::category(cp);
    if (gc.starts_with("Z") || gc.starts_with("C")) return {};
    if (gc == "Mn" || gc == "Me") return unicode::utf8(0x25CC) + unicode::utf8(cp);
    return unicode::utf8(cp);
}

}  // namespace

OverviewGrid::OverviewGrid(ViewContext& ctx)
    : widgets::Box("overview", Gtk::Orientation::VERTICAL, 0),
      m_ctx(ctx),
      m_bar("overview.bar", Gtk::Orientation::HORIZONTAL, 8),
      m_coverage("overview.coverage"),
      m_checklist("overview.checklist"),
      m_create("overview.create"),
      m_size("overview.size", Gtk::Orientation::HORIZONTAL),
      m_scope_label("overview.scope"),
      m_scope_all("overview.scope_all"),
      m_scroll("overview.scroll"),
      m_sections("overview.sections", Gtk::Orientation::VERTICAL, 0) {
    add_css_class("overview");
    // Remembered from last time (core::Settings, tolerant: a bad value was
    // already put back to its default on load).
    m_cell = std::clamp(m_ctx.settings.overview_cell, kMinCell, kMaxCell);
    if (unicode::charset(m_ctx.settings.overview_charset)) m_charset = m_ctx.settings.overview_charset;
    m_ctx.settings_changed.connect([this] {
        if (m_ctx.settings.overview_cell != m_cell) m_size.set_value(m_ctx.settings.overview_cell);
        if (m_ctx.settings.overview_charset != m_charset) set_charset(m_ctx.settings.overview_charset);
    });
    build_toolbar();
    append(m_bar);
    m_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_scroll.set_vexpand(true);
    m_sections.add_css_class("overview-body");
    m_scroll.set_child(m_sections);
    append(m_scroll);
    m_ctx.selection_changed.connect([this] { sync_selection(); });
    m_ctx.filter_changed.connect([this] { refresh(); });
    m_ctx.scope_changed.connect([this] { refresh(); });
}

void OverviewGrid::build_toolbar() {
    m_bar.add_css_class("overview-bar");

    m_coverage.set_tooltip_text("Coverage: check the font against a character set");
    m_coverage.set_always_show_arrow(true);
    m_checklist.add_css_class("checklist");
    m_checklist.set_selection_mode(Gtk::SelectionMode::NONE);
    m_checklist.set_activate_on_single_click(true);
    m_checklist.signal_row_activated().connect([this](Gtk::ListBoxRow* row) {
        const std::string id = row->get_name();
        m_coverage_pop.popdown();
        set_charset(id == "@none" ? std::string{} : id);
    });
    m_coverage_pop.set_child(m_checklist);
    m_coverage_pop.add_css_class("checklist-popover");
    m_coverage.set_popover(m_coverage_pop);
    m_bar.append(m_coverage);

    m_create.set_tooltip_text("Add a glyph for every missing character -- one undo step");
    m_create.signal_clicked().connect([this] { create_missing(); });
    m_create.add_css_class("create-missing");
    m_bar.append(m_create);

    m_scope_label.add_css_class("dim");
    m_scope_all.set_label("Show all");
    m_scope_all.set_tooltip_text("Back to the whole font (clears the navigator's pick)");
    m_scope_all.signal_clicked().connect([this] { m_ctx.set_scope(std::nullopt); });
    m_bar.append(m_scope_label);
    m_bar.append(m_scope_all);

    auto spacer = Gtk::make_managed<Gtk::Box>();
    spacer->set_hexpand(true);
    m_bar.append(*spacer);

    auto small = Gtk::make_managed<Gtk::Image>();
    small->set_from_icon_name("view-grid-symbolic");
    small->add_css_class("dim");
    m_bar.append(*small);
    m_size.set_range(kMinCell, kMaxCell);
    m_size.set_increments(4, 16);
    m_size.set_value(m_cell);
    m_size.set_draw_value(false);
    m_size.set_size_request(140, -1);
    m_size.set_tooltip_text("Cell size");
    m_size.signal_value_changed().connect([this] {
        const int v = static_cast<int>(m_size.get_value());
        if (v == m_cell) return;
        m_cell = v;
        apply_cell_size();
        remember();
    });
    m_bar.append(m_size);
    update_coverage_ui();
}

void OverviewGrid::remember() {
    auto& s = m_ctx.settings;
    if (s.overview_cell == m_cell && s.overview_charset == m_charset) return;
    s.overview_cell = m_cell;
    s.overview_charset = m_charset;
    if (m_ctx.save_settings) m_ctx.save_settings();
    m_ctx.settings_changed.emit();
}

void OverviewGrid::set_charset(const std::string& id) {
    m_charset = (id.empty() || unicode::charset(id)) ? id : std::string{};
    remember();
    refresh();
}

void OverviewGrid::rebuild_checklist() {
    while (auto* c = m_checklist.get_first_child()) m_checklist.remove(*c);
    const auto have = model::mapped(m_ctx.doc.font);
    const auto row = [&](const std::string& id, const std::string& title, const std::string& detail,
                         std::size_t missing, bool chosen) {
        auto r = Gtk::make_managed<Gtk::ListBoxRow>();
        r->set_name(id);
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto tick = Gtk::make_managed<Gtk::Image>();
        tick->set_from_icon_name("object-select-symbolic");
        tick->set_opacity(chosen ? 1.0 : 0.0);
        box->append(*tick);
        auto l = Gtk::make_managed<Gtk::Label>(title);
        l->set_xalign(0.0f);
        l->set_hexpand(true);
        box->append(*l);
        if (!detail.empty()) {
            auto d = Gtk::make_managed<Gtk::Label>(detail);
            d->add_css_class("cp");
            box->append(*d);
            auto b = Gtk::make_managed<Gtk::Label>(missing ? std::to_string(missing) + " missing" : "complete");
            b->add_css_class("badge");
            b->add_css_class(missing ? "missing" : "complete");
            box->append(*b);
        }
        r->set_child(*box);
        m_checklist.append(*r);
    };
    row("@none", "No coverage set", "", 0, m_charset.empty());
    for (const auto& cs : unicode::charsets()) {
        std::size_t got = 0;
        for (char32_t c : cs.cps) got += have.count(c);
        row(cs.id, cs.name, std::to_string(got) + " / " + std::to_string(cs.cps.size()),
            cs.cps.size() - got, cs.id == m_charset);
    }
    // Each set's source as its row tooltip.
    for (int i = 1; auto* r = m_checklist.get_row_at_index(i); ++i)
        if (const auto* cs = unicode::charset(r->get_name())) r->set_tooltip_text(cs->source);
}

void OverviewGrid::update_coverage_ui() {
    const auto* cs = unicode::charset(m_charset);
    if (!cs) {
        m_coverage.set_label("Coverage");
        m_create.set_visible(false);
        return;
    }
    m_coverage.set_label(cs->name + (m_missing.empty() ? "  ·  complete"
                                                       : "  ·  " + std::to_string(m_missing.size()) + " missing"));
    m_create.set_visible(!m_missing.empty());
    // How many of the gaps the font's own parts can build (é = e + ´).
    std::size_t built = 0;
    for (char32_t cp : m_missing) built += model::plan_composite(m_ctx.doc.font, cp).ok() ? 1 : 0;
    m_create.set_label("Create " + std::to_string(m_missing.size()) + " missing" +
                       (built ? ", " + std::to_string(built) + " as composites" : std::string{}));
}

Gtk::FlowBoxChild* OverviewGrid::glyph_cell(const std::string& name) {
    const model::Glyph* g = m_ctx.doc.font.glyph(name);
    auto child = Gtk::make_managed<Gtk::FlowBoxChild>();
    child->set_name(name);
    auto cell = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
    cell->add_css_class("glyph-cell");
    cell->add_css_class(model::status_name(g->status));

    auto art = Gtk::make_managed<Gtk::DrawingArea>();
    art->set_content_width(m_cell);
    art->set_content_height(m_cell);
    art->set_draw_func([this, name, art](const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
        const model::Glyph* gg = m_ctx.doc.font.glyph(name);
        if (!gg) return;
        const auto fg = art->get_color();
        cr->set_source_rgba(fg.get_red(), fg.get_green(), fg.get_blue(), fg.get_alpha());
        paint_glyph_fitted(cr, m_ctx.doc.font, *gg, w, h, std::max(3, w / 10));
    });
    m_arts.push_back(art);
    m_art_of[name] = art;
    m_box_of[name] = cell;
    m_known[name] = g->unicodes;
    cell->append(*art);

    auto l = Gtk::make_managed<Gtk::Label>(name);
    l->set_ellipsize(Pango::EllipsizeMode::MIDDLE);
    l->set_max_width_chars(11);
    l->add_css_class("cell-name");
    m_captions.push_back(l);
    cell->append(*l);
    auto cp = Gtk::make_managed<Gtk::Label>(g->unicodes.empty() ? std::string("—")
                                                                 : unicode::format_cp(g->unicodes.front()));
    cp->add_css_class("cp");
    m_captions.push_back(cp);
    cell->append(*cp);
    if (!g->unicodes.empty()) child->set_tooltip_text(unicode::name_of(g->unicodes.front()));

    child->set_child(*cell);
    m_cells[name] = child;
    return child;
}

Gtk::FlowBoxChild* OverviewGrid::gap_cell(char32_t cp) {
    auto child = Gtk::make_managed<Gtk::FlowBoxChild>();
    child->set_name(gap_id(cp));
    child->set_tooltip_text(unicode::name_of(cp) + "\nNot in the font -- double-click to create it");
    auto cell = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
    cell->add_css_class("glyph-cell");
    cell->add_css_class("gap");

    auto art = Gtk::make_managed<Gtk::DrawingArea>();
    art->set_content_width(m_cell);
    art->set_content_height(m_cell);
    const std::string text = gap_text(cp);
    art->set_draw_func([art, text](const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
        if (text.empty()) return;
        auto layout = art->create_pango_layout(text);
        Pango::FontDescription fd;
        fd.set_absolute_size(h * 0.55 * PANGO_SCALE);
        layout->set_font_description(fd);
        int lw = 0, lh = 0;
        layout->get_pixel_size(lw, lh);
        const auto fg = art->get_color();
        cr->set_source_rgba(fg.get_red(), fg.get_green(), fg.get_blue(), fg.get_alpha());
        cr->move_to((w - lw) / 2.0, (h - lh) / 2.0);
        layout->show_in_cairo_context(cr);
    });
    m_arts.push_back(art);
    cell->append(*art);

    auto l = Gtk::make_managed<Gtk::Label>(unicode::nice_name(cp));
    l->set_ellipsize(Pango::EllipsizeMode::MIDDLE);
    l->set_max_width_chars(11);
    l->add_css_class("cell-name");
    m_captions.push_back(l);
    cell->append(*l);
    auto c = Gtk::make_managed<Gtk::Label>(unicode::format_cp(cp));
    c->add_css_class("cp");
    m_captions.push_back(c);
    cell->append(*c);

    child->set_child(*cell);
    return child;
}

void OverviewGrid::update(const std::set<std::string>& ids) {
    const auto& font = m_ctx.doc.font;
    std::set<std::string> redraw;
    bool all = false;
    for (const auto& id : ids) {
        if (id == model::Font::kMetrics || id == model::Font::kInfo) { all = true; continue; }   // the em box moved
        const model::Glyph* g = font.glyph(id);
        const auto k = m_known.find(id);
        const bool shown_before = k != m_known.end();
        if (!g && !shown_before) {
            if (font.glyph_count() != m_known.size() && !m_ctx.filter && !m_ctx.scope) { refresh(); return; }
            continue;   // a font record, or a glyph the grid does not show
        }
        if (!g || !shown_before || g->unicodes != k->second) { refresh(); return; }   // the set changed
        redraw.insert(id);
        if (auto b = m_box_of.find(id); b != m_box_of.end()) {
            for (const char* cls : {"empty", "drafted", "done", "review"}) b->second->remove_css_class(cls);
            b->second->add_css_class(model::status_name(g->status));
        }
    }
    if (all) { for (auto* a : m_arts) a->queue_draw(); return; }
    if (redraw.empty()) return;
    // Composites draw their bases: a changed base redraws its users too.
    for (const auto& [name, art] : m_art_of) {
        if (redraw.count(name)) { art->queue_draw(); continue; }
        if (const model::Glyph* g = font.glyph(name))
            for (const auto& k : g->components)
                if (redraw.count(k.base)) { art->queue_draw(); break; }
    }
}

void OverviewGrid::refresh() {
    m_syncing = true;
    while (auto* c = m_sections.get_first_child()) m_sections.remove(*c);
    m_flows.clear();
    m_cells.clear();
    m_arts.clear();
    m_art_of.clear();
    m_box_of.clear();
    m_known.clear();
    m_captions.clear();

    const auto& font = m_ctx.doc.font;
    const auto order = font.glyph_order();
    const auto* cs = unicode::charset(m_charset);
    m_missing = cs ? model::missing_from(font, cs->cps) : std::vector<char32_t>{};
    const bool gaps = cs && !m_ctx.filter && !m_ctx.scope;   // a narrowed overview shows glyphs only
    m_scope_label.set_visible(m_ctx.scope.has_value());
    m_scope_all.set_visible(m_ctx.scope.has_value());
    if (m_ctx.scope)
        m_scope_label.set_text("Showing " + std::to_string(m_ctx.scope->size()) +
                               (m_ctx.scope->size() == 1 ? " glyph" : " glyphs") + " picked in the navigator");

    // A section's items in code point order: a glyph (by its first code
    // point), or a gap. Unencoded glyphs keep glyph order, last.
    struct Item { char32_t cp; std::size_t pos; std::string glyph; };
    std::map<std::pair<int, std::string>, std::vector<Item>> sections;
    for (std::size_t i = 0; i < order.size(); ++i) {
        const auto& name = order[i];
        if (!m_ctx.shows(name) || !m_ctx.in_scope(name)) continue;
        const model::Glyph* g = font.glyph(name);
        const std::string sec = unicode::section_of(g->unicodes);
        sections[{unicode::section_rank(sec), sec}].push_back(
            {g->unicodes.empty() ? char32_t(0) : g->unicodes.front(), i, name});
    }
    if (gaps)
        for (char32_t cp : m_missing) {
            const std::string sec = unicode::section_of({cp});
            sections[{unicode::section_rank(sec), sec}].push_back({cp, 0, {}});
        }

    for (auto& [key, items] : sections) {
        std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
            if (a.cp != b.cp) return a.cp < b.cp;
            return a.pos < b.pos;
        });
        std::size_t n_glyphs = 0, n_gaps = 0;
        for (const auto& it : items) (it.glyph.empty() ? n_gaps : n_glyphs)++;
        m_sections.append(*section_header(key.second, n_glyphs, n_gaps));

        auto flow = Gtk::make_managed<Gtk::FlowBox>();
        flow->set_selection_mode(Gtk::SelectionMode::SINGLE);
        flow->set_homogeneous(true);
        flow->set_valign(Gtk::Align::START);
        flow->set_max_children_per_line(256);
        flow->set_row_spacing(8);
        flow->set_column_spacing(8);
        flow->set_activate_on_single_click(false);
        flow->signal_selected_children_changed().connect([this, flow] { on_selected(*flow); });
        flow->signal_child_activated().connect([this](Gtk::FlowBoxChild* c) { on_activated(c); });
        for (const auto& it : items)
            flow->append(it.glyph.empty() ? *gap_cell(it.cp) : *glyph_cell(it.glyph));
        m_flows.push_back(flow);
        m_sections.append(*flow);
    }
    if (sections.empty()) {
        auto l = Gtk::make_managed<Gtk::Label>(m_ctx.filter || m_ctx.scope ? "No glyph matches." : "No glyphs yet.");
        l->add_css_class("hint");
        l->set_margin(32);
        m_sections.append(*l);
    }

    rebuild_checklist();
    update_coverage_ui();
    apply_cell_size();
    m_syncing = false;
    sync_selection();
}

void OverviewGrid::apply_cell_size() {
    for (auto* a : m_arts) {
        a->set_content_width(m_cell);
        a->set_content_height(m_cell);
    }
    // Below ~56 px the captions crowd the drawing: drawings only.
    const bool captions = m_cell >= 56;
    for (auto* l : m_captions) l->set_visible(captions);
}

void OverviewGrid::on_selected(Gtk::FlowBox& flow) {
    if (m_syncing) return;
    const auto sel = flow.get_selected_children();
    if (sel.empty()) return;
    const std::string id = sel.front()->get_name();
    // One selection across every section's box.
    m_syncing = true;
    for (auto* f : m_flows)
        if (f != &flow) f->unselect_all();
    m_syncing = false;
    if (is_gap(id)) {
        const char32_t cp = gap_cp(id);
        m_ctx.say(unicode::utf8(cp) + "  " + unicode::format_cp(cp) + "  " + unicode::name_of(cp) +
                  " -- not in the font; double-click to create it.", false);
        return;
    }
    m_ctx.select(id);
}

void OverviewGrid::on_activated(Gtk::FlowBoxChild* c) {
    const std::string id = c->get_name();
    auto& d = m_ctx.doc;
    if (is_gap(id)) {
        const char32_t cp = gap_cp(id);
        const std::string name = model::name_for_codepoint(d.font, cp);
        std::string why;
        bool built = false;
        if (command::create_glyph_for(d.journal, d.font, name, cp, {}, &built, &why)) {
            m_ctx.select(name);
            // A named list, not a braced temporary: GCC 16 at -O2 reports a false
            // -Wfree-nonheap-object on the temporary's destructor (s03).
            const std::vector<std::string> reselected{name};
            d.journal.reselect(reselected);
            m_ctx.say("Created " + name + " for " + unicode::format_cp(cp) + (built ? ", built from its parts." : "."), false);
        } else if (!why.empty()) {
            m_ctx.say(why, true);
        }
        return;
    }
    m_ctx.select(id);
    if (m_ctx.show_view) m_ctx.show_view("editor");
}

void OverviewGrid::create_missing() {
    const auto* cs = unicode::charset(m_charset);
    if (!cs || m_missing.empty()) return;
    auto& d = m_ctx.doc;
    auto todo = model::glyphs_for(d.font, m_missing);
    const std::size_t built = model::plan_composites(d.font, todo);
    const std::string label = "Create " + std::to_string(todo.size()) + " missing (" + cs->name + ")" +
                              (built ? ", " + std::to_string(built) + " as composites" : std::string{});
    std::string why;
    if (command::add_glyphs(d.journal, d.font, todo, label, &why))
        m_ctx.say(label + ". Ctrl+Z takes them all back.", false);
    else if (!why.empty())
        m_ctx.say(why, true);
}

void OverviewGrid::sync_selection() {
    m_syncing = true;
    for (auto* f : m_flows) f->unselect_all();
    if (!m_ctx.selected.empty())
        if (const auto it = m_cells.find(m_ctx.selected); it != m_cells.end())
            if (auto* flow = dynamic_cast<Gtk::FlowBox*>(it->second->get_parent())) flow->select_child(*it->second);
    m_syncing = false;
}

}  // namespace granjon::views
