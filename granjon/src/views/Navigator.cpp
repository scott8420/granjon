#include "views/Navigator.hpp"

#include "command/Verbs.hpp"
#include "unicode/Blocks.hpp"

#include <gtkmm/gestureclick.h>
#include <gtkmm/label.h>
#include <gtkmm/separator.h>

#include <algorithm>
#include <cctype>
#include <map>

namespace granjon::views {

namespace {
// Rows carry their glyph name as the widget name; the font row carries "@font".
constexpr const char* kFontRow = "@font";

Gtk::Box* section_header(const std::string& title, std::size_t count, Gtk::Label** chevron) {
    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    box->add_css_class("section-header");
    auto ch = Gtk::make_managed<Gtk::Label>("\u25BE");   // ▾ open, ▸ folded
    ch->add_css_class("chevron");
    box->append(*ch);
    *chevron = ch;
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


    // Multi-select (Ctrl / Shift + click, GTK's own rules); a double-click
    // opens the pick in editor tabs.
    m_list.set_selection_mode(Gtk::SelectionMode::MULTIPLE);
    m_list.set_activate_on_single_click(false);
    m_list.add_css_class("source-list");
    m_list.signal_selected_rows_changed().connect(sigc::mem_fun(*this, &Navigator::on_rows_changed));
    m_list.signal_row_activated().connect(sigc::mem_fun(*this, &Navigator::on_row_activated));
    {   // Which modifiers and which row the press had (the rows-changed signal knows neither).
        auto press = Gtk::GestureClick::create();
        press->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
        press->signal_pressed().connect([this, p = press.get()](int, double, double y) {
            const auto st = p->get_current_event_state();
            m_additive = (st & (Gdk::ModifierType::CONTROL_MASK | Gdk::ModifierType::SHIFT_MASK)) != Gdk::ModifierType{};
            auto* r = m_list.get_row_at_y(static_cast<int>(y));
            m_press_row = r ? std::string(r->get_name()) : std::string{};
        });
        m_list.add_controller(press);
    }
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

    m_ctx.selection_changed.connect([this] { sync_selection(); });
    m_ctx.filter_changed.connect([this] { refresh(); });
    // "Show all" in the overview (or a new font) clears the scope: the pick goes too.
    m_ctx.scope_changed.connect([this] {
        if (m_ctx.scope || (m_multi.empty() && m_picked_sections.empty())) return;
        m_syncing = true;
        m_multi.clear();
        m_picked_sections.clear();
        for (auto& [n, sec] : m_sections) if (sec.header) sec.header->remove_css_class("picked");
        m_list.unselect_all();
        m_syncing = false;
        sync_selection();
    });
}

void Navigator::refresh() {
    m_syncing = true;
    while (auto* c = m_list.get_first_child()) m_list.remove(*c);
    m_sections.clear();
    m_glyph_section.clear();
    m_known.clear();
    m_dots.clear();
    m_font_label = nullptr;
    const auto& font = m_ctx.doc.font;
    const bool filtering = m_ctx.filter.has_value();

    // Group glyph order by section, sections by rank.
    std::map<std::pair<int, std::string>, std::vector<const model::Glyph*>> groups;
    for (const auto& name : font.glyph_order()) {
        const model::Glyph* g = font.glyph(name);
        if (!m_ctx.shows(name)) continue;
        // M14c: parts have their own section, whatever their code points.
        const std::string sec = g->skip_export ? std::string("Parts") : unicode::section_of(g->unicodes);
        groups[{unicode::section_rank(sec), sec}].push_back(g);
    }

    // The font row, unless a search is narrowing to glyphs.
    if (!filtering) {
        auto head = Gtk::make_managed<Gtk::ListBoxRow>();
        head->set_selectable(false);
        head->set_activatable(false);
        head->add_css_class("section-row");
        Gtk::Label* chev = nullptr;
        head->set_child(*section_header("Font", 0, &chev));
        chev->set_visible(false);   // the font row does not fold
        m_list.append(*head);
        auto row = Gtk::make_managed<Gtk::ListBoxRow>();
        row->set_name(kFontRow);
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto icon = Gtk::make_managed<Gtk::Image>();
        icon->set_from_icon_name("preferences-desktop-font-symbolic");
        auto l = Gtk::make_managed<Gtk::Label>(m_ctx.doc.title());
        m_font_label = l;
        l->set_xalign(0.0f);
        l->set_ellipsize(Pango::EllipsizeMode::END);
        box->append(*icon);
        box->append(*l);
        row->set_child(*box);
        m_list.append(*row);
    }

    // Within a section: code point order (as the overview does); unencoded
    // glyphs keep glyph order (stable sort, 0 sorts first only in Unencoded,
    // where every key is 0).
    for (auto& [key, glyphs] : groups)
        std::stable_sort(glyphs.begin(), glyphs.end(), [](const model::Glyph* a, const model::Glyph* b) {
            const char32_t ca = a->unicodes.empty() ? char32_t(0) : a->unicodes.front();
            const char32_t cb = b->unicodes.empty() ? char32_t(0) : b->unicodes.front();
            return ca < cb;
        });

    std::size_t shown = 0;
    for (const auto& [key, glyphs] : groups) {
        // The section's header: its own row, not selectable; a click folds it.
        Section& sec = m_sections[key.second];
        auto hrow = Gtk::make_managed<Gtk::ListBoxRow>();
        hrow->set_name("@section:" + key.second);
        hrow->set_selectable(false);
        hrow->set_activatable(false);
        hrow->add_css_class("section-row");
        hrow->set_child(*section_header(key.second, glyphs.size(), &sec.chevron));
        hrow->set_tooltip_text("Click: show this section in the overview (Ctrl+click adds).  Double-click: open it in tabs.\n"
                               "The arrow folds / opens it (Ctrl+click the arrow: all of them).");
        if (m_picked_sections.count(key.second)) hrow->add_css_class("picked");
        auto click = Gtk::GestureClick::create();
        click->signal_pressed().connect([this, name = key.second, c = click.get()](int n, double x, double) {
            const auto st = c->get_current_event_state();
            const bool mod = (st & (Gdk::ModifierType::CONTROL_MASK | Gdk::ModifierType::SHIFT_MASK |
                                    Gdk::ModifierType::ALT_MASK)) != Gdk::ModifierType{};
            on_section_click(name, n, x, mod);
        });
        hrow->add_controller(click);
        sec.header = hrow;
        m_list.append(*hrow);
        for (const model::Glyph* g : glyphs) {
            auto row = Gtk::make_managed<Gtk::ListBoxRow>();
            row->set_name(g->name);
            auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
            auto* dot = status_dot(model::status_name(g->status));
            m_dots[g->name] = dot;
            m_known[g->name] = g->unicodes;
            box->append(*dot);
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
            sec.rows.push_back(row);
            m_glyph_section[g->name] = key.second;
            m_list.append(*row);
            ++shown;
        }
    }

    const std::size_t total = font.glyph_count();
    m_count.set_text(!filtering ? std::to_string(total) + " glyphs"
                                : std::to_string(shown) + " of " + std::to_string(total));
    // Put the multi-selection back (a rebuild drops it); forget what is gone.
    std::set<std::string> still;
    for (int i = 0; auto* r = m_list.get_row_at_index(i); ++i)
        if (m_multi.count(r->get_name())) { m_list.select_row(*r); still.insert(r->get_name()); }
    m_multi = std::move(still);
    for (auto it = m_picked_sections.begin(); it != m_picked_sections.end();)
        it = m_sections.count(*it) ? std::next(it) : m_picked_sections.erase(it);
    m_syncing = false;
    apply_folds();
    sync_selection();
}

void Navigator::update(const std::set<std::string>& ids) {
    const auto& font = m_ctx.doc.font;
    for (const auto& id : ids) {
        if (id == model::Font::kInfo) {
            if (m_font_label) m_font_label->set_text(m_ctx.doc.title());
            continue;
        }
        const model::Glyph* g = font.glyph(id);
        const auto k = m_known.find(id);
        if (!g && k == m_known.end()) continue;   // a font record, or a glyph never shown
        if (!g || k == m_known.end() || g->unicodes != k->second) { refresh(); return; }   // the set changed
        if (auto d = m_dots.find(id); d != m_dots.end()) {
            for (const char* cls : {"empty", "drafted", "done", "review"}) d->second->remove_css_class(cls);
            d->second->add_css_class(model::status_name(g->status));
        }
    }
    const std::size_t total = font.glyph_count();
    if (!m_ctx.filter) m_count.set_text(std::to_string(total) + " glyphs");
}

bool Navigator::is_open(const std::string& section) const {
    if (m_ctx.filter) return true;   // a search shows its hits, folded or not
    const auto& o = m_ctx.settings.navigator_open;
    return std::find(o.begin(), o.end(), section) != o.end();
}

void Navigator::set_open(const std::string& section, bool open, bool save) {
    auto& o = m_ctx.settings.navigator_open;
    const auto it = std::find(o.begin(), o.end(), section);
    if (open == (it != o.end())) return;
    if (open) o.push_back(section);
    else      o.erase(it);
    if (save && m_ctx.save_settings) m_ctx.save_settings();
    apply_folds();
}

void Navigator::toggle_all(bool open) {
    auto& o = m_ctx.settings.navigator_open;
    o.clear();
    if (open)
        for (const auto& [name, sec] : m_sections) o.push_back(name);
    if (m_ctx.save_settings) m_ctx.save_settings();
    apply_folds();
}

void Navigator::apply_folds() {
    for (auto& [name, sec] : m_sections) {
        const bool open = is_open(name);
        if (sec.chevron) sec.chevron->set_text(open ? "\u25BE" : "\u25B8");
        for (auto* r : sec.rows) r->set_visible(open);
    }
}

void Navigator::sync_selection() {
    m_syncing = true;
    const std::string want = m_ctx.selected.empty() ? std::string(kFontRow) : m_ctx.selected;
    // Inside the current pick (the overview shows only the pick, so a click
    // there lands here): leave the multi-selection alone.
    if (m_multi.count(want) || (m_ctx.scope && m_ctx.scope->count(want))) {
        m_remove.set_sensitive(!m_ctx.selected.empty());
        m_syncing = false;
        return;
    }
    // From outside the pick (search, undo, a new glyph): it becomes the pick.
    // In a folded section: open that section so the row can show selected.
    if (const auto it = m_glyph_section.find(want); it != m_glyph_section.end() && !is_open(it->second))
        set_open(it->second, true);
    Gtk::ListBoxRow* hit = nullptr;
    for (int i = 0; auto* r = m_list.get_row_at_index(i); ++i)
        if (std::string(r->get_name()) == want) { hit = r; break; }
    m_list.unselect_all();
    if (hit) m_list.select_row(*hit);
    m_multi.clear();
    if (hit && want != kFontRow) m_multi.insert(want);
    m_picked_sections.clear();
    for (auto& [n, sec] : m_sections) if (sec.header) sec.header->remove_css_class("picked");
    m_remove.set_sensitive(!m_ctx.selected.empty());
    m_syncing = false;
    update_scope();
}

std::vector<std::string> Navigator::picked_glyphs() const {
    std::vector<std::string> out;
    for (int i = 0; auto* r = m_list.get_row_at_index(i); ++i) {
        const std::string n = r->get_name();
        if (n.empty() || n[0] == '@') continue;
        const auto sec = m_glyph_section.find(n);
        if (m_multi.count(n) || (sec != m_glyph_section.end() && m_picked_sections.count(sec->second)))
            out.push_back(n);
    }
    return out;
}

void Navigator::update_scope() {
    if (m_multi.empty() && m_picked_sections.empty()) { m_ctx.set_scope(std::nullopt); return; }
    const auto names = picked_glyphs();
    m_ctx.set_scope(std::set<std::string>(names.begin(), names.end()));
}

void Navigator::on_rows_changed() {
    if (m_syncing) return;
    // A plain click starts a new pick: picked sections go.
    if (!m_additive && !m_picked_sections.empty()) {
        m_picked_sections.clear();
        for (auto& [n, sec] : m_sections) if (sec.header) sec.header->remove_css_class("picked");
    }
    m_multi.clear();
    bool font = false;
    for (auto* r : m_list.get_selected_rows()) {
        const std::string n = r->get_name();
        if (n == kFontRow) font = true;
        else if (!n.empty() && n[0] != '@') m_multi.insert(n);
    }
    if (font && !m_additive) {   // the font row alone: no pick, the whole font
        m_multi.clear();
        update_scope();
        m_ctx.select({});
        return;
    }
    update_scope();
    // The inspector / editor follow the row just clicked, if it is picked;
    // else the one they had, if still picked; else the first picked.
    std::string focus;
    if (m_multi.count(m_press_row)) focus = m_press_row;
    else if (m_multi.count(m_ctx.selected)) focus = m_ctx.selected;
    else if (!m_multi.empty()) focus = picked_glyphs().front();
    if (!focus.empty()) m_ctx.select(focus);
}

void Navigator::on_section_click(const std::string& section, int n_press, double x, bool additive) {
    if (x < 30) {   // the arrow: fold / open (Ctrl / Alt: all of them)
        if (n_press == 1) {
            if (additive) toggle_all(!is_open(section));
            else          set_open(section, !is_open(section));
        }
        return;
    }
    const auto sec = m_sections.find(section);
    if (sec == m_sections.end()) return;
    if (n_press == 2) {   // open the whole section in tabs
        std::vector<std::string> names;
        for (auto* r : sec->second.rows) names.push_back(r->get_name());
        if (!names.empty() && m_ctx.open_tabs) m_ctx.open_tabs(names, names.front());
        return;
    }
    m_syncing = true;
    if (additive) {
        if (!m_picked_sections.erase(section)) m_picked_sections.insert(section);
    } else {
        m_list.unselect_all();
        m_multi.clear();
        m_picked_sections = {section};
    }
    for (auto& [n, s] : m_sections)
        if (s.header) {
            if (m_picked_sections.count(n)) s.header->add_css_class("picked");
            else                            s.header->remove_css_class("picked");
        }
    m_syncing = false;
    update_scope();
    // The inspector / editor follow the section's first glyph.
    if (m_picked_sections.count(section) && !sec->second.rows.empty())
        m_ctx.select(sec->second.rows.front()->get_name());
}

void Navigator::on_row_activated(Gtk::ListBoxRow* row) {
    if (!row) return;
    const std::string id = row->get_name();
    if (id.empty() || id[0] == '@') return;
    auto names = picked_glyphs();
    if (std::find(names.begin(), names.end(), id) == names.end()) names = {id};
    if (m_ctx.open_tabs) m_ctx.open_tabs(names, id);
}

void Navigator::on_add() {
    auto& d = m_ctx.doc;
    std::string name = "newGlyph";
    for (int i = 2; d.font.glyph(name); ++i) name = "newGlyph" + std::to_string(i);
    std::string why;
    if (command::add_glyph(d.journal, d.font, name, {}, m_ctx.selected, &why)) {
        m_ctx.select(name);
        // A named list, not a braced temporary: GCC 16 at -O2 reports a false
        // -Wfree-nonheap-object on the temporary's destructor (s03).
        const std::vector<std::string> reselected{name};
        d.journal.reselect(reselected);
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
