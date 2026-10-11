#include "io/Glif.hpp"

#include "io/Plist.hpp"

#include <libxml/parser.h>
#include <libxml/tree.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace granjon::io {

using model::Contour;
using model::Glyph;
using model::Node;

namespace {

char type_code(Node::Type t) {
    switch (t) {
        case Node::Type::Smooth:    return 's';
        case Node::Type::Cusp:      return 'u';
        case Node::Type::Symmetric: return 'y';
        case Node::Type::Corner:    return 'c';
    }
    return 'c';
}
Node::Type type_from(char c) {
    switch (c) {
        case 's': return Node::Type::Smooth;
        case 'u': return Node::Type::Cusp;
        case 'y': return Node::Type::Symmetric;
        default:  return Node::Type::Corner;
    }
}
bool is_smooth(Node::Type t) { return t == Node::Type::Smooth || t == Node::Type::Symmetric; }

void point(std::string& o, double x, double y, const char* type, bool smooth) {
    o += "      <point x=\"" + format_number(x) + "\" y=\"" + format_number(y) + "\"";
    if (type) o += std::string(" type=\"") + type + "\"";
    if (smooth) o += " smooth=\"yes\"";
    o += "/>\n";
}

}  // namespace

// ── writing ────────────────────────────────────────────────────────────────

