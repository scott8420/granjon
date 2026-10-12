// model::Proof -- see Proof.hpp.
#include "model/Proof.hpp"
#include "geom/Edit.hpp"
#include "model/Fea.hpp"
#include "model/Font.hpp"
#include "model/Spacing.hpp"

#include <cstdio>
#include <set>

namespace granjon::model {

const char* const kProofParagraph =
    "Robert Granjon cut his first types in Paris around 1543 and worked on for nearly fifty years, in Lyon, "
    "Antwerp, Frankfurt and Rome. His italics set the pattern for the sixteenth century; his civilité "
    "imitated the French hand; his music types and his Arabic for the Medici press were cut for books no "
    "one had printed before. THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG — 0123456789 (§, &, ?!).";

namespace {

void utf8(std::string& s, char32_t c) {
    if (c < 0x80) s += static_cast<char>(c);
    else if (c < 0x800) { s += static_cast<char>(0xC0 | (c >> 6)); s += static_cast<char>(0x80 | (c & 0x3F)); }
    else if (c < 0x10000) {
        s += static_cast<char>(0xE0 | (c >> 12)); s += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        s += static_cast<char>(0xF0 | (c >> 18)); s += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        s += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); s += static_cast<char>(0x80 | (c & 0x3F));
    }
}

std::u32string decode(const std::string& s) {
    std::u32string out;
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        char32_t cp = c;
        int n = 0;
        if (c >= 0xF0) { cp = c & 0x07; n = 3; } else if (c >= 0xE0) { cp = c & 0x0F; n = 2; } else if (c >= 0xC0) { cp = c & 0x1F; n = 1; }
        ++i;
        for (int k = 0; k < n && i < s.size(); ++k, ++i) cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3F);
        out += cp;
    }
    return out;
}

bool drawn(const Glyph* g) { return g && !g->skip_export && (!g->contours.empty() || !g->components.empty()); }

// The character a glyph types as (its first code point), or 0.
char32_t typed(const Font& f, const std::string& name) {
    const Glyph* g = f.glyph(name);
    if (!g || g->unicodes.empty() || g->skip_export) return 0;
    return f.glyph_for(g->unicodes.front()) == g ? g->unicodes.front() : 0;
}

}  // namespace

ProofPlan proof_plan(const Font& f, const std::string& sample) {
    ProofPlan p;
    p.family = f.info().family.empty() ? "Untitled" : f.info().family;
    p.style = f.info().style.empty() ? "Regular" : f.info().style;
    char v[32];
    std::snprintf(v, sizeof v, "%d.%03d", f.info().version_major, f.info().version_minor);
    p.version = v;
    p.sample = sample.empty() ? "Hamburgefonstiv" : sample;
    p.paragraph = kProofParagraph;
    {
        std::set<char32_t> miss;
        for (char32_t c : decode(p.sample + p.paragraph))
            if (c != U' ' && !drawn(f.glyph_for(c))) miss.insert(c);
        for (char32_t c : miss) utf8(p.missing, c);
    }

    // spacing: each drawn letter between its controls, eight to a line
    auto between = [&](char32_t from, char32_t to, const char* ctl) {
        if (!drawn(f.glyph_for(static_cast<char32_t>(ctl[0])))) return;
        std::string line;
        int n = 0;
        for (char32_t c = from; c <= to; ++c) {
            if (!drawn(f.glyph_for(c))) continue;
            std::string grp = std::string(ctl) + std::string(ctl);
            std::string mid;
            utf8(mid, c);
            grp = std::string(ctl) + mid + std::string(ctl);
            line += (line.empty() ? "" : " ") + grp;
            if (++n == 8) { p.spacing.push_back(line); line.clear(); n = 0; }
        }
        if (!line.empty()) p.spacing.push_back(line);
    };
    between(U'a', U'z', "nn");
    between(U'A', U'Z', "HH");
    between(U'0', U'9', "00");
    for (const char* s : {"nnnn nnonoo nnoonn oooo", "HHHH HHOHOO HHOOHH OOOO"}) {
        bool ok = true;
        for (char32_t c : decode(s)) ok = ok && (c == U' ' || drawn(f.glyph_for(c)));
        if (ok) p.spacing.push_back(s);
    }

    // kerning: every pair in effect with a value, both sides typable
    {
        const auto& k = f.kerning();
        std::set<std::pair<std::string, std::string>> seen;
        std::vector<std::string> pairs;
        auto members = [&](const std::string& side) {
            std::vector<std::string> out;
            if (side.rfind("public.kern", 0) == 0) {
                if (auto it = k.groups.find(side); it != k.groups.end()) out = it->second;
            } else {
                out.push_back(side);
            }
            return out;
        };
        for (const auto& [key, _] : k.pairs)
            for (const auto& l : members(key.first))
                for (const auto& r : members(key.second)) {
                    if (pairs.size() >= 400 || !seen.insert({l, r}).second) continue;
                    const char32_t a = typed(f, l), b = typed(f, r);
                    if (!a || !b || kern_lookup(k, l, r).value == 0) continue;
                    std::string s;
                    utf8(s, a);
                    utf8(s, b);
                    pairs.push_back(s);
                }
        p.kern_pairs = static_cast<int>(pairs.size());
        std::string line;
        int n = 0;
        for (const auto& s : pairs) {
            line += (line.empty() ? "" : "  ") + s;
            if (++n == 12) { p.kerning.push_back(line); line.clear(); n = 0; }
        }
        if (!line.empty()) p.kerning.push_back(line);
    }

    // features: each feature's typable inputs
    if (!f.features().text.empty()) {
        const FeaDoc d = parse_fea(f.features().text, [&](const std::string& n) { return f.glyph(n) != nullptr; });
        if (d.ok())
            for (const auto& ft : d.features) {
                std::string tag = ft.tag;
                while (!tag.empty() && tag.back() == ' ') tag.pop_back();
                std::string text;
                int n = 0;
                for (int li : ft.lookups)
                    for (const auto& r : d.lookups[static_cast<std::size_t>(li)].rules) {
                        // the first combination of the rule's input
                        std::string w;
                        bool ok = true;
                        for (const auto& pos : r.in) {
                            const char32_t c = pos.empty() ? 0 : typed(f, pos.front());
                            if (!c) { ok = false; break; }
                            utf8(w, c);
                        }
                        if (r.kind == FeaRule::Kind::Chain) continue;   // its inputs are another lookup's outputs
                        if (r.kind == FeaRule::Kind::Single)   // every glyph a class names
                            for (std::size_t i = 1; i < r.in[0].size() && n < 40; ++i)
                                if (const char32_t c = typed(f, r.in[0][i])) { std::string x; utf8(x, c); w += x; }
                        if (!ok || w.empty() || n >= 40) continue;
                        text += (text.empty() ? "" : " ") + w;
                        ++n;
                    }
                if (tag == "frac") {   // a fraction to show, not its inputs
                    bool ok = true;
                    for (char32_t c : std::u32string(U"1/2 3/4 15/16")) ok = ok && (c == U' ' || f.glyph_for(c));
                    if (ok) text = "1/2 3/4 15/16";
                }
                if (!text.empty()) p.features.push_back({tag, text});
            }
    }

    for (const auto& n : f.glyph_order())   // only glyphs that draw something (an accented letter waits for its parts)
        if (const Glyph* g = f.glyph(n); drawn(g) && (!g->contours.empty() || !geom::all_contours(f, *g).empty()))
            p.glyphs.push_back(n);
    return p;
}

}  // namespace granjon::model
