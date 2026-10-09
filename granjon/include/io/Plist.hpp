#pragma once
// io::Plist -- Apple XML property lists, the container UFO uses for
// fontinfo, groups, kerning, lib, contents. GTK-free.
//
// Writing is deterministic -- dict keys sorted, tabs, "\n" -- so saving the
// same font twice gives byte-identical files (git-friendly, and the M2
// selftest checks it). Reading uses libxml2.
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

typedef struct _xmlNode xmlNode;

namespace granjon::io {

struct PValue {
    enum class Kind { String, Integer, Real, Bool, Array, Dict };
    Kind kind = Kind::String;
    std::string                                  s;
    std::int64_t                                 i = 0;
    double                                       r = 0;
    bool                                         b = false;
    std::vector<PValue>                          arr;
    std::vector<std::pair<std::string, PValue>>  dict;

    static PValue str(std::string v)  { PValue p; p.kind = Kind::String;  p.s = std::move(v); return p; }
    static PValue integer(std::int64_t v) { PValue p; p.kind = Kind::Integer; p.i = v; return p; }
    static PValue real(double v);     // an integral value is written as <integer>
    static PValue boolean(bool v)     { PValue p; p.kind = Kind::Bool;    p.b = v; return p; }
    static PValue array()             { PValue p; p.kind = Kind::Array;   return p; }
    static PValue dictionary()        { PValue p; p.kind = Kind::Dict;    return p; }

    // dict helpers
    void set(const std::string& key, PValue v);
    const PValue* get(const std::string& key) const;
    std::optional<std::string>  get_string(const std::string& key) const;
    std::optional<double>       get_number(const std::string& key) const;   // integer or real
    double number() const { return kind == Kind::Integer ? static_cast<double>(i) : r; }
    bool   is_number() const { return kind == Kind::Integer || kind == Kind::Real; }
};

// Serialise a whole plist document (header, DOCTYPE, <plist>).
std::string write_plist(const PValue& root);
// Serialise one value at a depth, as it appears inside another document
// (GLIF's <lib> embeds a plist dict).
void write_plist_value(std::string& out, const PValue& v, int depth);

// Parse a plist document from memory / a file. nullopt + `err` on failure.
std::optional<PValue> read_plist(const std::string& xml, std::string* err = nullptr);
std::optional<PValue> read_plist_file(const std::string& path, std::string* err = nullptr);
// Parse one value element (<dict>, <array>, ...) from a libxml2 node.
std::optional<PValue> read_plist_value(xmlNode* node, std::string* err);

// XML text escaping for element content and attribute values.
std::string xml_escape(const std::string& s);
// Shortest round-tripping text for a coordinate: integers without a point.
std::string format_number(double v);

}  // namespace granjon::io
