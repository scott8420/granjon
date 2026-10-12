// otf::Tables -- the small sfnt tables: head hhea maxp OS/2 name cmap post hmtx.
//
// Vertical metrics (decided s05, the Google Fonts scheme): typo ascender /
// descender = the font's ascender / descender, typo line gap tops the line up
// to 1.2 em, hhea copies the typo values, USE_TYPO_METRICS is set; win
// ascent / descent cover the drawn bbox so nothing clips on Windows.
#include "export/OtfData.hpp"
#include "export/OtfParts.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <set>

namespace granjon::otf {

namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    s.erase(std::remove_if(s.begin(), s.end(), [](char c) { return c == ' ' || c == '-' || c == '_'; }), s.end());
    return s;
}
bool has(const std::string& s, const char* w) { return s.find(w) != std::string::npos; }

int rnd(double v) { return static_cast<int>(std::lround(v)); }

struct Vert { int typo_asc, typo_desc, typo_gap, win_asc, win_desc; };
Vert vertical(const Prepared& p) {
    const int upm = p.info.units_per_em;
    Vert v{};
    v.typo_asc  = p.metrics.ascender;
    v.typo_desc = p.metrics.descender;
    v.typo_gap  = std::max(0, rnd(upm * 1.2) - (v.typo_asc - v.typo_desc));
    v.win_asc   = std::max(0, std::max(p.ymax, v.typo_asc));
    v.win_desc  = std::max(0, std::max(-p.ymin, -v.typo_desc));
    return v;
}

void utf16be(Writer& w, const std::string& s) {
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        char32_t cp = 0;
        int len = 1;
        if (c < 0x80) { cp = c; }
        else if ((c >> 5) == 6 && i + 1 < s.size()) { cp = (c & 0x1F) << 6 | (s[i + 1] & 0x3F); len = 2; }
        else if ((c >> 4) == 14 && i + 2 < s.size()) {
            cp = (c & 0x0F) << 12 | (s[i + 1] & 0x3F) << 6 | (s[i + 2] & 0x3F); len = 3;
        } else if ((c >> 3) == 30 && i + 3 < s.size()) {
            cp = (c & 0x07) << 18 | (s[i + 1] & 0x3F) << 12 | (s[i + 2] & 0x3F) << 6 | (s[i + 3] & 0x3F); len = 4;
        } else { cp = 0xFFFD; }
        i += static_cast<std::size_t>(len);
        if (cp >= 0x10000) {
            cp -= 0x10000;
            w.u16(0xD800 + (cp >> 10));
            w.u16(0xDC00 + (cp & 0x3FF));
        } else {
            w.u16(cp);
        }
    }
}

}  // namespace

// ── style ──────────────────────────────────────────────────────────────────
int weight_class(const std::string& style) {
    const std::string s = lower(style);
    if (has(s, "thin") || has(s, "hairline")) return 100;
    if (has(s, "extralight") || has(s, "ultralight")) return 200;
    if (has(s, "light")) return 300;
    if (has(s, "medium")) return 500;
    if (has(s, "semibold") || has(s, "demibold")) return 600;
    if (has(s, "extrabold") || has(s, "ultrabold")) return 800;
    if (has(s, "black") || has(s, "heavy")) return 900;
    if (has(s, "bold")) return 700;
    return 400;
}
bool is_bold(const std::string& style) { return weight_class(style) == 700; }
bool is_italic(const std::string& style) {
    const std::string s = lower(style);
    return has(s, "italic") || has(s, "oblique");
}

// ── head ───────────────────────────────────────────────────────────────────
Bytes build_head(const Prepared& p, int loca_format) {
    Writer w;
    w.u32(0x00010000);
    const double rev = p.info.version_major + p.info.version_minor / 1000.0;
    w.i32(static_cast<std::int32_t>(std::lround(rev * 65536.0)));
    w.u32(0);              // checkSumAdjustment: assemble_sfnt fills it
    w.u32(0x5F0F3CF5);
    w.u16(0x0003);         // baseline at y=0, lsb at x=0 (true for CFF: lsb = xMin)
    w.u16(static_cast<unsigned>(p.info.units_per_em));
    const std::int64_t mac = p.time + 2082844800LL;   // 1904 epoch
    w.i64(mac);
    w.i64(mac);
    w.i16(p.xmin); w.i16(p.ymin); w.i16(p.xmax); w.i16(p.ymax);
    unsigned mac_style = 0;
    if (is_bold(p.style)) mac_style |= 1;
    if (is_italic(p.style)) mac_style |= 2;
    w.u16(mac_style);
    w.u16(6);              // lowestRecPPEM
    w.i16(2);              // fontDirectionHint (deprecated, fixed)
    w.i16(loca_format);    // indexToLocFormat (0 for CFF: no loca)
    w.i16(0);
    return w.b;
}

