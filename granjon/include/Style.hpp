#pragma once
// Style -- install the house stylesheet for a display: the compiled-in
// resources/granjon.css (tokens + rules), then the user's
// ~/.config/granjon/styles.css at USER priority so it wins. A first run seeds
// that file with a commented stub saying how to use it (Curvz s145).
#include <gdkmm/display.h>

namespace granjon::style {

void install(const Glib::RefPtr<Gdk::Display>& display);

}  // namespace granjon::style
