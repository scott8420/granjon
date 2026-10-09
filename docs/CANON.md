# CANON.md — what we believe about building well

The body of beliefs carried forward from Curvz, Folio, Optik, Slate,
Plunk, Quire and delr. Each principle earned its place by failing
painfully when ignored.

**Provenance note.** Curvz's CANON is the long form — hundreds of
pages with the full worked example behind every rule. This is the
*portable* subset: the entries that apply to any project, stated
tightly. It is the half a new project actually inherits, which is why
it lives in the seed. Carrying the long form here was the original
mistake: a project starting from Cairn had to read Curvz's coordinate
spaces and scripting substrate to find the twenty rules that applied
to it. Where an entry names a Curvz session (s191, s203) it's a
pointer into that long form, not something you need to have read.
Entries specific to Curvz's vector model (the three coordinate
spaces) or its scripting substrate (RunContext, headless verb
singletons, row-bound Scriptables) are deliberately absent. A project
that grows one of those surfaces ports the matching entries then, not
preemptively.

This is canon — authoritative, durable, foundational — not gospel.
Beliefs revise when reality argues against them.

---

## The frame: structurally enforced discipline

> Make the right thing the easy thing and the wrong thing impossible.

Every "must remember to" rule is a future bug. Vigilance fails over
hundreds of sessions. Structure persists.

The unifying principle behind every rule below: **don't trust
yourself to maintain a discipline by attention; arrange the code so
the discipline is enforced by construction.** UUIDs over counters
(unique by construction, nobody checks for collisions). Encode/decode
pairs in the same file (changing one forces seeing the other).
Compile-time errors over runtime checks (the linker is the oracle).
A separator that can't appear in user input (presence of the
separator *is* the validation).

When you find yourself writing "remember to X" in a comment or a
handoff, that's the signal: there's a structural fix waiting that
makes the X automatic. The comment is a placeholder for not having
done the structural work yet.

This frame ranks above every specific rule below.

---

## Vocabulary first, file layout second

When a file or a codebase gets painful, the question isn't "where do
I cut?" It's "what categories of code does this contain?"

The vocabulary is the load-bearing decision; the file layout is its
expression. Let the code argue with the layout when it doesn't fit;
adjust the layout, keep the vocabulary.

**Test for vocabulary maturity:** can you answer "where does X live?"
deterministically from the vocabulary, without grepping? If yes,
the vocabulary is right. If no, you haven't found the categories yet.

---

## The header is the index

Wherever a class's implementation lives across multiple TUs, the
header becomes the table of contents. Without explicit pointers, the
split fragments findability instead of improving it.

Two pieces, both required:

1. **Top-of-file commentary block** naming the categories, the file
   convention, and concrete "where do I find X" examples.
2. **Per-method category tags** — a trailing comment on each
   declaration naming its category, spelled out, not coded.

```cpp
void on_save();         // handler: documents
void update_title();    // helper
void setup_menu();      // zone: menu
```

Together these make finding code a deterministic two-step: read the
header, read the tag, open the file. This is non-optional. A split
without it is half-done. `include/Shell.hpp` is the worked example
in this codebase.

---

## Discipline through inheritance

Project-namespaced wrappers around framework primitives let you make
project rules **mandatory at construction**. The framework class
becomes the base; the wrapper enforces the invariant in its only
constructor.

The motivating case: GTK widget names are enormously useful (the
Inspector, CSS targeting, a backtrace frame that names itself) and
`set_name()` is optional and easy to skip. Over hundreds of sessions
the Inspector becomes a forest of anonymous nodes. `cairn::widgets::Named<W>`
takes the name in its only constructor. Naming becomes a compile-time
invariant and an unnamed widget isn't expressible in app code.

Benefits beyond the original invariant: the widget vocabulary becomes
a curated subset instead of "anything in gtkmm"; project conventions
land at the wrapper level and apply uniformly; cross-cutting changes
become one wrapper edit.

**For a new project: write the wrappers on day one.** Don't wait
until the codebase feels the pain — by then the migration is a
multi-session refactor.

---

## The substrate registry is a live address book

The registry mirrors what exists **right now**, not what the source
could build. Conditionally-built surfaces only appear while alive.
Address what's there, not what could be there.

**Self-rebuild needs explicit pre-clear unregistration.** When a
handler rebuilds its own widget tree, the framework's destroy-idle
and the rebuild-idle run at the same priority with no ordering
guarantee: new widgets construct and register before the old ones
finish destructing, and the registry sees a duplicate name. The
structural answer is an explicit `clear()` that walks the subtree
being torn down and force-unregisters, then removes from the parent.
Idempotency lets the destructor's eventual unregister no-op safely.

