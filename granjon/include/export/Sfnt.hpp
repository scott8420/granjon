#pragma once
// otf::Sfnt -- the byte level of an OpenType file, shared by every sfnt the
// app writes (OTF now; TTF and WOFF2's inner font at M10). GTK-free.
//
//   Writer          big-endian appends + patching at an offset
//   checksum        the sfnt table checksum (sum of big-endian uint32s)
//   assemble_sfnt   tables by tag -> a whole font: directory sorted by tag,
//                   tables in the spec's recommended order, 4-byte padding,
//                   head.checkSumAdjustment set last
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace granjon::otf {

using Bytes = std::vector<std::uint8_t>;

struct Writer {
    Bytes b;
    void u8(unsigned v)        { b.push_back(static_cast<std::uint8_t>(v & 0xFF)); }
    void u16(unsigned v)       { u8(v >> 8); u8(v); }
    void i16(int v)            { u16(static_cast<unsigned>(v) & 0xFFFF); }
    void u32(std::uint32_t v)  { u16(v >> 16); u16(v & 0xFFFF); }
    void i32(std::int32_t v)   { u32(static_cast<std::uint32_t>(v)); }
    void i64(std::int64_t v)   { u32(static_cast<std::uint32_t>(static_cast<std::uint64_t>(v) >> 32)); u32(static_cast<std::uint32_t>(v)); }
    void tag(const char* t)    { for (int i = 0; i < 4; ++i) u8(static_cast<unsigned char>(t[i])); }
    void bytes(const Bytes& x) { b.insert(b.end(), x.begin(), x.end()); }
    void zeros(std::size_t n)  { b.insert(b.end(), n, 0); }
    std::size_t size() const   { return b.size(); }
    void put_u16(std::size_t at, unsigned v) {
        b[at] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
        b[at + 1] = static_cast<std::uint8_t>(v & 0xFF);
    }
    void put_u32(std::size_t at, std::uint32_t v) { put_u16(at, v >> 16); put_u16(at + 2, v & 0xFFFF); }
};

std::uint32_t checksum(const Bytes& data);

// sfnt_version: 0x4F54544F ('OTTO') for CFF outlines, 0x00010000 for glyf.
// `tables` keys are 4-char tags ("OS/2", "CFF "). A "head" table must be
// present (its checkSumAdjustment is written here).
Bytes assemble_sfnt(std::uint32_t sfnt_version, const std::map<std::string, Bytes>& tables);

// Reading back (the selftest, the Preview): the table's bytes, or empty.
Bytes table(const Bytes& font, const std::string& tag);

}  // namespace granjon::otf
