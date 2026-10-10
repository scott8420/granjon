#pragma once
#include "model/Outline.hpp"
// model::FontRecords -- the font-level records that are not glyphs. Each is one
// undo record (one id in the store), replaced whole on change.
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace granjon::model {

struct FontInfo {
    std::string family = "Untitled";
    std::string style  = "Regular";
    int         version_major = 1;
    int         version_minor = 0;
    std::string copyright;
    std::string license;
    std::string vendor;            // OS/2 achVendID, 4 chars
    int         units_per_em = 1000;
    std::vector<Guide> guides;     // font-wide guidelines (fontinfo.plist "guidelines")
    bool operator==(const FontInfo&) const = default;
};

// Vertical metrics, in font units, y-up from the baseline.
struct Metrics {
    int ascender   = 750;
    int descender  = -250;
    int x_height   = 500;
    int cap_height = 700;
    int overshoot  = 12;           // the zone rounds reach past a line
    bool operator==(const Metrics&) const = default;

    enum class Field { Ascender, Descender, XHeight, CapHeight, Overshoot };
    int  get(Field f) const;
    void set(Field f, int v);
};
const char* field_name(Metrics::Field f);

// Class kerning. A side is a glyph name or a group name ("public.kern1.O").
struct Kerning {
    std::map<std::string, std::vector<std::string>>   groups;   // name -> members
    std::map<std::pair<std::string, std::string>, int> pairs;   // (left, right) -> value
    bool operator==(const Kerning&) const = default;
};

// Metrics keys: "n: L=R=60", "m=n", "b.L=l.L". Raw text per glyph for now;
// M6 parses and propagates them.
struct MetricsKeys {
    struct Key { std::string left, right, width; bool operator==(const Key&) const = default; };
    std::map<std::string, Key> keys;   // glyph name -> rule
    bool operator==(const MetricsKeys&) const = default;
};

// OpenType feature source, verbatim. v2 compiles it.
struct Features {
    std::string text;
    bool operator==(const Features&) const = default;
};

}  // namespace granjon::model
