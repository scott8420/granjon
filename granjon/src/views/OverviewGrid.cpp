#include "views/OverviewGrid.hpp"

#include "unicode/Blocks.hpp"
#include "views/GlyphPaint.hpp"

#include <gtkmm/drawingarea.h>
#include <gtkmm/label.h>

namespace granjon::views {

OverviewGrid::OverviewGrid(ViewContext& ctx)
    : widgets::ScrolledWindow("overview"), m_ctx(ctx), m_flow("overview.flow") {
    add_css_class("overview");
    set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_flow.set_selection_mode(Gtk::SelectionMode::SINGLE);
    m_flow.set_homogeneous(true);
    m_flow.set_valign(Gtk::Align::START);
    m_flow.set_max_children_per_line(64);
    m_flow.set_row_spacing(10);
    m_flow.set_column_spacing(10);
    m_flow.set_activate_on_single_click(false);
    m_flow.signal_selected_children_changed().connect([this] {
        if (m_syncing) return;
        const auto sel = m_flow.get_selected_children();
        if (!sel.empty()) m_ctx.select(sel.front()->get_name());
    });
    m_flow.signal_child_activated().connect([this](Gtk::FlowBoxChild* c) {
        m_ctx.select(c->get_name());
        if (m_ctx.show_view) m_ctx.show_view("editor");
    });
    set_child(m_flow);
    m_ctx.selection_changed.connect([this] { sync_selection(); });
    m_ctx.filter_changed.connect([this] { refresh(); });
}

void OverviewGrid::refresh() {
    m_syncing = true;
    while (auto* c = m_flow.get_first_child()) m_flow.remove(*c);
    const auto& font = m_ctx.doc.font;
    for (const auto& name : font.glyph_order()) {
        if (!m_ctx.shows(name)) continue;
        const model::Glyph* g = font.glyph(name);
        auto child = Gtk::make_managed<Gtk::FlowBoxChild>();
        child->set_name(name);
        auto cell = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
        cell->add_css_class("glyph-cell");
        cell->add_css_class(model::status_name(g->status));

        auto art = Gtk::make_managed<Gtk::DrawingArea>();
        art->set_content_width(84);
        art->set_content_height(84);
        art->set_draw_func([this, name, art](const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
            const model::Glyph* gg = m_ctx.doc.font.glyph(name);
            if (!gg) return;
            const auto fg = art->get_color();
            cr->set_source_rgba(fg.get_red(), fg.get_green(), fg.get_blue(), fg.get_alpha());
            paint_glyph_fitted(cr, m_ctx.doc.font, *gg, w, h, 8);
        });
        cell->append(*art);

        auto l = Gtk::make_managed<Gtk::Label>(name);
        l->set_ellipsize(Pango::EllipsizeMode::MIDDLE);
        l->set_max_width_chars(11);
        l->add_css_class("cell-name");
        cell->append(*l);
        auto cp = Gtk::make_managed<Gtk::Label>(g->unicodes.empty() ? std::string("—")
                                                                     : unicode::format_cp(g->unicodes.front()));
        cp->add_css_class("cp");
        cell->append(*cp);

        child->set_child(*cell);
        m_flow.append(*child);
    }
    m_syncing = false;
    sync_selection();
}

void OverviewGrid::sync_selection() {
    m_syncing = true;
    m_flow.unselect_all();
    if (!m_ctx.selected.empty())
        for (int i = 0; auto* c = m_flow.get_child_at_index(i); ++i)
            if (std::string(c->get_name()) == m_ctx.selected) { m_flow.select_child(*c); break; }
    m_syncing = false;
}

}  // namespace granjon::views
