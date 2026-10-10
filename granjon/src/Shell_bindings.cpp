#include "Shell.hpp"
#include "Log.hpp"
#include "core/Recents.hpp"

#include <giomm/simpleactiongroup.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/settings.h>
#include <gtkmm/spinbutton.h>
#include <glibmm/variant.h>

#include <filesystem>

// Shell_bindings.cpp -- BINDINGS. What's wired to what: the win.* actions, the
// parameterised recents action group, and (where a keystroke must survive focus)
// accelerators. No slot bodies here -- those are handlers; this file only wires.

namespace granjon {

void Shell::bind_actions() {  // bindings: actions + recents group
    // Plain win.* actions -> handler bodies (Shell_handlers.cpp).
    add_action("new",           sigc::mem_fun(*this, &Shell::on_new));
    add_action("open",          sigc::mem_fun(*this, &Shell::on_open));
    add_action("save",          sigc::mem_fun(*this, &Shell::on_save));
    add_action("save-as",       sigc::mem_fun(*this, &Shell::on_save_as));
    signal_close_request().connect(sigc::mem_fun(*this, &Shell::on_close_request), false);
    add_action("dump-registry", sigc::mem_fun(*this, &Shell::on_dump_registry));
    add_action("about",         sigc::mem_fun(*this, &Shell::on_about));
    add_action("shortcuts",     sigc::mem_fun(*this, &Shell::on_shortcuts));
    add_action("preferences",   sigc::mem_fun(*this, &Shell::on_preferences));
    add_action("font-anchors",  sigc::mem_fun(*this, &Shell::on_font_anchors));
    add_action("quit",          [this]() { close(); });   // close-request asks about unsaved changes

    // Recents: a "recents" action group with a PARAMETERISED open verb --
    // recents.open(<path>) carries each entry's path as its target, so one
    // action serves every menu row (CANON/Curvz: the proven parameterised-action
    // shape). clear is held so rebuild_recents_menu can grey it when empty.
    auto group = Gio::SimpleActionGroup::create();
    group->add_action_with_parameter(
        "open", Glib::Variant<Glib::ustring>::variant_type(),
        [this](const Glib::VariantBase& param) {
            const std::string p =
                Glib::VariantBase::cast_dynamic<Glib::Variant<Glib::ustring>>(param).get();
            on_recent_open(p);
        });
    m_recents_clear = Gio::SimpleAction::create("clear");
    m_recents_clear->signal_activate().connect(
        [this](const Glib::VariantBase&) { on_recent_clear(); });
    group->add_action(m_recents_clear);
    insert_action_group("recents", group);

    // Now that the group exists, load the persisted list (prunes stale folders).
    m_recents = core::load_recents(m_recents_file);

    // ── undo ────────────────────────────────────────────────────────────────
    m_act_undo = add_action("undo", sigc::mem_fun(*this, &Shell::on_undo));
    m_act_redo = add_action("redo", sigc::mem_fun(*this, &Shell::on_redo));

    // ── views and panels ────────────────────────────────────────────────────
    // win.view is a radio action: the switcher's segments are its targets, the
    // registry binds Ctrl+1..5 to "win.view::<id>", the overview's
    // double-click goes through ctx.show_view -> the same action.
    m_act_view = add_action_radio_string("view", sigc::mem_fun(*this, &Shell::on_view), "overview");
    m_act_nav  = add_action_bool("show-navigator", sigc::mem_fun(*this, &Shell::on_toggle_navigator), true);
    m_act_insp = add_action_bool("show-inspector", sigc::mem_fun(*this, &Shell::on_toggle_inspector), true);
    add_action("find", [this] { m_search.focus(); });
    add_action("other-case", sigc::mem_fun(*this, &Shell::on_other_case));

    // ── the model <-> journal <-> UI wiring ─────────────────────────────────
    // The document told the font to ask its journal whether the door is open
    // (the one-door guard). Here: the font's writes queue one repaint; the
    // journal tells the UI when Undo / Redo change, and reads the selection
    // so a step hands it back on undo / redo.
    m_doc.font.set_on_change([this] { queue_repaint(); });
    m_doc.journal.set_on_change([this] { update_undo_ui(); });
    m_doc.journal.set_selection_source([this] {
        return m_ctx.selected.empty() ? std::vector<std::string>{} : std::vector<std::string>{m_ctx.selected};
    });
    m_ctx.say       = [this](const std::string& s, bool warn) { say(s, warn); };
    m_ctx.show_view = [this](const std::string& id) {
        activate_action("win.view", Glib::Variant<Glib::ustring>::create(id));
    };
    m_ctx.open_tabs = [this](const std::vector<std::string>& names, const std::string& active) {
        m_editor.open_tabs(names, active);
        activate_action("win.view", Glib::Variant<Glib::ustring>::create("editor"));
    };

    // ── follow the desktop's light / dark ───────────────────────────────────
    if (auto settings = Gtk::Settings::get_default())
        settings->property_gtk_application_prefer_dark_theme().signal_changed().connect(
            [this] { apply_motif(); });

    // ── Ctrl+Z from ANYWHERE but a text box ─────────────────────────────────
    // NOT an accelerator: a set_accels_for_action chord fires before the
    // focused widget, so it would take Ctrl+Z away from every Entry and
    // TextView (jot s016c met that with Ctrl+Delete). Instead the WINDOW
    // hears it in the BUBBLE phase -- only keys nothing else took -- and lets
    // a text box keep its own even when it had nothing to undo. Binding undo
    // to one widget's focus is the other trap: after a menu closes, or the
    // focused row is rebuilt away, the keyboard is nowhere and Ctrl+Z is dead
    // (jot s046a: "undo a delete" did nothing). The registry carries these as
    // doc-only rows for that reason.
    auto keys = Gtk::EventControllerKey::create();
    keys->signal_key_pressed().connect(
        [this](guint keyval, guint, Gdk::ModifierType state) {
            const auto mods = state & (Gdk::ModifierType::CONTROL_MASK |
                                       Gdk::ModifierType::SHIFT_MASK |
                                       Gdk::ModifierType::ALT_MASK);
            const auto CTRL  = Gdk::ModifierType::CONTROL_MASK;
            const auto SHIFT = Gdk::ModifierType::SHIFT_MASK;
            const char* act = nullptr;
            if ((keyval == GDK_KEY_z || keyval == GDK_KEY_Z) && mods == CTRL)               act = "win.undo";
            else if ((keyval == GDK_KEY_z || keyval == GDK_KEY_Z) && mods == (CTRL | SHIFT)) act = "win.redo";
            else if ((keyval == GDK_KEY_y || keyval == GDK_KEY_Y) && mods == CTRL)          act = "win.redo";
            if (!act || focus_is_text()) return false;
            if (auto lg = log::get(log::Area::Shell)) lg->info("window key -> {}", act);
            activate_action(act);   // the FULL name: "undo" alone would do nothing
            return true;
        }, false);
    add_controller(keys);

    // A spin button's inner text takes Ctrl+Z for itself even with its undo
    // off, so the bubble-phase handler above never hears it. A spin is a
    // number, not a text box: in the CAPTURE phase, and only when the focus
    // is inside a spin, the window takes Ctrl+Z / redo first.
    auto spin_keys = Gtk::EventControllerKey::create();
    spin_keys->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    spin_keys->signal_key_pressed().connect(
        [this](guint keyval, guint, Gdk::ModifierType state) {
            bool in_spin = false;
            for (const Gtk::Widget* w = get_focus(); w; w = w->get_parent())
                if (dynamic_cast<const Gtk::SpinButton*>(w)) { in_spin = true; break; }
            if (!in_spin) return false;
            const auto mods = state & (Gdk::ModifierType::CONTROL_MASK | Gdk::ModifierType::SHIFT_MASK |
                                       Gdk::ModifierType::ALT_MASK);
            const auto CTRL = Gdk::ModifierType::CONTROL_MASK, SHIFT = Gdk::ModifierType::SHIFT_MASK;
            const bool z = keyval == GDK_KEY_z || keyval == GDK_KEY_Z;
            const char* act = nullptr;
            if (z && mods == CTRL)                                           act = "win.undo";
            else if (z && mods == (CTRL | SHIFT))                            act = "win.redo";
            else if ((keyval == GDK_KEY_y || keyval == GDK_KEY_Y) && mods == CTRL) act = "win.redo";
            if (!act) return false;
            if (auto lg = log::get(log::Area::Shell)) lg->info("spin key -> {}", act);
            activate_action(act);
            return true;
        }, false);
    add_controller(spin_keys);
    update_undo_ui();
}

}  // namespace granjon
