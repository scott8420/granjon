#pragma once
// views::FeatureView (M16c) -- the Features view (Ctrl+6): the font's feature
// file (features.fea), edited as text. It checks as you type (after a pause):
// the bar says how many features and rules compile, or the first error; the
// error lines are tinted and listed below -- click one to go to its line.
// Generate writes substitutions from the glyph names (f_i -> liga, .sc ->
// smcp / c2sc, .ss01, .onum, .alt ...) into an automatic block at the top,
// leaving hand-written code alone. Typing is ONE undo step per pause-run
// (command::set_features merges); undo / redo bring the text back here.
// Export compiles it to GSUB; the Preview shapes with it.
#include "views/ViewContext.hpp"
#include "widgets/Widgets.hpp"

#include <gtkmm/textbuffer.h>
#include <sigc++/connection.h>

#include <set>
#include <string>

namespace granjon::views {

class FeatureView : public widgets::Box {
public:
    explicit FeatureView(ViewContext& ctx);
    void refresh();                                   // a new / opened font
    void update(const std::set<std::string>& ids);    // a step touched these records

private:
    void load();          // the model's text into the buffer (no step)
    void changed();       // the buffer changed: schedule a check + commit
    void commit();        // after the pause: check, write through set_features
    void check();         // parse against the font; the bar, the tint, the list
    void generate();

    ViewContext& m_ctx;
    widgets::Box            m_bar;
    widgets::Label          m_summary;
    widgets::Button         m_generate;
    widgets::ScrolledWindow m_scroll;
    widgets::TextView       m_text;
    widgets::Box            m_errors;
    Glib::RefPtr<Gtk::TextBuffer>      m_buf;
    Glib::RefPtr<Gtk::TextBuffer::Tag> m_err_tag;
    Glib::RefPtr<Gtk::TextBuffer::Tag> m_comment_tag;
    Glib::RefPtr<Gtk::TextBuffer::Tag> m_kw_tag;
    sigc::connection m_pending;
    bool m_loading = false;
};

}  // namespace granjon::views