The bait in this trap is that *timing patches feel diagnostic*.
Bumping an idle to a 1ms timeout widens the window and may make the
crash go away on your machine. The bug is still there. **A timing
patch that survives the structural fix is worse than no patch at
all**, because the next reader thinks the timing is load-bearing.

### The `unregistered_t` primitive

A widget can opt out of the registry via a parallel tagged
constructor: it keeps the type, the name (for the Inspector), and the
lifecycle; only the registry entry is skipped. Use it wherever a
widget is constructed in a shape that would force two live instances
to claim the same name — per-row content in a list, per-click
popovers built in a lambda, per-loop helper multipliers, per-rebuild
factory lambdas.

The test: imagine addressing the widget by name from a debugger. If
a per-row name like `row_visible_23` is useful, the widget wants
registration. If you'd instead address the *model object* the row is
drawn for, the widget wants the tag.

---

## Per-area logging with runtime toggle

Logging is a design surface, not a sprinkle of `LOG_INFO`. Every
method belongs to a *log area* named by concern, not by file. Each
area has a runtime-toggleable level. When something goes wrong: turn
the suspect area to TRACE, reproduce, read the trace.

Three benefits: granular signal-to-noise; a new reader can list the
areas and see the conceptual decomposition; and it pressures design —
a method that can't decide which area it belongs to is doing too
much.

**Don't strip diagnostics on milestone schedule.** Strip when the
*next* arc doesn't need them. Keeping a debug consumer alive one more
milestone costs near-zero; re-adding it for an unexpected diagnosis
costs a session.

---

## Pumps at conceptual seams

Find the seam where a concept lives, build a small abstraction at
that seam, compose callers. Structural fix; the problem class goes
away. A procedural fix patches every instance.

**Test for "is this a pump?"** — if the operation has a natural
inverse (encode/decode, parse/format, normalize/denormalize), the two
halves belong **in the same file** so changes force seeing both
sides. Bidirectional pumps prevent skew between halves.

When you write the same 5-10 lines in three places, that's a pump
waiting. Extract before the fourth — extracting after-the-fact
requires migrating prior sites, and that migration risks behavior
drift.

### A pump must be the only path

A helper that routes a class of operation correctly is only half the
structural job. If the thing it wraps is still reachable from the
same call sites, new callers (or old ones copied for a new shape)
skip the helper and the bug class returns through the back door.

Make it enforceable: private the wrapped field so callers physically
can't reach it, or move the helper into the same TU and make the
access static-internal, or at minimum a grep-level CI rule that fails
the build when the underlying form appears outside the helper.
**The pump existing is half the job; the pump being unbypassable is
the other half.**

---

## Round-trip fidelity is non-negotiable

User-typed text survives load-save unchanged. Period.

Sanitization applies to derived ids only — never to user-visible
content. When designing a format or a persistence layer, ask: "if the
user types X, does X come back exactly?" If you can't promise yes,
the format has a bug. Even invisible transformations (whitespace
normalization, encoding conversion, case folding) are losses that
surface eventually.

Prefer plain text formats specifically because round-trip fidelity is
verifiable by reading the file. **Binary formats hide skew.**

---

## Tracking is an illusion; the model is the truth

User input — mouse position, modifier state, key sequences — is a
*method of producing model mutations*. It is not the operation being
performed.

A "drag this node onto that one" gesture is one field write:
`node.parent_id = target.id`. A drag handler that instead copies the
node, appends the copy under the target, and deletes the original has
mistaken the gesture for the operation — and it will fail the moment
the node has children, or the moment anything else in the system
holds the node's identity.

Where this rule wouldn't hold: applications where the path of motion
is itself the data (paint brushes, signature capture, gesture
recognition). Most projects have none of those.

---

## "Guest in the framework's home"

When the framework behaves unexpectedly, default assumption: we're
using a widget non-idiomatically. Look for the idiomatic alternative
before fighting with workarounds. GTK has its own opinions about how
things compose; the project lives inside that opinion. The path of
least resistance is usually correct.

Same principle for any framework — sigc++, nlohmann/json, Cairo,
Pango. The framework's design choices encode constraints you don't
see at first glance.

### "What's different about this widget compared to the ones that work?"

When one widget misbehaves where its siblings behave, the productive
question is comparative, not API-level. Line up the broken thing next
to the working ones and look at the diffs — base class, ownership,
who constructs it, what the show path does. The diffs that turn out
to matter often look like trivial structural choices until you
measure. "Non-idiomatic" includes *composition, ownership and
lifetime patterns*, not just direct API calls.

