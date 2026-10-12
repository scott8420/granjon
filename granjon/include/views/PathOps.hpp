#pragma once
// s09: the path operations on the editor's pick -- arrange, duplicate / clone,
// align / distribute, the booleans, overlaps and directions. ONE place, called
// by the inspector's buttons, the editor's keys and the canvas context menu,
// so the three never drift: each is one verb call, then the pick follows the
// result and the status line says what happened.
#include "command/Verbs.hpp"
#include "views/ViewContext.hpp"
#include "model/Bounds.hpp"

#include <optional>
#include <string>

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
// Align / distribute: whole paths picked -> command::align_contours (rigid
// shapes; one path aligns to the glyph's box); loose nodes / anchors ->
// command::align_points (as before).
void align_picked(ViewContext& ctx, command::Align how);
// True when every picked node belongs to a fully picked path (the Select
// tool's pick) -- align then moves paths, not points.
bool whole_paths_picked(const ViewContext& ctx);
void boolean_picked(ViewContext& ctx, geom::BoolOp op);   // the result comes picked
void remove_overlaps_here(ViewContext& ctx);              // the shown glyph
void correct_directions_here(ViewContext& ctx);
void reverse_picked(ViewContext& ctx);
// M15a: transform. The pick's box: exact bounds of whole picked paths plus the
// positions of other picked points; an empty pick = the whole glyph.
std::optional<model::Rect> pick_box(const ViewContext& ctx);
std::string pick_subject(const ViewContext& ctx);   // "2 paths", "3 points", "the whole glyph"
// The point of the box a 3 x 3 origin index names (0 = top-left .. 8 = bottom-right).
geom::Vec2 box_point(const model::Rect& b, int origin);
bool transform_picked(ViewContext& ctx, const geom::Affine& m, const std::string& label, const std::string& merge = {});
// r1: the transform origin (see ViewContext::tf_origin). transform_origin()
// is the point in font units for the current pick's box; origin_chosen()
// says whether the user set one (else the handles use their defaults).
geom::Vec2 transform_origin(const ViewContext& ctx, const model::Rect& box);
bool       origin_chosen(const ViewContext& ctx);
void       set_origin_point(ViewContext& ctx, int k);         // a box point, 0..8
void       set_custom_origin(ViewContext& ctx, geom::Vec2 p); // a free point
void       clear_origin(ViewContext& ctx);                    // back to the defaults
// r1: one transform STEP about the origin (the card's step buttons). Presses
// in a row on the same pick within 1.5 s add up from the shape before the run
// (rounded once) and fold into one undo step.
void transform_step(ViewContext& ctx, double sx, double sy, double rot, double skx, double sky, const std::string& label);
void flip_picked(ViewContext& ctx, bool horizontal);   // about the box centre
void rotate_picked(ViewContext& ctx, double deg);      // about the box centre
double clone_offset(const ViewContext& ctx);

}  // namespace granjon::views
