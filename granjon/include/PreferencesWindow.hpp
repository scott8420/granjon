#pragma once
// PreferencesWindow -- the app's settings (core::Settings), grouped in cards
// the way the inspector is: General, Editor, Overview.
//
// Every control writes through at once (no Apply): the setting changes, the
// file is saved, ViewContext::settings_changed tells the views. The views'
// own controls (the editor's Fill / Context, the overview's slider and
// Coverage menu) write the same fields, so this window re-syncs whenever
// settings_changed fires. Not undoable -- preferences are app state.
//
// Lifetime: the About / Shortcuts shape -- built once on first open,
// hide-on-close, re-presented.
#include "views/ViewContext.hpp"
#include "widgets/Widgets.hpp"

#include <gtkmm/colordialogbutton.h>
#include <gtkmm/dropdown.h>
#include <gtkmm/switch.h>
#include <gtkmm/window.h>

namespace granjon {

class PreferencesWindow : public Gtk::Window {
public:
    PreferencesWindow(Gtk::Window& parent, views::ViewContext& ctx, const std::string& file);
    ~PreferencesWindow() override;

private:
    void sync();      // settings -> controls
    void commit();    // controls -> settings, save, tell the views

    views::ViewContext&        m_ctx;
    widgets::ScrolledWindow    m_scroll;
    widgets::Box               m_root;
    widgets::Named<Gtk::Switch> m_reopen;
    widgets::Named<Gtk::Switch> m_autohint;     // s10
    widgets::Named<Gtk::DropDown> m_paper;      // s10: proof paper
    widgets::Named<Gtk::Switch> m_fill;
    widgets::Named<Gtk::Switch> m_context;
    widgets::Entry             m_upper;
    widgets::Entry             m_lower;
    widgets::DropDown          m_modifier;
    widgets::Named<Gtk::ColorDialogButton> m_image_tint;   // M13: colour + transparency of new background images
    widgets::SpinButton        m_cell;
    widgets::DropDown          m_charset;
    widgets::Label             m_file;
    widgets::Button            m_reset;
    bool                       m_syncing = false;
};

}  // namespace granjon
