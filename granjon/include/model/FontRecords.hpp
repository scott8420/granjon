#pragma once
#include "model/Outline.hpp"
// model::FontRecords -- the font-level records that are not glyphs. Each is one
// undo record (one id in the store), replaced whole on change.
#include <map>
#include <set>
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
    // M16a: the rest of the font info a release wants (UFO fontinfo keys in brackets)
    double      italic_angle = 0;  // degrees, counter-clockwise from upright: italics are negative (-12) [italicAngle]
    std::string designer;          // [openTypeNameDesigner]       name ID 9
    std::string designer_url;      // [openTypeNameDesignerURL]    name ID 12
    std::string manufacturer;      // [openTypeNameManufacturer]   name ID 8
    std::string manufacturer_url;  // [openTypeNameManufacturerURL] name ID 11
    std::string trademark;         // [trademark]                  name ID 7
    std::string description;       // [openTypeNameDescription]    name ID 10
    int         weight_class = 0;  // OS/2 usWeightClass 1..1000; 0 = from the style name [openTypeOS2WeightClass]
    int         width_class  = 5;  // OS/2 usWidthClass 1..9, 5 = normal [openTypeOS2WidthClass]
    int         units_per_em = 1000;
    std::vector<Guide> guides;     // font-wide guidelines (fontinfo.plist "guidelines")
    // M12: the preset the font was started from ("" = blank / imported) and
    // the stems it set out with -- lib "granjon.preset". A record of intent;
    // nothing enforces them.
    struct Origin {
        std::string preset;
        int stem_v = 0, stem_h = 0, cap_stem_v = 0, cap_stem_h = 0;
        bool operator==(const Origin&) const = default;
    };
    Origin origin;
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

// Check findings Scott chose to skip (M7): "check|glyph|detail" keys. Font
// data (saved in the lib, undoable), not view state.
struct CheckSkips {
    std::set<std::string> keys;
    bool operator==(const CheckSkips&) const = default;
};

// OpenType feature source, verbatim. v2 compiles it.
struct Features {
    std::string text;
    bool operator==(const Features&) const = default;
};

}  // namespace granjon::model
