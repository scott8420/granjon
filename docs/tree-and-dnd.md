# The tree, and drag-and-drop on it

> **⚠ This page is DISTILLED, NOT COMPILED.** Every other paradigm in
> Cairn builds and is exercised here. This one is not — Cairn has no
> tree and nothing to drag. It is written down because the alternative
> is a future you rediscovering it from scratch, and because Cairn's
> own rule says a paradigm that can't be honestly exercised gets
> *documented and pointed at its source*, never faked. The source is
> Folio's `src/Sidebar.cpp` (3,604 lines) and `include/Sidebar.hpp`.
>
> **It is also ONE instance.** Curvz has tree/list surfaces too and
> they have not been read. Per "a precedent's preconditions travel
> with it," a single instance cannot tell you which parts are
> essential and which are one project's habit. Treat the shapes below
> as evidence, not as canon, until a second instance agrees.

---

## The fork this page exists to name

GTK4 gives you two genuinely different ways to build a tree, and the
choice is architectural, not stylistic.

**Pole A — the model-view stack.** `Gio::ListStore` →
`Gtk::TreeListModel` → a selection model → `Gtk::ListView` with a
`Gtk::SignalListItemFactory`. Rows are *recycled*: the factory gets
setup / bind / unbind / teardown, and a row widget is reused for
different items as you scroll. Virtualised, so it stays fast at
scale. It is also nothing like GTK3's `TreeView`, has a genuinely
steep lifecycle, and every item has to be a `Glib::Object` subclass.

**Pole B — hand-rolled recursive boxes.** A `Gtk::Box` per level,
`Gtk::Revealer` per collapsible group, one row widget per node, all
of it rebuilt from the model on every structural change. No
virtualisation, no factory lifecycle, no `Glib::Object` wrapper. Row
widgets are ordinary widgets you own and can track in a vector.

**Folio chose Pole B** — for its binder, which is arbitrary-depth,
multi-select, inline-renameable, filterable and drag-reorderable,
across roughly 350 sessions. `SignalListItemFactory` appears in Folio
only in dropdowns. That is a real data point and it argues against
the reflex answer.

Read the choice the way CANON reads any architectural shape: ask what
invariant makes it correct. Pole B's invariant is **the tree is small
enough that a full rebuild is cheap**. A binder of a few hundred
nodes rebuilds imperceptibly. A tree of fifty thousand does not, and
at that size Pole A's virtualisation stops being optional.

The corollary matters for anything seeded from here: **if your tree
can plausibly reach tens of thousands of visible rows, Folio's shape
is the wrong precedent for you, however well it worked there.**

---

## What Pole B looks like

### Rebuild is the update mechanism

There is no incremental row update. A structural change calls
`rebuild_section(section)`, which tears down that section's box and
walks the model recursively:

```cpp
void add_node_recursive(Section section,
                        const std::vector<int>& path,
                        int depth,
                        Gtk::Box* parent_box);
```

Depth drives indentation. Collapsible nodes get a `Gtk::Revealer`
holding their children's box. The row widgets are tracked so
highlight and rename can find them later:

```cpp
struct RowEntry {
    Section          section;
    std::vector<int> path;
    Gtk::Widget*     row       = nullptr;
    Gtk::Label*      title_lbl = nullptr;  // in-place rename: the title widget
    Gtk::Box*        title_box = nullptr;  // where an entry gets swapped in
};
std::vector<RowEntry> m_row_entries;
```

This is the cost of Pole B stated plainly: **you maintain the row
index yourself.** Pole A's factory would hand it to you.

### Collapse state must survive the rebuild

Since rebuild destroys everything, expansion state lives outside the
widgets, keyed by something stable:

```cpp
struct CollapseEntry {
    Section          section;
    std::vector<int> path;
    Gtk::Revealer*   revealer = nullptr;
    Gtk::Label*      arrow    = nullptr;
    bool             expanded = true;
    std::string      iid;          // stable key for collapse persistence
};
```

Note the `iid` alongside the positional `path`. Folio added it later
(its comment marks the session) precisely because path is not stable
across a move — which is the same lesson the drop code below arrives
at independently.

### Positional internally, identity at the edge

Folio's sidebar works in `std::vector<int>` paths and converts to
identity only where it talks to anything else:

> the sidebar is an intrinsically positional widget — rows live at
> indices, range-select walks row indices, DnD computes target paths —
> and it rebuilds its rows from the model on every structural change,
> so this set is transient-by-construction with no stale-path hazard.
> It mints iid-keyed items only at its callback edge.

That reasoning is worth keeping even if you don't keep the code. The
claim isn't "positional is fine," it's "positional is fine *because
nothing positional outlives a rebuild*." If you ever cache a path
across a mutation, the invariant is gone and so is the argument.

---

## Drag and drop — the part worth lifting verbatim

This is the most transferable thing in Folio's sidebar, because every
piece of it is a scar.

### Three zones, and the 25/75 split

```cpp
DropZone compute_drop_zone(Gtk::Widget* widget, double y, bool is_group) const {
    int h = widget->get_height();
    if (h <= 0) return DropZone::After;

    if (is_group) {
        if (y < h * 0.25) return DropZone::Before;
        if (y > h * 0.75) return DropZone::After;
        return DropZone::Inside;
    }
    return (y < h * 0.5) ? DropZone::Before : DropZone::After;
}
```

