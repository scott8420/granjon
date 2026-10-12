#pragma once
// s09: arrange the picked paths (Bring to front / forward, Send backward /
// to back) from the inspector's OUTLINE row or the editor's Ctrl+] keys --
// one verb call, then the pick follows its paths to their new places.
#include "command/Verbs.hpp"
#include "views/ViewContext.hpp"

#include <set>

namespace granjon::views {

// The contours with a picked node or handle (the booleans' rule).
std::set<int> picked_contours(const ViewContext& ctx);
// Runs command::arrange_contours on them; says what happened on the status line.
void arrange_picked(ViewContext& ctx, command::Arrange how);
// s09: Duplicate (Ctrl+D) -- copies of the picked paths in place, on top, picked.
void duplicate_picked(ViewContext& ctx);
// s09: Clone (Alt+D) -- the same, offset right and down by 2 % of the em
// (20 units at 1000), so the copy shows beside its original.
void clone_picked(ViewContext& ctx);
double clone_offset(const ViewContext& ctx);

}  // namespace granjon::views