---

## Concatenated paths are a silent-failure surface

When a framework joins two pieces of configuration into a runtime
lookup key — a prefix plus a filename, a namespace plus a class, a
base URL plus a route — the join is silent. If either piece is wrong,
lookups return null with no error. The build is green; the path
doesn't resolve.

The canonical case is `glib-compile-resources`, which concatenates
the `<gresource prefix=…>` with the literal `<file>` content. A file
declared as `<file>icons/foo.svg</file>` under prefix `/app/icons`
lands at `/app/icons/icons/foo.svg`. The fix is
`<file alias="foo.svg">icons/foo.svg</file>`.

**Whenever the framework joins configuration at runtime, the join
itself is a verification point.** Find the tool that prints what got
joined (`gresource list <binary>`) and run it after every change to
either half. Don't trust that the compile succeeded.

Same family: path joins by string concatenation rather than
`std::filesystem::path operator/`; HTTP route prefixes where one
layer strips a slash and another adds one.

---

## "In your face" design

Controls are visible, not buried in modal Preferences dialogs. Nested
scope lives in the inspector in scope order. The user can see the
model, point at the thing they want to change, and change it.

The "could a user point at it" test extends beyond UI. When designing
a data structure, a menu, an API surface — ask: "could a user or
another developer point at this concept and name it?" If yes, it
deserves first-class structural representation. If no, you may be
inventing concepts the system doesn't actually have.

---

## Don't predict, look

When uncertain about what code does, read it. Don't reason from
priors.

In long-running projects your model of the code drifts from reality.
Past-you wrote it; current-you remembers what past-you *intended*;
the actual code may diverge from both. This applies especially to AI
assistance — the assistant carries an accumulated model from prior
sessions that can be wrong in non-obvious ways. **When the assistant
states what code does without having just read it, that's a
hypothesis pending verification, not a fact.**

Two-attempts-then-instrument is the floor for diagnosing bugs. Logs
are cheap; theory-patches are expensive.

---

## The instrument is part of the claim

> Before believing "X is broken," run the case that must work.

"Don't predict, look" says stop reasoning from priors and read. This
is that rule's failure mode: you looked, and the thing you looked
*through* lied to you.

Plunk s002: a sandbox reproduction reported that a GResource icon
path didn't resolve. Four variations agreed. The obvious conclusion
was that the resource arrangement was wrong. It wasn't — the sandbox
had no gdk-pixbuf SVG loader, so *every* custom SVG failed, including
definitionally correct ones. A rewrite of working code was one step
away. The tell had been sitting in the output for rounds: system
icons resolved and every custom one failed.

The asymmetry to internalize:

- A **positive** result is largely self-validating. A broken
  instrument rarely produces a correct-looking success.
- A **negative** result is worth exactly as much as the detector
  behind it. Absence of evidence requires a working detector.

So the discipline is asymmetric too: believe your successes cheaply,
and make your failures earn it. **Run a control that should PASS.**
It costs one extra line.

### A precedent's preconditions travel with it

Same session: every symbolic icon in Adwaita declares
`width="16px" height="16px"`, 200 out of 200. That was taken as
licence to declare a width on an icon with a 1000-unit viewBox. But
Adwaita's viewBox is `0 0 16 16` — the numbers *agree*, so the
declared size costs nothing. On a 1000-unit viewBox the identical
attribute pins the drawing to a 16-pixel speck.

"200 of 200 do it" was true and useless. When lifting a pattern from
another codebase, find the invariant that makes it correct *there*
and check it holds *here*. The precondition is usually unstated,
because in its home context it's too obviously true to write down.

### A fix that works is not a fix you understand

**Understanding the mechanism is what tells you where else the bug
is.** In the icon case the same pinning was sitting unhit in the
About window, where a 48px-declared icon was about to be rendered
into an 88px hero. The empirical fix closed one instance; the
mechanism closed the second before it was ever seen.

Not every fix earns that effort. But any fix whose mechanism you
can't state is a fix you cannot generalise, and you will meet its
siblings one at a time.

---

## Check the elevation before fixing

When a fix feels load-bearing for a constraint that's everywhere,
check whether the constraint is real or inherited.

Most "obvious" architectural shapes aren't design decisions — they're
muscle memory dressed as design rules. The tutorial showed it that
way; the first instance copied the tutorial; every subsequent
instance copied the first. Nobody asked whether the shape was right.

