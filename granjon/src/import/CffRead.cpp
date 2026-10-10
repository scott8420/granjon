// reader::CffRead -- the 'CFF ' table read: INDEXes, DICTs, charset, CID
// FDArray / FDSelect, and a Type 2 charstring interpreter (CFF 1.0 and the
// Type 2 Charstring spec, Adobe TN 5176 / 5177).
#include "export/OtfData.hpp"
#include "import/ReaderParts.hpp"

#include <cmath>
#include <cstdio>
#include <functional>

namespace granjon::reader {

namespace {

struct Index {
    std::vector<Span> items;
    std::size_t end = 0;   // offset just past the INDEX, from the CFF start
};

Index read_index(Span cff, std::size_t at) {
    Index ix;
    const unsigned count = cff.u16(at);
    if (count == 0) { ix.end = at + 2; return ix; }
    const unsigned os = cff.u8(at + 2);
    if (os < 1 || os > 4) throw BadFont("a CFF INDEX with a bad offset size");
    auto off = [&](unsigned i) {
        std::size_t v = 0;
        for (unsigned k = 0; k < os; ++k) v = v << 8 | cff.u8(at + 3 + i * os + k);
        return v;
    };
    const std::size_t data = at + 3 + (static_cast<std::size_t>(count) + 1) * os - 1;
    for (unsigned i = 0; i < count; ++i) {
        const std::size_t a = off(i), b = off(i + 1);
        if (b < a) throw BadFont("a CFF INDEX runs backwards");
        ix.items.push_back(cff.sub(data + a, b - a));
    }
    ix.end = data + off(count);
    return ix;
}

using Dict = std::map<int, std::vector<double>>;

Dict read_dict(Span d) {
    Dict out;
    std::vector<double> ops;
    for (std::size_t i = 0; i < d.n;) {
        const unsigned b = d.u8(i);
        if (b <= 21) {
            int op = static_cast<int>(b);
            ++i;
            if (b == 12) { op = 1200 + d.u8(i); ++i; }
            out[op] = ops;
            ops.clear();
        } else if (b == 28) { ops.push_back(d.i16(i + 1)); i += 3; }
        else if (b == 29) { ops.push_back(d.i32(i + 1)); i += 5; }
        else if (b == 30) {   // real: BCD nibbles
            std::string s;
            ++i;
            bool done = false;
            while (!done && i < d.n) {
                const unsigned byte = d.u8(i++);
                for (unsigned nib : {byte >> 4, byte & 0xFu}) {
                    if (nib <= 9) s += static_cast<char>('0' + nib);
                    else if (nib == 0xA) s += '.';
                    else if (nib == 0xB) s += 'E';
                    else if (nib == 0xC) s += "E-";
                    else if (nib == 0xE) s += '-';
                    else if (nib == 0xF) { done = true; break; }
                }
            }
            ops.push_back(std::atof(s.c_str()));
        } else if (b >= 32 && b <= 246) { ops.push_back(static_cast<double>(b) - 139); ++i; }
        else if (b >= 247 && b <= 250) { ops.push_back((static_cast<double>(b) - 247) * 256 + d.u8(i + 1) + 108); i += 2; }
        else if (b >= 251 && b <= 254) { ops.push_back(-(static_cast<double>(b) - 251) * 256 - d.u8(i + 1) - 108); i += 2; }
        else ++i;   // reserved
    }
    return out;
}

int dict_int(const Dict& d, int op, int fallback) {
    auto it = d.find(op);
    return it == d.end() || it->second.empty() ? fallback : static_cast<int>(it->second.back());
}

int bias(std::size_t n) { return n < 1240 ? 107 : n < 33900 ? 1131 : 32768; }

// ── the Type 2 interpreter ─────────────────────────────────────────────────
struct T2 {
    const std::vector<Span>* gsubrs = nullptr;
    const std::vector<Span>* lsubrs = nullptr;
    std::vector<model::Contour> contours;
    std::vector<double> st;
    double x = 0, y = 0;
    int nstems = 0;
    bool width_done = false;
    bool ended = false;
    int depth = 0;
    std::vector<double> transient = std::vector<double>(32, 0.0);
    bool seac = false;

