#include "views/GlyphSearch.hpp"

#include "command/Verbs.hpp"
#include "model/Find.hpp"
#include "unicode/Blocks.hpp"
#include "unicode/UcdTable.hpp"
#include "views/GlyphPaint.hpp"

#include <gtkmm/drawingarea.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/label.h>
#include <gtkmm/window.h>
#include <glibmm/markup.h>

#include <set>

namespace granjon::views {

namespace {
constexpr int         kMaxHits  = 8;
constexpr const char* kCreateId = "@create";

std::string utf8(char32_t cp) {
    std::string out;
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return out;
}
}  // namespace

GlyphSearch::GlyphSearch(ViewContext& ctx)
    : widgets::Box("search", Gtk::Orientation::HORIZONTAL),
      m_ctx(ctx),
      m_entry("search.entry"),
      m_list("search.results") {
    add_css_class("glyph-search");
    set_valign(Gtk::Align::CENTER);
    m_entry.set_placeholder_text("Find glyph  —  name, é, U+00E9");
    m_entry.set_width_chars(24);
    m_entry.signal_search_changed().connect(sigc::mem_fun(*this, &GlyphSearch::on_changed));
    m_entry.signal_activate().connect([this] { on_activate_row(m_list.get_selected_row()); });
    m_entry.signal_stop_search().connect([this] { clear(); });
    // Arrows walk the dropdown while the keyboard stays in the field.
    auto keys = Gtk::EventControllerKey::create();
    keys->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    keys->signal_key_pressed().connect([this](guint k, guint, Gdk::ModifierType) {
        if (k == GDK_KEY_Down) { move(+1); return true; }
        if (k == GDK_KEY_Up)   { move(-1); return true; }
        return false;
    }, false);
    m_entry.add_controller(keys);
    append(m_entry);

    // The dropdown never takes the keyboard (autohide off): typing continues
    // in the field while it shows.
    m_list.set_selection_mode(Gtk::SelectionMode::SINGLE);
    m_list.set_activate_on_single_click(true);
    m_list.signal_row_activated().connect([this](Gtk::ListBoxRow* r) { on_activate_row(r); });
    m_pop.add_css_class("search-results");
    m_pop.set_child(m_list);
    m_pop.set_autohide(false);
    m_pop.set_has_arrow(false);
    m_pop.set_position(Gtk::PositionType::BOTTOM);
    m_pop.set_size_request(340, -1);
    m_pop.set_parent(m_entry);
}

GlyphSearch::~GlyphSearch() { m_pop.unparent(); }

void GlyphSearch::focus() { m_entry.grab_focus(); }

void GlyphSearch::on_changed() {
    if (m_clearing) return;
    const auto r = model::find_glyphs(m_ctx.doc.font, m_entry.get_text());
    // Down and up again, so the dropdown re-measures: a shown popover keeps
    // its old height when the list shrinks.
    m_pop.popdown();
    while (auto* c = m_list.get_first_child()) m_list.remove(*c);
    m_create = r.create;

    if (r.empty_query) {
        m_ctx.set_filter(std::nullopt);
        m_pop.popdown();
        return;
    }
    std::set<std::string> names;
    for (const auto& h : r.hits) names.insert(h.name);
    m_ctx.set_filter(std::move(names));

    int shown = 0;
    for (const auto& h : r.hits) {
        if (shown++ == kMaxHits) break;
        const model::Glyph* g = m_ctx.doc.font.glyph(h.name);
        auto row = Gtk::make_managed<Gtk::ListBoxRow>();
        row->set_name(h.name);
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
        auto art = Gtk::make_managed<Gtk::DrawingArea>();
        art->set_content_width(30);
        art->set_content_height(30);
        art->add_css_class("hit-art");
        const std::string name = h.name;
        art->set_draw_func([this, name, art](const Cairo::RefPtr<Cairo::Context>& cr, int w, int hh) {
            const model::Glyph* gg = m_ctx.doc.font.glyph(name);
            if (!gg) return;
            const auto fg = art->get_color();
            cr->set_source_rgba(fg.get_red(), fg.get_green(), fg.get_blue(), fg.get_alpha());
            paint_glyph_fitted(cr, m_ctx.doc.font, *gg, w, hh, 2);
        });
        box->append(*art);
        auto l = Gtk::make_managed<Gtk::Label>(h.name);
        l->set_xalign(0.0f);
        l->set_hexpand(true);
        l->set_ellipsize(Pango::EllipsizeMode::END);
        box->append(*l);
        if (g && !g->unicodes.empty()) {
            auto cp = Gtk::make_managed<Gtk::Label>(unicode::format_cp(g->unicodes.front()));
            cp->add_css_class("cp");
            box->append(*cp);
        }
        row->set_child(*box);
        m_list.append(*row);
    }
    if (static_cast<int>(r.hits.size()) > kMaxHits) {
        auto more = Gtk::make_managed<Gtk::ListBoxRow>();
        more->set_selectable(false);
        more->set_activatable(false);
        auto l = Gtk::make_managed<Gtk::Label>("+" + std::to_string(r.hits.size() - kMaxHits) +
                                               " more in the navigator");
        l->add_css_class("hint");
        more->set_child(*l);
        m_list.append(*more);
    }
    for (std::size_t i = 0; i < m_create.size(); ++i) {
        const auto& c = m_create[i];
        auto row = Gtk::make_managed<Gtk::ListBoxRow>();
        row->set_name(std::string(kCreateId) + std::to_string(i));
        row->set_tooltip_text(unicode::name_of(c.cp));
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
        auto icon = Gtk::make_managed<Gtk::Image>();
        icon->set_from_icon_name("list-add-symbolic");
        box->append(*icon);
        auto l = Gtk::make_managed<Gtk::Label>();
        l->set_xalign(0.0f);
        l->set_hexpand(true);
        l->set_ellipsize(Pango::EllipsizeMode::END);
        l->set_markup("Create <b>" + Glib::Markup::escape_text(utf8(c.cp)) + "</b> as " +
                      Glib::Markup::escape_text(c.name));
        box->append(*l);
        auto cp = Gtk::make_managed<Gtk::Label>(unicode::format_cp(c.cp));
        cp->add_css_class("cp");
        box->append(*cp);
        row->set_child(*box);
        m_list.append(*row);
    }
    if (r.hits.empty() && m_create.empty()) {
        auto none = Gtk::make_managed<Gtk::ListBoxRow>();
        none->set_selectable(false);
        none->set_activatable(false);
        auto l = Gtk::make_managed<Gtk::Label>("No glyph matches.");
        l->add_css_class("hint");
        none->set_child(*l);
        m_list.append(*none);
    }
    if (auto* first = m_list.get_row_at_index(0); first && first->get_selectable()) m_list.select_row(*first);
    m_pop.popup();
}

void GlyphSearch::move(int delta) {
    if (!m_pop.get_visible()) return;
    auto* cur = m_list.get_selected_row();
    int i = cur ? cur->get_index() + delta : 0;
    while (auto* r = m_list.get_row_at_index(i)) {
        if (r->get_selectable()) { m_list.select_row(*r); return; }
        i += delta;
        if (i < 0) return;
    }
}

void GlyphSearch::on_activate_row(Gtk::ListBoxRow* row) {
    if (!row) return;
    const std::string id = row->get_name();
    auto& d = m_ctx.doc;
    if (id.rfind(kCreateId, 0) == 0) {
        const std::size_t i = std::stoul(id.substr(std::string(kCreateId).size()));
        if (i >= m_create.size()) return;
        const model::FindCreate c = m_create[i];
        std::string why;
        clear();
        bool built = false;
        if (command::create_glyph_for(d.journal, d.font, c.name, c.cp, m_ctx.selected, &built, &why)) {
            m_ctx.select(c.name);
            // A named list, not a braced temporary: GCC 16 at -O2 reports a false
            // -Wfree-nonheap-object on the temporary's destructor (s03).
            const std::vector<std::string> reselected{c.name};
            d.journal.reselect(reselected);
            m_ctx.say("Created " + c.name + " for " + unicode::format_cp(c.cp) + (built ? ", built from its parts." : "."), false);
        } else if (!why.empty()) {
            m_ctx.say(why, true);
        }
        return;
    }
    if (!d.font.glyph(id)) return;
    clear();
    m_ctx.select(id);
}

void GlyphSearch::clear() {
    // Hand the keyboard back to the window, so the next Ctrl+Z is the
    // document's, not the search field's own text undo.
    if (auto* win = dynamic_cast<Gtk::Window*>(get_root())) win->unset_focus();
    m_clearing = true;
    m_entry.set_text("");
    m_clearing = false;
    while (auto* c = m_list.get_first_child()) m_list.remove(*c);
    m_create.clear();
    m_pop.popdown();
    m_ctx.set_filter(std::nullopt);
}

}  // namespace granjon::views
