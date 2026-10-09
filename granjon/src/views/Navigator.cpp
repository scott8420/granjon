#include "views/Navigator.hpp"

#include "command/Verbs.hpp"
#include "unicode/Blocks.hpp"

#include <gtkmm/label.h>
#include <gtkmm/separator.h>

#include <algorithm>
#include <cctype>
#include <map>

namespace granjon::views {

namespace {
// Rows carry their glyph name as the widget name; the font row carries "@font".
constexpr const char* kFontRow = "@font";

Gtk::Widget* section_header(const std::string& title, std::size_t count) {
    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    box->add_css_class("section-header");
    // Small caps by hand: GTK CSS has no text-transform.
    std::string upper = title;
    for (auto& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    auto l = Gtk::make_managed<Gtk::Label>(upper);
    l->set_xalign(0.0f);
    l->set_hexpand(true);
    box->append(*l);
    if (count > 0) {
        auto b = Gtk::make_managed<Gtk::Label>(std::to_string(count));
        b->add_css_class("badge");
        box->append(*b);
    }
    return box;
}

Gtk::Widget* status_dot(const char* cls) {
    auto d = Gtk::make_managed<Gtk::Box>();
    d->add_css_class("status-dot");
    d->add_css_class(cls);
    d->set_valign(Gtk::Align::CENTER);
    return d;
}

}  // namespace

Navigator::Navigator(ViewContext& ctx)
    : widgets::Box("nav", Gtk::Orientation::VERTICAL),
      m_ctx(ctx),
      m_scroll("nav.scroll"),
      m_list("nav.list"),
      m_add("nav.add"),
      m_remove("nav.remove"),
      m_count("nav.count") {
    add_css_class("navigator");
    set_size_request(200, -1);


    m_list.set_selection_mode(Gtk::SelectionMode::SINGLE);
    m_list.add_css_class("source-list");
    m_list.signal_row_selected().connect(sigc::mem_fun(*this, &Navigator::on_row_selected));
    m_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_scroll.set_vexpand(true);
    m_scroll.set_child(m_list);
    append(m_scroll);

    append(*Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL));
    auto bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 2);
    bar->add_css_class("nav-bar");
    m_add.set_icon_name("list-add-symbolic");
    m_add.set_has_frame(false);
    m_add.set_tooltip_text("New glyph after the selection");
    m_add.signal_clicked().connect(sigc::mem_fun(*this, &Navigator::on_add));
    m_remove.set_icon_name("list-remove-symbolic");
    m_remove.set_has_frame(false);
    m_remove.set_tooltip_text("Delete the selected glyph");
    m_remove.signal_clicked().connect(sigc::mem_fun(*this, &Navigator::on_remove));
    m_count.set_hexpand(true);
    m_count.set_xalign(1.0f);
    m_count.add_css_class("dim-label");
    bar->append(m_add);
    bar->append(m_remove);
    bar->append(m_count);
    append(*bar);

    // Section headers ride on the first row of each section, so a header is
    // never a selectable row of its own (the source-list shape).
    m_list.set_header_func([this](Gtk::ListBoxRow* row, Gtk::ListBoxRow* before) {
        const auto it = m_section.find(row->get_name());
        if (it == m_section.end()) { row->unset_header(); return; }
        const auto prev = before ? m_section.find(before->get_name()) : m_section.end();
        if (prev != m_section.end() && prev->second.first == it->second.first) { row->unset_header(); return; }
        row->set_header(*section_header(it->second.first, it->second.second));
    });

    m_ctx.selection_changed.connect([this] { sync_selection(); });
    m_ctx.filter_changed.connect([this] { refresh(); });
}

