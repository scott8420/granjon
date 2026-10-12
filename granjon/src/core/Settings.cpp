#include "core/Settings.hpp"

#include "json.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace granjon::core {

namespace {
using nlohmann::json;

// One reader per type: the key's value if it is there and the right type,
// else the default already in `out`.
void get(const json& j, const char* key, bool& out) {
    if (j.contains(key) && j[key].is_boolean()) out = j[key].get<bool>();
}
void get(const json& j, const char* key, int& out) {
    if (j.contains(key) && j[key].is_number_integer()) out = j[key].get<int>();
}
void get(const json& j, const char* key, std::string& out) {
    if (j.contains(key) && j[key].is_string()) out = j[key].get<std::string>();
}
void get(const json& j, const char* key, std::vector<std::string>& out) {
    if (!j.contains(key) || !j[key].is_array()) return;
    out.clear();
    for (const auto& e : j[key])
        if (e.is_string() && std::find(out.begin(), out.end(), e.get<std::string>()) == out.end())
            out.push_back(e.get<std::string>());
}
}  // namespace

Settings load_settings(const std::string& file) {
    Settings s;
    std::ifstream f(file);
    if (!f) return s;   // first run
    json j;
    try { f >> j; } catch (const std::exception&) { return Settings{}; }
    if (!j.is_object()) return Settings{};
    const Settings d;

    const json g = j.value("general", json::object());
    const json e = j.value("editor", json::object());
    const json o = j.value("overview", json::object());
    const json n = j.value("navigator", json::object());
    const json sp = j.value("spacing", json::object());
    if (sp.is_object()) {
        get(sp, "text", s.spacing_text);
        get(sp, "size", s.spacing_size);
    }
    if (n.is_object()) get(n, "open", s.navigator_open);
    const json pv = j.value("preview", json::object());
    if (pv.is_object()) {
        get(pv, "text", s.preview_text);
        get(pv, "kern", s.preview_kern);
        get(pv, "invert", s.preview_invert);
    }
    const json ex = j.value("export", json::object());
    if (ex.is_object()) get(ex, "folder", s.export_folder);
    if (const json in = j.value("inspector", json::object()); in.is_object()) get(in, "code_format", s.code_format);
    if (const json nf = j.value("new_font", json::object()); nf.is_object()) {
        get(nf, "preset", s.new_preset);
        get(nf, "create_set", s.new_create_set);
    }
    if (s.code_format != "cff" && s.code_format != "truetype" && s.code_format != "svg") s.code_format = Settings{}.code_format;
    if (g.is_object()) {
        get(g, "reopen_last", s.reopen_last);
        get(g, "last_font", s.last_font);
        get(g, "open_folder", s.open_folder);
    }
    if (e.is_object()) {
        get(e, "fill", s.editor_fill);
        get(e, "context", s.editor_context);
        get(e, "context_upper", s.context_upper);
        get(e, "context_lower", s.context_lower);
        get(e, "sidebearing_modifier", s.sidebearing_mod);
        get(e, "grid", s.editor_grid);
        get(e, "snap", s.editor_snap);
        get(e, "align", s.editor_align);
        get(e, "image", s.editor_image);
        get(e, "image_tint", s.image_tint);
        get(e, "image_opacity", s.image_opacity);
        get(e, "line_width", s.line_width);
        get(e, "corner_radius", s.corner_radius);
        get(e, "trap_width", s.trap_width);
        get(e, "trap_depth", s.trap_depth);
        get(e, "offset_distance", s.offset_distance);
        get(e, "offset_join", s.offset_join);
        get(e, "stroke_cap", s.stroke_cap);
        get(e, "handles_relative", s.handles_relative);
    }
    if (o.is_object()) {
        get(o, "cell", s.overview_cell);
        get(o, "charset", s.overview_charset);
    }
    // Ranges and vocabularies: out of bounds -> the default.
    if (s.image_opacity < 5 || s.image_opacity > 100) s.image_opacity = d.image_opacity;
    if (s.line_width < 0 || s.line_width > 10000) s.line_width = d.line_width;
    if (s.corner_radius < 0 || s.corner_radius > 10000) s.corner_radius = d.corner_radius;
    if (s.trap_width < 0 || s.trap_width > 10000) s.trap_width = d.trap_width;
    if (s.trap_depth < 0 || s.trap_depth > 10000) s.trap_depth = d.trap_depth;
    if (s.offset_distance < 0 || s.offset_distance > 10000) s.offset_distance = d.offset_distance;
    if (s.offset_join < 0 || s.offset_join > 2) s.offset_join = d.offset_join;
    if (s.stroke_cap < 0 || s.stroke_cap > 2) s.stroke_cap = d.stroke_cap;
    {
        const auto& t = s.image_tint;
        const bool hex = t.size() == 7 && t[0] == '#' &&
                         t.find_first_not_of("0123456789abcdefABCDEF", 1) == std::string::npos;
        if (!hex) s.image_tint = d.image_tint;
    }
    if (s.overview_cell < kOverviewCellMin || s.overview_cell > kOverviewCellMax) s.overview_cell = d.overview_cell;
    if (s.sidebearing_mod != "alt" && s.sidebearing_mod != "ctrl" && s.sidebearing_mod != "shift")
        s.sidebearing_mod = d.sidebearing_mod;
    if (s.spacing_size < kSpacingSizeMin || s.spacing_size > kSpacingSizeMax) s.spacing_size = d.spacing_size;
    if (s.context_upper.empty()) s.context_upper = d.context_upper;
    if (s.context_lower.empty()) s.context_lower = d.context_lower;
    return s;
}

bool save_settings(const std::string& file, const Settings& s) {
    json j = {
        {"general", {{"reopen_last", s.reopen_last}, {"last_font", s.last_font}, {"open_folder", s.open_folder}}},
        {"editor", {{"fill", s.editor_fill},
                    {"context", s.editor_context},
                    {"context_upper", s.context_upper},
                    {"context_lower", s.context_lower},
                    {"sidebearing_modifier", s.sidebearing_mod},
                    {"grid", s.editor_grid},
                    {"snap", s.editor_snap},
                    {"align", s.editor_align},
                    {"image", s.editor_image},
                    {"image_tint", s.image_tint},
                    {"image_opacity", s.image_opacity},
                    {"line_width", s.line_width},
                    {"corner_radius", s.corner_radius},
                    {"trap_width", s.trap_width},
                    {"trap_depth", s.trap_depth},
                    {"offset_distance", s.offset_distance},
                    {"offset_join", s.offset_join},
                    {"stroke_cap", s.stroke_cap},
                    {"handles_relative", s.handles_relative}}},
        {"overview", {{"cell", s.overview_cell}, {"charset", s.overview_charset}}},
        {"navigator", {{"open", s.navigator_open}}},
        {"spacing", {{"text", s.spacing_text}, {"size", s.spacing_size}}},
        {"preview", {{"text", s.preview_text}, {"kern", s.preview_kern}, {"invert", s.preview_invert}}},
        {"export", {{"folder", s.export_folder}}},
        {"inspector", {{"code_format", s.code_format}}},
        {"new_font", {{"preset", s.new_preset}, {"create_set", s.new_create_set}}},
    };
    std::error_code ec;
    const auto dir = std::filesystem::path(file).parent_path();
    if (!dir.empty()) std::filesystem::create_directories(dir, ec);
    // Write beside, then rename: a crash mid-write never leaves half a file.
    const std::string tmp = file + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f) return false;
        f << j.dump(2) << '\n';
        if (!f) return false;
    }
    std::filesystem::rename(tmp, file, ec);
    return !ec;
}

}  // namespace granjon::core
