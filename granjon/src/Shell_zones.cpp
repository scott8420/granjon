#include "Shell.hpp"
#include "Log.hpp"

#include <gtkmm/headerbar.h>
#include <gtkmm/label.h>
#include <gtkmm/separator.h>

// Shell_zones.cpp -- ZONES. Construction of the named regions: the header
// (Mac unified toolbar), the three-pane body, the view stack, the menu model.

namespace granjon {

namespace {
struct ViewSpec { const char* id; const char* title; };
constexpr ViewSpec kViews[] = {
    {"overview", "Overview"}, {"editor", "Editor"}, {"spacing", "Spacing"},
    {"preview", "Preview"},   {"report", "Report"},
};
}  // namespace

void Shell::build_header() {  // zone
    set_title("Granjon");
    set_default_size(1180, 760);

    auto header = Gtk::make_managed<Gtk::HeaderBar>();
    header->add_css_class("unified");

    m_logo_button.set_icon_name("granjon-logo-symbolic");
    m_logo_button.set_has_frame(false);
    m_logo_button.set_tooltip_text("About Granjon");
    m_logo_button.set_action_name("win.about");
    header->pack_start(m_logo_button);


    // Undo / Redo: every view writes through the one journal, so they belong
    // to the window. A linked pair, Mac-style.
    auto history = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL);
    history->add_css_class("linked");
    m_undo_button.set_icon_name("edit-undo-symbolic");
    m_undo_button.set_action_name("win.undo");
    m_redo_button.set_icon_name("edit-redo-symbolic");
    m_redo_button.set_action_name("win.redo");
    history->append(m_undo_button);
    history->append(m_redo_button);
    header->pack_start(*history);

    // The view switcher: one segmented pill where the title would be. Each
    // segment is a radio target of win.view, so the checked state follows the
    // action -- a click, Ctrl+1..5, or a double-click in the overview all
    // land in the same place.
    m_switcher.add_css_class("segmented");
    m_switcher.set_valign(Gtk::Align::CENTER);
    int n = 1;
    for (const auto& v : kViews) {
        auto b = Gtk::make_managed<widgets::ToggleButton>(std::string("shell.view.") + v.id);
        b->set_label(v.title);
        b->set_action_name("win.view");
        b->set_action_target_value(Glib::Variant<Glib::ustring>::create(v.id));
        b->set_tooltip_text(std::string(v.title) + "  (Ctrl+" + std::to_string(n++) + ")");
        m_switcher.append(*b);
    }
    header->set_title_widget(m_switcher);

    m_menu_button.set_icon_name("open-menu-symbolic");
    m_menu_button.set_menu_model(build_menu());
    header->pack_end(m_menu_button);

    // The panel toggles: ONE linked widget, left half the navigator, right
    // half the inspector -- the same pair as Folio / jot, so the halves sit
    // where the panels do.
    m_nav_toggle.set_icon_name("sidebar-show-symbolic");
    m_nav_toggle.set_tooltip_text("Navigator  (F9)");
    m_nav_toggle.set_action_name("win.show-navigator");
    m_insp_toggle.set_icon_name("sidebar-show-right-symbolic");
    m_insp_toggle.set_tooltip_text("Inspector  (Ctrl+I)");
    m_insp_toggle.set_action_name("win.show-inspector");
    m_panels.add_css_class("linked");
    m_panels.add_css_class("panel-toggles");
    m_panels.set_valign(Gtk::Align::CENTER);
    m_panels.append(m_nav_toggle);
    m_panels.append(m_insp_toggle);
    header->pack_end(m_panels);
    header->pack_end(m_search);

    set_titlebar(*header);
}