**The tell:** a fix has to ride into every instance of a pattern, the
fix is correct at each site, and the pattern is widespread but rarely
questioned. When all three are true, step back: is this pattern
necessary, or inherited? Sometimes the answer is "necessary — changing
the shape costs more than the fix." Sometimes the shape itself is the
bug.

Curvz's worked example: four heap-allocated dialogs needed a
force-unregister dance on every close. The local fix was correct and
generalised. The elevation question was whether they should be
heap-allocated at all. Converting them to hide-on-close singletons
made the entire discipline evaporate.

Adjacent failure mode: treating "this is how we always do it" as
proof the shape is right. Conventions that survived because nobody
re-examined them aren't the same as conventions that survived
re-examination.

---

## A moved responsibility looks like a bug in your code

The app came up light on a dark desktop. Every instinct says find
what's wrong in the window — a stylesheet, a hard-coded colour, a
stale theme lookup. All wrong, because there was nothing to find. The
defect was an **absence**: the app never asked the desktop what it
wanted.

This is a class, not an incident. Across a major version a framework
moved a responsibility onto the application. GTK3 expressed "dark" as
the theme name, so acting on the preference and having a theme were
the same act; GTK4 split them and left the acting to the app
(libadwaita is where that acting usually lives). Nothing regressed.
The code kept doing exactly what it always did, which was nothing,
and *nothing* stopped being sufficient.

The diagnostic signature:

- **The symptom is a wrong default, not a wrong result.** Wrong
  results have a line of code that produced them. Wrong defaults have
  no line at all, so reading the code proves innocence repeatedly
  while the bug stays.
- **Every component individually looks correct.** Auditing them is
  time spent confirming nobody is at fault.
- **It appeared on an upgrade nobody made.** Distro moved, toolkit
  moved, the app is the same bytes.

The question that finds it is not *what is wrong here* but **what
used to be done for me that isn't any more.**

### Corollary: ask the cross-desktop interface, not the popular one

Two sources hold the appearance preference: GNOME's private GSettings
key (what the control panel writes, what every search result reaches
for) and `org.freedesktop.appearance` via the XDG portal (what every
desktop is expected to answer). Taking the first hard-codes another
desktop's schema name into your app and works only where that desktop
is installed.

**When a platform value has both a popular source and a standard one,
the popular source is a private implementation detail you're reading
over the owner's shoulder.** It'll be correct on the machine you
tested and blind everywhere else, silently.

### Corollary: a fallback that aborts is worse than the bug

`g_settings_new()` on an uninstalled schema does not return null. It
**terminates the process.** The obvious two-line fallback turns "this
machine has no GNOME schemas" into a startup crash — strictly worse
than the light window it was written to fix. A fallback path runs, by
definition, on configurations you did not test; it earns *more*
suspicion than the primary path, not less. Look up the schema, check
the key, then open it.

### Corollary: the tri-state that gets flattened

The preference is `0 = no preference, 1 = prefer dark, 2 = prefer
light`. **No-preference is not light.** Written as bare integers,
`!= 1` looks like a simplification and silently converts *prefer
light* into *no preference*; written as a named enum, the same edit
reads as the mistake it is. **Name the states of anything a later
reader could mistake for a boolean.**

---

## Lifetime shape is design, not convention

Long-lived objects (main window, panels, singleton dialogs) build
once and mutate often; substrate widgets inside register once and
live until shutdown. Short-lived objects (transient dialogs,
popovers, context menus) build, present, tear down; their registration
story is structurally hard.

The mistake is treating the choice as stylistic. It determines what
coordination discipline the object needs. Pick wrong and the
coordination work either disappears (you got lucky) or multiplies.

**The tell that lifetime is wrong:** a coordination problem the rest
of the codebase doesn't have. Look at sibling objects of the same
category. Are they coordinating around the same problem, or silent on
it? If silent, your object chose a different lifetime shape than its
siblings.

Don't convert lifetime shapes mid-session unless the conversion is
the headline. Make the flip its own milestone, and the follow-on
sweep a separate one, so each is evaluated on its own.

**Born-modern is the default now.** New dialogs and windows copy the
shape from a recent sibling: owned by the main window, `set_hide_on_close(true)`,
`present()` to show. The conversion arc is a legacy-rescue pattern,
not the standard.

---

## Singletons for many-fingered widgets, lazy and class-local

> A widget that (a) is reused from many fingers through the code and
> (b) carries no per-host state between presentations should be a
> single instance, lazily constructed on first demand, living until
> process exit, accessed via a static method on the class itself.

Class-local placement matters as much as the singleton. A bare global
or a `Utils::singletons` entry becomes orphaned from the type it
serves; readers write a second instance because they don't know the
first exists. `ColorPickerPopover::shared()` sits in the type's own
header, three lines from the class doc.

