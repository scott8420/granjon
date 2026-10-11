#include "PrimerWindow.hpp"

#include "Log.hpp"
#include "Registry.hpp"

#include <gdkmm/texture.h>
#include <glibmm/bytes.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/label.h>
#include <gtkmm/separator.h>

namespace granjon {

PrimerWindow::PrimerWindow(Follow follow)
    : m_follow(std::move(follow)),
      m_root("primer.root", Gtk::Orientation::HORIZONTAL),
      m_list("primer.list"),
      m_scroll("primer.scroll"),
      m_page("primer.page", Gtk::Orientation::VERTICAL, 12),
      m_title("primer.title"),
      m_no_figure("primer.no_figure"),
      m_body("primer.body", Gtk::Orientation::VERTICAL, 10),
      m_links("primer.links", Gtk::Orientation::VERTICAL, 4),
      m_prev("primer.prev"),
      m_next("primer.next") {
    set_name("shell.primer");
    registry::add("shell.primer", this);
    set_title("Primer -- drawing type");
    set_modal(false);
    set_default_size(900, 680);
    set_hide_on_close(true);

    // The list of notes.
    m_list.add_css_class("navigator");
    m_list.add_css_class("primer-list");
    m_list.set_selection_mode(Gtk::SelectionMode::BROWSE);
    m_list.set_size_request(230, -1);
    int i = 1;
    for (const auto& n : core::primer_notes()) {
        auto l = Gtk::make_managed<Gtk::Label>(std::to_string(i++) + ".  " + n.title.substr(0, n.title.find(" -- ")));
        l->set_xalign(0.0f);
        l->set_ellipsize(Pango::EllipsizeMode::END);
        l->set_margin_start(6);
        l->set_tooltip_text(n.title);
        m_list.append(*l);
    }
    m_list.signal_row_selected().connect([this](Gtk::ListBoxRow* r) { if (r) open(r->get_index()); });

    // The page.
    m_page.set_margin(24);
    m_page.set_margin_top(18);
    m_title.set_xalign(0.0f);
    m_title.set_wrap(true);
    m_title.add_css_class("primer-title");
    m_figure.set_can_shrink(true);
    m_figure.set_content_fit(Gtk::ContentFit::SCALE_DOWN);
    m_figure.set_size_request(-1, 260);
    m_figure.add_css_class("primer-figure");
    m_no_figure.add_css_class("hint");
    m_no_figure.set_wrap(true);
    m_no_figure.set_xalign(0.0f);
    m_page.append(m_title);
    m_page.append(m_figure);
    m_page.append(m_no_figure);
    m_page.append(m_body);

    auto try_it = Gtk::make_managed<Gtk::Label>("TRY IT");
    try_it->add_css_class("card-title");
    try_it->set_xalign(0.0f);
    try_it->set_margin_top(6);
    m_page.append(*try_it);
    m_page.append(m_links);

    auto nav = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    nav->set_margin_top(12);
    m_prev.set_label("← Previous");
    m_next.set_label("Next →");
    m_next.set_hexpand(true);
    m_next.set_halign(Gtk::Align::END);
    m_prev.signal_clicked().connect([this] { if (m_index > 0) open(m_index - 1); });
    m_next.signal_clicked().connect([this] { open(m_index + 1); });
    nav->append(m_prev);
    nav->append(m_next);
    m_page.append(*nav);

    m_scroll.set_child(m_page);
    m_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_scroll.set_hexpand(true);
    m_scroll.add_css_class("primer-page");

    m_root.append(m_list);
    m_root.append(*Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::VERTICAL));
    m_root.append(m_scroll);
    set_child(m_root);

    auto keys = Gtk::EventControllerKey::create();
    keys->signal_key_pressed().connect([this](guint key, guint, Gdk::ModifierType) {
        if (key == GDK_KEY_Escape) { set_visible(false); return true; }
        return false;
    }, false);
    add_controller(keys);
}

PrimerWindow::~PrimerWindow() { registry::remove(this); }

void PrimerWindow::show(Gtk::Window& parent, const std::string& note) {
    const auto& notes = core::primer_notes();
    int want = m_index < 0 ? 0 : m_index;
    for (std::size_t i = 0; i < notes.size(); ++i)
        if (notes[i].id == note) want = static_cast<int>(i);
    if (auto* r = m_list.get_row_at_index(want)) m_list.select_row(*r);
    open(want);
    set_transient_for(parent);
    if (parent.has_css_class("granjon-light")) add_css_class("granjon-light");
    else                                       remove_css_class("granjon-light");
    present();
}

void PrimerWindow::open(int index) {
    const auto& notes = core::primer_notes();
    if (index < 0 || index >= static_cast<int>(notes.size())) return;
    if (index == m_index && m_title.get_text() != "") return;
    m_index = index;
    if (auto* r = m_list.get_row_at_index(index); r && m_list.get_selected_row() != r) m_list.select_row(*r);
    const auto& n = notes[static_cast<std::size_t>(index)];
    m_title.set_text(n.title);

    // The figure: SVG from the core's data, through GdkPixbuf's SVG loader.
    m_figure.set_visible(false);
    m_no_figure.set_visible(false);
    if (const auto svg = core::primer_figure(n.figure); !svg.empty()) {
        try {
            auto bytes = Glib::Bytes::create(svg.data(), svg.size());
            m_figure.set_paintable(Gdk::Texture::create_from_bytes(bytes));
            m_figure.set_visible(true);
        } catch (const Glib::Error& e) {
            m_no_figure.set_text("(The figure needs GTK's SVG image loader -- librsvg / gdk-pixbuf's svg module.)");
            m_no_figure.set_visible(true);
            if (auto lg = log::get(log::Area::Shell)) lg->warn("primer figure '{}': {}", n.figure, e.what());
        }
    }

    while (auto* c = m_body.get_first_child()) m_body.remove(*c);
    for (const auto& p : n.paragraphs) {
        auto l = Gtk::make_managed<Gtk::Label>();
        l->set_markup(core::primer_markup(p));
        l->set_wrap(true);
        l->set_xalign(0.0f);
        l->set_max_width_chars(72);
        l->set_selectable(true);
        l->add_css_class("primer-text");
        m_body.append(*l);
    }
    while (auto* c = m_links.get_first_child()) m_links.remove(*c);
    for (const auto& lk : n.links) {
        auto b = Gtk::make_managed<Gtk::Button>("→  " + lk.label);
        b->set_has_frame(false);
        b->set_halign(Gtk::Align::START);
        b->add_css_class("primer-link");
        if (auto* lbl = dynamic_cast<Gtk::Label*>(b->get_child())) lbl->set_xalign(0.0f);
        const std::string target = lk.target;
        b->signal_clicked().connect([this, target] { if (m_follow) m_follow(target); });
        m_links.append(*b);
    }
    m_prev.set_sensitive(index > 0);
    m_next.set_sensitive(index + 1 < static_cast<int>(notes.size()));
    m_next.set_label(index + 1 < static_cast<int>(notes.size())
                         ? notes[static_cast<std::size_t>(index) + 1].title.substr(0, notes[static_cast<std::size_t>(index) + 1].title.find(" -- ")) + "  →"
                         : "Next →");
    m_scroll.get_vadjustment()->set_value(0);
}

}  // namespace granjon
