#include "Shell.hpp"
#include "AboutWindow.hpp"      // complete type for ~unique_ptr<AboutWindow> in ~Shell()
#include "ShortcutsDialog.hpp"  // ditto -- same singleton shape, same dtor requirement
#include "PreferencesWindow.hpp"  // ditto
#include "Log.hpp"

#include <cstdlib>
#include <filesystem>

// Shell.cpp -- GLUE. The ctor and the ordered build_ui() pass live here; every
// other method body lives in a category file (see the index in Shell.hpp).

namespace granjon {

Shell::Shell()
    : m_logo_button("shell.logo_button"),
      m_nav_toggle("shell.nav_toggle"),
      m_undo_button("shell.undo_button"),
      m_redo_button("shell.redo_button"),
      m_switcher("shell.switcher", Gtk::Orientation::HORIZONTAL),
      m_insp_toggle("shell.inspector_toggle"),
      m_panels("shell.panel_toggles", Gtk::Orientation::HORIZONTAL),
      m_menu_button("shell.menu_button"),
      m_frame("shell.frame", Gtk::Orientation::VERTICAL),
      m_outer("shell.outer", Gtk::Orientation::HORIZONTAL),
      m_inner("shell.inner", Gtk::Orientation::HORIZONTAL),
      m_stack("shell.stack"),
      m_status("shell.status") {
    set_name("shell.window");
    // GRANJON_SAMPLE=1 opens the selftest's fixture font (H O n o Odotbelow,
    // a kerning group) instead of an empty one -- something to look at until
    // M2 can open a real font. Loaded, not a step.
    if (const char* s = std::getenv("GRANJON_SAMPLE"); s && *s == '1') command::make_fixture(m_doc.font);
    // The motif class goes on BEFORE the header is built: GTK caches the CSD
    // wrapper's and the headerbar's style at construction, and a class added
    // later does not re-walk them (Curvz S117 m2 -- the light motif booted dark).
    apply_motif();
}

Shell::~Shell() = default;

// The ordered pass: construct the regions, bind actions + keys, load persisted
// state, paint. Each step is one call into a category file.
void Shell::build_ui() {
    m_ctx.save_settings = [this] { save_settings(); };
    build_header();    // zone
    build_body();      // zone
    m_recents_file = recents_file();   // before bind_actions loads the list
    bind_actions();    // bindings
    rebuild_recents_menu();
    repaint();
    update_undo_ui();
    // Reopen the last font (a preference). The sample env wins: it is asking
    // for the fixture on purpose.
    const char* sample = std::getenv("GRANJON_SAMPLE");
    const bool want_sample = sample && *sample == '1';
    std::error_code ec;
    if (!want_sample && m_settings.reopen_last && !m_settings.last_font.empty() &&
        std::filesystem::is_directory(m_settings.last_font, ec))
        open_path(m_settings.last_font);
    if (auto lg = log::get(log::Area::Shell)) lg->info("ready");
}

}  // namespace granjon