void Shell::build_body() {  // zone: navigator | views | inspector, status strip
    build_views();

    // Panels keep their width when the window grows; the view takes the slack.
    m_inner.set_start_child(m_stack);
    m_inner.set_end_child(m_inspector);
    m_inner.set_resize_start_child(true);
    m_inner.set_resize_end_child(false);
    m_inner.set_shrink_end_child(false);
    m_inner.set_shrink_start_child(false);

    m_outer.set_start_child(m_navigator);
    m_outer.set_end_child(m_inner);
    m_outer.set_resize_start_child(false);
    m_outer.set_shrink_start_child(false);
    m_outer.set_shrink_end_child(false);
    m_outer.set_position(230);
    m_outer.set_vexpand(true);

    m_status.set_xalign(0.0f);
    m_status.set_ellipsize(Pango::EllipsizeMode::END);
    m_status.add_css_class("statusbar");
    m_status.set_text("Ready.");

    m_frame.append(m_outer);
    m_frame.append(m_status);
    set_child(m_frame);
}

void Shell::build_views() {  // zone: the view stack
    m_stack.set_hexpand(true);
    m_stack.set_vexpand(true);
    m_stack.add_css_class("view-stack");
    m_stack.set_transition_type(Gtk::StackTransitionType::CROSSFADE);
    m_stack.set_transition_duration(120);
    m_stack.add(m_overview, "overview", "Overview");
    m_stack.add(m_editor, "editor", "Editor");
    add_stub_view("spacing", "Spacing",
        "A live HarfBuzz-shaped strip. Click a pair and drag to kern; metrics keys "
        "propagate; contact gap and optical area read out per pair.");
    add_stub_view("preview", "Preview", "Waterfall sizes, sample text, light and dark.");
    add_stub_view("report", "Report",
        "Every check's findings, each with why it matters and a fix / skip / fix-all "
        "that goes through undo.");
}

void Shell::add_stub_view(const char* id, const char* title, const char* what) {  // zone
    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
    box->set_name(std::string("view.") + id);
    box->add_css_class("empty-state");
    box->set_valign(Gtk::Align::CENTER);
    box->set_halign(Gtk::Align::CENTER);
    auto head = Gtk::make_managed<Gtk::Label>(title);
    head->add_css_class("empty-title");
    auto body = Gtk::make_managed<Gtk::Label>(what);
    body->set_wrap(true);
    body->set_max_width_chars(48);
    body->set_justify(Gtk::Justification::CENTER);
    body->add_css_class("hint");
    box->append(*head);
    box->append(*body);
    m_stack.add(*box, id, title);
}

Glib::RefPtr<Gio::Menu> Shell::build_menu() {  // zone: hamburger model
    auto menu = Gio::Menu::create();

    auto file = Gio::Menu::create();
    file->append("New", "win.new");
    file->append("Open…", "win.open");
    m_recents_menu = Gio::Menu::create();
    file->append_submenu("Open recent", m_recents_menu);
    menu->append_section(file);
    auto save = Gio::Menu::create();
    save->append("Save", "win.save");
    save->append("Save as…", "win.save-as");
    menu->append_section(save);

    // Undo / Redo, re-labelled live and greyed when there is nothing to do --
    // update_undo_ui() owns this section.
    m_edit_menu = Gio::Menu::create();
    m_edit_menu->append("Undo", "win.undo");
    m_edit_menu->append("Redo", "win.redo");
    menu->append_section(m_edit_menu);

    auto fontm = Gio::Menu::create();
    fontm->append("Add default anchors to every letter and mark", "win.font-anchors");
    menu->append_section(fontm);

    auto view = Gio::Menu::create();
    view->append("Navigator", "win.show-navigator");
    view->append("Inspector", "win.show-inspector");
    menu->append_section(view);

    auto diag = Gio::Menu::create();
    diag->append("Keyboard shortcuts", "win.shortcuts");
    diag->append("Dump registry", "win.dump-registry");
    menu->append_section(diag);

    auto app = Gio::Menu::create();
    app->append("Preferences…", "win.preferences");
    app->append("About Granjon", "win.about");
    app->append("Quit", "win.quit");
    menu->append_section(app);
    return menu;
}

}  // namespace granjon
