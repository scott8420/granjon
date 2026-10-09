#pragma once
// views::OverviewGrid -- the Overview view: every glyph as a card (drawing,
// name, codepoint), in glyph order. Click selects (the shared selection);
// double-click / Enter opens the glyph in the editor. M3 adds grouping by
// block, coverage gaps and the cell-size slider.
#include "views/ViewContext.hpp"
#include "widgets/Widgets.hpp"

namespace granjon::views {

class OverviewGrid : public widgets::ScrolledWindow {
public:
    explicit OverviewGrid(ViewContext& ctx);
    void refresh();

private:
    void sync_selection();

    ViewContext&     m_ctx;
    widgets::FlowBox m_flow;
    bool             m_syncing = false;
};

}  // namespace granjon::views
