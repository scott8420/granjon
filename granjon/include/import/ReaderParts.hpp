#pragma once
// reader::ReaderParts -- the importer's inside: a bounds-checked byte cursor
// and one reader per table family. Internal to import/ and the selftest.
#include "import/FontReader.hpp"
#include "model/Outline.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace granjon::reader {

using Bytes = std::vector<std::uint8_t>;

// Any read past the end throws BadFont: a truncated or hostile file fails the
// import with a reason instead of reading garbage.
struct BadFont : std::runtime_error { using std::runtime_error::runtime_error; };

struct Span {
    const std::uint8_t* p = nullptr;
    std::size_t n = 0;
    Span() = default;
    Span(const Bytes& b) : p(b.data()), n(b.size()) {}
    Span(const std::uint8_t* d, std::size_t len) : p(d), n(len) {}
    Span sub(std::size_t at, std::size_t len) const {
        if (at > n || len > n - at) throw BadFont("a table points outside itself");
        return {p + at, len};
    }
    Span from(std::size_t at) const { if (at > n) throw BadFont("an offset past the end"); return {p + at, n - at}; }
    std::uint8_t  u8(std::size_t at) const { if (at >= n) throw BadFont("read past the end"); return p[at]; }
    unsigned      u16(std::size_t at) const { return static_cast<unsigned>(u8(at) << 8 | u8(at + 1)); }
    int           i16(std::size_t at) const { return static_cast<std::int16_t>(u16(at)); }
    std::uint32_t u32(std::size_t at) const { return static_cast<std::uint32_t>(u16(at)) << 16 | u16(at + 2); }
    std::int32_t  i32(std::size_t at) const { return static_cast<std::int32_t>(u32(at)); }
    bool empty() const { return n == 0; }
};

// sfnt directory: tag -> table bytes (empty Span when missing).
struct Sfnt {
    std::uint32_t version = 0;
    std::map<std::string, Span> tables;
    Span get(const std::string& tag) const { auto it = tables.find(tag); return it == tables.end() ? Span{} : it->second; }
};
Sfnt read_sfnt(Span font);

// One glyph's outline as read (model direction already: outers counter-clockwise).
struct ReadGlyph {
    std::vector<model::Contour>   contours;
    std::vector<model::Component> components;   // base names filled by the caller from gids
    std::vector<int>              component_gids;
};

// CFF: every glyph's contours (in GID order) and the charset names ("" when
// the font has none -- CID fonts give "cidNNNNN").
struct CffResult {
    std::vector<std::vector<model::Contour>> glyphs;
    std::vector<std::string> names;
    std::vector<std::string> notes;
};
CffResult read_cff(Span cff);

// TrueType glyf / loca.
std::vector<ReadGlyph> read_glyf(Span glyf, Span loca, int loca_format, int glyph_count, std::vector<std::string>& notes);

// Mark nodes whose in- and out-handles (or a handle and the straight segment
// on the other side) line up as Smooth.
void infer_smooth(model::Contour& c);

// GPOS 'kern' (or the old 'kern' table) into a model Kerning, given names by gid.
model::Kerning read_kerning(Span gpos, Span kern, const std::vector<std::string>& names, std::vector<std::string>& notes);

}  // namespace granjon::reader
