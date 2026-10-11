#pragma once
// views::ImagePlace -- M13: getting a scan from disk behind a glyph.
//
// The file is read, turned into PNG if it isn't one (UFO 3 keeps PNG only in
// images/; GdkTexture reads JPEG, TIFF, WebP and the rest where GTK can),
// added to the font's image pool, and placed on the glyph: one undo step.
// A fresh image spans descender to ascender from the origin; a replacement
// keeps the old one's opacity. The status line says what happened.
#include "views/ViewContext.hpp"

#include <giomm/file.h>
#include <gtkmm/window.h>

#include <string>

namespace granjon::views {

bool place_image_file(ViewContext& ctx, const std::string& glyph, const Glib::RefPtr<Gio::File>& file);
// The file chooser (images only), then place_image_file.
void choose_image_file(ViewContext& ctx, Gtk::Window* parent, const std::string& glyph);

}  // namespace granjon::views
