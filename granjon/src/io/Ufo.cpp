#include "io/Ufo.hpp"

#include "io/Glif.hpp"
#include "io/Plist.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace granjon::io {

namespace fs = std::filesystem;
using model::Font;
using model::Record;

namespace {

bool write_file(const fs::path& p, const std::string& data, std::string* err) {
    std::ofstream o(p, std::ios::binary | std::ios::trunc);
    if (!o) { if (err) *err = "cannot write " + p.string(); return false; }
    o << data;
    o.close();
    if (!o) { if (err) *err = "write failed for " + p.string(); return false; }
    return true;
}

std::optional<std::string> read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return std::nullopt;
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

}  // namespace

std::string glif_file_name(const std::string& glyph, const std::vector<std::string>& taken_lower) {
    static const std::set<std::string> reserved = {"con", "prn", "aux", "clock$", "nul", "a:-z:", "com1",
        "lpt1", "lpt2", "lpt3", "com2", "com3", "com4"};
    const std::string illegal = "\"*+/:<>?[\\]|";
    std::string out;
    for (std::size_t i = 0; i < glyph.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(glyph[i]);
        if (c < 0x20 || c == 0x7F || illegal.find(static_cast<char>(c)) != std::string::npos) out += '_';
        else if (i == 0 && c == '.') out += '_';
        else if (std::isupper(c)) { out += static_cast<char>(c); out += '_'; }
        else out += static_cast<char>(c);
    }
    // Reserved names, part by part (".": "con.alt" -> "_con.alt").
    std::string rebuilt, part;
    std::stringstream ss(out);
    bool first = true;
    while (std::getline(ss, part, '.')) {
        if (reserved.count(lower(part))) part = "_" + part;
        rebuilt += (first ? "" : ".") + part;
        first = false;
    }
    if (!out.empty() && out.back() == '.') rebuilt += '.';
    out = rebuilt.substr(0, 255 - 5);
    std::string name = out + ".glif";
    for (int n = 1; std::find(taken_lower.begin(), taken_lower.end(), lower(name)) != taken_lower.end(); ++n)
        name = out + std::to_string(n) + ".glif";
    return name;
}

// ── save ───────────────────────────────────────────────────────────────────

