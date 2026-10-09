#include "Style.hpp"
#include "Log.hpp"

#include <glibmm/miscutils.h>
#include <gtkmm/cssprovider.h>
#include <gtkmm/stylecontext.h>

#include <filesystem>
#include <fstream>

namespace granjon::style {

namespace fs = std::filesystem;

namespace {
const char* kUserStub =
    "/* Granjon user stylesheet\n"
    " *\n"
    " * Loaded after Granjon's built-in CSS; rules here win. Properties you\n"
    " * don't set fall through to the built-in styles.\n"
    " *\n"
    " * The look is built from design tokens. Redefine one to retint the app:\n"
    " *\n"
    " *     window               { --accent: #c061cb; }    (dark motif)\n"
    " *     window.granjon-light { --accent: #9141ac; }    (light motif)\n"
    " *\n"
    " * To find a widget's CSS classes: Ctrl+Shift+D opens the GTK Inspector;\n"
    " * use its crosshair to pick a widget.\n"
    " *\n"
    " * Changes take effect on restart. Empty this file to restore defaults.\n"
    " */\n";

void report(const char* what, const Glib::RefPtr<Gtk::CssProvider>& css) {
    css->signal_parsing_error().connect(
        [what](const Glib::RefPtr<const Gtk::CssSection>& section, const Glib::Error& e) {
            if (auto lg = log::get(log::Area::App))
                lg->warn("css {}: {} ({})", what, e.what(),
                         section ? std::string(section->to_string()) : std::string("?"));
        });
}
}  // namespace

void install(const Glib::RefPtr<Gdk::Display>& display) {
    if (!display) return;
    auto lg = log::get(log::Area::App);

    auto css = Gtk::CssProvider::create();
    report("built-in", css);
    css->load_from_resource("/io/github/granjon/Granjon/granjon.css");
    Gtk::StyleContext::add_provider_for_display(display, css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    const fs::path dir  = fs::path(Glib::get_user_config_dir()) / "granjon";
    const fs::path path = dir / "styles.css";
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (!ec && !fs::exists(path)) {
        std::ofstream(path) << kUserStub;
        if (lg) lg->info("css: seeded {}", path.string());
    }
    if (!fs::exists(path)) return;
    auto user = Gtk::CssProvider::create();
    report("user", user);
    try {
        user->load_from_path(path.string());
        Gtk::StyleContext::add_provider_for_display(display, user, GTK_STYLE_PROVIDER_PRIORITY_USER);
        if (lg) lg->info("css: user stylesheet {}", path.string());
    } catch (const Glib::Error& e) {
        if (lg) lg->warn("css: user stylesheet {} failed: {}", path.string(), e.what());
    }
}

}  // namespace granjon::style
