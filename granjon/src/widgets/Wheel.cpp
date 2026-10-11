#include "widgets/Named.hpp"

#include <gtkmm/adjustment.h>
#include <gtkmm/eventcontrollerscroll.h>
#include <gtkmm/scrolledwindow.h>

#include <algorithm>
#include <cmath>

namespace granjon::widgets {

namespace {

// Scroll an adjustment by one wheel / touchpad delta, as GtkScrolledWindow does
// (a wheel notch moves page_size^(2/3); a touchpad delta is already pixels).
void nudge(const Glib::RefPtr<Gtk::Adjustment>& adj, double delta, bool wheel) {
    if (!adj || delta == 0) return;
    const double page = adj->get_page_size();
    const double step = wheel ? std::pow(std::max(page, 1.0), 2.0 / 3.0) : 1.0;
    const double hi = std::max(adj->get_lower(), adj->get_upper() - page);
    adj->set_value(std::clamp(adj->get_value() + delta * step, adj->get_lower(), hi));
}

}  // namespace

void guard_wheel(Gtk::Widget& w) {
    auto sc = Gtk::EventControllerScroll::create();
    sc->set_flags(Gtk::EventControllerScroll::Flags::BOTH_AXES);
    // Capture: we see the wheel before the field's own handler does.
    sc->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    sc->signal_scroll().connect([&w, c = sc.get()](double dx, double dy) {
        // A field being edited (the cursor is in it) keeps the wheel.
        if ((w.get_state_flags() & Gtk::StateFlags::FOCUS_WITHIN) == Gtk::StateFlags::FOCUS_WITHIN) return false;
        // Otherwise the wheel belongs to the panel the field sits in.
        const bool wheel = c->get_unit() == Gdk::ScrollUnit::WHEEL;
        for (Gtk::Widget* p = w.get_parent(); p; p = p->get_parent())
            if (auto* sw = dynamic_cast<Gtk::ScrolledWindow*>(p)) {
                nudge(sw->get_vadjustment(), dy, wheel);
                nudge(sw->get_hadjustment(), dx, wheel);
                break;
            }
        return true;   // never the field's value
    }, false);
    w.add_controller(sc);
}

}  // namespace granjon::widgets