bool save_ufo(const Font& f, const std::string& path_str, std::string* err) {
    const fs::path path = fs::absolute(path_str);
    const fs::path tmp  = path.string() + ".saving";
    const fs::path old  = path.string() + ".previous";
    std::error_code ec;
    fs::remove_all(tmp, ec);
    if (!fs::create_directories(tmp / "glyphs", ec)) {
        if (err) *err = "cannot create " + tmp.string() + ": " + ec.message();
        return false;
    }

    // metainfo
    PValue meta = PValue::dictionary();
    meta.set("creator", PValue::str("io.github.granjon.Granjon"));
    meta.set("formatVersion", PValue::integer(3));
    if (!write_file(tmp / "metainfo.plist", write_plist(meta), err)) return false;

    // fontinfo
    const auto& info = f.info();
    const auto& m = f.metrics();
    PValue fi = PValue::dictionary();
    fi.set("familyName", PValue::str(info.family));
    fi.set("styleName", PValue::str(info.style));
    fi.set("versionMajor", PValue::integer(info.version_major));
    fi.set("versionMinor", PValue::integer(info.version_minor));
    if (!info.copyright.empty()) fi.set("copyright", PValue::str(info.copyright));
    if (!info.license.empty())   fi.set("openTypeNameLicense", PValue::str(info.license));
    if (!info.vendor.empty())    fi.set("openTypeOS2VendorID", PValue::str(info.vendor));
    fi.set("unitsPerEm", PValue::integer(info.units_per_em));
    fi.set("ascender", PValue::integer(m.ascender));
    fi.set("descender", PValue::integer(m.descender));
    fi.set("xHeight", PValue::integer(m.x_height));
    fi.set("capHeight", PValue::integer(m.cap_height));
    if (!info.guides.empty()) {
        PValue arr = PValue::array();
        for (const auto& gd : info.guides) {
            PValue d = PValue::dictionary();
            if (!gd.horizontal()) d.set("x", PValue::real(gd.x));
            if (!gd.vertical())   d.set("y", PValue::real(gd.y));
            if (!gd.horizontal() && !gd.vertical()) d.set("angle", PValue::real(gd.angle));
            if (!gd.name.empty()) d.set("name", PValue::str(gd.name));
            arr.arr.push_back(std::move(d));
        }
        fi.set("guidelines", std::move(arr));
    }
    if (!write_file(tmp / "fontinfo.plist", write_plist(fi), err)) return false;

    // groups + kerning
    const auto& k = f.kerning();
    if (!k.groups.empty()) {
        PValue g = PValue::dictionary();
        for (const auto& [name, members] : k.groups) {
            PValue a = PValue::array();
            for (const auto& mbr : members) a.arr.push_back(PValue::str(mbr));
            g.set(name, std::move(a));
        }
        if (!write_file(tmp / "groups.plist", write_plist(g), err)) return false;
    }
    if (!k.pairs.empty()) {
        PValue kern = PValue::dictionary();
        for (const auto& [pair, value] : k.pairs) {
            PValue* row = nullptr;
            for (auto& [first, v] : kern.dict) if (first == pair.first) row = &v;
            if (!row) { kern.set(pair.first, PValue::dictionary()); row = &kern.dict.back().second; }
            row->set(pair.second, PValue::integer(value));
        }
        if (!write_file(tmp / "kerning.plist", write_plist(kern), err)) return false;
    }

    // features
    if (!f.features().text.empty())
        if (!write_file(tmp / "features.fea", f.features().text, err)) return false;

    // lib
    PValue lib = PValue::dictionary();
    PValue order = PValue::array();
    const auto glyph_order = f.glyph_order();
    for (const auto& n : glyph_order) order.arr.push_back(PValue::str(n));
    lib.set("public.glyphOrder", std::move(order));
    lib.set("granjon.overshoot", PValue::integer(m.overshoot));
    if (!f.metrics_keys().keys.empty()) {
        PValue mk = PValue::dictionary();
        for (const auto& [glyph, key] : f.metrics_keys().keys) {
            PValue e = PValue::dictionary();
            if (!key.left.empty())  e.set("left", PValue::str(key.left));
            if (!key.right.empty()) e.set("right", PValue::str(key.right));
            if (!key.width.empty()) e.set("width", PValue::str(key.width));
            mk.set(glyph, std::move(e));
        }
        lib.set("granjon.metricsKeys", std::move(mk));
    }
    if (!f.skips().keys.empty()) {
        PValue sk = PValue::array();
        for (const auto& k : f.skips().keys) sk.arr.push_back(PValue::str(k));
        lib.set("granjon.checkSkips", std::move(sk));
    }
    if (!write_file(tmp / "lib.plist", write_plist(lib), err)) return false;

    // layers
    PValue layers = PValue::array();
    PValue def = PValue::array();
    def.arr = {PValue::str("public.default"), PValue::str("glyphs")};
    layers.arr.push_back(std::move(def));
    if (!write_file(tmp / "layercontents.plist", write_plist(layers), err)) return false;

    // glyphs -- file names assigned in sorted-name order, so they are stable.
    std::vector<std::string> names = glyph_order;
    std::sort(names.begin(), names.end());
    std::vector<std::string> taken;
    PValue contents = PValue::dictionary();
    for (const auto& n : names) {
        const std::string file = glif_file_name(n, taken);
        taken.push_back(lower(file));
        contents.set(n, PValue::str(file));
        if (!write_file(tmp / "glyphs" / file, write_glif(*f.glyph(n)), err)) return false;
    }
    if (!write_file(tmp / "glyphs" / "contents.plist", write_plist(contents), err)) return false;

    // Swap in: target -> .previous, new -> target, drop .previous.
    fs::remove_all(old, ec);
    if (fs::exists(path)) {
        fs::rename(path, old, ec);
        if (ec) { if (err) *err = "cannot replace " + path.string() + ": " + ec.message(); return false; }
    }
    fs::rename(tmp, path, ec);
    if (ec) {
        if (fs::exists(old)) fs::rename(old, path);
        if (err) *err = "cannot move the saved font into place: " + ec.message();
        return false;
    }
    fs::remove_all(old, ec);
    return true;
}

// ── load ───────────────────────────────────────────────────────────────────

