// reader::GsubRead (s10) -- an imported font's GSUB back into feature-file
// text (model::Fea's syntax), so the substitutions are editable in the
// Features view and compile again on export.
//
// Every lookup a feature uses becomes a named lookup block, in the font's own
// lookup order (the order OpenType applies them); each feature then lists its
// lookups by name. Single (both formats), multiple, alternate and ligature
// substitutions are read, through Extension lookups too. Contextual lookups
// (s10) of the coverage kind (chaining format 3, the kind Granjon writes) come
// in as `sub a x' lookup NAME b;` / `ignore sub ...`; the other contextual
// formats (5, 6.1, 6.2, 8) are counted in the report and left out. Lookup
// flags are not carried.
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
    std::set<int> chained;   // lookups a contextual rule applies
    std::vector<int> todo(used.begin(), used.end());
    for (std::size_t q = 0; q < todo.size(); ++q) {   // grows as contextual rules name more lookups
        const int li = todo[q];
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
            if (t == 6 && st.u16(0) == 3) {   // s10: chaining context, coverage-based -- read
                auto covs = [&](std::size_t at, unsigned n) {
                    std::vector<std::vector<int>> v;
                    for (unsigned k = 0; k < n; ++k) v.push_back(coverage(st.from(st.u16(at + 2 * k))));
                    return v;
                };
                std::size_t at = 2;
                const unsigned nb = st.u16(at);
                auto back = covs(at + 2, nb);
                std::reverse(back.begin(), back.end());   // the table holds them closest-first
                at += 2 + 2 * nb;
                const unsigned ni = st.u16(at);
                const auto in = covs(at + 2, ni);
                at += 2 + 2 * ni;
                const unsigned na = st.u16(at);
                const auto ahead = covs(at + 2, na);
                at += 2 + 2 * na;
                const unsigned nr = st.u16(at);
                std::map<unsigned, int> rec;
                for (unsigned k = 0; k < nr; ++k) {
                    const unsigned seq = st.u16(at + 2 + 4 * k), target = st.u16(at + 4 + 4 * k);
                    rec[seq] = static_cast<int>(target);
                    chained.insert(static_cast<int>(target));
                }
                auto pos = [&](const std::vector<int>& g) -> std::string {
                    std::vector<std::string> ns;
                    for (int x : g) {
                        const std::string n = name(static_cast<unsigned>(x));
                        if (!fea_name(n)) return {};
                        ns.push_back(n);
                    }
                    if (ns.size() == 1) return ns[0];
                    std::string c = "[";
                    for (std::size_t i = 0; i < ns.size(); ++i) c += (i ? " " : "") + ns[i];
                    return c + "]";
                };
                std::string l = nr == 0 ? "    ignore sub" : "    sub";
                bool ok = true;
                for (const auto& g : back) { const auto x = pos(g); ok = ok && !x.empty(); l += " " + x; }
                for (unsigned k = 0; k < in.size(); ++k) {
                    const auto x = pos(in[k]);
                    ok = ok && !x.empty();
                    l += " " + x + "'";
                    if (auto r = rec.find(k); r != rec.end()) l += " lookup @L" + std::to_string(r->second) + "@";
                }
                for (const auto& g : ahead) { const auto x = pos(g); ok = ok && !x.empty(); l += " " + x; }
                if (!ok) { ++odd_names; continue; }
                out.push_back(l + ";");
                continue;
            }
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
        for (int c : chained)
            if (std::find(todo.begin(), todo.end(), c) == todo.end()) todo.push_back(c);
    }

    std::string text = "# Read from the imported font's GSUB.\nlanguagesystem DFLT dflt;\nlanguagesystem latn dflt;\n";
    std::map<int, std::string> lname;
    for (const auto& [li, r] : rules) {
        std::string first;
        for (const auto& tag : order) {
            const auto& v = by_tag[tag];
            if (std::find(v.begin(), v.end(), li) != v.end()) { first = tag; break; }
        }
        while (!first.empty() && first.back() == ' ') first.pop_back();
        lname[li] = (first.empty() ? "ctx" : first) + "_" + std::to_string(li);
    }
    // a contextual rule names its lookups: those (applied only through it) are
    // written first, so every name is known where it is used; the rest keep
    // the font's order (the order OpenType applies them)
    std::vector<int> emit;
    for (const auto& [li, r] : rules) if (chained.count(li) && !used.count(li)) emit.push_back(li);
    for (const auto& [li, r] : rules) if (!(chained.count(li) && !used.count(li))) emit.push_back(li);
    std::set<int> dropped;
    for (int li : emit) {
        auto& r = rules[li];
        // resolve the placeholders; a rule naming a lookup that came out empty goes
        std::vector<std::string> keep;
        for (auto l : r) {
            bool bad = false;
            for (std::size_t a; (a = l.find("@L")) != std::string::npos;) {
                const std::size_t b = l.find('@', a + 2);
                const int target = std::stoi(l.substr(a + 2, b - a - 2));
                if (!rules.count(target) || rules[target].empty() || dropped.count(target)) { bad = true; break; }
                l.replace(a, b - a + 1, lname[target]);
            }
            if (!bad) keep.push_back(l);
        }
        r = keep;
        if (r.empty()) { dropped.insert(li); continue; }
        text += "\nlookup " + lname[li] + " {\n";
        for (const auto& l : r) text += l + "\n";
        text += "} " + lname[li] + ";\n";
    }
    int features = 0;
    for (const auto& tag : order) {
        std::string body;
        for (int li : by_tag[tag])
            if (lname.count(li) && rules.count(li) && !rules[li].empty() && !dropped.count(li)) body += "    lookup " + lname[li] + ";\n";
        if (body.empty()) continue;
        std::string t = tag;
        while (!t.empty() && t.back() == ' ') t.pop_back();
        text += "\nfeature " + t + " {\n" + body + "} " + t + ";\n";
        ++features;
    }
    if (contextual) notes.push_back("GSUB: " + std::to_string(contextual) + " contextual lookup(s) of the glyph- or class-list kind not read "
                                    "(Granjon reads and writes the coverage kind); the features using only those are left out.");
    if (odd_names) notes.push_back("GSUB: " + std::to_string(odd_names) + " rule(s) skipped -- a glyph name the feature syntax can't write.");
    if (flagged) notes.push_back("GSUB: lookup flags (ignore marks ...) on " + std::to_string(flagged) + " lookup(s) were not kept.");
    if (features == 0) return {};
    notes.push_back("GSUB: " + std::to_string(features) + " feature(s) read into the feature file (Features view, Ctrl+6).");
    return text;
}

}  // namespace granjon::reader
