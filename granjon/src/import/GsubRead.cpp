// reader::GsubRead (s10) -- an imported font's GSUB back into feature-file
// text (model::Fea's syntax), so the substitutions are editable in the
// Features view and compile again on export.
//
// Every lookup a feature uses becomes a named lookup block, in the font's own
// lookup order (the order OpenType applies them); each feature then lists its
// lookups by name. Single (both formats), multiple, alternate and ligature
// substitutions are read, through Extension lookups too. Contextual lookups
// (s10) of every format (5 / 6, glyph-, class- and coverage-based) come in as
// `sub a x' lookup NAME b;` / `ignore sub ...` -- class 0 written as the glyphs
// its ClassDef leaves out; reverse chaining (8) is counted and left out.
// Lookup flags: the four plain bits (IgnoreMarks ...) come in as lookupflag;
// mark classes and filtering sets are noted and dropped.
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

std::map<int, int> class_def(Span s) {
    std::map<int, int> out;
    const unsigned fmt = s.u16(0);
    if (fmt == 1) {
        const unsigned start = s.u16(2), n = s.u16(4);
        for (unsigned i = 0; i < n; ++i) if (const unsigned c = s.u16(6 + 2 * i)) out[static_cast<int>(start + i)] = static_cast<int>(c);
    } else if (fmt == 2) {
        const unsigned n = s.u16(2);
        for (unsigned i = 0; i < n; ++i) {
            const unsigned a = s.u16(4 + 6 * i), b = s.u16(6 + 6 * i), c = s.u16(8 + 6 * i);
            if (c) for (unsigned g = a; g <= b; ++g) out[static_cast<int>(g)] = static_cast<int>(c);
        }
    } else {
        throw BadFont("an unknown ClassDef format in GSUB");
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

    // features: tag -> lookup indices. Only the records every script's
    // default language uses (Granjon writes DFLT + latn defaults); records
    // only a language asks for (a Sami locl, a Turkish liga) are left out --
    // merged in, they would apply to all text.
    std::set<unsigned> wanted, anywhere;
    {
        const Span scripts = gsub.from(gsub.u16(4));
        auto take = [&](Span ls, std::set<unsigned>& into) {
            if (const unsigned req = ls.u16(2); req != 0xFFFF) into.insert(req);
            for (unsigned k = 0, m = ls.u16(4); k < m; ++k) into.insert(ls.u16(6 + 2 * k));
        };
        for (unsigned i = 0, n = scripts.u16(0); i < n; ++i) {
            const Span s = scripts.from(scripts.u16(2 + 6 * i + 4));
            if (const unsigned d = s.u16(0)) take(s.from(d), wanted);
            for (unsigned k = 0, m = s.u16(2); k < m; ++k) take(s.from(s.u16(4 + 6 * k + 4)), anywhere);
        }
        if (wanted.empty()) wanted = anywhere;   // no defaults at all: every record
    }
    int lang_only = 0;
    std::vector<std::string> order;
    std::map<std::string, std::vector<int>> by_tag;
    for (unsigned i = 0, n = feats.u16(0); i < n; ++i) {
        if (!wanted.contains(i)) { lang_only += anywhere.contains(i); continue; }
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
    std::map<int, int> flags;   // s10: lookupflag (the four plain bits)
    std::vector<int> todo(used.begin(), used.end());
    for (std::size_t q = 0; q < todo.size(); ++q) {   // grows as contextual rules name more lookups
        const int li = todo[q];
        if (li < 0 || static_cast<unsigned>(li) >= nl) continue;
        const Span L = lookups.from(lookups.u16(2 + 2 * static_cast<unsigned>(li)));
        unsigned type = L.u16(0);
        const unsigned lf = L.u16(2);
        if (lf & 0xFFF0) ++flagged;   // mark classes / filtering sets: not kept
        if (lf & 0x0F) flags[li] = static_cast<int>(lf & 0x0F);
        std::vector<std::string>& out = rules[li];
        std::set<int> seen_single;
        for (unsigned s = 0, ns = L.u16(4); s < ns; ++s) {
            Span st = L.from(L.u16(6 + 2 * s));
            unsigned t = type;
            if (t == 7) { t = st.u16(2); st = st.from(st.u32(4)); }
            if (t == 5 || t == 6) {   // s10: contextual -- every format, as coverage-style rules
                using Pos = std::vector<std::vector<int>>;
                // one rule out: back in reading order; records (input index, lookup)
                auto emit = [&](const Pos& back, const Pos& in, const Pos& ahead, const std::vector<std::pair<unsigned, unsigned>>& recs) {
                    auto pos = [&](const std::vector<int>& g) -> std::string {
                        if (g.empty()) return {};
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
                    std::map<unsigned, unsigned> rec;
                    for (const auto& [seq, target] : recs) {
                        if (!rec.count(seq)) rec[seq] = target;   // one lookup per position (Granjon's syntax)
                        chained.insert(static_cast<int>(target));
                    }
                    std::string l = recs.empty() ? "    ignore sub" : "    sub";
                    bool ok = true;
                    for (const auto& g : back) { const auto x = pos(g); ok = ok && !x.empty(); l += " " + x; }
                    for (unsigned k = 0; k < in.size(); ++k) {
                        const auto x = pos(in[k]);
                        ok = ok && !x.empty();
                        l += " " + x + "'";
                        if (auto r = rec.find(k); r != rec.end()) l += " lookup @L" + std::to_string(r->second) + "@";
                    }
                    for (const auto& g : ahead) { const auto x = pos(g); ok = ok && !x.empty(); l += " " + x; }
                    if (!ok) { ++odd_names; return; }
                    out.push_back(l + ";");
                };
                auto records = [&](Span at, unsigned n) {
                    std::vector<std::pair<unsigned, unsigned>> r;
                    for (unsigned k = 0; k < n; ++k) r.push_back({at.u16(4 * k), at.u16(2 + 4 * k)});
                    return r;
                };
                const unsigned fmt = st.u16(0);
                const unsigned ng = static_cast<unsigned>(names.size());
                // a class's glyphs (class 0: every glyph the ClassDef leaves out)
                auto members = [&](const std::map<int, int>& cd, unsigned cls) {
                    std::vector<int> v;
                    if (cls == 0) { for (unsigned g = 0; g < ng; ++g) if (!cd.count(static_cast<int>(g))) v.push_back(static_cast<int>(g)); }
                    else for (const auto& [g, c] : cd) if (static_cast<unsigned>(c) == cls) v.push_back(g);
                    return v;
                };
                if (fmt == 3) {
                    auto covs = [&](std::size_t at, unsigned n) {
                        Pos v;
                        for (unsigned k = 0; k < n; ++k) v.push_back(coverage(st.from(st.u16(at + 2 * k))));
                        return v;
                    };
                    if (t == 5) {
                        const unsigned ni = st.u16(2), nr = st.u16(4);
                        emit({}, covs(6, ni), {}, records(st.from(6 + 2 * ni), nr));
                    } else {
                        std::size_t at = 2;
                        const unsigned nb = st.u16(at);
                        Pos back = covs(at + 2, nb);
                        std::reverse(back.begin(), back.end());   // the table holds them closest-first
                        at += 2 + 2 * nb;
                        const unsigned ni = st.u16(at);
                        const Pos in = covs(at + 2, ni);
                        at += 2 + 2 * ni;
                        const unsigned na = st.u16(at);
                        const Pos ahead = covs(at + 2, na);
                        at += 2 + 2 * na;
                        emit(back, in, ahead, records(st.from(at + 2), st.u16(at)));
                    }
                    continue;
                }
                if (fmt != 1 && fmt != 2) { ++contextual; out.clear(); break; }
                const auto cov = coverage(st.from(st.u16(2)));
                std::map<int, int> cdb, cdi, cda;
                std::size_t sets_at = 4;
                if (fmt == 2) {
                    if (t == 5) { cdi = class_def(st.from(st.u16(4))); sets_at = 6; }
                    else {
                        cdb = class_def(st.from(st.u16(4)));
                        cdi = class_def(st.from(st.u16(6)));
                        cda = class_def(st.from(st.u16(8)));
                        sets_at = 10;
                    }
                }
                const unsigned nsets = st.u16(sets_at);
                for (unsigned si = 0; si < nsets; ++si) {
                    const unsigned so = st.u16(sets_at + 2 + 2 * si);
                    if (so == 0) continue;
                    const Span set = st.from(so);
                    // the first input position: format 1 -- the covered glyph; format 2 -- covered glyphs of class si
                    std::vector<int> first;
                    if (fmt == 1) { if (si < cov.size()) first.push_back(cov[si]); }
                    else for (int g : cov) if ((cdi.count(g) ? static_cast<unsigned>(cdi.at(g)) : 0u) == si) first.push_back(g);
                    if (first.empty()) continue;
                    auto as_pos = [&](const std::map<int, int>& cd, unsigned v) {
                        return fmt == 1 ? std::vector<int>{static_cast<int>(v)} : members(cd, v);
                    };
                    for (unsigned ri = 0, nr = set.u16(0); ri < nr; ++ri) {
                        const Span r = set.from(set.u16(2 + 2 * ri));
                        Pos back, in, ahead;
                        std::size_t at = 0;
                        if (t == 6) {
                            const unsigned nb = r.u16(0);
                            for (unsigned k = 0; k < nb; ++k) back.push_back(as_pos(cdb, r.u16(2 + 2 * k)));
                            std::reverse(back.begin(), back.end());
                            at = 2 + 2 * nb;
                            const unsigned ni = r.u16(at);
                            in.push_back(first);
                            for (unsigned k = 1; k < ni; ++k) in.push_back(as_pos(cdi, r.u16(at + 2 * k)));
                            at += 2 * ni;
                            const unsigned na = r.u16(at);
                            for (unsigned k = 0; k < na; ++k) ahead.push_back(as_pos(cda, r.u16(at + 2 + 2 * k)));
                            at += 2 + 2 * na;
                            emit(back, in, ahead, records(r.from(at + 2), r.u16(at)));
                        } else {
                            const unsigned ni = r.u16(0), nsub = r.u16(2);
                            in.push_back(first);
                            for (unsigned k = 1; k < ni; ++k) in.push_back(as_pos(cdi, r.u16(4 + 2 * (k - 1))));
                            emit({}, in, {}, records(r.from(4 + 2 * (ni - 1)), nsub));
                        }
                    }
                }
                continue;
            }
            if (t == 8) { ++contextual; out.clear(); break; }   // reverse chaining: not supported
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
    // A contextual rule names its lookups, and a name must be defined before
    // it is used. Lookups applied ONLY through contexts are written first; a
    // lookup a feature also uses directly gets a COPY written first ("_ctx")
    // for the contexts, the original staying in the font's order (the order
    // OpenType applies a feature's lookups) -- exact either way.
    auto resolve = [&](std::vector<std::string> r, bool& bad) {
        std::vector<std::string> keep;
        for (auto l : r) {
            bool b = false;
            for (std::size_t a; (a = l.find("@L")) != std::string::npos;) {
                const std::size_t e = l.find('@', a + 2);
                const int target = std::stoi(l.substr(a + 2, e - a - 2));
                if (!rules.count(target) || rules[target].empty()) { b = true; break; }
                l.replace(a, e - a + 1, lname[target] + (used.count(target) ? "_ctx" : ""));
            }
            if (b) { bad = true; continue; }
            keep.push_back(l);
        }
        return keep;
    };
    auto block = [&](const std::string& nm, const std::vector<std::string>& r, int li) {
        text += "\nlookup " + nm + " {\n";
        if (auto f = flags.find(li); f != flags.end()) {
            std::string fl = "    lookupflag";
            if (f->second & 1) fl += " RightToLeft";
            if (f->second & 2) fl += " IgnoreBaseGlyphs";
            if (f->second & 4) fl += " IgnoreLigatures";
            if (f->second & 8) fl += " IgnoreMarks";
            text += fl + ";\n";
        }
        for (const auto& l : r) text += l + "\n";
        text += "} " + nm + ";\n";
    };
    std::set<int> dropped;
    int unresolved = 0;
    for (int pass = 0; pass < 3; ++pass)
        for (auto& [li, r] : rules) {
            const bool only_ctx = chained.count(li) && !used.count(li);
            const bool copy = chained.count(li) && used.count(li);
            if ((pass == 0 && !only_ctx) || (pass == 1 && !copy) || (pass == 2 && only_ctx)) continue;
            bool bad = false;
            auto rr = resolve(r, bad);
            if (bad) ++unresolved;
            if (rr.empty()) { if (pass != 1) dropped.insert(li); continue; }
            block(lname[li] + (pass == 1 ? "_ctx" : ""), rr, li);
        }
    if (unresolved) notes.push_back("GSUB: " + std::to_string(unresolved) + " contextual rule(s) named a lookup that couldn't be read; left out.");
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
    if (contextual) notes.push_back("GSUB: " + std::to_string(contextual) + " reverse-chaining lookup(s) not read (Granjon doesn't compile them); "
                                    "the features using only those are left out.");
    if (odd_names) notes.push_back("GSUB: " + std::to_string(odd_names) + " rule(s) skipped -- a glyph name the feature syntax can't write.");
    if (flagged) notes.push_back("GSUB: mark classes / filtering sets in the lookup flags of " + std::to_string(flagged) + " lookup(s) were not kept (the ignore flags were).");
    if (lang_only) notes.push_back("GSUB: " + std::to_string(lang_only) + " language-specific feature record(s) left out (Granjon writes the default language only).");
    if (features == 0) return {};
    notes.push_back("GSUB: " + std::to_string(features) + " feature(s) read into the feature file (Features view, Ctrl+6).");
    return text;
}

}  // namespace granjon::reader
