#pragma once
// model::Fea (M16c) -- the OpenType feature file (features.fea, Adobe's
// syntax), the GSUB part: parsed here, compiled by otf::build_gsub. GTK-free.
//
// Understood:
//   # comments                     languagesystem DFLT dflt;   (DFLT + latn are always written)
//   @class = [a b c.sc  a.sc - z.sc  @other];
//   feature liga { ... } liga;     lookup NAME { ... } NAME;   lookup NAME;   (a reference)
//   sub a by a.sc;                 sub [a b] by [a.sc b.sc];   sub @lc by @sc;      single
//   sub f_f_i by f f i;                                                             multiple
//   sub a from [a.alt1 a.alt2];                                                     alternate
//   sub f f i by f_f_i;            sub [f f.alt] i by f_i;                          ligature
//   sub fraction @numr' by @dnom;  sub a x' lookup L b;   ignore sub a x', x' b;   contextual (s10)
// Read and skipped with a warning: pos rules (kerning comes from KERN, marks
// from ANCHORS), lookupflag, script / language other than DFLT / latn, table
// blocks, featureNames. Errors (with the line): reverse chaining (rsub),
// include, unknown glyphs or lookups, unbalanced braces, a malformed rule.
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace granjon::model {

class Font;

struct FeaRule {
    enum class Kind { Single, Multiple, Alternate, Ligature, Chain };
    Kind kind = Kind::Single;
    std::vector<std::vector<std::string>> in;    // one list per input position (a class expanded)
    std::vector<std::string>              out;   // Single: one per in[0] entry (or one for all);
                                                 // Multiple: the sequence; Alternate: the choices;
                                                 // Ligature: the one ligature
    int line = 0;
    // Chain (s10, contextual): `in` holds the MARKED glyphs (sub a x' b by y;
    // -- x), `back` / `ahead` the context before / after (back in reading
    // order). What happens at the marked run: `lookups` -- (index into `in`,
    // a lookup in FeaDoc::lookups) -- either named in the rule (x' lookup L)
    // or one made for its `by` / `from`. None: an ignore rule.
    std::vector<std::vector<std::string>> back, ahead;
    std::vector<std::pair<int, int>>      lookups;
    bool                                  ignore = false;
};
struct FeaLookup {
    std::string          name;                   // "" = made for a run of rules inside a feature
    FeaRule::Kind        kind = FeaRule::Kind::Single;
    std::vector<FeaRule> rules;
    int                  line = 0;
};
struct FeaFeature {
    std::string      tag;
    std::vector<int> lookups;                    // into FeaDoc::lookups
    int              line = 0;
};
struct FeaMessage { int line = 0; std::string text; };
struct FeaDoc {
    std::vector<FeaLookup>  lookups;
    std::vector<FeaFeature> features;
    std::vector<FeaMessage> errors;              // any error: nothing is compiled
    std::vector<FeaMessage> warnings;            // skipped statements
    int rules = 0;
    bool ok() const { return errors.empty(); }
};

// `exists` says whether a glyph name is in the font (nullptr: every name is).
FeaDoc parse_fea(const std::string& text, const std::function<bool(const std::string&)>& exists = nullptr);

// Feature code generated from the glyph names (Glyphs' convention), between
// the markers below: f_i / f_f_i (parts present) -> liga; x.sc -> smcp (and
// c2sc from the capital); .onum .lnum .tnum .pnum .sups .subs .sinf .numr
// .dnom .case .swsh -> their features; .ss01 .. .ss20 -> ss01 ..; .salt /
// .alt / .alt1 .. -> salt (several -> an alternate set); (s10) figures with
// .numr and .dnom plus a fraction (else the slash) -> frac (slash -> fraction, figures ->
// numerators, a numerator after the fraction or a denominator -> denominator).
// Parts and .notdef never take part. Empty when nothing qualifies.
std::string generate_fea(const Font& f);
extern const char* const kFeaAutoBegin;   // "# --- automatic (Granjon: Generate replaces this block) ---"
extern const char* const kFeaAutoEnd;     // "# --- end automatic ---"
// The text with its automatic block replaced by `generated` (added at the
// top when it has none; removed when `generated` is empty).
std::string replace_auto_block(const std::string& text, const std::string& generated);

}  // namespace granjon::model
