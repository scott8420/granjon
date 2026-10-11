#pragma once
// NewFontDialog (M12) -- the chooser on New: Blank, Sans, Serif, Display,
// Mono as cards, each showing the control glyphs that preset DRAWS (built
// from the preset with model::starter_glyphs, painted with GlyphPaint -- what
// you see is what you get), the preset's numbers, a family name, the
// coverage set, and whether to add the set's glyphs.
//
// Hide-on-close singleton (CANON "Lifetime shape is design"), like the
// Preferences window: Shell builds it once and re-presents it. Modal: New
// replaces the document, so nothing else should happen while it's up.
#include "model/Presets.hpp"
#include "widgets/Widgets.hpp"

#include <gtkmm/checkbutton.h>
#include <gtkmm/window.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace granjon {

class NewFontDialog : public Gtk::Window {
public:
    using Create = std::function<void(const model::NewFontOptions&)>;
    NewFontDialog();
    ~NewFontDialog() override;

    // Present over `parent`, `preset` picked ("" = Blank), the set check as
    // given; `create` runs on Create (the window hides first).
    void show(Gtk::Window& parent, const std::string& preset, bool create_set, Create create);

private:
    struct Card {
        std::string id;                       // "" = Blank
        std::unique_ptr<model::Font> sample;  // the starter glyphs, for the preview
        widgets::ToggleButton* button = nullptr;
    };
    void pick(const std::string& id);
    void sync_details();
    void create();
    std::string chosen_charset() const;

    std::vector<Card>      m_cards;
    std::string            m_pick;
    Create                 m_create;

    widgets::Box           m_root;
    widgets::Box           m_cards_box;
    widgets::Label         m_blurb;
    widgets::Label         m_numbers;
    widgets::Entry         m_family;
    widgets::DropDown      m_charset;
    Gtk::CheckButton       m_create_set;
    widgets::Button        m_cancel;
    widgets::Button        m_ok;
    std::vector<std::string> m_charset_ids;   // the dropdown's rows: "@none", then the bundled sets
};

}  // namespace granjon
