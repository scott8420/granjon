#pragma once
// model::Composite -- building accented glyphs from parts (M5).
//
//   plan_composite   é -> base "e" + mark (acutecomb, or the spacing "acute"
//                    when the font has no combining one), placed so the
//                    mark's "_top" anchor meets the base's "top". Capitals take
//                    a ".case" (or ".cap") mark when there is one; i / j take
//                    their dotless forms under an above-mark. Without anchors:
//                    centred on the base's drawing, an above-mark lifted by
//                    cap height - x-height on a capital that had no case form.
//   default_anchors  "top" / "bottom" on letters, "_top" / "_bottom" on marks,
//                    from the drawing and the metrics -- a starting point the
//                    designer then drags.
//
// GTK-free; the verbs (command::add_glyphs, add_default_anchors) do the writing.
#include "model/Coverage.hpp"
#include "model/Font.hpp"

#include <optional>
#include <string>
#include <vector>

namespace granjon::model {

enum class MarkPlace { None, Above, Below };
// Where a combining mark (U+0300..) or its spacing twin (´ ¨ ¸ ...) sits.
MarkPlace mark_place(char32_t cp);
// The spacing accent for a combining mark (U+0301 -> U+00B4), 0 if none.
char32_t spacing_twin(char32_t combining);

struct CompositePlan {
    std::vector<Component>   components;   // base first, then marks
    double                   advance = 0;  // the base's
    std::vector<std::string> missing;      // what the font lacks ("U+0301 (a mark for it)")
    bool ok() const { return missing.empty() && !components.empty(); }
};
// Empty plan (no components, no missing) when cp does not decompose
// canonically into a base + marks.
CompositePlan plan_composite(const Font& f, char32_t cp);

// Fill in components + advance for each new glyph that can be built from
// parts already in the font. Returns how many.
std::size_t plan_composites(const Font& f, std::vector<NewGlyph>& glyphs);

// The anchors `g` should get that it does not have yet (by name). Letters
// (L*): top, bottom. Marks: _top or _bottom (and top / bottom for stacking).
// Nothing for other glyphs or an empty drawing.
std::vector<Anchor> default_anchors(const Font& f, const Glyph& g);

}  // namespace granjon::model
