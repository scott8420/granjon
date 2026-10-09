#include "AboutWindow.hpp"
#include "widgets/Widgets.hpp"
#include "Registry.hpp"

#include <gtkmm/box.h>
#include <gtkmm/headerbar.h>
#include <gtkmm/enums.h>

namespace granjon {

AboutWindow::AboutWindow(Gtk::Window& parent) {
    set_name("shell.about");
    registry::add("shell.about", this);

    set_title("About Granjon");
    set_modal(true);
    set_resizable(false);
    set_default_size(380, 340);
    set_transient_for(parent);
    set_hide_on_close(true);   // singleton: X hides, never destructs mid-session

    // Raw Gtk::HeaderBar, matching Shell's headerbar (Granjon doesn't wrap it --
    // widen on demand, and nothing addresses it).
    auto* hb = Gtk::make_managed<Gtk::HeaderBar>();
    hb->set_show_title_buttons(true);
    set_titlebar(*hb);

    auto* page = Gtk::make_managed<widgets::Box>(
        widgets::unregistered, "shell.about.page", Gtk::Orientation::VERTICAL);
    page->set_spacing(10);
    page->set_margin(24);
    page->set_halign(Gtk::Align::CENTER);

    // Hero logo: symbolic icon from the gresource bundle, recoloured to the
    // theme foreground by GTK. Proof the resource pipeline resolved.
    auto* logo = Gtk::make_managed<widgets::Image>(widgets::unregistered, "shell.about.logo");
    logo->set_from_icon_name("granjon-logo-symbolic");
    logo->set_pixel_size(88);
    page->append(*logo);

    auto* name = Gtk::make_managed<widgets::Label>(widgets::unregistered, "shell.about.name");
    name->set_markup("<span size='xx-large' weight='bold'>Granjon</span>");
    page->append(*name);

    auto* tagline = Gtk::make_managed<widgets::Label>(
        widgets::unregistered, "shell.about.tagline", "A font editor that teaches as it corrects.");
    tagline->add_css_class("dim-label");
    page->append(*tagline);

    // The window labels itself with the paradigms it embodies -- honest for a
    // seed (self-documenting), where a product About box would carry chrome.
    auto* note = Gtk::make_managed<widgets::Label>(widgets::unregistered, "shell.about.note");
    note->set_wrap(true);
    note->set_justify(Gtk::Justification::CENTER);
    note->set_max_width_chars(42);
    note->set_margin_top(6);
    note->add_css_class("dim-label");
    note->set_markup(
        "Named for Robert Granjon (1513\u20131590), punchcutter, "
        "whose types are still read today.\n\nVersion 0.1.0 \u2014 built from the Cairn seed.");
    page->append(*note);

    auto* close_btn = Gtk::make_managed<widgets::Button>(
        widgets::unregistered, "shell.about.close", "Close");
    close_btn->add_css_class("suggested-action");
    close_btn->set_halign(Gtk::Align::CENTER);
    close_btn->set_margin_top(8);
    close_btn->signal_clicked().connect([this]() { close(); });
    page->append(*close_btn);

    set_child(*page);
}

AboutWindow::~AboutWindow() {
    registry::remove(this);
}

}  // namespace granjon