void Navigator::refresh() {
    m_syncing = true;
    while (auto* c = m_list.get_first_child()) m_list.remove(*c);
    m_section.clear();
    m_section[kFontRow] = {"Font", 0};
    const auto& font = m_ctx.doc.font;
    const bool filtering = m_ctx.filter.has_value();

    // Group glyph order by section, sections by rank.
    std::map<std::pair<int, std::string>, std::vector<const model::Glyph*>> groups;
    for (const auto& name : font.glyph_order()) {
        const model::Glyph* g = font.glyph(name);
        if (!m_ctx.shows(name)) continue;
        const std::string sec = unicode::section_of(g->unicodes);
        groups[{unicode::section_rank(sec), sec}].push_back(g);
    }

    // The font row, unless a search is narrowing to glyphs.
    if (!filtering) {
        auto row = Gtk::make_managed<Gtk::ListBoxRow>();
        row->set_name(kFontRow);
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto icon = Gtk::make_managed<Gtk::Image>();
        icon->set_from_icon_name("preferences-desktop-font-symbolic");
        auto l = Gtk::make_managed<Gtk::Label>(m_ctx.doc.title());
        l->set_xalign(0.0f);
        l->set_ellipsize(Pango::EllipsizeMode::END);
        box->append(*icon);
        box->append(*l);
        row->set_child(*box);
        m_list.append(*row);
    }

    std::size_t shown = 0;
    for (const auto& [key, glyphs] : groups) {
        for (const model::Glyph* g : glyphs) {
            auto row = Gtk::make_managed<Gtk::ListBoxRow>();
            row->set_name(g->name);
            auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
            box->append(*status_dot(model::status_name(g->status)));
            auto name = Gtk::make_managed<Gtk::Label>(g->name);
            name->set_xalign(0.0f);
            name->set_hexpand(true);
            name->set_ellipsize(Pango::EllipsizeMode::END);
            box->append(*name);
            if (!g->unicodes.empty()) {
                auto cp = Gtk::make_managed<Gtk::Label>(unicode::format_cp(g->unicodes.front()));
                cp->add_css_class("cp");
                box->append(*cp);
            }
            row->set_child(*box);
            m_section[g->name] = {key.second, glyphs.size()};
            m_list.append(*row);
            ++shown;
        }
    }

    const std::size_t total = font.glyph_count();
    m_count.set_text(!filtering ? std::to_string(total) + " glyphs"
                                : std::to_string(shown) + " of " + std::to_string(total));
    m_syncing = false;
    sync_selection();
}

void Navigator::sync_selection() {
    m_syncing = true;
    const std::string want = m_ctx.selected.empty() ? std::string(kFontRow) : m_ctx.selected;
    Gtk::ListBoxRow* hit = nullptr;
    for (int i = 0; auto* r = m_list.get_row_at_index(i); ++i)
        if (std::string(r->get_name()) == want) { hit = r; break; }
    if (hit) m_list.select_row(*hit);
    else     m_list.unselect_all();
    m_remove.set_sensitive(!m_ctx.selected.empty());
    m_syncing = false;
}

void Navigator::on_row_selected(Gtk::ListBoxRow* row) {
    if (m_syncing || !row) return;
    const std::string id = row->get_name();
    m_ctx.select(id == kFontRow ? std::string{} : id);
}

void Navigator::on_add() {
    auto& d = m_ctx.doc;
    std::string name = "newGlyph";
    for (int i = 2; d.font.glyph(name); ++i) name = "newGlyph" + std::to_string(i);
    std::string why;
    if (command::add_glyph(d.journal, d.font, name, {}, m_ctx.selected, &why)) {
        m_ctx.select(name);
        d.journal.reselect({name});
        m_ctx.say("Added " + name + " -- name it in the inspector.", false);
    } else if (!why.empty()) {
        m_ctx.say(why, true);
    }
}

void Navigator::on_remove() {
    auto& d = m_ctx.doc;
    const std::string gone = m_ctx.selected;
    if (gone.empty()) return;
    // The neighbour takes the selection: next in glyph order, else previous.
    const auto order = d.font.glyph_order();
    const auto it = std::find(order.begin(), order.end(), gone);
    std::string next;
    if (it != order.end()) {
        if (it + 1 != order.end()) next = *(it + 1);
        else if (it != order.begin()) next = *(it - 1);
    }
    std::string why;
    if (command::remove_glyph(d.journal, d.font, gone, &why)) {
        m_ctx.select(next);
        d.journal.reselect(next.empty() ? std::vector<std::string>{} : std::vector<std::string>{next});
        m_ctx.say("Deleted " + gone + ".", false);
    } else if (!why.empty()) {
        m_ctx.say(why, true);
    }
}

}  // namespace granjon::views