// ── hhea / hmtx ────────────────────────────────────────────────────────────
namespace {
int hmetric_count(const Prepared& p) {
    std::size_t n = p.glyphs.size();
    while (n > 1 && p.glyphs[n - 1].advance == p.glyphs[n - 2].advance) --n;
    return static_cast<int>(n);
}
}  // namespace

Bytes build_hhea(const Prepared& p) {
    const Vert v = vertical(p);
    int adv_max = 0, min_lsb = 0, min_rsb = 0, x_extent = 0;
    bool first = true;
    for (const auto& g : p.glyphs) {
        adv_max = std::max(adv_max, g.advance);
        if (g.empty) continue;
        const int lsb = g.xmin, rsb = g.advance - g.xmax, ext = g.xmax;
        if (first) { min_lsb = lsb; min_rsb = rsb; x_extent = ext; first = false; }
        min_lsb = std::min(min_lsb, lsb);
        min_rsb = std::min(min_rsb, rsb);
        x_extent = std::max(x_extent, ext);
    }
    Writer w;
    w.u32(0x00010000);
    w.i16(v.typo_asc);
    w.i16(v.typo_desc);
    w.i16(v.typo_gap);
    w.u16(static_cast<unsigned>(adv_max));
    w.i16(min_lsb);
    w.i16(min_rsb);
    w.i16(x_extent);
    // M16a: the caret leans with the italic angle (rise / run; upright 1 / 0)
    if (p.info.italic_angle != 0) {
        const int rise = p.info.units_per_em;
        w.i16(rise);
        w.i16(static_cast<int>(std::lround(rise * std::tan(-p.info.italic_angle * 3.14159265358979 / 180.0))));
    } else {
        w.i16(1);          // caretSlopeRise
        w.i16(0);          // caretSlopeRun
    }
    w.i16(0);              // caretOffset
    w.zeros(8);            // reserved
    w.i16(0);              // metricDataFormat
    w.u16(static_cast<unsigned>(hmetric_count(p)));
    return w.b;
}

Bytes build_hmtx(const Prepared& p) {
    Writer w;
    const int n = hmetric_count(p);
    for (std::size_t i = 0; i < p.glyphs.size(); ++i) {
        if (static_cast<int>(i) < n) w.u16(static_cast<unsigned>(p.glyphs[i].advance));
        w.i16(p.glyphs[i].xmin);
    }
    return w.b;
}

// ── maxp (0.5, CFF) ────────────────────────────────────────────────────────
Bytes build_maxp(const Prepared& p) {
    Writer w;
    w.u32(0x00005000);
    w.u16(static_cast<unsigned>(p.glyphs.size()));
    return w.b;
}

