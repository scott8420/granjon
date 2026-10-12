#pragma once
// model::Checks (M7) -- the report's findings, GTK-free so the selftest walks
// every check and every fix.
//
// A check looks at one glyph and says what it found; each finding names its
// spot (a contour / node, when there is one), why it matters (the check's
// note), and -- when it can be fixed mechanically -- what the fix sets. The
// fixes run through command::fix_findings (one undo step for any number);
// skipping writes the finding's key into the font's CheckSkips (undoable,
// saved).
//
//   Direction    a contour running the wrong way for CFF (M5d's rule)
//   Open         an open contour (fonts want them closed)
//   Overlap      (s10) two paths sharing area, the same path twice, or a path
//                crossing itself -> Remove overlaps (or a boolean)
//   Extremes     (M15b) a curve bulging past its nodes with no node at its
//                horizontal / vertical extreme -> add it
//   Zones        a flat node a unit or three off a metric line -> snap to it
//   Overshoot    a round top / bottom with no overshoot, or one off the font's
//                overshoot -> move the extreme to the overshoot line
//   Origin       coordinates off the unit grid (-> round); a drawn glyph with
//                no width that isn't a mark; an outline outside its own box
//   Keys         a metrics key the glyph no longer matches (M6) -> apply it
#include "model/Font.hpp"

#include <string>
#include <vector>

namespace granjon::model {

enum class CheckId { Direction, Open, Zones, Overshoot, Origin, Keys, Overlap, Extremes };

struct CheckInfo {
    CheckId     id;
    const char* tag;     // stable, in skip keys ("direction", "zones", ...)
    const char* name;    // "Contour direction"
    const char* why;     // one or two sentences: why it matters
};
const std::vector<CheckInfo>& checks();
const CheckInfo& check_info(CheckId id);

struct Finding {
    CheckId     check = CheckId::Direction;
    std::string glyph;
    std::string message;          // "Contour 2 runs counter-clockwise inside an outer"
    int         contour = -1;     // the spot, -1 when the finding is the whole glyph
    int         index = -1;       // node index in that contour, -1 = the contour itself
    bool        fixable = false;
    double      target = 0;       // Zones / Overshoot: the y the node goes to
    std::string detail;           // the stable part of the key (contour, line, ...)
    std::string key() const;      // "zones|n|0.3|500" -- what a skip remembers
};

// The tolerance for "a unit or three off": 3 units at 1000 UPM, scaled.
double zone_tolerance(const Font& f);

// Every check on one glyph (skipped findings included -- the caller filters
// with is_skipped, so the report can still show them on request).
std::vector<Finding> check_glyph(const Font& f, const std::string& glyph);
bool is_skipped(const Font& f, const Finding& x);

// The fix for these findings of ONE glyph, on a copy: node moves first, then
// rounding, closing, reversing (indices stay valid until then). Keys are not
// geometry and are left to apply_key. nullopt when nothing changes.
std::optional<Glyph> fix_glyph(const Font& f, const std::string& glyph, const std::vector<Finding>& xs);

}  // namespace granjon::model
