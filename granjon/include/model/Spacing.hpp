#pragma once
// model::Spacing (M6) -- what the spacing strip and the metrics keys read,
// with no GTK in it so the selftest walks all of it.
//
//   parse_strip      "nnHOHnn" or "/n/o/eacute" -> glyph names (the /name syntax)
//   kern_lookup      a glyph pair -> the kerning entry in effect (UFO order:
//                    glyph/glyph, glyph/group, group/glyph, group/group)
//   kern_target      which entry a kern edit writes (the one in effect, else
//                    the group pair; `exception` forces glyph/glyph)
//   layout_strip     advances + kerning, in font units
//   metrics keys     "L=R=60", "=n", "L=n R=|o", "W=500", "=n+10" -- per side
//                    expressions; evaluate, compare, apply
#include "model/Font.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace granjon::model {

// ── the strip text ─────────────────────────────────────────────────────────
// A character maps through the cmap (Font::glyph_for); "/name" names a glyph
// directly and ends at a space or the next "/" (the space is eaten). "//" is a
// literal slash. Unknown characters / names are skipped and listed in `missing`.
std::vector<std::string> parse_strip(const Font& f, const std::string& text,
                                     std::vector<std::string>* missing = nullptr);

// ── kerning ────────────────────────────────────────────────────────────────
// The kerning group holding `glyph` on that side ("" if none): side 1 is the
// LEFT glyph of a pair (public.kern1.*), side 2 the RIGHT (public.kern2.*).
std::string kern_group(const Kerning& k, const std::string& glyph, int side);

using KernKey = std::pair<std::string, std::string>;
struct KernHit {
    KernKey key;          // the entry in effect (empty strings if none)
    int     value = 0;    // 0 when there is no entry
    bool    found = false;
};
KernHit kern_lookup(const Kerning& k, const std::string& left, const std::string& right);
// The entry an edit of (left, right) writes. `exception` -> (left, right)
// itself; otherwise the entry in effect if there is one, else the groups'
// pair (a side without a group kerns as itself).
KernKey kern_target(const Kerning& k, const std::string& left, const std::string& right, bool exception);
// "group / group", "exception", "glyph / group", ... for the inspector.
std::string kern_kind(const KernKey& key);

// ── layout ─────────────────────────────────────────────────────────────────
struct Placed {
    std::string name;
    double x = 0;         // the glyph's origin, font units from the strip's start
    double advance = 0;
    int    kern = 0;      // applied BEFORE this glyph (with the previous one)
};
std::vector<Placed> layout_strip(const Font& f, const std::vector<std::string>& names);

// ── metrics keys ───────────────────────────────────────────────────────────
// One side's expression, as stored in MetricsKeys::Key (left / right / width):
//   "60"        a number
//   "n", "=n"   n's same side (width: n's width)
//   "|n"        n's opposite side
//   "n.L" "n.R" n's named side
//   "=n+10" "=n-4"   any of the above plus / minus a number
struct KeyExpr {
    bool        valid = false;
    double      number = 0;   // the constant, or the offset when ref is set
    std::string ref;          // glyph referenced ("" = a plain number)
    char        side = 0;     // 'L' / 'R' explicit, 0 = same side, '|' = opposite
};
KeyExpr parse_key_expr(const std::string& s);

// A whole rule line into a Key. Accepts:
//   "L=R=60"  "L=n R=|o"  "W=500"  "=n" (both sides from n)  "n" (same)
//   "m: L=R=60" / "m=n" / "b.L=l.L" when `target` is given and named in the
// line (the target part is checked against it and dropped). False with a
// reason on a parse error. An empty line is a valid empty key (no rule).
bool parse_key_rule(const std::string& line, const std::string& target, MetricsKeys::Key* out,
                    std::string* why = nullptr);
// The Key back as one rule line ("L=R=60", "=n", "L=n R=|o W=500").
std::string format_key_rule(const MetricsKeys::Key& k);

// The value a side's expression asks for, from the font as it is now.
// side: 'L', 'R', 'W'. nullopt when the expression is bad or the reference is
// missing / has no outline.
std::optional<double> eval_key_side(const Font& f, const std::string& expr, char side);

struct KeyCheck {
    bool has_key = false;
    bool left_off = false, right_off = false, width_off = false;   // differs by more than half a unit
    std::optional<double> left, right, width;                     // what the key asks for
    bool off() const { return left_off || right_off || width_off; }
};
KeyCheck check_key(const Font& f, const std::string& glyph);

// The glyph with its key applied, from the font as it is now (L, then R, then
// W -- a width key wins over a right key). nullopt when nothing changes.
std::optional<Glyph> apply_key(const Font& f, const std::string& glyph);
// The order to apply keys font-wide so a chain (m=n, n=60) resolves:
// referenced glyphs first. Cycles are broken and listed in `cyclic`.
std::vector<std::string> key_order(const Font& f, std::vector<std::string>* cyclic = nullptr);

// Sidebearing setters on a glyph copy (used by apply_key and the verbs).
void set_lsb(const Font& f, Glyph& g, double v);
void set_rsb(const Font& f, Glyph& g, double v);

}  // namespace granjon::model