// ── OS/2 (version 4) ───────────────────────────────────────────────────────
Bytes build_os2(const Prepared& p, int max_context) {
    const int upm = p.info.units_per_em;
    const Vert v = vertical(p);

    long sum = 0;
    int count = 0;
    for (const auto& g : p.glyphs)
        if (g.advance > 0) { sum += g.advance; ++count; }
    const int avg = count ? rnd(static_cast<double>(sum) / count) : 0;

    // ulUnicodeRange from what is actually encoded (never hand-set).
    std::uint32_t ur[4] = {0, 0, 0, 0};
    std::set<char32_t> cps;
    for (const auto& [cp, _] : p.cmap) cps.insert(cp);
    for (char32_t cp : cps) {
        for (int i = 0; i < kUnicodeRangeCount; ++i) {
            const auto& b = kUnicodeRanges[i];
            if (cp >= b.first && cp <= b.last) { ur[b.bit / 32] |= 1u << (b.bit % 32); break; }
        }
        if (cp >= 0x10000) ur[57 / 32] |= 1u << (57 % 32);
    }
    // ulCodePageRange: fontTools' port of FontForge's test characters.
    std::uint32_t cpr[2] = {0, 0};
    auto in = [&](char32_t c) { return cps.count(c) > 0; };
    auto bit = [&](int b) { cpr[b / 32] |= 1u << (b % 32); };
    bool ascii = true;
    for (char32_t c = 0x20; c < 0x7E; ++c)
        if (!in(c)) { ascii = false; break; }
    const bool lineart = in(0x2524);
    if (ascii && in(0x00DE)) bit(0);                  // Latin 1
    if (ascii && in(0x013D)) { bit(1); if (lineart) bit(58); }   // Latin 2
    if (in(0x0411)) { bit(2); if (in(0x0405) && lineart) bit(57); if (in(0x255C) && lineart) bit(49); }
    if (in(0x0386)) { bit(3); if (lineart && in(0x00BD)) bit(48); if (lineart && in(0x221A)) bit(60); }
    if (ascii && in(0x0130)) { bit(4); if (lineart) bit(56); }
    if (in(0x05D0)) { bit(5); if (lineart && in(0x221A)) bit(53); }
    if (in(0x0631)) { bit(6); if (in(0x221A)) bit(51); if (lineart) bit(61); }
    if (ascii && in(0x0157)) { bit(7); if (lineart) bit(59); }
    if (ascii && in(0x20AB)) bit(8);
    if (in(0x0E45)) bit(16);
    if (in(0x30A8)) bit(17);
    if (in(0x3105)) bit(18);
    if (in(0x3131)) bit(19);
    if (in(0x592E)) bit(20);
    if (in(0xACF4)) bit(21);
    if (ascii && in(0x2665)) bit(30);
    if (ascii && lineart && in(0x00FE)) bit(54);
    if (ascii && in(0x255A)) { bit(62); bit(63); }
    if (ascii && lineart && in(0x221A)) {
        if (in(0x00C5)) bit(50);
        if (in(0x00E9)) bit(52);
        if (in(0x00F5)) bit(55);
    }
    if (ascii && in(0x2030) && in(0x2211)) bit(29);
    if (!cpr[0] && !cpr[1]) bit(0);   // Word wants one (fontmake #468)

    unsigned first_char = 0xFFFF, last_char = 0;
    for (char32_t cp : cps) {
        first_char = std::min<unsigned>(first_char, std::min<char32_t>(cp, 0xFFFF));
        last_char  = std::max<unsigned>(last_char, std::min<char32_t>(cp, 0xFFFF));
    }
    if (cps.empty()) first_char = 0;

    unsigned fs = 1u << 7;   // USE_TYPO_METRICS
    const bool bold = is_bold(p.style), ital = is_italic(p.style);
    if (ital) fs |= 1u;
    if (bold) fs |= 1u << 5;
    if (!bold && !ital) fs |= 1u << 6;

    std::string vend = p.info.vendor;
    if (vend.empty()) vend = "NONE";
    vend.resize(4, ' ');

    const int ul_thick = rnd(upm * 0.05);
    Writer w;
    w.u16(4);
    w.i16(avg);
    w.u16(static_cast<unsigned>(p.info.weight_class > 0 ? p.info.weight_class : weight_class(p.style)));
    w.u16(static_cast<unsigned>(std::clamp(p.info.width_class, 1, 9)));   // usWidthClass
    w.u16(0);              // fsType: installable
    w.i16(rnd(upm * 0.65)); w.i16(rnd(upm * 0.6)); w.i16(0); w.i16(rnd(upm * 0.075));   // subscript
    w.i16(rnd(upm * 0.65)); w.i16(rnd(upm * 0.6)); w.i16(0); w.i16(rnd(upm * 0.35));    // superscript
    w.i16(ul_thick);
    w.i16(p.metrics.x_height > 0 ? rnd(p.metrics.x_height * 0.6) : rnd(upm * 0.22));
    w.i16(0);              // sFamilyClass
    {   // panose: any -- except a monospaced font says so (Latin Text, proportion 9)
        bool mono = true;
        int adv = -1;
        for (const auto& g : p.glyphs) {
            if (g.advance == 0) continue;
            if (adv < 0) adv = g.advance;
            else if (g.advance != adv) { mono = false; break; }
        }
        Bytes pan(10, 0);
        if (mono && adv > 0) { pan[0] = 2; pan[3] = 9; }
        w.bytes(pan);
    }
    for (auto u : ur) w.u32(u);
    w.tag(vend.c_str());
    w.u16(fs);
    w.u16(first_char);
    w.u16(last_char);
    w.i16(v.typo_asc);
    w.i16(v.typo_desc);
    w.i16(v.typo_gap);
    w.u16(static_cast<unsigned>(v.win_asc));
    w.u16(static_cast<unsigned>(v.win_desc));
    w.u32(cpr[0]);
    w.u32(cpr[1]);
    w.i16(p.metrics.x_height);
    w.i16(p.metrics.cap_height);
    w.u16(0);              // usDefaultChar
    w.u16(p.cmap.count(0x20) ? 0x20 : 0);   // usBreakChar
    w.u16(static_cast<unsigned>(std::max(1, max_context)));   // usMaxContext
    return w.b;
}

