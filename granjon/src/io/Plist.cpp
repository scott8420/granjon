#include "io/Plist.hpp"

#include <libxml/parser.h>
#include <libxml/tree.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>

namespace granjon::io {

PValue PValue::real(double v) {
    PValue p;
    if (std::isfinite(v) && v == std::round(v) && std::abs(v) < 9e15) {
        p.kind = Kind::Integer;
        p.i = static_cast<std::int64_t>(v);
    } else {
        p.kind = Kind::Real;
        p.r = v;
    }
    return p;
}

void PValue::set(const std::string& key, PValue v) {
    for (auto& [k, val] : dict) if (k == key) { val = std::move(v); return; }
    dict.emplace_back(key, std::move(v));
}

const PValue* PValue::get(const std::string& key) const {
    for (const auto& [k, v] : dict) if (k == key) return &v;
    return nullptr;
}

std::optional<std::string> PValue::get_string(const std::string& key) const {
    const PValue* v = get(key);
    if (!v || v->kind != Kind::String) return std::nullopt;
    return v->s;
}

std::optional<double> PValue::get_number(const std::string& key) const {
    const PValue* v = get(key);
    if (!v || !v->is_number()) return std::nullopt;
    return v->number();
}

std::string xml_escape(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&':  o += "&amp;";  break;
            case '<':  o += "&lt;";   break;
            case '>':  o += "&gt;";   break;
            case '"':  o += "&quot;"; break;
            default:   o += c;
        }
    }
    return o;
}

std::string format_number(double v) {
    if (v == std::round(v) && std::abs(v) < 9e15) {
        const auto n = static_cast<long long>(v);
        return std::to_string(n == 0 ? 0 : n);   // no "-0"
    }
    char buf[32];
    const auto res = std::to_chars(buf, buf + sizeof buf, v);   // shortest round-trip
    return std::string(buf, res.ptr);
}

// ── writing ────────────────────────────────────────────────────────────────

void write_plist_value(std::string& out, const PValue& v, int depth) {
    const std::string tab(static_cast<std::size_t>(depth), '\t');
    switch (v.kind) {
        case PValue::Kind::String:  out += tab + "<string>" + xml_escape(v.s) + "</string>\n"; break;
        case PValue::Kind::Integer: out += tab + "<integer>" + std::to_string(v.i) + "</integer>\n"; break;
        case PValue::Kind::Real:    out += tab + "<real>" + format_number(v.r) + "</real>\n"; break;
        case PValue::Kind::Bool:    out += tab + (v.b ? "<true/>\n" : "<false/>\n"); break;
        case PValue::Kind::Array:
            if (v.arr.empty()) { out += tab + "<array/>\n"; break; }
            out += tab + "<array>\n";
            for (const auto& e : v.arr) write_plist_value(out, e, depth + 1);
            out += tab + "</array>\n";
            break;
        case PValue::Kind::Dict: {
            if (v.dict.empty()) { out += tab + "<dict/>\n"; break; }
            std::vector<const std::pair<std::string, PValue>*> items;
            for (const auto& kv : v.dict) items.push_back(&kv);
            std::sort(items.begin(), items.end(), [](auto* a, auto* b) { return a->first < b->first; });
            out += tab + "<dict>\n";
            for (const auto* kv : items) {
                out += tab + "\t<key>" + xml_escape(kv->first) + "</key>\n";
                write_plist_value(out, kv->second, depth + 1);
            }
            out += tab + "</dict>\n";
            break;
        }
    }
}

std::string write_plist(const PValue& root) {
    std::string out =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
        "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\">\n";
    write_plist_value(out, root, 0);
    out += "</plist>\n";
    return out;
}

// ── reading ────────────────────────────────────────────────────────────────

namespace {

bool is(xmlNode* n, const char* name) {
    return n && n->type == XML_ELEMENT_NODE && std::strcmp(reinterpret_cast<const char*>(n->name), name) == 0;
}

std::string text_of(xmlNode* n) {
    xmlChar* c = xmlNodeGetContent(n);
    std::string s = c ? reinterpret_cast<const char*>(c) : "";
    if (c) xmlFree(c);
    return s;
}

xmlNode* next_element(xmlNode* n) {
    while (n && n->type != XML_ELEMENT_NODE) n = n->next;
    return n;
}

}  // namespace

std::optional<PValue> read_plist_value(xmlNode* n, std::string* err) {
    const auto fail = [&](const std::string& m) -> std::optional<PValue> {
        if (err) *err = m;
        return std::nullopt;
    };
    if (!n || n->type != XML_ELEMENT_NODE) return fail("expected a plist value");
    if (is(n, "string")) return PValue::str(text_of(n));
    if (is(n, "integer")) {
        try { return PValue::integer(std::stoll(text_of(n))); }
        catch (...) { return fail("bad <integer> '" + text_of(n) + "'"); }
    }
    if (is(n, "real")) {
        try { PValue p; p.kind = PValue::Kind::Real; p.r = std::stod(text_of(n)); return p; }
        catch (...) { return fail("bad <real> '" + text_of(n) + "'"); }
    }
    if (is(n, "true"))  return PValue::boolean(true);
    if (is(n, "false")) return PValue::boolean(false);
    if (is(n, "array")) {
        PValue a = PValue::array();
        for (xmlNode* c = next_element(n->children); c; c = next_element(c->next)) {
            auto v = read_plist_value(c, err);
            if (!v) return std::nullopt;
            a.arr.push_back(std::move(*v));
        }
        return a;
    }
    if (is(n, "dict")) {
        PValue d = PValue::dictionary();
        for (xmlNode* c = next_element(n->children); c; c = next_element(c->next)) {
            if (!is(c, "key")) return fail("<dict> expected <key>");
            const std::string key = text_of(c);
            xmlNode* val = next_element(c->next);
            if (!val) return fail("<key>" + key + "</key> has no value");
            auto v = read_plist_value(val, err);
            if (!v) return std::nullopt;
            d.dict.emplace_back(key, std::move(*v));
            c = val;
        }
        return d;
    }
    if (is(n, "data") || is(n, "date")) return PValue::str(text_of(n));   // kept as text, unused
    return fail(std::string("unknown plist element <") + reinterpret_cast<const char*>(n->name) + ">");
}

std::optional<PValue> read_plist(const std::string& xml, std::string* err) {
    xmlDocPtr doc = xmlReadMemory(xml.data(), static_cast<int>(xml.size()), "plist.xml", nullptr,
                                  XML_PARSE_NONET | XML_PARSE_NOBLANKS | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    if (!doc) { if (err) *err = "not well-formed XML"; return std::nullopt; }
    std::optional<PValue> out;
    xmlNode* root = xmlDocGetRootElement(doc);
    if (!is(root, "plist")) { if (err) *err = "root element is not <plist>"; }
    else out = read_plist_value(next_element(root->children), err);
    xmlFreeDoc(doc);
    return out;
}

std::optional<PValue> read_plist_file(const std::string& path, std::string* err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { if (err) *err = "cannot read " + path; return std::nullopt; }
    std::stringstream ss;
    ss << in.rdbuf();
    std::string e;
    auto v = read_plist(ss.str(), &e);
    if (!v && err) *err = path + ": " + e;
    return v;
}

}  // namespace granjon::io