A leaf gets two zones; a group gets three, with the middle half
meaning "into." The asymmetry is the whole usability of tree DnD —
without an inside zone you cannot nest, and without before/after you
cannot order. The `h <= 0` guard matters: an unrealised or
zero-height row would otherwise divide into nonsense.

Feedback is CSS classes on the row (`binder-drop-before`,
`binder-drop-after`, `binder-drop-inside`), applied on motion and
stripped when the target or zone changes. No custom drawing.

### Reject drops into your own subtree

On motion, before highlighting:

```cpp
// Reject drop onto any item that is part of the drag
for (const auto& p : m_drag.all_paths)
    if (p == path) return Gdk::DragAction{};

// Block dropping onto a descendant of any dragged node
for (const auto& src : m_drag.all_paths) {
    if (path.size() > src.size()) {
        bool is_desc = true;
        for (int i = 0; i < (int)src.size(); ++i)
            if (path[i] != src[i]) { is_desc = false; break; }
        if (is_desc) return Gdk::DragAction{};
    }
}
```

Dropping a node into its own descendant detaches the subtree from the
tree and makes a cycle. Nothing downstream survives it. The check is
cheap and belongs on the motion handler so the cursor shows the
rejection, not just on drop.

### The `alive` flag — the lifetime scar

**A drop-target callback can arrive after the rebuild destroyed its
widget.** Drop triggers a rebuild; the rebuild destroys every row;
`signal_leave` for the old row is still in flight.

```cpp
auto alive = std::make_shared<bool>(true);
widget->signal_destroy().connect([alive]() { *alive = false; });

drop_tgt->signal_leave().connect([this, widget, alive]() {
    if (!*alive) return;
    ...
}, false);
```

This is CANON's liveness-flag pattern — the `shared_ptr<bool>`
captured by value so the flag outlives the object it describes —
arrived at independently in Folio's sidebar. Two independent
discoveries of the same shape is the strongest evidence a pattern is
real.

### Defer the drop, capture the state first

```cpp
// Capture everything needed now — signal_drag_end fires before the
// idle and clears m_drag, so we can't read it there.
auto src_paths = m_drag.all_paths;
Glib::signal_idle().connect_once(
    [this, section, path, zone, src_paths = std::move(src_paths)]() {
        execute_drop(section, path, zone, src_paths);
    });
return true;
```

Two things at once. The mutation is deferred out of the drop handler
so the rebuild doesn't happen underneath GTK's drag machinery — and
because `signal_drag_end` runs *before* the idle and clears the drag
state, everything the deferred work needs is copied into the closure
first. Reading `m_drag` inside the idle reads cleared state.

### Moves are keyed by identity, not path

```cpp
// Snapshot stable node IDs before any mutation so we can find the
// actual landed paths afterwards regardless of index shifting.
```

Then sources are sorted deepest-first, highest-index-first so each
removal doesn't shift the paths of items not yet moved, the insertion
index is adjusted when a source was a sibling before the target, and
after the rebuild the landed paths are looked back up by id.

**This is the whole of the lesson.** A tree move is a model
operation with an identity, not a widget operation on a position. The
counter-example, from an older app of ours: a drop handler that
copied the node, appended the copy under the target, and deleted the
original. It could not move a subtree at all — it had to refuse and
tell the user to move the children first — and it silently minted a
new identity on every drag, which would have broken every inbound
link the moment links existed.

Note how much of Folio's `execute_drop` is *sibling-index
arithmetic*. A model where a node stores its parent's identity rather
than living at an index in a parent's vector does not have that
arithmetic: a move is one field write and the children follow because
they never referenced anything but their parent. **Before lifting
this code, check which model you have.** The identity discipline
transfers; the index bookkeeping may be solving a problem you don't
have.

---

## If you build Pole A instead

Not exercised anywhere we've read, so this is a pointer, not a
recipe. The pieces you will need, in order: a `Glib::Object` subclass
per item; a `Gio::ListStore` of those; a `Gtk::TreeListModel` whose
child-model callback returns the children of a given item (or null
for a leaf); a selection model; a `Gtk::SignalListItemFactory` with
all four of setup / bind / **unbind** / teardown wired, not just the
first two; and `Gtk::TreeExpander` in the row to get the expander
arrow and indentation.

The two traps worth naming ahead of time:

- **Rows are recycled.** Anything you connect in `bind` must be
  disconnected in `unbind`, or a row will fire handlers for an item
  it no longer shows. This is the factory equivalent of the `alive`
  flag above.
- **Per-row widgets should take the `unregistered_t` tag.** Row
  content is rebuilt constantly; the registry is a live address book
  of meaningful surfaces, not a dumping ground for recycled row
  internals. See `include/widgets/Named.hpp`.

---

## The stone this page is waiting to become

When a project seeded from Cairn builds a real tree, the shapes that
survived contact come back here as compiled, exercised code and this
banner comes off. Until then it is a map with a legend that says
"drawn from one survey."