// ── name ───────────────────────────────────────────────────────────────────
Bytes build_name(const Prepared& p) {
    // RIBBI styles name the family as is; any other style moves into the
    // legacy family (ID 1, "Family Light") with the typographic pair 16 / 17.
    const bool ribbi = p.style == "Regular" || p.style == "Bold" || p.style == "Italic" || p.style == "Bold Italic";
    char ver[64];
    std::snprintf(ver, sizeof ver, "Version %d.%03d", p.info.version_major, p.info.version_minor);
    char uid[256];
    std::string vend = p.info.vendor.empty() ? "NONE" : p.info.vendor;
    std::snprintf(uid, sizeof uid, "%d.%03d;%s;%s", p.info.version_major, p.info.version_minor, vend.c_str(),
                  p.ps_name.c_str());

    std::vector<std::pair<int, std::string>> recs;
    if (!p.info.copyright.empty()) recs.push_back({0, p.info.copyright});
    if (ribbi) {
        recs.push_back({1, p.family});
        recs.push_back({2, p.style});
    } else {
        std::string legacy_style = (is_italic(p.style) ? "Italic" : "Regular");
        std::string leg = p.style;   // drop "Italic" from the legacy family's style word
        for (const char* w : {" Italic", "Italic"})
            if (auto at = leg.find(w); at != std::string::npos) leg.erase(at, std::string(w).size());
        recs.push_back({1, p.family + (leg.empty() ? "" : " " + leg)});
        recs.push_back({2, legacy_style});
    }
    recs.push_back({3, uid});
    recs.push_back({4, p.family + (p.style == "Regular" ? "" : " " + p.style)});
    recs.push_back({5, ver});
    recs.push_back({6, p.ps_name});
    if (!p.info.trademark.empty()) recs.push_back({7, p.info.trademark});
    if (!p.info.manufacturer.empty()) recs.push_back({8, p.info.manufacturer});
    if (!p.info.designer.empty()) recs.push_back({9, p.info.designer});
    if (!p.info.description.empty()) recs.push_back({10, p.info.description});
    if (!p.info.manufacturer_url.empty()) recs.push_back({11, p.info.manufacturer_url});
    if (!p.info.designer_url.empty()) recs.push_back({12, p.info.designer_url});
    if (!p.info.license.empty()) recs.push_back({13, p.info.license});
    if (!ribbi) {
        recs.push_back({16, p.family});
        recs.push_back({17, p.style});
    }

    Writer strings;
    std::vector<std::array<unsigned, 2>> spans;   // offset, length
    for (const auto& [id, s] : recs) {
        const std::size_t at = strings.size();
        utf16be(strings, s);
        spans.push_back({static_cast<unsigned>(at), static_cast<unsigned>(strings.size() - at)});
    }
    Writer w;
    w.u16(0);
    w.u16(static_cast<unsigned>(recs.size()));
    w.u16(static_cast<unsigned>(6 + 12 * recs.size()));
    for (std::size_t i = 0; i < recs.size(); ++i) {   // sorted: one platform, IDs ascending
        w.u16(3); w.u16(1); w.u16(0x0409);
        w.u16(static_cast<unsigned>(recs[i].first));
        w.u16(spans[i][1]);
        w.u16(spans[i][0]);
    }
    w.bytes(strings.b);
    return w.b;
}

