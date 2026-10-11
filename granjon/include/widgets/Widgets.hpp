#pragma once
#include "widgets/Named.hpp"

#include <gtkmm/box.h>
#include <gtkmm/drawingarea.h>
#include <gtkmm/dropdown.h>
#include <gtkmm/flowbox.h>
#include <gtkmm/paned.h>
#include <gtkmm/searchentry.h>
#include <gtkmm/togglebutton.h>
#include <gtkmm/button.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/entry.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/listbox.h>
#include <gtkmm/menubutton.h>
#include <gtkmm/scale.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/stack.h>
#include <gtkmm/stacksidebar.h>
#include <gtkmm/textview.h>

// The wrapper aliases. Widen on demand, not for symmetry (CANON). Each line is
// the whole cost of making a GTK widget name-mandatory in Granjon's app code.
namespace granjon::widgets {

using Box            = Named<Gtk::Box>;
using DrawingArea    = Named<Gtk::DrawingArea>;
using DropDown       = Named<Gtk::DropDown>;
using FlowBox        = Named<Gtk::FlowBox>;
using Paned          = Named<Gtk::Paned>;
using SearchEntry    = Named<Gtk::SearchEntry>;
using ToggleButton   = Named<Gtk::ToggleButton>;
using Button         = Named<Gtk::Button>;
using CheckButton    = Named<Gtk::CheckButton>;
using Entry          = Named<Gtk::Entry>;
using Image          = Named<Gtk::Image>;
using Label          = Named<Gtk::Label>;
using ListBox        = Named<Gtk::ListBox>;
using MenuButton     = Named<Gtk::MenuButton>;
using Scale          = Named<Gtk::Scale>;
using ScrolledWindow = Named<Gtk::ScrolledWindow>;
using SpinButton     = Named<Gtk::SpinButton>;
using Stack          = Named<Gtk::Stack>;
using StackSidebar   = Named<Gtk::StackSidebar>;
using TextView       = Named<Gtk::TextView>;

}  // namespace granjon::widgets