std::string write_glif(const Glyph& g) {
    std::string o = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    o += "<glyph name=\"" + xml_escape(g.name) + "\" format=\"2\">\n";
    if (g.advance != 0) o += "  <advance width=\"" + format_number(g.advance) + "\"/>\n";
    for (char32_t cp : g.unicodes) {
        char buf[16];
        std::snprintf(buf, sizeof buf, "%04X", static_cast<unsigned>(cp));
        o += std::string("  <unicode hex=\"") + buf + "\"/>\n";
    }
    if (!g.note.empty()) o += "  <note>" + xml_escape(g.note) + "</note>\n";
    // M13: the background image (GLIF's order: after <note>, before guidelines).
    if (const auto& im = g.image) {
        o += "  <image fileName=\"" + xml_escape(im->file) + "\"";
        if (im->xx != 1) o += " xScale=\"" + format_number(im->xx) + "\"";
        if (im->xy != 0) o += " xyScale=\"" + format_number(im->xy) + "\"";
        if (im->yx != 0) o += " yxScale=\"" + format_number(im->yx) + "\"";
        if (im->yy != 1) o += " yScale=\"" + format_number(im->yy) + "\"";
        if (im->dx != 0) o += " xOffset=\"" + format_number(im->dx) + "\"";
        if (im->dy != 0) o += " yOffset=\"" + format_number(im->dy) + "\"";
        if (!im->color.empty()) o += " color=\"" + xml_escape(im->color) + "\"";
        o += "/>\n";
    }
    // Guidelines: horizontal -> y only, vertical -> x only, else x, y, angle (UFO 3).
    for (const auto& gd : g.guides) {
        o += "  <guideline";
        if (gd.horizontal())     o += " y=\"" + format_number(gd.y) + "\"";
        else if (gd.vertical())  o += " x=\"" + format_number(gd.x) + "\"";
        else o += " x=\"" + format_number(gd.x) + "\" y=\"" + format_number(gd.y) + "\" angle=\"" +
                  format_number(gd.angle) + "\"";
        if (!gd.name.empty()) o += " name=\"" + xml_escape(gd.name) + "\"";
        o += "/>\n";
    }
    for (const auto& a : g.anchors)
        o += "  <anchor x=\"" + format_number(a.x) + "\" y=\"" + format_number(a.y) + "\" name=\"" +
             xml_escape(a.name) + "\"/>\n";

    bool special_types = false;
    if (!g.contours.empty() || !g.components.empty()) {
        o += "  <outline>\n";
        for (const auto& k : g.components) {
            o += "    <component base=\"" + xml_escape(k.base) + "\"";
            if (k.xx != 1) o += " xScale=\"" + format_number(k.xx) + "\"";
            if (k.xy != 0) o += " xyScale=\"" + format_number(k.xy) + "\"";
            if (k.yx != 0) o += " yxScale=\"" + format_number(k.yx) + "\"";
            if (k.yy != 1) o += " yScale=\"" + format_number(k.yy) + "\"";
            if (k.dx != 0) o += " xOffset=\"" + format_number(k.dx) + "\"";
            if (k.dy != 0) o += " yOffset=\"" + format_number(k.dy) + "\"";
            o += "/>\n";
        }
        for (const auto& c : g.contours) {
            const auto& ns = c.nodes;
            if (ns.empty()) continue;
            o += "    <contour>\n";
            const std::size_t n = ns.size();
            for (const auto& nd : ns)
                if (nd.type == Node::Type::Cusp || nd.type == Node::Type::Symmetric) special_types = true;
            if (c.closed) {
                // Node 0 first, typed by the closing segment (last -> 0).
                const Node& last = ns[n - 1];
                const bool closing_curve = last.has_out() || ns[0].has_in();
                point(o, ns[0].x, ns[0].y, closing_curve ? "curve" : "line", is_smooth(ns[0].type));
                for (std::size_t i = 1; i < n; ++i) {
                    const Node& a = ns[i - 1];
                    const Node& b = ns[i];
                    if (a.has_out() || b.has_in()) {
                        point(o, a.cx2, a.cy2, nullptr, false);
                        point(o, b.cx1, b.cy1, nullptr, false);
                        point(o, b.x, b.y, "curve", is_smooth(b.type));
                    } else {
                        point(o, b.x, b.y, "line", is_smooth(b.type));
                    }
                }
                if (closing_curve) {
                    point(o, last.cx2, last.cy2, nullptr, false);
                    point(o, ns[0].cx1, ns[0].cy1, nullptr, false);
                }
            } else {
                point(o, ns[0].x, ns[0].y, "move", false);
                for (std::size_t i = 1; i < n; ++i) {
                    const Node& a = ns[i - 1];
                    const Node& b = ns[i];
                    if (a.has_out() || b.has_in()) {
                        point(o, a.cx2, a.cy2, nullptr, false);
                        point(o, b.cx1, b.cy1, nullptr, false);
                        point(o, b.x, b.y, "curve", is_smooth(b.type));
                    } else {
                        point(o, b.x, b.y, "line", is_smooth(b.type));
                    }
                }
            }
            o += "    </contour>\n";
        }
        o += "  </outline>\n";
    }

    PValue lib = PValue::dictionary();
    // Empty is the default -- except on a glyph with a drawing, where a missing
    // status now reads as Drafted (a foreign glyph), so Empty must be said.
    const bool drawn = !g.contours.empty() || !g.components.empty();
    if (g.status != model::GlyphStatus::Empty || drawn)
        lib.set("granjon.status", PValue::str(model::status_name(g.status)));
    if (special_types) {
        PValue arr = PValue::array();
        for (const auto& c : g.contours) {
            std::string codes;
            for (const auto& nd : c.nodes) codes += type_code(nd.type);
            arr.arr.push_back(PValue::str(codes));
        }
        lib.set("granjon.nodeTypes", std::move(arr));
    }
    if (g.image && (g.image->opacity != model::GlyphImage{}.opacity || g.image->locked)) {
        PValue im = PValue::dictionary();
        // Opacity in whole percent, so the file reads (and stays stable).
        im.set("opacity", PValue::integer(static_cast<long long>(std::lround(g.image->opacity * 100))));
        if (g.image->locked) im.set("locked", PValue::boolean(true));
        lib.set("granjon.image", std::move(im));
    }
    if (!lib.dict.empty()) {
        // The lib is a plist dict; GLIF bodies indent with spaces, so its
        // tabs become two spaces each.
        std::string body;
        write_plist_value(body, lib, 2);
        std::string spaced;
        bool line_start = true;
        for (char ch : body) {
            if (line_start && ch == '\t') { spaced += "  "; continue; }
            line_start = ch == '\n';
            spaced += ch;
        }
        o += "  <lib>\n" + spaced + "  </lib>\n";
    }
    o += "</glyph>\n";
    return o;
}

