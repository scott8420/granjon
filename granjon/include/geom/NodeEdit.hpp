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

}  // namespace granjon::geom
