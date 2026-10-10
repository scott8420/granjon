// otf::Woff2 -- M10b: the web font. A WOFF2 (W3C WOFF 2.0) wrapper around the
// TrueType compile: the sfnt's tables, sorted by tag, concatenated unpadded
// and compressed as one Brotli stream (font mode, quality 11). No glyf / loca
// transform (version 3, the null transform -- valid, decoders and OTS take it;
// the transform would shave ~10-25 % more and is a follow-on). head.flags bit
// 11 is set (a lossless modifying transform happened), checksums redone.
#include "export/OtfParts.hpp"
#include "export/Woff2.hpp"

#include <brotli/encode.h>

#include <chrono>
#include <cmath>

namespace granjon::otf {

namespace {

const char* const kKnownTags[63] = {
    "cmap", "head", "hhea", "hmtx", "maxp", "name", "OS/2", "post", "cvt ", "fpgm", "glyf", "loca", "prep",
    "CFF ", "VORG", "EBDT", "EBLC", "gasp", "hdmx", "kern", "LTSH", "PCLT", "VDMX", "vhea", "vmtx", "BASE",
    "GDEF", "GPOS", "GSUB", "EBSC", "JSTF", "MATH", "CBDT", "CBLC", "COLR", "CPAL", "SVG ", "sbix", "acnt",
    "avar", "bdat", "bloc", "bsln", "cvar", "fdsc", "feat", "fmtx", "fvar", "gvar", "hsty", "just", "lcar",
    "mort", "morx", "opbd", "prop", "trak", "Zapf", "Silf", "Glat", "Gloc", "Feat", "Sill"};

void base128(Writer& w, std::uint32_t v) {
    int size = 1;
    for (std::uint32_t t = v >> 7; t; t >>= 7) ++size;
    for (int i = size - 1; i >= 0; --i) w.u8(((v >> (7 * i)) & 0x7F) | (i ? 0x80u : 0u));
}

}  // namespace

Bytes woff2_from_sfnt(const Bytes& sfnt, std::string* why) {
    if (sfnt.size() < 12) { if (why) *why = "empty font"; return {}; }
    const auto rd16 = [&](std::size_t i) { return static_cast<unsigned>(sfnt[i] << 8 | sfnt[i + 1]); };
    const std::uint32_t flavor = static_cast<std::uint32_t>(rd16(0)) << 16 | rd16(2);
    const unsigned n = rd16(4);
    std::map<std::string, Bytes> tables;
    for (unsigned i = 0; i < n; ++i) {
        const std::string tag(sfnt.begin() + 12 + 16 * i, sfnt.begin() + 16 + 16 * i);
        tables[tag] = table(sfnt, tag);
    }
    if (!tables.count("head") || tables["head"].size() < 54) { if (why) *why = "no head table"; return {}; }
    // head: bit 11 on, checkSumAdjustment cleared; assemble_sfnt redoes the sums.
    auto& head = tables["head"];
    head[16] |= 0x08;
    head[8] = head[9] = head[10] = head[11] = 0;
    const Bytes rebuilt = assemble_sfnt(flavor, tables);
    for (auto& [tag, data] : tables) data = table(rebuilt, tag);   // head with its final adjustment

    Bytes stream;
    for (const auto& [tag, data] : tables) stream.insert(stream.end(), data.begin(), data.end());   // sorted by tag
    std::size_t out_size = BrotliEncoderMaxCompressedSize(stream.size());
    Bytes packed(out_size ? out_size : stream.size() + 1024);
    out_size = packed.size();
    if (!BrotliEncoderCompress(BROTLI_MAX_QUALITY, BROTLI_DEFAULT_WINDOW, BROTLI_MODE_FONT, stream.size(), stream.data(),
                               &out_size, packed.data())) {
        if (why) *why = "Brotli compression failed";
        return {};
    }
    packed.resize(out_size);

    Writer dir;
    for (const auto& [tag, data] : tables) {
        int known = -1;
        for (int k = 0; k < 63; ++k) if (tag == kKnownTags[k]) { known = k; break; }
        const unsigned transform = (tag == "glyf" || tag == "loca") ? 3u : 0u;   // the null transform
        dir.u8((transform << 6) | static_cast<unsigned>(known < 0 ? 63 : known));
        if (known < 0) dir.tag(tag.c_str());
        base128(dir, static_cast<std::uint32_t>(data.size()));
    }
    const double rev = static_cast<std::int32_t>(static_cast<std::uint32_t>(head[4] << 24 | head[5] << 16 | head[6] << 8 | head[7])) / 65536.0;
    const unsigned major = static_cast<unsigned>(std::floor(rev)), minor = static_cast<unsigned>(std::lround((rev - std::floor(rev)) * 1000));

    Writer w;
    const std::size_t total = (48 + dir.size() + packed.size() + 3) & ~std::size_t{3};
    w.tag("wOF2");
    w.u32(flavor);
    w.u32(static_cast<std::uint32_t>(total));
    w.u16(static_cast<unsigned>(tables.size()));
    w.u16(0);
    w.u32(static_cast<std::uint32_t>(rebuilt.size()));   // totalSfntSize: the reconstructed font
    w.u32(static_cast<std::uint32_t>(packed.size()));
    w.u16(major);
    w.u16(minor);
    w.zeros(20);   // no metadata, no private data
    w.bytes(dir.b);
    w.bytes(packed);
    while (w.size() % 4) w.u8(0);
    return w.b;
}

Bytes compile_woff2(const model::Font& f, const OtfOptions& opt, OtfReport* rep) {
    const auto t0 = std::chrono::steady_clock::now();
    OtfReport r;
    const Bytes ttf = compile_ttf(f, opt, &r);
    std::string why;
    Bytes out = woff2_from_sfnt(ttf, &why);
    if (!why.empty()) r.notes.push_back("WOFF2: " + why);
    r.notes.push_back("WOFF2 of the TrueType build: " + std::to_string(ttf.size() / 1024) + " KB -> " +
                      std::to_string(out.size() / 1024) + " KB.");
    r.bytes = out.size();
    r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (rep) *rep = std::move(r);
    return out;
}

bool export_woff2(const model::Font& f, const std::filesystem::path& to, const OtfOptions& opt, OtfReport* rep,
                  std::string* why) {
    const Bytes data = compile_woff2(f, opt, rep);
    if (data.empty()) { if (why) *why = "The WOFF2 build failed."; return false; }
    return write_font_file(data, to, why);
}

}  // namespace granjon::otf