    void close() {
        if (contours.empty()) return;
        auto& n = contours.back().nodes;
        if (n.size() > 1 && n.back().x == n.front().x && n.back().y == n.front().y) {
            n.front().cx1 = n.back().cx1;
            n.front().cy1 = n.back().cy1;
            n.pop_back();
        }
    }
    void move(double dx, double dy) {
        close();
        x += dx; y += dy;
        model::Contour c;
        c.nodes.push_back(model::Node::corner(x, y));
        contours.push_back(c);
    }
    void need_path() { if (contours.empty()) move(0, 0); }
    void line(double dx, double dy) {
        need_path();
        x += dx; y += dy;
        contours.back().nodes.push_back(model::Node::corner(x, y));
    }
    void curve(double dx1, double dy1, double dx2, double dy2, double dx3, double dy3) {
        need_path();
        auto& prev = contours.back().nodes.back();
        const double x1 = x + dx1, y1 = y + dy1, x2 = x1 + dx2, y2 = y1 + dy2;
        x = x2 + dx3; y = y2 + dy3;
        prev.cx2 = x1; prev.cy2 = y1;
        model::Node n = model::Node::corner(x, y);
        n.cx1 = x2; n.cy1 = y2;
        contours.back().nodes.push_back(n);
    }
    // The first stack-clearing operator may carry the width first: drop it.
    void width(std::size_t expected_even_or_count, bool parity) {
        if (width_done) return;
        width_done = true;
        const bool extra = parity ? (st.size() % 2 == 1) : (st.size() > expected_even_or_count);
        if (extra && !st.empty()) st.erase(st.begin());
    }
    double arg(std::size_t i) const { if (i >= st.size()) throw BadFont("a charstring operator short of arguments"); return st[i]; }

