#pragma once
// geom::NodeEdit (M5f) -- adding and removing nodes, line <-> curve. GTK-free.
//
//   insert_node       a node on a segment at t, the shape unchanged (a curve is
//                     split by de Casteljau; the new node is Smooth on a curve,
//                     Corner on a line)
//   delete_nodes      nodes out of a glyph, KEEPING THE SHAPE where a curve ran
//                     through them: the merged segment keeps both end tangents
//                     and its handle lengths are fitted to the old outline
//                     (least squares, Schneider's method). Two lines merge to a
//                     line. A contour left with fewer than two nodes goes.
//   retract_handle    a handle back onto its node
//   segment_to_line / segment_to_curve
//                     a curve's handles retracted / a line given handles at a
//                     third and two thirds
#include "geom/Vec2.hpp"
#include "model/Glyph.hpp"

#include <set>
#include <utility>

namespace granjon::geom {

int  insert_node(model::Contour& c, int segment, double t);
void delete_nodes(model::Glyph& g, const std::set<std::pair<int, int>>& nodes);   // (contour, node)
void retract_handle(model::Contour& c, int node, bool out);
void segment_to_line(model::Contour& c, int segment);
void segment_to_curve(model::Contour& c, int segment);
bool is_line(const model::Contour& c, int segment);

// ── the pen (M5g) ──────────────────────────────────────────────────────────
// A node at p at the end of an open contour; contour -1 starts a new open
// contour. Returns the contour's index.
int  pen_add(model::Glyph& g, int contour, Vec2 p);
// A pen drag of d from node `node`: out-handle at node + d and the in-handle
// mirrored (Symmetric); `cusp` (Alt) moves the out-handle alone (Corner).
// A drag of zero leaves a corner.
void pen_drag_handles(model::Contour& c, int node, Vec2 d, bool cusp);
// Closing on the first node with a drag of d: its IN-handle goes to node - d
// (the path arrives along the drag); the out-handle it already has stays.
void pen_close_handles(model::Contour& c, Vec2 d);
// An open contour, end to start (handles swapped) -- the pen continues from a
// first node by appending to the reversed contour.
void reverse_open(model::Contour& c);
// Shift: d turned to the nearest multiple of 45 degrees (length kept along it).
Vec2 constrain45(Vec2 d);

}  // namespace granjon::geom