Lazy construction matters because GTK constructors often need a live
widget tree. A function-static *inside the accessor* runs the first
time something asks, by which point an asker exists.

```cpp
static T& shared() {
    static T* instance = new T();
    return *instance;
}
```

Heap allocation plus leak-at-exit is intentional: GTK destroys
widgets through their parent toplevel during shutdown, before a
function-local-static destructor would run.

**The two tests:** many fingers (held as a member in many classes with
no semantic distinction between copies — boilerplate, not variation),
and no retained state between sessions (everything needed arrives as
arguments to `open()`). Mutually-exclusive-at-presentation is a clean
tell that the second holds.

### Singleton callbacks must not capture by raw `[this]`

A singleton that stores host-supplied callbacks outlives every host.
Pre-singleton patterns were safe by *coupling lifetimes*: the host
owned the widget, the widget owned the callbacks. Singleton-ising
breaks that. A captured `[this]` from a dead host is a delayed crash.

Three workable patterns: a **liveness flag** the host owns
(`shared_ptr<bool>` captured by value, flipped false in the dtor,
checked at the top of the lambda); **capture stable references** only
(an id, an enum slot, a long-lived pointer, and walk the model);
or **host the callbacks in a long-lived parent**.

Why this isn't tracking: the popover can't reliably identify whether
its current session's host is alive — raw pointer identity isn't a
lifetime predicate. The right place to track lifetime is *at the
boundary the lifetime crosses*, which is the host's own callback.

### Re-parenting a managed widget across toplevels needs an explicit ref

`make_managed<T>()` + `set_parent()` means the parent owns the only
reference. For a singleton that moves between toplevels, `unparent()`
→ `set_parent(new_root)` finalizes the widget mid-transition and the
next call trips a `GTK_IS_WIDGET` assertion. One line at
construction fixes it:

```cpp
g_object_ref(G_OBJECT(pop->gobj()));   // we own a strong ref forever
```

Never released. The GObject equivalent of the singleton holding one
share.

---

## Belt-and-braces controllers when a keystroke must survive focus shifts

GTK4 routes keystrokes through whichever widget holds focus. A
window-level CAPTURE controller catches a key *unless* a descendant
claims it first, and "first" depends on focus chain plus internal
widget behavior in unpredictable ways. A TextView's edit machinery
eats text input; a tab strip eats plain space.

The robust pattern is **two controllers, same handler, different
anchors**: one on the window (CAPTURE), one on the inner target
(CAPTURE). Either fires, both call the same code, the keystroke is
claimed so it can't fire twice. The handler must be idempotent
against double-fire.

These are siblings, not duplicates — they cover different failure
modes of GTK's focus routing. Removing one re-opens the bug class it
was added to close.

---

## `Glib::RefPtr<T>` is `std::shared_ptr<T>` in modern gtkmm

Pre-2.62 gtkmm shipped its own `RefPtr` with member casts. Most
reference material on the web predates the change. In modern gtkmm
`Glib::RefPtr<T>` is a `using` alias for `std::shared_ptr<T>`, so
those member functions don't exist:

| Old (gtkmm-era) | New (std-alias) |
|---|---|
| `Glib::RefPtr<U>::cast_dynamic(p)` | `std::dynamic_pointer_cast<U>(p)` |
| `Glib::RefPtr<U>::cast_static(p)`  | `std::static_pointer_cast<U>(p)`  |
| `Glib::RefPtr<U>::cast_const(p)`   | `std::const_pointer_cast<U>(p)`   |

The error (`no member named 'cast_dynamic' in 'std::shared_ptr<T>'`)
is diagnostic but easy to miss, because the call *reads* like gtkmm.

---

## Audit checklists pay multiple dividends

When work is large enough to be uncomfortable to hold in your head,
write the checklist *before* doing the work. The artifact exists once;
the value compounds across the original migration, later verification,
and later diagnosis.

A good audit checklist states its scope explicitly (which sites, which
files, which predicates), classifies each item by required action,
carries line-number references as of audit time, and notes deferrals
with reasons. **The scope statement is as important as the findings** —
the most valuable diagnosis Curvz got from an audit doc was "the audit
was scoped past external callers," which was only an immediate insight
because the scope had been written down.

---

## Dead mapping entries lie about structure

A mapping table is a document about the structure of the code, not
just a lookup. Every entry tells a future reader: this association
exists, and the thing on the right is a real place.

Entries pointing at places that don't exist compile fine — they're
string-to-string. They make the mapping *look* exhaustive and the
codebase look more symmetric than it is, and the next reader spends
an hour looking for a section that was never built.

