#include "core/Primer.hpp"

#include "unicode/Embedded.hpp"

#include <algorithm>
#include <array>

namespace granjon::core {

namespace {

std::string trim(std::string_view s) {
    const auto a = s.find_first_not_of(" \t\r");
    if (a == std::string_view::npos) return {};
    const auto b = s.find_last_not_of(" \t\r");
    return std::string(s.substr(a, b - a + 1));
}

bool starts(const std::string& s, std::string_view p) { return s.rfind(p, 0) == 0; }

}  // namespace

std::vector<PrimerNote> parse_primer(std::string_view text, std::vector<std::string>* errors) {
    std::vector<PrimerNote> out;
    PrimerNote* cur = nullptr;
    std::string para;
    int line_no = 0;
    const auto err = [&](const std::string& m) {
        if (errors) errors->push_back("line " + std::to_string(line_no) + ": " + m);
    };
    const auto flush = [&] {
        if (cur && !para.empty()) cur->paragraphs.push_back(para);
        para.clear();
    };
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const auto nl = text.find('\n', pos);
        const std::string_view raw = text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
        pos = nl == std::string_view::npos ? text.size() + 1 : nl + 1;
        ++line_no;
        const std::string line = trim(raw);
        if (!line.empty() && line[0] == '#') continue;
        if (line.empty()) { flush(); continue; }
        if (line.front() == '[' && line.back() == ']') {
            flush();
            out.push_back({});
            cur = &out.back();
            cur->id = line.substr(1, line.size() - 2);
            continue;
        }
        if (!cur) { err("text before any [note]"); continue; }
        if (para.empty() && starts(line, "title:"))  { cur->title = trim(std::string_view(line).substr(6)); continue; }
        if (para.empty() && starts(line, "figure:")) { cur->figure = trim(std::string_view(line).substr(7)); continue; }
        if (para.empty() && starts(line, "link:")) {
            const std::string v = trim(std::string_view(line).substr(5));
            const auto bar = v.find('|');
            if (bar == std::string::npos) { err("link: Label | target"); continue; }
            PrimerLink l{trim(std::string_view(v).substr(0, bar)), trim(std::string_view(v).substr(bar + 1))};
            if (l.label.empty() || l.target.empty()) { err("link: Label | target"); continue; }
            cur->links.push_back(std::move(l));
            continue;
        }
        if (!para.empty()) para += ' ';
        para += line;
    }
    flush();
    for (const auto& n : out)
        if (n.title.empty() && errors) errors->push_back("note [" + n.id + "] has no title");
    return out;
}

const std::vector<PrimerNote>& primer_notes() {
    static const std::vector<PrimerNote> notes = parse_primer(unicode::embedded::get("primer/notes.txt"));
    return notes;
}

std::string_view primer_figure(const std::string& name) {
    if (name.empty()) return {};
    return unicode::embedded::get("primer/" + name);
}

LinkTarget parse_link_target(const std::string& target) {
    LinkTarget t;
    const auto c = target.find(':');
    if (c == std::string::npos) return t;
    t.kind = target.substr(0, c);
    t.arg  = target.substr(c + 1);
    static const std::array<std::string_view, 5> views{"overview", "editor", "spacing", "preview", "report"};
    static const std::array<std::string_view, 6> checks{"direction", "open", "zones", "overshoot", "origin", "keys"};
    static const std::array<std::string_view, 4> actions{"font-metrics-keys", "font-directions", "font-overlaps",
                                                         "font-anchors"};
    const auto in = [](const auto& list, const std::string& s) {
        return std::find(list.begin(), list.end(), s) != list.end();
    };
    if (t.kind == "tool") {
        const std::string tool = t.arg.substr(0, t.arg.find(':'));
        t.valid = tool == "measure" || tool == "pen" || tool == "pick" || tool == "hand";
    } else if (t.kind == "glyph")   t.valid = !t.arg.empty();
    else if (t.kind == "view")      t.valid = in(views, t.arg);
    else if (t.kind == "report")    t.valid = in(checks, t.arg);
    else if (t.kind == "spacing")   t.valid = !t.arg.empty();
    else if (t.kind == "font")      t.valid = t.arg == "metrics";
    else if (t.kind == "action")    t.valid = in(actions, t.arg);
    return t;
}

std::string primer_markup(const std::string& p) {
    std::string out;
    bool bold = false;
    for (char ch : p) {
        switch (ch) {
            case '*': out += bold ? "</b>" : "<b>"; bold = !bold; break;
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            default:  out += ch;
        }
    }
    if (bold) out += "</b>";
    return out;
}

}  // namespace granjon::core
