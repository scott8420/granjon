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
    }
    if (o.is_object()) {
        get(o, "cell", s.overview_cell);
        get(o, "charset", s.overview_charset);
    }
    // Ranges and vocabularies: out of bounds -> the default.
    if (s.overview_cell < kOverviewCellMin || s.overview_cell > kOverviewCellMax) s.overview_cell = d.overview_cell;
    if (s.sidebearing_mod != "alt" && s.sidebearing_mod != "ctrl" && s.sidebearing_mod != "shift")
        s.sidebearing_mod = d.sidebearing_mod;
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
                    {"sidebearing_modifier", s.sidebearing_mod}}},
        {"overview", {{"cell", s.overview_cell}, {"charset", s.overview_charset}}},
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