// ── cmap ───────────────────────────────────────────────────────────────────
namespace {
Bytes cmap_format4(const std::map<char32_t, int>& cmap) {
    struct Seg { unsigned start, end; int delta; bool ranged; std::vector<unsigned> gids; };
    std::vector<Seg> segs;
    for (const auto& [cp, gid] : cmap) {
        if (cp >= 0xFFFF) break;
        if (!segs.empty() && segs.back().end + 1 == cp) {
            auto& s = segs.back();
            s.end = cp;
            s.gids.push_back(static_cast<unsigned>(gid));
        } else {
            segs.push_back({static_cast<unsigned>(cp), static_cast<unsigned>(cp), 0, false, {static_cast<unsigned>(gid)}});
        }
    }
    for (auto& s : segs) {
        const int d = static_cast<int>(s.gids[0]) - static_cast<int>(s.start);
        bool same = true;
        for (std::size_t i = 0; i < s.gids.size(); ++i)
            if (static_cast<int>(s.gids[i]) - static_cast<int>(s.start + i) != d) { same = false; break; }
        s.ranged = !same;
        s.delta  = same ? d : 0;
    }
    segs.push_back({0xFFFF, 0xFFFF, 1, false, {}});

    const unsigned n = static_cast<unsigned>(segs.size());
    unsigned pow2 = 1, log2 = 0;
    while (pow2 * 2 <= n) { pow2 *= 2; ++log2; }
    std::vector<unsigned> glyph_array;
    std::vector<unsigned> range_off(n, 0);
    for (unsigned i = 0; i < n; ++i) {
        if (!segs[i].ranged) continue;
        range_off[i] = (n - i) * 2 + static_cast<unsigned>(glyph_array.size()) * 2;
        glyph_array.insert(glyph_array.end(), segs[i].gids.begin(), segs[i].gids.end());
    }
    Writer w;
    w.u16(4);
    w.u16(static_cast<unsigned>(16 + 8 * n + 2 * glyph_array.size()));
    w.u16(0);
    w.u16(n * 2);
    w.u16(pow2 * 2);
    w.u16(log2);
    w.u16(n * 2 - pow2 * 2);
    for (const auto& s : segs) w.u16(s.end);
    w.u16(0);
    for (const auto& s : segs) w.u16(s.start);
    for (const auto& s : segs) w.u16(static_cast<unsigned>(s.delta) & 0xFFFF);
    for (unsigned i = 0; i < n; ++i) w.u16(range_off[i]);
    for (unsigned g : glyph_array) w.u16(g);
    return w.b;
}

Bytes cmap_format12(const std::map<char32_t, int>& cmap) {
    struct Group { std::uint32_t start, end, gid; };
    std::vector<Group> gs;
    for (const auto& [cp, gid] : cmap) {
        if (!gs.empty() && gs.back().end + 1 == cp &&
            gs.back().gid + (gs.back().end - gs.back().start) + 1 == static_cast<std::uint32_t>(gid))
            gs.back().end = cp;
        else
            gs.push_back({cp, cp, static_cast<std::uint32_t>(gid)});
    }
    Writer w;
    w.u16(12);
    w.u16(0);
    w.u32(static_cast<std::uint32_t>(16 + 12 * gs.size()));
    w.u32(0);
    w.u32(static_cast<std::uint32_t>(gs.size()));
    for (const auto& g : gs) { w.u32(g.start); w.u32(g.end); w.u32(g.gid); }
    return w.b;
}
}  // namespace

Bytes build_cmap(const Prepared& p) {
    const Bytes f4 = cmap_format4(p.cmap);
    const bool supplementary = !p.cmap.empty() && p.cmap.rbegin()->first > 0xFFFF;
    const Bytes f12 = supplementary ? cmap_format12(p.cmap) : Bytes{};
    const unsigned n = supplementary ? 4 : 2;
    const std::uint32_t at4 = 4 + 8 * n, at12 = at4 + static_cast<std::uint32_t>(f4.size());
    Writer w;
    w.u16(0);
    w.u16(n);
    w.u16(0); w.u16(3); w.u32(at4);                         // Unicode BMP
    if (supplementary) { w.u16(0); w.u16(4); w.u32(at12); }  // Unicode full
    w.u16(3); w.u16(1); w.u32(at4);                         // Windows BMP
    if (supplementary) { w.u16(3); w.u16(10); w.u32(at12); } // Windows full
    w.bytes(f4);
    w.bytes(f12);
    return w.b;
}

// ── post (3.0: CFF carries the names) ──────────────────────────────────────
Bytes build_post(const Prepared& p) {
    const int upm = p.info.units_per_em;
    int adv = -1;
    bool fixed = true;
    for (const auto& g : p.glyphs) {
        if (g.advance == 0) continue;
        if (adv < 0) adv = g.advance;
        else if (g.advance != adv) { fixed = false; break; }
    }
    Writer w;
    w.u32(0x00030000);
    w.i32(static_cast<std::int32_t>(std::lround(p.info.italic_angle * 65536.0)));   // italicAngle (Fixed)
    w.i16(-rnd(upm * 0.075));          // underlinePosition
    w.i16(rnd(upm * 0.05));            // underlineThickness
    w.u32(fixed && adv > 0 ? 1 : 0);
    w.zeros(16);
    return w.b;
}

}  // namespace granjon::otf
