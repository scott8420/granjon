# Widget naming — the fork, and which half to build

Naming widgets is not one decision. It's a fork, and copying whichever example
you saw first is how a project ends up carrying machinery it never needed. This
page names the two poles and the one question that chooses between them, so a
future you picks on purpose.

## Pole A — names for a debugger (what Cairn implements)

`Named<W>` mandates a name in its only constructor and registers the widget in a
live address book (`registry`) by construction. Names are **dotted-readable**:
`shell.sidebar`, `shell.recents_list`. The registry warns on a duplicate live
name and offers `dump()`.

The payoff is **debuggability**, nothing more:

- a backtrace frame names itself, because the widget has a name;
- `registry::find("shell.stack")` resolves a name to a pointer (call it in gdb);
- `registry::dump()` prints the live tree (call it in gdb or a crash handler —
  Cairn wires it to a button so you can see it).

That's the whole reason Pole A exists. There is no `invoke`, no `query`, no
`emit`, no second name. See `include/widgets/Named.hpp`, `include/Registry.hpp`,
and the "Registry" page in the running app.

## Pole B — names as an addressable API (what Curvz runs, Cairn does NOT)

Curvz addresses widgets by name from a scripting layer constantly, so its naming
is a different, heavier thing:

- a **two-name system**: a short **abbrev** (`tb_nod`) as the registry key
  (terse because scripts type it), plus a **long_name**
  (`main_toolbar_node_tool_btn`) that a resolver DB translates back to the abbrev
  on a lookup miss. Same data, two surfaces.
- the registry is an **address book *and* an event bus**: real user interactions
  and script-driven calls both route through `emit()` to subscribers, so a
  recorded script and a real click run the same code.
- the wrapper (`ScriptableWidget<GtkBase>`) carries `invoke`/`query`/`emit`, name
  *validation* (rejects DSL keywords), a pure-virtual `bind_canonical()` so a
  leaf can't compile without wiring its signal, and a signal-bound synchronizer
  for thread-crossing writes.

None of that is in Cairn, on purpose. Shipping the resolver here — with no DSL to
consume it — would be unexercised code masquerading as a working paradigm, the
exact failure a seed exists to avoid. So Pole B is documented, not run. The real
code lives in Curvz's `scripting/Scriptable.hpp`, `ScriptableWidget.hpp`,
`ScriptableRegistry.hpp`, `WidgetNameResolver.hpp`.

## The selector

> **Does anything address widgets by name programmatically** — scripts, macros,
> remote tests, record/replay?

- **No** → Pole A. The only reader of a name is a human at a debugger, so
  readable-dotted-names win and the abbrev/resolver/invoke/emit machinery is pure
  overhead. (Optik and Cairn.)
- **Yes** → Pole B earns its keep. The name is a stable addressable key, terse
  because it's typed, with a resolver for readability and an event bus for
  record/replay. (Curvz.)

One tell shows up even in the registry's tiny surface: **warn vs throw on a
duplicate name.** Cairn warns and keeps running (a seed should survive its own
teaching mistakes). A Pole-B project throws — a name collision means two things
claim one address, and addressing is load-bearing. If you're unsure which you're
building, you're building Pole A.

This is "check the elevation before fixing" (CANON) aimed at naming: don't inherit
the shape, choose it.