    void run(Span cs) {
        if (++depth > 12) throw BadFont("charstring subroutines nest too deep");
        for (std::size_t i = 0; i < cs.n && !ended;) {
            const unsigned b = cs.u8(i);
            if (b == 28) { st.push_back(cs.i16(i + 1)); i += 3; continue; }
            if (b >= 32 && b <= 246) { st.push_back(static_cast<double>(b) - 139); ++i; continue; }
            if (b >= 247 && b <= 250) { st.push_back((static_cast<double>(b) - 247) * 256 + cs.u8(i + 1) + 108); i += 2; continue; }
            if (b >= 251 && b <= 254) { st.push_back(-(static_cast<double>(b) - 251) * 256 - cs.u8(i + 1) - 108); i += 2; continue; }
            if (b == 255) { st.push_back(cs.i32(i + 1) / 65536.0); i += 5; continue; }
            ++i;
            switch (b) {
                case 1: case 3: case 18: case 23:   // hstem vstem hstemhm vstemhm
                    width(0, true);
                    nstems += static_cast<int>(st.size() / 2);
                    st.clear();
                    break;
                case 19: case 20: {                // hintmask cntrmask
                    width(0, true);
                    nstems += static_cast<int>(st.size() / 2);   // implied vstems
                    st.clear();
                    i += static_cast<std::size_t>((nstems + 7) / 8);
                    break;
                }
                case 21: width(2, false); move(arg(0), arg(1)); st.clear(); break;
                case 22: width(1, false); move(arg(0), 0); st.clear(); break;
                case 4:  width(1, false); move(0, arg(0)); st.clear(); break;
                case 5:
                    for (std::size_t k = 0; k + 1 < st.size(); k += 2) line(st[k], st[k + 1]);
                    st.clear();
                    break;
                case 6: case 7: {                  // hlineto / vlineto, alternating
                    bool h = b == 6;
                    for (double v : st) { if (h) line(v, 0); else line(0, v); h = !h; }
                    st.clear();
                    break;
                }
                case 8:
                    for (std::size_t k = 0; k + 5 < st.size(); k += 6) curve(st[k], st[k + 1], st[k + 2], st[k + 3], st[k + 4], st[k + 5]);
                    st.clear();
                    break;
                case 24: {                         // rcurveline
                    std::size_t k = 0;
                    for (; k + 7 < st.size(); k += 6) curve(st[k], st[k + 1], st[k + 2], st[k + 3], st[k + 4], st[k + 5]);
                    if (k + 1 < st.size()) line(st[k], st[k + 1]);
                    st.clear();
                    break;
                }
                case 25: {                         // rlinecurve
                    std::size_t k = 0;
                    for (; k + 7 < st.size(); k += 2) line(st[k], st[k + 1]);
                    if (k + 5 < st.size()) curve(st[k], st[k + 1], st[k + 2], st[k + 3], st[k + 4], st[k + 5]);
                    st.clear();
                    break;
                }
                case 26: {                         // vvcurveto
                    std::size_t k = 0;
                    double dx1 = 0;
                    if (st.size() % 2 == 1) dx1 = st[k++];
                    for (; k + 3 < st.size(); k += 4) { curve(dx1, st[k], st[k + 1], st[k + 2], 0, st[k + 3]); dx1 = 0; }
                    st.clear();
                    break;
                }
                case 27: {                         // hhcurveto
                    std::size_t k = 0;
                    double dy1 = 0;
                    if (st.size() % 2 == 1) dy1 = st[k++];
                    for (; k + 3 < st.size(); k += 4) { curve(st[k], dy1, st[k + 1], st[k + 2], st[k + 3], 0); dy1 = 0; }
                    st.clear();
                    break;
                }
                case 30: case 31: {                // vhcurveto / hvcurveto, alternating
                    bool h = b == 31;
                    std::size_t k = 0;
                    while (st.size() - k >= 4) {
                        const double df = st.size() - k == 5 ? st[k + 4] : 0;   // the last curve's extra arg
                        if (h) curve(st[k], 0, st[k + 1], st[k + 2], df, st[k + 3]);
                        else   curve(0, st[k], st[k + 1], st[k + 2], st[k + 3], df);
                        h = !h;
                        k += 4;
                    }
                    st.clear();
                    break;
                }
                case 10: case 29: {                // callsubr / callgsubr
                    const auto* subrs = b == 10 ? lsubrs : gsubrs;
                    if (!subrs || st.empty()) throw BadFont("a call to a missing subroutine");
                    const int idx = static_cast<int>(st.back()) + bias(subrs->size());
                    st.pop_back();
                    if (idx < 0 || idx >= static_cast<int>(subrs->size())) throw BadFont("a subroutine index out of range");
                    run((*subrs)[static_cast<std::size_t>(idx)]);
                    break;
                }
                case 11: --depth; return;          // return
                case 14:                           // endchar (4 more args = seac)
                    width(st.size() >= 4 ? 4 : 0, false);
                    if (st.size() >= 4) seac = true;
                    close();
                    ended = true;
                    st.clear();
                    break;
                case 12: {
                    const unsigned e = cs.u8(i++);
                    switch (e) {
                        case 35:   // flex
                            curve(arg(0), arg(1), arg(2), arg(3), arg(4), arg(5));
                            curve(arg(6), arg(7), arg(8), arg(9), arg(10), arg(11));
                            break;
                        case 34:   // hflex: dx1 dx2 dy2 dx3 dx4 dx5 dx6
                            curve(arg(0), 0, arg(1), arg(2), arg(3), 0);
                            curve(arg(4), 0, arg(5), -arg(2), arg(6), 0);
                            break;
                        case 36: { // hflex1: dx1 dy1 dx2 dy2 dx3 dx4 dx5 dy5 dx6 -- back to the starting y
                            const double y0 = y;
                            curve(arg(0), arg(1), arg(2), arg(3), arg(4), 0);
                            curve(arg(5), 0, arg(6), arg(7), arg(8), y0 - (y + arg(7)));
                            break;
                        }
                        case 37: { // flex1
                            const double x0 = x, y0 = y;
                            double dx = 0, dy = 0;
                            for (int k = 0; k < 10; k += 2) { dx += arg(static_cast<std::size_t>(k)); dy += arg(static_cast<std::size_t>(k + 1)); }
                            curve(arg(0), arg(1), arg(2), arg(3), arg(4), arg(5));
                            const double x5 = x + arg(6) + arg(8), y5 = y + arg(7) + arg(9);
                            double dx6, dy6;
                            if (std::abs(dx) > std::abs(dy)) { dx6 = arg(10); dy6 = y0 - y5; }
                            else { dx6 = x0 - x5; dy6 = arg(10); }
                            curve(arg(6), arg(7), arg(8), arg(9), dx6, dy6);
                            break;
                        }
                        // arithmetic and storage (rare in real fonts)
                        case 3:  { const double b2 = arg(st.size() - 1), a2 = arg(st.size() - 2); st.resize(st.size() - 2); st.push_back(a2 != 0 && b2 != 0); break; }   // and
                        case 4:  { const double b2 = arg(st.size() - 1), a2 = arg(st.size() - 2); st.resize(st.size() - 2); st.push_back(a2 != 0 || b2 != 0); break; }   // or
                        case 5:  { const double a2 = arg(st.size() - 1); st.back() = a2 == 0; break; }        // not
                        case 9:  { st.back() = std::abs(arg(st.size() - 1)); break; }                         // abs
                        case 10: { const double b2 = arg(st.size() - 1); st.pop_back(); st.back() += b2; break; }  // add
                        case 11: { const double b2 = arg(st.size() - 1); st.pop_back(); st.back() -= b2; break; }  // sub
                        case 12: { const double b2 = arg(st.size() - 1); st.pop_back(); st.back() = b2 != 0 ? st.back() / b2 : 0; break; } // div
                        case 14: { st.back() = -arg(st.size() - 1); break; }                                  // neg
                        case 15: { const double b2 = arg(st.size() - 1); st.pop_back(); st.back() = st.back() == b2; break; } // eq
                        case 18: { if (!st.empty()) st.pop_back(); break; }                                   // drop
                        case 20: { const int ix = static_cast<int>(arg(st.size() - 1)); const double v = arg(st.size() - 2); st.resize(st.size() - 2); if (ix >= 0 && ix < 32) transient[static_cast<std::size_t>(ix)] = v; break; } // put
                        case 21: { const int ix = static_cast<int>(arg(st.size() - 1)); st.back() = (ix >= 0 && ix < 32) ? transient[static_cast<std::size_t>(ix)] : 0; break; } // get
                        case 22: { const double v2 = arg(st.size() - 1), v1 = arg(st.size() - 2), s2 = arg(st.size() - 3), s1 = arg(st.size() - 4); st.resize(st.size() - 4); st.push_back(v1 <= v2 ? s1 : s2); break; } // ifelse
                        case 23: { st.push_back(0.5); break; }                                                // random
                        case 24: { const double b2 = arg(st.size() - 1); st.pop_back(); st.back() *= b2; break; } // mul
                        case 26: { st.back() = std::sqrt(std::max(0.0, arg(st.size() - 1))); break; }        // sqrt
                        case 27: { st.push_back(arg(st.size() - 1)); break; }                                 // dup
                        case 28: { std::swap(st[st.size() - 1], st[st.size() - 2]); break; }                  // exch
                        case 29: { const int ix = static_cast<int>(arg(st.size() - 1)); st.pop_back();        // index
                                   const int k2 = ix < 0 ? 0 : ix; st.push_back(arg(st.size() - 1 - static_cast<std::size_t>(std::min<int>(k2, static_cast<int>(st.size()) - 1)))); break; }
                        case 30: { const int j = static_cast<int>(arg(st.size() - 1)), nn = static_cast<int>(arg(st.size() - 2)); // roll
                                   st.resize(st.size() - 2);
                                   if (nn > 0 && nn <= static_cast<int>(st.size())) {
                                       const auto first = st.end() - nn;
                                       const int sh = ((j % nn) + nn) % nn;
                                       std::rotate(first, st.end() - sh, st.end());
                                   }
                                   break; }
                        default: st.clear(); break;
                    }
                    if (e == 34 || e == 35 || e == 36 || e == 37) st.clear();
                    break;
                }
                default:   // reserved: clear and go on
                    st.clear();
                    break;
            }
        }
        --depth;
    }
};

}  // namespace

CffResult read_cff(Span cff) {
    CffResult r;
    const unsigned hdr = cff.u8(2);
    const Index names = read_index(cff, hdr);
    const Index top = read_index(cff, names.end);
    const Index strings = read_index(cff, top.end);
    const Index gsubrs = read_index(cff, strings.end);
    if (top.items.empty()) throw BadFont("the CFF has no Top DICT");
    if (names.items.size() > 1) r.notes.push_back("The CFF holds " + std::to_string(names.items.size()) + " fonts; the first was read.");
    const Dict td = read_dict(top.items[0]);
    if (dict_int(td, 1206, 2) != 2) throw BadFont("charstrings are not Type 2");
    if (!td.count(17)) throw BadFont("the CFF has no CharStrings");
    const Index cs = read_index(cff, static_cast<std::size_t>(dict_int(td, 17, 0)));
    const std::size_t n = cs.items.size();

    auto sid_name = [&](int sid) -> std::string {
        if (sid >= 0 && sid < otf::kCffStandardCount) return otf::kCffStandardStrings[sid];
        const std::size_t k = static_cast<std::size_t>(sid - otf::kCffStandardCount);
        if (k < strings.items.size()) return std::string(reinterpret_cast<const char*>(strings.items[k].p), strings.items[k].n);
        return {};
    };

    // charset -> SIDs (or CIDs) per GID
    const bool cid = td.count(1230) > 0;   // ROS
    std::vector<int> ids(n, 0);
    const int cs_off = dict_int(td, 15, 0);
    if (cs_off == 0) {   // ISOAdobe: SIDs 1.. in order
        for (std::size_t g = 1; g < n; ++g) ids[g] = static_cast<int>(g);
    } else if (cs_off == 1 || cs_off == 2) {
        r.notes.push_back("An Expert charset: glyph names were made up.");
        for (std::size_t g = 1; g < n; ++g) ids[g] = -1;
    } else {
        const Span c = cff.from(static_cast<std::size_t>(cs_off));
        const unsigned fmt = c.u8(0);
        std::size_t g = 1, at = 1;
        if (fmt == 0) {
            for (; g < n; ++g, at += 2) ids[g] = static_cast<int>(c.u16(at));
        } else if (fmt == 1 || fmt == 2) {
            while (g < n) {
                const int first = static_cast<int>(c.u16(at));
                const int left = fmt == 1 ? c.u8(at + 2) : static_cast<int>(c.u16(at + 2));
                at += fmt == 1 ? 3 : 4;
                for (int k = 0; k <= left && g < n; ++k, ++g) ids[g] = first + k;
            }
        } else {
            throw BadFont("an unknown CFF charset format");
        }
    }
    r.names.resize(n);
    for (std::size_t g = 0; g < n; ++g) {
        if (g == 0) { r.names[g] = ".notdef"; continue; }
        if (ids[g] < 0) continue;
        if (cid) {
            char buf[16];
            std::snprintf(buf, sizeof buf, "cid%05d", ids[g]);
            r.names[g] = buf;
        } else {
            r.names[g] = sid_name(ids[g]);
        }
    }

    // Private DICTs and local subrs: one, or one per FD (CID-keyed).
    struct Priv { std::vector<Span> subrs; };
    auto read_private = [&](const Dict& d) {
        Priv p;
        auto it = d.find(18);
        if (it == d.end() || it->second.size() < 2) return p;
        const std::size_t size = static_cast<std::size_t>(it->second[0]), off = static_cast<std::size_t>(it->second[1]);
        const Dict pd = read_dict(cff.sub(off, size));
        if (pd.count(19)) p.subrs = read_index(cff, off + static_cast<std::size_t>(dict_int(pd, 19, 0))).items;
        return p;
    };
    std::vector<Priv> fds;
    std::vector<int> fd_of(n, 0);
    if (cid) {
        if (!td.count(1236)) throw BadFont("a CID font with no FDArray");
        const Index fda = read_index(cff, static_cast<std::size_t>(dict_int(td, 1236, 0)));
        for (const auto& fd : fda.items) fds.push_back(read_private(read_dict(fd)));
        if (td.count(1237)) {
            const Span s = cff.from(static_cast<std::size_t>(dict_int(td, 1237, 0)));
            const unsigned fmt = s.u8(0);
            if (fmt == 0) {
                for (std::size_t g = 0; g < n; ++g) fd_of[g] = s.u8(1 + g);
            } else if (fmt == 3) {
                const unsigned nr = s.u16(1);
                for (unsigned k = 0; k < nr; ++k) {
                    const unsigned first = s.u16(3 + 3 * k), fd = s.u8(5 + 3 * k);
                    const unsigned next = s.u16(3 + 3 * (k + 1));
                    for (unsigned g = first; g < next && g < n; ++g) fd_of[g] = static_cast<int>(fd);
                }
            } else {
                throw BadFont("an unknown FDSelect format");
            }
        }
        r.notes.push_back("A CID-keyed CFF: glyph names are cidNNNNN.");
    } else {
        fds.push_back(read_private(td));
    }

    int seacs = 0;
    for (std::size_t g = 0; g < n; ++g) {
        T2 t;
        t.gsubrs = &gsubrs.items;
        const std::size_t fd = static_cast<std::size_t>(fd_of[g]);
        t.lsubrs = fd < fds.size() ? &fds[fd].subrs : nullptr;
        t.run(cs.items[g]);
        t.close();
        if (t.seac) ++seacs;
        r.glyphs.push_back(std::move(t.contours));
    }
    if (seacs) r.notes.push_back(std::to_string(seacs) + " glyph(s) used the old seac accent operator; their accents were not built.");
    return r;
}

}  // namespace granjon::reader