// ── reading ────────────────────────────────────────────────────────────────

namespace {

bool is(xmlNode* n, const char* name) {
    return n && n->type == XML_ELEMENT_NODE && std::strcmp(reinterpret_cast<const char*>(n->name), name) == 0;
}
std::optional<std::string> attr(xmlNode* n, const char* name) {
    xmlChar* v = xmlGetProp(n, reinterpret_cast<const xmlChar*>(name));
    if (!v) return std::nullopt;
    std::string s = reinterpret_cast<const char*>(v);
    xmlFree(v);
    return s;
}
double num(xmlNode* n, const char* name, double fallback, bool& ok) {
    const auto v = attr(n, name);
    if (!v) return fallback;
    try { return std::stod(*v); } catch (...) { ok = false; return fallback; }
}

struct Pt { double x, y; std::string type; bool smooth; };

// One GLIF contour -> one model contour.
bool build_contour(std::vector<Pt> pts, Contour& out, std::string* err) {
    out.nodes.clear();
    if (pts.empty()) return true;
    out.closed = pts.front().type != "move";
    if (!out.closed) pts.front().type = "line";

    // Closed: rotate so the list starts at an on-curve point; the off-curves
    // that led into it go to the end (they belong to the closing segment).
    if (out.closed) {
        std::size_t first_on = pts.size();
        for (std::size_t i = 0; i < pts.size(); ++i) if (!pts[i].type.empty()) { first_on = i; break; }
        if (first_on == pts.size()) { if (err) *err = "a contour with no on-curve point"; return false; }
        std::rotate(pts.begin(), pts.begin() + static_cast<long>(first_on), pts.end());
    }

    // Expand qcurve runs into cubic segments; collect (offcurves, oncurve).
    const auto on_node = [](const Pt& p) {
        Node n = Node::corner(p.x, p.y);
        n.type = p.smooth ? Node::Type::Smooth : Node::Type::Corner;
        return n;
    };
    out.nodes.push_back(on_node(pts[0]));
    std::vector<Pt> off;
    const std::size_t count = pts.size();
    const auto segment_to = [&](const Pt& on, bool to_start) {
        Node& a = out.nodes.back();
        Node b = to_start ? out.nodes.front() : on_node(on);
        if (on.type == "line" || on.type == "move") {
            if (!off.empty()) { if (err) *err = "off-curve points before a line"; return false; }
        } else if (on.type == "curve") {
            if (off.size() == 2) {
                a.cx2 = off[0].x; a.cy2 = off[0].y;
                b.cx1 = off[1].x; b.cy1 = off[1].y;
            } else if (off.size() == 1) {   // a cubic with one control: treat as quadratic
                a.cx2 = a.x + 2.0 / 3.0 * (off[0].x - a.x); a.cy2 = a.y + 2.0 / 3.0 * (off[0].y - a.y);
                b.cx1 = b.x + 2.0 / 3.0 * (off[0].x - b.x); b.cy1 = b.y + 2.0 / 3.0 * (off[0].y - b.y);
            } else if (!off.empty()) {
                if (err) *err = "a cubic curve with more than two off-curve points"; return false;
            }
        } else if (on.type == "qcurve") {
            // Quadratic B-spline: implied on-curves at midpoints between offs.
            for (std::size_t k = 0; k < off.size(); ++k) {
                Node& p0 = out.nodes.back();
                const Pt& q = off[k];
                double ex, ey;
                const bool last = k + 1 == off.size();
                if (last) { ex = b.x; ey = b.y; }
                else      { ex = (q.x + off[k + 1].x) / 2; ey = (q.y + off[k + 1].y) / 2; }
                p0.cx2 = p0.x + 2.0 / 3.0 * (q.x - p0.x); p0.cy2 = p0.y + 2.0 / 3.0 * (q.y - p0.y);
                const double c1x = ex + 2.0 / 3.0 * (q.x - ex), c1y = ey + 2.0 / 3.0 * (q.y - ey);
                if (last) { b.cx1 = c1x; b.cy1 = c1y; }
                else {
                    Node mid = Node::corner(ex, ey);
                    mid.type = Node::Type::Smooth;
                    mid.cx1 = c1x; mid.cy1 = c1y;
                    out.nodes.push_back(mid);
                }
            }
        } else {
            if (err) *err = "unknown point type '" + on.type + "'"; return false;
        }
        off.clear();
        if (to_start) out.nodes.front() = b;
        else out.nodes.push_back(b);
        return true;
    };
    for (std::size_t i = 1; i < count; ++i) {
        if (pts[i].type.empty()) { off.push_back(pts[i]); continue; }
        if (!segment_to(pts[i], false)) return false;
    }
    if (out.closed) {
        // The closing segment: back to node 0, typed by node 0's own point.
        if (!segment_to(pts[0], true)) return false;
    } else if (!off.empty()) {
        if (err) *err = "an open contour ends in off-curve points"; return false;
    }
    return true;
}

}  // namespace