**Sweep dead entries when fixing adjacent live ones.** Grep each
target; if it has no binding site, remove it. The grep takes seconds.
When *reading* a mapping, spot-check a few — if any are dead, the
rest need an audit.

---

## Routing through indirection means routing through every level

A routing helper that walks a tree to put it in a desired shape must
touch **every level** the desired shape requires, not just the
endpoints. Flipping a leaf flag and a top-level group flag works
while the tree is exactly two layers and fails silently at three —
the leaf is "visible" in its own state but sits inside an unopened
parent.

Corollary for open/close verbs on nested UI: **open is
order-dependent** (outer-first, walk the ancestor chain), **close is
monotonic** (order-independent), and **close does not cascade up**
(that would close siblings the caller didn't ask about).

When the parent resolver doesn't know a key, **return empty — don't
guess a default parent.** A resolver that guesses turns a typo into a
side-effect.

---

## Branch off the latest, not the baseline

When a session has multiple milestones touching the same file, each
subsequent milestone's working copy must start from the **most recent
shipped version**, not from the session-start codebase.

The failure is mechanical: m3's source dir gets cloned from
session-start because that's the obvious source of truth, but m1
already edited the file. Applying m3 overwrites m1's edits. Symptom:
a method that was working produces a linker error — its *definition*
is gone, its declaration and call site survive.

Keep a `latest/` pointer at the most recent shipped tree. Copy from
it for any file an earlier milestone in this session already touched.
Session-start is fine only for genuinely new files.

---

## Verify before assuming fit

When new work needs to reuse an existing class, helper, or command,
the **first task of the milestone is to read that thing's body** —
before designing a replacement.

The discipline is symmetric. Sometimes the existing shape doesn't fit
and a parallel class is right (Curvz's `ZOrderCommand` keyed on an SVG
id that's empty for script-minted objects — multiple siblings would
collide on an empty key). Sometimes it fits exactly and the new work
is a thin shell over it.

The discipline isn't "always reuse" or "always parallel." It's
**always verify first.** The first-task read is the falsifiable step.

**The tail:** once a broad-capture class fits the first caller at a
seam, sibling callers tend to fit unmodified. Curvz ran seven
consecutive "fits unmodified" on one command across five sessions.
The structural property to notice: a command that captures a *whole
struct* rather than individual fields covers an arbitrary set of
single-field edits at zero marginal design cost. **Commands designed
for breadth pay back over the long tail of verbs they enable.**

The tail breaks where the capture boundary breaks. Recognising the
break *is* the verify-before-assume read; you can't skip the read
because the tail has been long.

---

## Close the architectural gap to unblock the work above it

When a milestone is blocked behind a pre-existing architectural gap
on the layer below, **close the gap first** — as its own milestone —
even though closing it doesn't move the visible scoreboard.

The shape: the intended milestone looks mechanical; the first-task
read discovers the method it would reuse has never been undoable /
containment-checked / gated; shipping over it would close the work on
a technicality while breaking an invariant the rest of the system
holds. The right answer is two milestones. m1 builds the
infrastructure and migrates existing call sites; m2 ships the work
over the now-stable base.

The temptation is real — the wrap is small, the arc is ready to close,
momentum says ship. **When you find a gap on the path, the gap is the
milestone.**

### Bridge milestones

A **bridge milestone** ships complete infrastructure with **no caller
yet**, gaining its caller in the immediately-following milestone. Its
acceptance criteria are **build + clean run + regression-free** — not
"a test passes," not "the user observes new behaviour."

This is not "shipping incomplete work." It's shipping a complete
piece of infrastructure under an explicit no-caller-yet contract, and
that contract *is* the acceptance criterion.

A milestone that ships infrastructure AND its first caller together is
not a bridge — it's a single-milestone arc that happens to include
infrastructure. The bridge shape applies when the split is deliberate.

**The bridge is a narrow exception, not the default order.** It is
for when the layer below genuinely BLOCKS the layer above — the
method you'd reuse isn't undoable, the path isn't containment-checked,
the verb isn't gated. It is NOT for when building underneath first is
simply the tidier way to work. Reaching for it on tidiness grounds
produces a run of milestones that each end underground, which is the
failure the next two entries are about. If you can't name what the
consumer is blocked ON, you don't have a bridge; you have a
preference for building bottom-up, and it should terminate at a
surface like any other milestone.

---

## A milestone ends at a surface

> **Banked — one instance, and that instance unverified.** Proposed
> from a working-practice conversation rather than earned by a project
> that hit the pain and recovered. The candidate instance is Optik /
> Quire / delr, all three pipeline-shaped tools whose disappointments
> are suspected to be UI-shaped; none has been read against the claim.
> Graduates when a project runs this and it holds, or when that
> postmortem confirms the diagnosis. Until then, treat as a strong
> working hypothesis, not settled canon.

Core work may lead inside a milestone. A milestone must not *end*
inside the core.

The argument isn't morale, though morale is real. It's that
**convergent evidence has two channels and the assistant can only
reach one of them.** A green build and a passing headless test say the
code does what it was told to do. They say nothing about whether it
was the right thing, and "is this the right thing" is precisely the
judgment that belongs to the person who can look at it. So a milestone
that lands entirely underground has been validated by the weaker
channel alone — and by the project's own standard for *done*, it
cannot be called done at all.

That is the strong form of the rule: not "invisible milestones are
unsatisfying" but **"invisible milestones are unverifiable."**

The second argument is that **the design questions live in the
surface, and they are invisible from underneath.** A packet sniffer is
the clean example: sockets, BPF filters, header parsing and ring
buffers could absorb six sessions with nothing to look at but a rising
test count. Meanwhile the packet list is where you must decide what a
packet *is* to this app — which columns, so which fields are
first-class; whether a row is one frame or one reassembled message;
what happens when fifty thousand arrive in a second; whether the
detail pane is a tree of decoded layers, which means the parser has to
produce a tree and not a flat struct. Every one of those is a model
decision. Build the engine first and they get answered by accident, in
whatever shape the sockets code found convenient.

**The surface is not the reward for the real work. It is the
interrogation that tells the real work what shape to be.**

What this does NOT say:

- Not "UI first, model second." The order inside a milestone is free.
- Not "split the work evenly between UI and core." Even-division is
  unenforceable — evenly by lines, by hours, by sessions? — and wrong
  in the cases where it's wrong. A parser that genuinely needs four
  days shouldn't spend half its budget padding a surface to hit a
  ratio. A rule that gets ignored the first time it's inconvenient was
  the wrong rule.
- Not "the surface has to be good." Crude is fine. A tree with three
  hardcoded rows and a pane that shows only a title is worth more as a
  checkpoint than a perfect model with a round-trip test, because the
  first one can be looked at and argued with and the second can only
  be taken on faith.

The test at milestone close is one question: **did anything change
that a person can look at?** Yes or no, checkable, and a "no" means
the milestone isn't finished — the same shape as the four-case table
in RULES.md, applied to the milestone rather than the bug.

Companion to **Visual gaps surface in field use** (below), which says
the eyes catch what the trace can't. This entry says the corollary:
**give the eyes something to catch.**

---

## Fixture data through the real seam

> **Banked alongside the entry above**, and the mechanism that makes it
> safe. Same provenance, same unverified status.

The objection to ending every milestone at a surface is a good one:
build the UI first and you get a model shaped by whatever the widget
made convenient. That failure is real and we have the specimen.

Notr — a tree-of-notes app — stored a flat node set keyed by parent
UUID, which is the right shape and makes a move one field write. Its
drag handler ignored all of that and worked through the tree widget's
API instead: it copied the node, appended the copy under the target,
and deleted the original. Consequences: it could not move a subtree at
all (it refused, and told the user to move the children first), and it
minted a new identity on every drag, which would have broken every
inbound link the moment links existed. The model was correct and the
UI quietly stopped using it.

So the discipline is not "build the model first, in private." It is:

> **The first surface is fed by fixture data, and the fixtures arrive
> through the same interface the real implementation will use.**

The packet list reads from a `PacketSource` that returns eight
hardcoded frames. Nothing captures anything. Later, a live capture
implements `PacketSource` and the list doesn't change by one line —
which is the proof that the UI never got to dictate the model's shape.
If instead the list reads a `std::vector` directly, you have built a
UI-shaped model and you will pay for it at the swap.

The swap is the test, and it's falsifiable: **if wiring the real
implementation requires changing the surface, the seam was drawn in
the wrong place.** That's a finding, not a chore — it means the fixture
milestone did its job and found the mistake while it was still cheap.

Why this composes with the rest of the canon:

- It IS "pumps at conceptual seams," applied at the start rather than
  discovered later. The interface between surface and source is a
  seam; fixtures are just the first thing plugged into it.
- It IS "tracking is an illusion; the model is the truth" — the
  surface is a renderer over the model, and the fixture proves it by
  making the model's only contribution an interface.
- It gives "don't predict, look" something to look at in the first
  milestone rather than the tenth.

The shape of a founding milestone, then: a window with the real
surfaces, fed by fixtures through the real seam, ugly and hardcoded
and *lookable*. Then the model thickens underneath something already
being watched.

---

## Prediction before observation

When shipping a milestone, state the expected result **before** the
run — test counts, acceptance criteria, what should visibly change.
The retroactive version ("yep, that's what I expected") carries no
information; anything can be rationalised after the fact. The
pre-stated version can be *wrong*, which is the whole point.

Curvz ran fourteen consecutive matching predictions. The streak is
informative on its own terms: it means the work's shape is known
before it ships, not discovered. A missed prediction would be *more*
informative than a match.

The shape: "expected: N pass / M fail / K error" alongside the
download link, with a one-line gloss of what the K errors are if
non-zero. For work without a test count, pre-state the acceptance
criteria instead.

---

## Visual gaps surface in field use, not in smokes

A clean trace is necessary and not sufficient. A test exercises what
it can MEASURE — dispatch, round-trip, stack effects, counts. It
cannot exercise what only an eye can SEE: a colour painted on a
zero-width stroke that renders nothing; a row added to a panel and
never removed; geometry that lands off-page because an axis flipped.

In each of those Curvz cases the assert passed truthfully at the data
level while the canvas was wrong at the pixel level. **A milestone
with a clean test and no visual confirmation is half-shipped.**
Every appearance-bearing milestone earns a visual-confirmation step.

---

## One-shot ships come from surfacing forks before zipping

When implementation forks are surfaced and resolved **before** the
zip — in the pre-implementation message, not mid-implementation — the
implementation reliably rides clean on first build.

The shape: the pre-implementation message names the architectural
calls being made without asking, surfaces the genuine forks, Scott
ratifies or redirects, *then* code writes.

What this does **not** claim: that one-shot ships are always
achievable. It claims that when they happen, it's reliably because
the forks were surfaced before zipping — not because the work was
simple or lucky.

Related: handoffs that name forks **with defaults** enable in-session
execution without polling. Handoffs that name forks without defaults
force a poll-or-guess.

---

## Three-doc top-level structure

Three documents at the project root cover three different concerns,
plus the per-session handoff:

- **`ARCHITECTURE.md`** — what the project *is*. Stable. Updated when
  major systems land or change shape. Reader: someone orienting.
- **`ARC.md`** — what the project is *doing*. Active. Updated as arcs
  progress. Reader: someone joining a session in progress.
- **`CANON.md`** — what we believe *carries forward*. Cross-project.
  Reader: someone starting a new project.
- **`HANDOFF.md`** — session-to-session continuity. See RULES.md.

Resist one giant README that mixes them. The scopes are genuinely
different and mixing dilutes each.

---

## Undo is foundation, not a feature (jot s046, Scott)

> "In Folio and Curvz we got to a point that the discussion on undo was
> important. But, we should integrate a system for undo early in a project
> and make any user choice an undoable event." -- Scott

Three projects (Folio, Curvz, jot) reached the undo talk late, and each time
the cost was not the journal -- it was the audit of every write path built
without it. Curvz retrofitted group / ungroup, layers and palettes as their
own milestones; jot spent s046 finding the doors its s045 undo did not cover.

- **The undo system ships with the model**, in the first milestone that
  writes anything -- before a second surface edits the model.
- **Every user choice is an undoable event.** A click, a tick, a field, a
  drag, a menu verb. A write that is not a user choice (a load, a migration)
  says so by name.
- **One door, and a gate.** User writes go through one wrapper; a selftest
  walks every verb and fails on a write that went around it, so "forgot to
  wrap it" is a red build, not a report.
- **A step is two pictures, by id and value** -- not a per-verb inverse, not
  a pointer. A verb added later is undoable the day it goes through the door.
- **Ctrl+Z reaches it from anywhere** but a text box -- the window, bubble
  phase; not an accelerator, not one widget's focus (jot s046a).

Compiled and exercised in Cairn: `core/Undo.hpp`, the Undo page,
`docs/undo.md`. **Banked -- three projects' pain, one seed instance.**

---

## What this document is not

- Not a coding-style guide. C++ formatting and naming conventions
  live elsewhere.
- Not architecture-specific. A project's own systems are documented
  in its `ARCHITECTURE.md`.
- Not a tutorial. Each section assumes the reader has hit the pain
  that motivated the rule, or trusts it by analogy.
- Not gospel. Each rule earned its place; no rule is above revision
  when a new pattern argues against it.

Add when a new belief is earned. Revise when one turns out to be
wrong or incomplete. Canon is durable but not frozen.
