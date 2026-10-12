// reader::GsubRead (s10) -- an imported font's GSUB back into feature-file
// text (model::Fea's syntax), so the substitutions are editable in the
// Features view and compile again on export.
//
// Every lookup a feature uses becomes a named lookup block, in the font's own
// lookup order (the order OpenType applies them); each feature then lists its
// lookups by name. Single (both formats), multiple, alternate and ligature
// substitutions are read, through Extension lookups too. Contextual lookups
// (types 5, 6, 8 -- calt, frac, ...) are not: the report counts them, and the
// features that used only those are left out. Lookup flags are not carried.
#include "import/ReaderParts.hpp"

#include <algorithm>
#include <cctype>
#include <set>

namespace granjon::reader {

namespace {

std::vector<int> coverage(Span s) {
    std::vector<int> out;
    const unsigned fmt = s.u16(0), n = s.u16(2);
    if (fmt == 1) {
        for (unsigned i = 0; i < n; ++i) out.push_back(static_cast<int>(s.u16(4 + 2 * i)));
    } else if (fmt == 2) {
        for (unsigned i = 0; i < n; ++i) {
            const unsigned a = s.u16(4 + 6 * i), b = s.u16(6 + 6 * i);
            for (unsigned g = a; g <= b; ++g) out.push_back(static_cast<int>(g));
        }
    } else {
        throw BadFont("an unknown Coverage format in GSUB");
    }
    return out;
}

bool fea_name(const std::string& n) {
    if (n.empty() || n.size() > 63) return false;
    for (char c : n)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-')) return false;
    return n[0] != '-';
}

}  // namespace

std::string read_gsub(Span gsub, const std::vector<std::string>& names, std::vector<std::string>& notes) {
    if (gsub.empty()) return {};
    auto name = [&](unsigned g) -> std::string { return g < names.size() ? names[g] : std::string(); };
    const Span feats = gsub.from(gsub.u16(6)), lookups = gsub.from(gsub.u16(8));

    // features: tag -> lookup indices (records repeat per script: merged)
    std::vector<std::string> order;
    std::map<std::string, std::vector<int>> by_tag;
    for (unsigned i = 0, n = feats.u16(0); i < n; ++i) {
        const std::string tag(reinterpret_cast<const char*>(feats.sub(2 + 6 * i, 4).p), 4);
        const Span f = feats.from(feats.u16(6 + 6 * i));
        auto& v = by_tag[tag];
        if (std::find(order.begin(), order.end(), tag) == order.end()) order.push_back(tag);
        for (unsigned k = 0, m = f.u16(2); k < m; ++k) {
            const int li = static_cast<int>(f.u16(4 + 2 * k));
            if (std::find(v.begin(), v.end(), li) == v.end()) v.push_back(li);
        }
    }
    std::set<int> used;
    for (const auto& [_, v] : by_tag) used.insert(v.begin(), v.end());

    // lookups -> rule text
    const unsigned nl = lookups.u16(0);
    std::map<int, std::vector<std::string>> rules;
    int contextual = 0, odd_names = 0, flagged = 0;
    for (int li : used) {
        if (li < 0 || static_cast<unsigned>(li) >= nl) continue;
        const Span L = lookups.from(lookups.u16(2 + 2 * static_cast<unsigned>(li)));
        unsigned type = L.u16(0);
        if (L.u16(2) & 0x00FF) ++flagged;
        std::vector<std::string>& out = rules[li];
        std::set<int> seen_single;
        for (unsigned s = 0, ns = L.u16(4); s < ns; ++s) {
            Span st = L.from(L.u16(6 + 2 * s));
            unsigned t = type;
            if (t == 7) { t = st.u16(2); st = st.from(st.u32(4)); }
            if (t == 5 || t == 6 || t == 8) { ++contextual; out.clear(); break; }
            auto line = [&](std::vector<std::string> in, std::vector<std::string> outs, bool alternate) {
                for (const auto& x : in) if (!fea_name(x)) { ++odd_names; return; }
                for (const auto& x : outs) if (!fea_name(x)) { ++odd_names; return; }
                std::string l = "    sub";
                for (const auto& x : in) l += " " + x;
                if (alternate) {
                    l += " from [";
                    for (std::size_t i = 0; i < outs.size(); ++i) l += (i ? " " : "") + outs[i];
                    l += "]";
                } else {
                    l += " by";
                    for (const auto& x : outs) l += " " + x;
                }
                out.push_back(l + ";");
            };
            const unsigned fmt = st.u16(0);
            const auto cov = coverage(st.from(st.u16(2)));
            if (t == 1) {
                for (std::size_t i = 0; i < cov.size(); ++i) {
                    if (!seen_single.insert(cov[i]).second) continue;   // the first subtable wins
                    const unsigned to = fmt == 1 ? static_cast<unsigned>((cov[i] + st.i16(4)) & 0xFFFF)
                                                 : st.u16(6 + 2 * static_cast<unsigned>(i));
                    line({name(static_cast<unsigned>(cov[i]))}, {name(to)}, false);
                }
            } else if (t == 2 || t == 3) {
                for (std::size_t i = 0; i < cov.size(); ++i) {
                    if (!seen_single.insert(cov[i]).second) continue;
                    const Span set = st.from(st.u16(6 + 2 * static_cast<unsigned>(i)));
                    std::vector<std::string> outs;
                    for (unsigned k = 0, m = set.u16(0); k < m; ++k) outs.push_back(name(set.u16(2 + 2 * k)));
                    if (!outs.empty()) line({name(static_cast<unsigned>(cov[i]))}, outs, t == 3);
                }
            } else if (t == 4) {
                for (std::size_t i = 0; i < cov.size(); ++i) {
                    const Span set = st.from(st.u16(6 + 2 * static_cast<unsigned>(i)));
                    for (unsigned k = 0, m = set.u16(0); k < m; ++k) {
                        const Span lig = set.from(set.u16(2 + 2 * k));
                        std::vector<std::string> in = {name(static_cast<unsigned>(cov[i]))};
                        for (unsigned c = 1, cc = lig.u16(2); c < cc; ++c) in.push_back(name(lig.u16(4 + 2 * (c - 1))));
                        line(in, {name(lig.u16(0))}, false);
                    }
                }
            } else {
                notes.push_back("GSUB: a lookup of an unknown type (" + std::to_string(t) + ") was skipped.");
                out.clear();
                break;
            }
        }
    }

    std::string text = "# Read from the imported font's GSUB.\nlanguagesystem DFLT dflt;\nlanguagesystem latn dflt;\n";
    std::map<int, std::string> lname;
    for (const auto& [li, r] : rules) {   // lookup order = the font's
        if (r.empty()) continue;
        std::string first;
        for (const auto& tag : order) {
            const auto& v = by_tag[tag];
            if (std::find(v.begin(), v.end(), li) != v.end()) { first = tag; break; }
        }
        while (!first.empty() && first.back() == ' ') first.pop_back();
        lname[li] = first + "_" + std::to_string(li);
        text += "\nlookup " + lname[li] + " {\n";
        for (const auto& l : r) text += l + "\n";
        text += "} " + lname[li] + ";\n";
    }
    int features = 0;
    for (const auto& tag : order) {
        std::string body;
        for (int li : by_tag[tag])
            if (lname.count(li)) body += "    lookup " + lname[li] + ";\n";
        if (body.empty()) continue;
        std::string t = tag;
        while (!t.empty() && t.back() == ' ') t.pop_back();
        text += "\nfeature " + t + " {\n" + body + "} " + t + ";\n";
        ++features;
    }
    if (contextual) notes.push_back("GSUB: " + std::to_string(contextual) + " contextual lookup(s) (calt, frac ...) not read -- Granjon doesn't compile contextual rules yet.");
    if (odd_names) notes.push_back("GSUB: " + std::to_string(odd_names) + " rule(s) skipped -- a glyph name the feature syntax can't write.");
    if (flagged) notes.push_back("GSUB: lookup flags (ignore marks ...) on " + std::to_string(flagged) + " lookup(s) were not kept.");
    if (features == 0) return {};
    notes.push_back("GSUB: " + std::to_string(features) + " feature(s) read into the feature file (Features view, Ctrl+6).");
    return text;
}

}  // namespace granjon::reader