std::optional<Glyph> read_glif(const std::string& xml, std::string* err) {
    xmlDocPtr doc = xmlReadMemory(xml.data(), static_cast<int>(xml.size()), "glyph.glif", nullptr,
                                  XML_PARSE_NONET | XML_PARSE_NOBLANKS | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    if (!doc) { if (err) *err = "not well-formed XML"; return std::nullopt; }
    const auto fail = [&](const std::string& m) -> std::optional<Glyph> {
        if (err) *err = m;
        xmlFreeDoc(doc);
        return std::nullopt;
    };
    xmlNode* root = xmlDocGetRootElement(doc);
    if (!is(root, "glyph")) return fail("root element is not <glyph>");
    Glyph g;
    g.name = attr(root, "name").value_or("");
    if (g.name.empty()) return fail("<glyph> has no name");
    bool ok = true;
    std::vector<std::string> node_types;
    bool had_status = false;   // granjon.status in the lib
    std::optional<double> image_opacity;   // granjon.image (the lib may come before or after <image>)
    bool image_locked = false;

    for (xmlNode* c = root->children; c; c = c->next) {
        if (c->type != XML_ELEMENT_NODE) continue;
        if (is(c, "advance")) {
            g.advance = num(c, "width", 0, ok);
        } else if (is(c, "unicode")) {
            const auto h = attr(c, "hex");
            if (!h) return fail("<unicode> without hex");
            try { g.unicodes.push_back(static_cast<char32_t>(std::stoul(*h, nullptr, 16))); }
            catch (...) { return fail("bad unicode hex '" + *h + "'"); }
        } else if (is(c, "note")) {
            xmlChar* t = xmlNodeGetContent(c);
            g.note = t ? reinterpret_cast<const char*>(t) : "";
            if (t) xmlFree(t);
        } else if (is(c, "image")) {
            model::GlyphImage im;
            im.file = attr(c, "fileName").value_or("");
            if (im.file.empty()) return fail("<image> has no fileName");
            im.xx = num(c, "xScale", 1, ok);  im.xy = num(c, "xyScale", 0, ok);
            im.yx = num(c, "yxScale", 0, ok); im.yy = num(c, "yScale", 1, ok);
            im.dx = num(c, "xOffset", 0, ok); im.dy = num(c, "yOffset", 0, ok);
            im.color = attr(c, "color").value_or("");
            g.image = im;
        } else if (is(c, "guideline")) {
            model::Guide gd;
            const bool hx = attr(c, "x").has_value(), hy = attr(c, "y").has_value();
            gd.x = num(c, "x", 0, ok);
            gd.y = num(c, "y", 0, ok);
            gd.angle = attr(c, "angle") ? num(c, "angle", 0, ok) : (hx && !hy ? 90.0 : 0.0);
            gd.name = attr(c, "name").value_or("");
            g.guides.push_back(gd);
        } else if (is(c, "anchor")) {
            g.anchors.push_back({attr(c, "name").value_or(""), num(c, "x", 0, ok), num(c, "y", 0, ok)});
        } else if (is(c, "outline")) {
            for (xmlNode* o = c->children; o; o = o->next) {
                if (is(o, "component")) {
                    model::Component k;
                    k.base = attr(o, "base").value_or("");
                    k.xx = num(o, "xScale", 1, ok);  k.xy = num(o, "xyScale", 0, ok);
                    k.yx = num(o, "yxScale", 0, ok); k.yy = num(o, "yScale", 1, ok);
                    k.dx = num(o, "xOffset", 0, ok); k.dy = num(o, "yOffset", 0, ok);
                    g.components.push_back(k);
                } else if (is(o, "contour")) {
                    std::vector<Pt> pts;
                    for (xmlNode* p = o->children; p; p = p->next) {
                        if (!is(p, "point")) continue;
                        pts.push_back({num(p, "x", 0, ok), num(p, "y", 0, ok), attr(p, "type").value_or(""),
                                       attr(p, "smooth").value_or("") == "yes"});
                        if (pts.back().type == "offcurve") pts.back().type.clear();
                    }
                    Contour con;
                    std::string e;
                    if (!build_contour(pts, con, &e)) return fail(e);
                    if (!con.nodes.empty()) g.contours.push_back(std::move(con));
                }
            }
        } else if (is(c, "lib")) {
            xmlNode* d = c->children;
            while (d && d->type != XML_ELEMENT_NODE) d = d->next;
            std::string e;
            auto lib = read_plist_value(d, &e);
            if (!lib) return fail("<lib>: " + e);
            if (auto s = lib->get_string("granjon.status")) {
                had_status = true;
                for (auto st : {model::GlyphStatus::Empty, model::GlyphStatus::Drafted,
                                model::GlyphStatus::Done, model::GlyphStatus::Review})
                    if (*s == model::status_name(st)) g.status = st;
            }
            if (const PValue* nt = lib->get("granjon.nodeTypes"); nt && nt->kind == PValue::Kind::Array)
                for (const auto& v : nt->arr) node_types.push_back(v.s);
            if (const PValue* im = lib->get("granjon.image"); im && im->kind == PValue::Kind::Dict) {
                image_opacity = im->get_number("opacity");
                if (const PValue* lk = im->get("locked")) image_locked = lk->kind == PValue::Kind::Bool && lk->b;
            }
        }
    }
    xmlFreeDoc(doc);
    if (!ok) { if (err) *err = "a number attribute did not parse"; return std::nullopt; }
    if (g.image) {
        if (image_opacity) g.image->opacity = std::clamp(*image_opacity / 100.0, 0.0, 1.0);
        g.image->locked = image_locked;
    }
    // A glyph from another tool carries no Granjon status. One that already
    // has a drawing is not "Empty" -- call it Drafted (Scott, s03).
    if (!had_status && (!g.contours.empty() || !g.components.empty())) g.status = model::GlyphStatus::Drafted;

    // Restore Cusp / Symmetric where the lib recorded them (only if the
    // contour shape still matches what was written).
    if (node_types.size() == g.contours.size())
        for (std::size_t i = 0; i < g.contours.size(); ++i)
            if (node_types[i].size() == g.contours[i].nodes.size())
                for (std::size_t k = 0; k < node_types[i].size(); ++k)
                    g.contours[i].nodes[k].type = type_from(node_types[i][k]);
    return g;
}

}  // namespace granjon::io
