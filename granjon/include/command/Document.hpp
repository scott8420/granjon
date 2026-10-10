#pragma once
// command::Document -- one open font: the model, its undo journal, and where it
// lives on disk. The journal belongs to the document, not the window: two
// windows on one font share one history; closing a font drops its history.
#include "command/Verbs.hpp"
#include "io/Ufo.hpp"
#include "import/FontReader.hpp"
#include "model/Font.hpp"

#include <string>

namespace granjon::command {

class Document {
public:
    Document() {
        font.set_door([this] { return journal.open(); });
        model::make_new_font(font);
    }
    Document(const Document&)            = delete;
    Document& operator=(const Document&) = delete;

    // Declaration order matters: the store before the journal that holds it.
    model::Font        font;
    model::FontJournal journal{font};
    std::string        path;      // "" = never saved

    std::string title() const { return font.info().family + " " + font.info().style; }

    // ── the file ────────────────────────────────────────────────────────────
    // Modified = the journal is not at the state that was last saved / loaded
    // (Journal::stamp). Undoing back to the saved state is unmodified again.
    bool modified() const { return journal.stamp() != m_saved; }

    // A fresh untitled font. History goes: a new document has none.
    void new_font() {
        model::make_new_font(font);
        path.clear();
        journal.clear();
        m_saved = journal.stamp();
    }
    // Replace the font with the one at `p`. On failure nothing changes.
    bool open(const std::string& p, std::string* err) {
        if (!io::load_ufo(p, font, err)) return false;
        path = p;
        journal.clear();
        m_saved = journal.stamp();
        return true;
    }
    // M11: an OTF / TTF read into a NEW, unsaved document (no path: Save asks
    // where; modified, so closing asks first). On failure nothing changes.
    bool import(const std::string& p, reader::ImportReport* rep, std::string* err) {
        if (!reader::import_font_file(p, font, rep, err)) return false;
        path.clear();
        journal.clear();
        m_saved = ~journal.stamp();   // never the current stamp: unsaved
        return true;
    }
    // Write to `p` (or the current path). History stays: saving is not a step.
    bool save(const std::string& p, std::string* err) {
        const std::string target = p.empty() ? path : p;
        if (target.empty()) { if (err) *err = "no file name"; return false; }
        if (!io::save_ufo(font, target, err)) return false;
        path = target;
        m_saved = journal.stamp();
        return true;
    }

private:
    std::uint64_t m_saved = 0;
};

}  // namespace granjon::command
