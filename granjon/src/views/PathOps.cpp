#include "views/PathOps.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <vector>

namespace granjon::views {

std::set<int> picked_contours(const ViewContext& ctx) {
    std::set<int> cs;
    const model::Glyph* g = ctx.doc.font.glyph(ctx.selected);
    if (!g) return cs;
    for (const auto& p : ctx.pick)
        if (p.contour >= 0 && p.contour < static_cast<int>(g->contours.size())) cs.insert(p.contour);
    return cs;
}

void arrange_picked(ViewContext& ctx, command::Arrange how) {
    const std::set<int> cs = picked_contours(ctx);
    std::vector<int> at;
    std::string why;
    if (!command::arrange_contours(ctx.doc.journal, ctx.doc.font, ctx.selected, {cs.begin(), cs.end()}, how, &at, &why)) {
        if (ctx.say) ctx.say(why, false);
        return;
    }
    // The picked paths keep their own order, so the k-th picked old index
    // is the k-th new one; the points picked inside them stay picked.
    std::map<int, int> to;
    std::size_t k = 0;
    for (int c : cs) to[c] = at[k++];
    geom::Points moved;
    for (auto p : ctx.pick) {
        if (p.contour >= 0) {
            const auto it = to.find(p.contour);
            if (it == to.end()) continue;   // (none: only picked contours have picked points)
            p.contour = it->second;
        }
        moved.insert(p);
    }
    ctx.pick = std::move(moved);
    ctx.pick_changed.emit();
    if (ctx.say) ctx.say(ctx.doc.journal.undo_label() + (cs.size() == 1 ? ": 1 path." : ": " + std::to_string(cs.size()) + " paths."), false);
}

namespace {
void copy_picked(ViewContext& ctx, double dx, double dy) {
    const std::set<int> cs = picked_contours(ctx);
    std::vector<int> at;
    std::string why;
    if (!command::duplicate_contours(ctx.doc.journal, ctx.doc.font, ctx.selected, {cs.begin(), cs.end()}, dx, dy, &at, &why)) {
        if (ctx.say) ctx.say(why, false);
        return;
    }
    ctx.pick.clear();
    if (const model::Glyph* g = ctx.doc.font.glyph(ctx.selected))
        for (int ci : at)
            for (int i = 0; i < static_cast<int>(g->contours[static_cast<std::size_t>(ci)].nodes.size()); ++i)
                ctx.pick.insert({ci, i, geom::PointRef::Part::Node});
    ctx.pick_changed.emit();
    if (!ctx.say) return;
    const std::string n = at.size() == 1 ? "1 path" : std::to_string(at.size()) + " paths";
    if (dx == 0 && dy == 0) ctx.say("Duplicated " + n + " in place, on top -- the copy is picked: drag or nudge it.", false);
    else ctx.say("Cloned " + n + ", offset " + std::to_string(static_cast<int>(dx)) + " right and down, on top -- picked.", false);
}
}  // namespace

double clone_offset(const ViewContext& ctx) {
    return std::max(1.0, std::round(ctx.doc.font.info().units_per_em * 0.02));
}

void duplicate_picked(ViewContext& ctx) { copy_picked(ctx, 0, 0); }

void clone_picked(ViewContext& ctx) {
    const double o = clone_offset(ctx);
    copy_picked(ctx, o, -o);
}

bool whole_paths_picked(const ViewContext& ctx) {
    const model::Glyph* g = ctx.doc.font.glyph(ctx.selected);
    if (!g) return false;
    const std::set<int> cs = picked_contours(ctx);
    if (cs.empty()) return false;
    for (int c : cs)
        for (int i = 0; i < static_cast<int>(g->contours[static_cast<std::size_t>(c)].nodes.size()); ++i)
            if (!ctx.pick.count({c, i, geom::PointRef::Part::Node})) return false;
    return true;
}

void align_picked(ViewContext& ctx, command::Align how) {
    if (whole_paths_picked(ctx)) {
        const std::set<int> cs = picked_contours(ctx);
        std::string why;
        if (command::align_contours(ctx.doc.journal, ctx.doc.font, ctx.selected, {cs.begin(), cs.end()}, how, &why)) {
            if (ctx.say)
                ctx.say(ctx.doc.journal.undo_label() + (cs.size() == 1 ? " -- 1 path, to the glyph's box." : ": " + std::to_string(cs.size()) + " paths."), false);
            ctx.pick_changed.emit();   // (same refs, new places: the inspector's numbers follow)
        } else if (ctx.say) {
            ctx.say(why, false);
        }
        return;
    }
    if (!command::align_points(ctx.doc.journal, ctx.doc.font, ctx.selected, ctx.pick, how) && ctx.say)
        ctx.say("Align: pick two or more nodes, or whole paths (Select, V).", false);
    else
        ctx.pick_changed.emit();
}

void boolean_picked(ViewContext& ctx, geom::BoolOp op) {
    const std::set<int> cs = picked_contours(ctx);
    std::vector<int> at;
    std::string why;
    if (!command::boolean_contours(ctx.doc.journal, ctx.doc.font, ctx.selected, {cs.begin(), cs.end()}, op, &at, &why)) {
        if (ctx.say) ctx.say(why, true);
        return;
    }
    // The result is picked, so the next drag / op acts on it.
    ctx.pick.clear();
    if (const model::Glyph* g = ctx.doc.font.glyph(ctx.selected))
        for (int ci : at)
            for (int i = 0; i < static_cast<int>(g->contours[static_cast<std::size_t>(ci)].nodes.size()); ++i)
                ctx.pick.insert({ci, i, geom::PointRef::Part::Node});
    ctx.pick_changed.emit();
    if (ctx.say)
        ctx.say(ctx.doc.journal.undo_label() + ": " + std::to_string(at.size()) + (at.size() == 1 ? " contour." : " contours."), false);
}

void remove_overlaps_here(ViewContext& ctx) {
    std::size_t n = 0, fb = 0, ww = 0;
    if (command::remove_overlaps(ctx.doc.journal, ctx.doc.font, {ctx.selected}, &n, &fb, &ww)) {
        ctx.pick.clear();   // the contours were rebuilt: old refs name other points
        ctx.pick_changed.emit();
        if (ctx.say) ctx.say(ctx.doc.journal.undo_label() + ".", false);
    } else if (!ctx.say) {
    } else if (ww) {
        ctx.say("A contour runs the wrong way -- correct directions first (the union would fill that counter).", true);
    } else if (fb) {
        ctx.say("The overlaps could not be removed cleanly; the outline is as drawn.", true);
    } else {
        ctx.say("No overlaps to remove.", false);
    }
}

void correct_directions_here(ViewContext& ctx) {
    std::size_t n = 0;
    if (command::correct_directions(ctx.doc.journal, ctx.doc.font, {ctx.selected}, &n)) {
        if (ctx.say) ctx.say(ctx.doc.journal.undo_label() + ".", false);
    } else if (ctx.say) {
        ctx.say("Every contour runs the right way already.", false);
    }
}

void reverse_picked(ViewContext& ctx) {
    const std::set<int> cs = picked_contours(ctx);
    if (cs.empty()) { if (ctx.say) ctx.say("Reverse: pick a path first.", false); return; }
    if (command::reverse_contours(ctx.doc.journal, ctx.doc.font, ctx.selected, {cs.begin(), cs.end()}) && ctx.say)
        ctx.say(ctx.doc.journal.undo_label() + ".", false);
}

std::optional<model::Rect> pick_box(const ViewContext& ctx) {
    const model::Glyph* g = ctx.doc.font.glyph(ctx.selected);
    if (!g) return std::nullopt;
    if (ctx.pick.empty()) return model::bounds(ctx.doc.font, *g);
    std::optional<model::Rect> box;
    const auto grow = [&](const model::Rect& r) {
        if (!box) { box = r; return; }
        box->x0 = std::min(box->x0, r.x0); box->y0 = std::min(box->y0, r.y0);
        box->x1 = std::max(box->x1, r.x1); box->y1 = std::max(box->y1, r.y1);
    };
    // Whole paths by their exact curve bounds; any other point by where it is.
    std::set<int> whole;
    for (int c : picked_contours(ctx)) {
        bool all = true;
        for (int i = 0; i < static_cast<int>(g->contours[static_cast<std::size_t>(c)].nodes.size()); ++i)
            all = all && ctx.pick.count({c, i, geom::PointRef::Part::Node});
        if (!all) continue;
        whole.insert(c);
        model::Glyph one;
        one.contours = {g->contours[static_cast<std::size_t>(c)]};
        if (const auto b = model::bounds(ctx.doc.font, one)) grow(*b);
    }
    for (const auto& p : ctx.pick) {
        if (p.contour >= 0 && whole.count(p.contour)) continue;
        if (!geom::valid(*g, p)) continue;
        if (p.part == geom::PointRef::Part::Component) {
            model::Glyph one;
            one.components = {g->components[static_cast<std::size_t>(p.index)]};
            if (const auto b = model::bounds(ctx.doc.font, one)) grow(*b);
            continue;
        }
        const auto v = geom::position(*g, p);
        grow({v.x, v.y, v.x, v.y});
    }
    return box;
}

std::string pick_subject(const ViewContext& ctx) {
    if (ctx.pick.empty()) return "the whole glyph";
    if (whole_paths_picked(ctx)) {
        const auto n = picked_contours(ctx).size();
        return n == 1 ? "1 path" : std::to_string(n) + " paths";
    }
    const auto n = ctx.pick.size();
    return n == 1 ? "1 point" : std::to_string(n) + " points";
}

geom::Vec2 box_point(const model::Rect& b, int origin) {
    const int col = origin % 3, row = origin / 3;
    const double x = col == 0 ? b.x0 : col == 1 ? (b.x0 + b.x1) / 2 : b.x1;
    const double y = row == 0 ? b.y1 : row == 1 ? (b.y0 + b.y1) / 2 : b.y0;   // row 0 = top (y up)
    return {x, y};
}

bool transform_picked(ViewContext& ctx, const geom::Affine& m, const std::string& label, const std::string& merge) {
    if (!command::transform_points(ctx.doc.journal, ctx.doc.font, ctx.selected, ctx.pick, m, label, merge)) {
        if (ctx.say) ctx.say("Nothing to transform -- the values leave it as it is.", false);
        return false;
    }
    ctx.pick_changed.emit();   // same refs, new places
    if (ctx.say) ctx.say(label + ": " + pick_subject(ctx) + ".", false);
    return true;
}

void flip_picked(ViewContext& ctx, bool horizontal) {
    const auto b = pick_box(ctx);
    if (!b) return;
    const geom::Vec2 c = box_point(*b, 4);
    transform_picked(ctx, geom::make_transform(horizontal ? -1 : 1, horizontal ? 1 : -1, 0, 0, 0, c),
                     horizontal ? "Flip horizontal" : "Flip vertical");
}

void rotate_picked(ViewContext& ctx, double deg) {
    const auto b = pick_box(ctx);
    if (!b) return;
    transform_picked(ctx, geom::make_transform(1, 1, deg, 0, 0, box_point(*b, 4)),
                     deg > 0 ? "Rotate left" : "Rotate right");
}

namespace {
std::string origin_key(const ViewContext& ctx) { return geom::pick_to_text(ctx.selected, ctx.pick); }
}  // namespace

bool origin_chosen(const ViewContext& ctx) {
    if (!ctx.tf_explicit) return false;
    return ctx.tf_origin >= 0 || ctx.tf_for == origin_key(ctx);
}

geom::Vec2 transform_origin(const ViewContext& ctx, const model::Rect& box) {
    if (ctx.tf_explicit && ctx.tf_origin < 0 && ctx.tf_for == origin_key(ctx)) return ctx.tf_custom;
    return box_point(box, ctx.tf_explicit && ctx.tf_origin >= 0 ? ctx.tf_origin : 4);
}

void set_origin_point(ViewContext& ctx, int k) {
    ctx.tf_origin = k;
    ctx.tf_explicit = true;
    ctx.tf_origin_changed.emit();
}

void set_custom_origin(ViewContext& ctx, geom::Vec2 p) {
    ctx.tf_origin = -1;
    ctx.tf_explicit = true;
    ctx.tf_custom = p;
    ctx.tf_for = origin_key(ctx);
    ctx.tf_origin_changed.emit();
}

void clear_origin(ViewContext& ctx) {
    ctx.tf_origin = 4;
    ctx.tf_explicit = false;
    ctx.tf_origin_changed.emit();
}

namespace {
struct StepRun {
    std::string  glyph, key, label;
    model::Glyph base, result;
    geom::Affine acc;
    std::chrono::steady_clock::time_point at{};
};
StepRun& step_run() { static StepRun r; return r; }
}  // namespace

void transform_step(ViewContext& ctx, double sx, double sy, double rot, double skx, double sky, const std::string& label) {
    const model::Glyph* g = ctx.doc.font.glyph(ctx.selected);
    const auto box = pick_box(ctx);
    if (!g || !box) return;
    StepRun& r = step_run();
    const auto now = std::chrono::steady_clock::now();
    const std::string key = geom::pick_to_text(ctx.selected, ctx.pick);
    // Still the same run: same glyph, pick and kind, nothing else changed it,
    // and the last press was recent.
    const bool same = r.glyph == ctx.selected && r.key == key && r.label == label && *g == r.result &&
                      ctx.doc.journal.undo_label() == label && now - r.at <= std::chrono::milliseconds(1500);
    if (!same) {
        r.glyph = ctx.selected;
        r.key = key;
        r.label = label;
        r.base = *g;
        r.acc = {};
    }
    // The origin of this press, in the run's frame: where it is now.
    const geom::Vec2 o = transform_origin(ctx, *box);
    r.acc = geom::compose(geom::make_transform(sx, sy, rot, skx, sky, o), r.acc);
    const std::string merge = "tfstep:" + ctx.selected + ":" + label;
    if (!command::transform_from(ctx.doc.journal, ctx.doc.font, ctx.selected, r.base, ctx.pick, r.acc, label, merge)) {
        r.glyph.clear();
        return;
    }
    r.result = *ctx.doc.font.glyph(ctx.selected);
    r.at = now;
    ctx.pick_changed.emit();
    if (ctx.say) ctx.say(label + ": " + pick_subject(ctx) + " (steps in a row are one undo step).", false);
}

// ── M15b ───────────────────────────────────────────────────────────────────

namespace {
double em(const ViewContext& ctx) { return std::max(16, ctx.doc.font.info().units_per_em); }
double setting_or(int v, double dflt) { return v > 0 ? v : std::round(dflt); }

// After an op that replaced nodes: a whole-path pick re-picks those paths
// (their node count changed), a node pick is dropped.
void repick(ViewContext& ctx, bool whole, const std::set<int>& cs) {
    ctx.pick.clear();
    if (whole)
        if (const model::Glyph* g = ctx.doc.font.glyph(ctx.selected))
            for (int ci : cs)
                if (ci >= 0 && ci < static_cast<int>(g->contours.size()))
                    for (int i = 0; i < static_cast<int>(g->contours[static_cast<std::size_t>(ci)].nodes.size()); ++i)
                        ctx.pick.insert({ci, i, geom::PointRef::Part::Node});
    ctx.pick_changed.emit();
}
}  // namespace

double corner_radius(const ViewContext& ctx) { return setting_or(ctx.settings.corner_radius, 0.02 * em(ctx)); }
double trap_width(const ViewContext& ctx) { return setting_or(ctx.settings.trap_width, 0.015 * em(ctx)); }
double trap_depth(const ViewContext& ctx) { return setting_or(ctx.settings.trap_depth, 0.025 * em(ctx)); }
model::FixAmounts fix_amounts(const ViewContext& ctx) { return {trap_width(ctx), trap_depth(ctx)}; }

void round_picked(ViewContext& ctx) {
    const bool whole = whole_paths_picked(ctx);
    const auto cs = picked_contours(ctx);
    std::string why;
    int n = 0;
    if (!command::round_corners(ctx.doc.journal, ctx.doc.font, ctx.selected, ctx.pick, corner_radius(ctx), &n, &why)) {
        if (ctx.say) ctx.say(why, true);
        return;
    }
    repick(ctx, whole, cs);
    if (ctx.say) ctx.say(ctx.doc.journal.undo_label() + ", radius " + std::to_string(std::lround(corner_radius(ctx))) + ".", false);
}

void ink_trap_picked(ViewContext& ctx) {
    const bool whole = whole_paths_picked(ctx);
    const auto cs = picked_contours(ctx);
    std::string why;
    int n = 0;
    if (!command::ink_traps(ctx.doc.journal, ctx.doc.font, ctx.selected, ctx.pick, trap_width(ctx), trap_depth(ctx), &n, &why)) {
        if (ctx.say) ctx.say(why, true);
        return;
    }
    repick(ctx, whole, cs);
    if (ctx.say)
        ctx.say(ctx.doc.journal.undo_label() + " -- " + std::to_string(std::lround(trap_width(ctx))) + " wide, " +
                    std::to_string(std::lround(trap_depth(ctx))) + " deep.", false);
}

void extremes_picked(ViewContext& ctx) {
    const bool whole = whole_paths_picked(ctx);
    const auto cs = picked_contours(ctx);
    std::string why;
    int n = 0;
    if (!command::add_extremes(ctx.doc.journal, ctx.doc.font, ctx.selected, {cs.begin(), cs.end()}, &n, &why)) {
        if (ctx.say) ctx.say(cs.empty() ? why : why.substr(0, why.size() - 1) + " on the picked paths.", false);
        return;
    }
    if (!cs.empty()) repick(ctx, whole, cs);
    if (ctx.say) ctx.say(ctx.doc.journal.undo_label() + (cs.empty() ? " -- the whole glyph." : "."), false);
}

double offset_distance(const ViewContext& ctx) { return setting_or(ctx.settings.offset_distance, 0.02 * em(ctx)); }
double stroke_width(const ViewContext& ctx) {
    if (ctx.settings.line_width > 0) return ctx.settings.line_width;
    const auto& info = ctx.doc.font.info();
    if (info.origin.stem_v > 0) return info.origin.stem_v;
    return std::max(1.0, std::round(info.units_per_em * 0.08));
}
geom::Join offset_join(const ViewContext& ctx) {
    return ctx.settings.offset_join == 1 ? geom::Join::Round : ctx.settings.offset_join == 2 ? geom::Join::Bevel : geom::Join::Miter;
}
geom::Cap stroke_cap(const ViewContext& ctx) {
    return ctx.settings.stroke_cap == 1 ? geom::Cap::Round : ctx.settings.stroke_cap == 2 ? geom::Cap::Square : geom::Cap::Butt;
}

namespace {
void pick_result(ViewContext& ctx, const std::vector<int>& at) {
    ctx.pick.clear();
    if (const model::Glyph* g = ctx.doc.font.glyph(ctx.selected))
        for (int ci : at)
            for (int i = 0; i < static_cast<int>(g->contours[static_cast<std::size_t>(ci)].nodes.size()); ++i)
                ctx.pick.insert({ci, i, geom::PointRef::Part::Node});
    ctx.pick_changed.emit();
}
}  // namespace

void offset_picked(ViewContext& ctx, bool bolder) {
    const auto cs = picked_contours(ctx);
    const double d = offset_distance(ctx) * (bolder ? 1 : -1);
    std::vector<int> at;
    std::string why;
    if (!command::offset_contours(ctx.doc.journal, ctx.doc.font, ctx.selected, {cs.begin(), cs.end()}, d, offset_join(ctx), &at, &why)) {
        if (ctx.say) ctx.say(why, true);
        return;
    }
    pick_result(ctx, at);
    if (ctx.say)
        ctx.say(ctx.doc.journal.undo_label() + (cs.empty() ? " -- the whole glyph, " : " -- ") +
                    std::to_string(at.size()) + (at.size() == 1 ? " path." : " paths."), false);
}

void stroke_picked(ViewContext& ctx) {
    const auto cs = picked_contours(ctx);
    std::vector<int> at;
    std::string why;
    if (!command::expand_stroke(ctx.doc.journal, ctx.doc.font, ctx.selected, {cs.begin(), cs.end()}, stroke_width(ctx),
                                offset_join(ctx), stroke_cap(ctx), &at, &why)) {
        if (ctx.say) ctx.say(why, true);
        return;
    }
    pick_result(ctx, at);
    if (ctx.say) ctx.say(ctx.doc.journal.undo_label() + " -- " + std::to_string(at.size()) + (at.size() == 1 ? " path." : " paths."), false);
}

}  // namespace granjon::views
