#pragma once
// PrimerWindow (M12) -- the Primer: short illustrated notes on drawing type
// (core::primer_notes, data in resources/primer/). A list of notes on the
// left; the note on the right -- title, figure, text, and "Try it" buttons
// that act in the main window (the Shell follows them: the Measure tool on o,
// the Checks report's Overshoot section, the Spacing strip with "nnonoo" ...).
//
// Its own window rather than an inspector card or a view: it stays open
// BESIDE the work -- read, click "Measure o", measure, read on. Non-modal,
// hide-on-close singleton like the shortcuts window. F1 or ☰ Primer.
#include "core/Primer.hpp"
#include "widgets/Widgets.hpp"

#include <gtkmm/picture.h>
#include <gtkmm/window.h>

#include <functional>
#include <string>

namespace granjon {

class PrimerWindow : public Gtk::Window {
public:
    using Follow = std::function<void(const std::string& target)>;
    explicit PrimerWindow(Follow follow);
    ~PrimerWindow() override;

    // Present beside `parent`; `note` = an id to open ("" = where it was).
    void show(Gtk::Window& parent, const std::string& note = {});

private:
    void open(int index);

    Follow                  m_follow;
    int                     m_index = -1;
    widgets::Box            m_root;
    widgets::ListBox        m_list;
    widgets::ScrolledWindow m_scroll;
    widgets::Box            m_page;
    widgets::Label          m_title;
    Gtk::Picture            m_figure;
    widgets::Label          m_no_figure;
    widgets::Box            m_body;
    widgets::Box            m_links;
    widgets::Button         m_prev;
    widgets::Button         m_next;
};

}  // namespace granjon