bool load_ufo(const std::string& path_str, Font& into, std::string* err) {
    const fs::path path = path_str;
    const auto fail = [&](const std::string& m) { if (err) *err = m; return false; };
    if (!fs::is_directory(path))
        return fail(path.string() + " is not a folder. A UFO / .granjon font is a folder of files.");
    std::string e;

    auto meta = read_plist_file((path / "metainfo.plist").string(), &e);
    if (!meta) return fail(e + "  (every UFO has metainfo.plist -- is this a font folder?)");
    const auto version = meta->get_number("formatVersion");
    if (!version || (*version != 2 && *version != 3))
        return fail("metainfo.plist: formatVersion must be 2 or 3 (UFO 2 / UFO 3).");

    model::FontInfo info;
    model::Metrics  metrics;
    if (fs::exists(path / "fontinfo.plist")) {
        auto fi = read_plist_file((path / "fontinfo.plist").string(), &e);
        if (!fi) return fail(e);
        info.family    = fi->get_string("familyName").value_or(info.family);
        info.style     = fi->get_string("styleName").value_or(info.style);
        info.version_major = static_cast<int>(fi->get_number("versionMajor").value_or(info.version_major));
        info.version_minor = static_cast<int>(fi->get_number("versionMinor").value_or(info.version_minor));
        info.copyright = fi->get_string("copyright").value_or("");
        info.license   = fi->get_string("openTypeNameLicense").value_or("");
        info.vendor    = fi->get_string("openTypeOS2VendorID").value_or("");
        info.units_per_em   = static_cast<int>(fi->get_number("unitsPerEm").value_or(info.units_per_em));
        metrics.ascender    = static_cast<int>(fi->get_number("ascender").value_or(metrics.ascender));
        metrics.descender   = static_cast<int>(fi->get_number("descender").value_or(metrics.descender));
        metrics.x_height    = static_cast<int>(fi->get_number("xHeight").value_or(metrics.x_height));
        metrics.cap_height  = static_cast<int>(fi->get_number("capHeight").value_or(metrics.cap_height));
        if (const PValue* gl = fi->get("guidelines"); gl && gl->kind == PValue::Kind::Array)
            for (const auto& d : gl->arr) {
                if (d.kind != PValue::Kind::Dict) continue;
                model::Guide gd;
                const auto x = d.get_number("x"), y = d.get_number("y"), a = d.get_number("angle");
                gd.x = x.value_or(0);
                gd.y = y.value_or(0);
                gd.angle = a ? *a : (x && !y ? 90.0 : 0.0);
                gd.name = d.get_string("name").value_or("");
                info.guides.push_back(gd);
            }
    }

    model::Kerning kerning;
    if (fs::exists(path / "groups.plist")) {
        auto g = read_plist_file((path / "groups.plist").string(), &e);
        if (!g) return fail(e);
        for (const auto& [name, v] : g->dict) {
            std::vector<std::string> members;
            for (const auto& mbr : v.arr) members.push_back(mbr.s);
            kerning.groups[name] = std::move(members);
        }
    }
    if (fs::exists(path / "kerning.plist")) {
        auto kp = read_plist_file((path / "kerning.plist").string(), &e);
        if (!kp) return fail(e);
        for (const auto& [first, row] : kp->dict)
            for (const auto& [second, v] : row.dict)
                kerning.pairs[{first, second}] = static_cast<int>(v.number());
    }

    model::Features features;
    if (auto fea = read_file(path / "features.fea")) features.text = *fea;

    model::MetricsKeys keys;
    model::CheckSkips skips;
    std::vector<std::string> order;
    if (fs::exists(path / "lib.plist")) {
        auto lib = read_plist_file((path / "lib.plist").string(), &e);
        if (!lib) return fail(e);
        if (const PValue* go = lib->get("public.glyphOrder"))
            for (const auto& v : go->arr) order.push_back(v.s);
        if (auto os = lib->get_number("granjon.overshoot")) metrics.overshoot = static_cast<int>(*os);
        if (const PValue* mk = lib->get("granjon.metricsKeys"))
            for (const auto& [glyph, v] : mk->dict)
                keys.keys[glyph] = {v.get_string("left").value_or(""), v.get_string("right").value_or(""),
                                    v.get_string("width").value_or("")};
        if (const PValue* sk = lib->get("granjon.checkSkips"))
            for (const auto& v : sk->arr) skips.keys.insert(v.s);
    }

    // The default layer's folder: layercontents (UFO 3), else "glyphs".
    fs::path glyph_dir = path / "glyphs";
    if (fs::exists(path / "layercontents.plist")) {
        auto lc = read_plist_file((path / "layercontents.plist").string(), &e);
        if (!lc) return fail(e);
        for (const auto& layer : lc->arr)
            if (layer.arr.size() == 2 && layer.arr[0].s == "public.default") glyph_dir = path / layer.arr[1].s;
    }
    auto contents = read_plist_file((glyph_dir / "contents.plist").string(), &e);
    if (!contents) return fail(e);

    std::map<std::string, model::Glyph> glyphs;
    for (const auto& [name, file] : contents->dict) {
        const fs::path gp = glyph_dir / file.s;
        auto xml = read_file(gp);
        if (!xml) return fail("contents.plist names " + file.s + " for " + name + ", but the file is missing.");
        auto g = read_glif(*xml, &e);
        if (!g) return fail(gp.string() + ": " + e);
        if (g->name != name)
            return fail(gp.string() + ": the file says its glyph is \"" + g->name + "\" but contents.plist says \"" +
                        name + "\".");
        glyphs[name] = std::move(*g);
    }

    // Order: public.glyphOrder first, then any glyph it omits, by name.
    double n = 1;
    std::set<std::string> placed;
    for (const auto& name : order)
        if (auto it = glyphs.find(name); it != glyphs.end() && !placed.count(name)) {
            it->second.order = n++;
            placed.insert(name);
        }
    for (auto& [name, g] : glyphs)
        if (!placed.count(name)) g.order = n++;

    // Everything read: now replace the open font, as one load.
    Font::Loading load(into);
    for (const auto& [id, r] : into.snapshot())
        if (id.empty() || id[0] != '@') into.put(id, std::nullopt);
    into.put(Font::kInfo, Record{info});
    into.put(Font::kMetrics, Record{metrics});
    into.put(Font::kKerning, Record{kerning});
    into.put(Font::kMetricsKeys, Record{keys});
    into.put(Font::kSkips, Record{skips});
    into.put(Font::kFeatures, Record{features});
    for (auto& [name, g] : glyphs) into.put(name, Record{g});
    return true;
}

}  // namespace granjon::io
