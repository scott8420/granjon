#pragma once
// views::ReportList (M7) -- the Report view (Ctrl+5): every check's findings,
// grouped by check, each with why it matters and Fix / Skip; Fix all per
// check and for the whole report. All of it goes through the verbs (one undo
// step per click). Clicking a finding selects the glyph, picks the spot and
// brings the editor forward.
//
// Speed: while hidden, a step only marks the report stale; shown, it re-checks
// just the glyphs the step touched (plus keyed glyphs when a glyph they refer
// to changed), and a font-record change (metrics, keys) re-checks everything.
#include "model/Checks.hpp"
#include "views/ViewContext.hpp"
#include "widgets/Widgets.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace granjon::views {

class ReportList : public widgets::Box {
public:
    explicit ReportList(ViewContext& ctx);

    void refresh();                                   // everything may have changed (open / new)
    void update(const std::set<std::string>& ids);    // a step touched these records
    // M12 (the Primer's links): unfold one check's section ("overshoot", ...).
    // Returns its live finding count (-1 for an unknown tag).
    int open_check(const std::string& tag);

private:
    void recheck_all();
    void recheck(const std::string& glyph);
    void rebuild();                                   // the rows, from m_found
    void go_to(const model::Finding& x);
    std::vector<model::Finding> visible(model::CheckId id) const;   // skips filtered unless shown

    ViewContext& m_ctx;
    widgets::Box            m_bar;
    widgets::Label          m_summary;
    widgets::ToggleButton   m_show_skipped;
    widgets::Button         m_fix_all;
    widgets::Button         m_recheck;
    widgets::ScrolledWindow m_scroll;
    widgets::Box            m_list;

    std::map<std::string, std::vector<model::Finding>> m_found;   // glyph -> findings (skips included)
    std::set<model::CheckId> m_toggled;   // sections flipped from their default (open up to 50 findings)
    bool m_stale = true;
    bool m_shown = false;
};

}  // namespace granjon::views
