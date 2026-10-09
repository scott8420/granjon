#pragma once
// unicode::UcdTable -- the bundled, trimmed Unicode Character Database
// (resources/ucd/ucd.txt, Unicode 17.0, compiled in): names, general
// category, decomposition, simple case mappings. Parsed once, on first use;
// names are views into the embedded data (no copies). GTK- and GLib-free, so
// the selftest and granjon-cli see exactly what the app sees.
//
// Regenerate with scripts/ucd_trim.py when Unicode moves on.
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace granjon::unicode {

std::string_view ucd_version();     // "17.0.0"

bool        is_assigned(char32_t cp);
// "LATIN SMALL LETTER E WITH ACUTE"; algorithmic names for CJK / Hangul /
// Tangut ranges; a control's Unicode 1 name ("LINE FEED (LF)"); "" when
// unassigned or private use.
std::string name_of(char32_t cp);
// Two-letter general category ("Lu", "Mn", ...); "Cn" when unassigned.
std::string_view category(char32_t cp);
// One level of the UCD decomposition: é -> e U+0301. `compat` (when given)
// says whether it is a compatibility mapping (<compat>, <font>, <super>, ...)
// rather than canonical. Empty when the character does not decompose.
std::vector<char32_t> decomposition(char32_t cp, bool* compat = nullptr);
// Simple case mappings; 0 when there is none.
char32_t upper_of(char32_t cp);
char32_t lower_of(char32_t cp);
// The other case of a letter (A -> a, a -> A, Ж -> ж); 0 when it has none.
char32_t other_case(char32_t cp);

// Characters whose name holds every word of `query` as the start of one of
// its words, case-insensitive: "cedilla", "cyrillic zhe", "e acute". Best
// first (an exact name, then the fewest extra words, then code point order).
// Algorithmic ranges (CJK, Hangul) are not searched.
std::vector<char32_t> find_by_name(std::string_view query, std::size_t limit = 50);

// The UTF-8 for `cp` ("é").
std::string utf8(char32_t cp);

}  // namespace granjon::unicode
