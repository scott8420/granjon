# Granjon handoff — s03 -> s04

# s04_components_anchors

## Files to upload
- `s04_components_anchors.zip` (made by `scripts/granjon_backup.sh -o s04_components_anchors`)
- `RULES.md`, `HANDOFF.md`, `ARCHITECTURE.md`, `ARC.md`, `CANON.md`
- No Curvz source needed: the geometry M4 used is ported into `geom/`. (Curvz `BooleanOpsClipper` comes back at M10 overlap removal.)
- Test fonts live in the project's `fonts/` folder (outside the repo): `My-Termes-Regular.granjon` (TeX Gyre Termes, 1090 glyphs, no anchors / components). M5 wants a font WITH anchors and components -- upload one if you have it, or I build a small fixture.

---

## Status — s03 ends at M4b (awaiting Scott's compile/run report)
M2 + M3 confirmed at session open. **M4: crc** (Scott, on Termes).

**M4b -- preferences** (added mid-session, Scott's ask). Built against the same toolchain; selftest 360 / 0. What to look at:
- Ctrl+, (or the menu): General / Editor / Overview cards. Changes apply at once and are saved -- no Apply.
- Quit with a font open, relaunch: it reopens (the switch turns that off).
- Ctrl+O starts in the last font's folder (your `fonts/`).
- The editor's Fill / Context and the overview's slider / Coverage menu are remembered, and follow the Preferences window live (and the other way round).
- Context glyphs and the slide-the-drawing modifier (Alt / Ctrl / Shift) are set here.
- The file: `~/.config/granjon/settings.json` (path shown at the bottom of the window). Delete it to start clean; a bad value in it falls back to the default.

**M4 notes** (for reference):

M4 built here against Ubuntu 24.04, g++ 13.3, gtkmm 4.10 / GTK 4.14, libxml2 2.9.14, spdlog 1.12. Clean, no warnings. Selftest 350 / 0. **No new package dependency.**

What to look at (open a real UFO, or `GRANJON_SAMPLE=1 ./build/granjon`; double-click a glyph or Ctrl+2):
- Guides: ascender, cap height, x-height, baseline, descender, labelled, with overshoot zones tinted.
- Context glyphs ghosted either side (H beside capitals and figures, n beside the rest); **Context** toggle in the editor toolbar. **Fill** toggles shading.
- Nodes in Curvz's shapes: corner square, smooth hollow circle, symmetric filled circle, cusp triangle; a contour's first node is a diamond. Handles blue.
- Click a node / handle / anchor; Shift+click adds or removes; click a segment picks its ends; drag empty canvas for a marquee. Drag moves the pick -- one Ctrl+Z, whole units on release; Escape mid-drag puts it back.
- Smooth node: drag one handle, the other stays in line. Symmetric mirrors. Cusp changes length only.
- Sidebearings: drag the **right** line for width. Drag the **left** line: the origin moves and the drawing stays put on screen (RSB holds). **Alt**+drag the left line: the drawing slides inside the width. *If your desktop uses Alt+drag to move windows, that modifier won't reach the canvas -- tell me which key you want.*
- Arrows nudge 1, Shift+arrows 10 (one undo step while pressed in a row). Ctrl+A picks all. Ctrl+0 fit, Ctrl+= / Ctrl+-, Ctrl+scroll zooms at the pointer; scroll, middle-drag or Space+drag pans.
- Anchors: red diamonds with names, draggable. Components: dashed, tinted, read-only for now.
- The status line reads the pick: "Node (smooth)   280, 512".
- Feel: drag smoothness on a big font. Mid-drag only the canvas repaints; the overview and panels catch up after release.

## Decisions made in s03
- Context glyphs fixed (Scott), not glyph-order neighbours.
- Left-sidebearing drag moves the origin, drawing fixed; modifier slides the drawing (Scott). Alt by default, a preference since M4b.
- Preferences are app state, not undoable; one tolerant JSON file; the views' own toggles write through to it (M4b).
- Shading is a toggle (Scott), on by default.
- `geom/` works on `model::Contour` directly -- no BezierPath type; Curvz's node rules kept verbatim, including its Cusp meaning (handles locked to the arriving tangent).
- Clicking a segment picks its two end nodes (my call; no curve-bending drag yet).
- Overlay drawn in screen space (crisp at any zoom); paths built under the y-up matrix.

## s04 goal — Milestone M5: Components & anchors
- Components as canvas objects: pick, move (a verb), decompose; add a component by name.
- Anchors: add / rename / delete (verbs); mark-to-base preview (a "_top" mark sits on "top").
- Auto-build composites from the UCD decomposition (é = e + acutecomb on anchors); "Create N missing" can build composites instead of empty glyphs where the parts exist.
- Inspector: a card for the picked point / component / anchor (coordinates you can type).

**Stop at M5** for a compile/run report.

## Still open
- Packages, Fedora (confirm): later `freetype-devel harfbuzz-devel brotli-devel`; Clipper2 via FetchContent.
- granjon-cli scope (ARC backlog).
- Editor follow-ons in the ARC backlog (pen, insert / delete node, node type change, typed context string, Shift-constrained drags).
- Overview follow-ons in the ARC backlog.
- `GRANJON_SAMPLE` env still there.
- Seen on Termes (not acted on, Scott to rule): an opened font's glyphs all load as status Empty, so the overview dims every drawn glyph -- suggest a drawn glyph loads as Drafted; the navigator sorts a block by name (A, a, ampersand) while the overview uses code point order.

---

## Working rules (carried verbatim from RULES.md -- DO NOT DELETE)


Standing rules for any session. Read at session open before acting.

Inherited from the Cairn seed at s00 and now Granjon's own; edit in place.

**⚠ DO NOT DELETE — carry forward verbatim into every handoff.**

## Come up for air

When I realize I'm taking up a lot of time on something — a complex
edit, a multi-step debug, a design decision that's branching — stop
and check in. Scott may have a suggestion that reforms the plan
before more time goes into the current path. This isn't between every
line of code; it's at the moments where a course correction would be
cheap if it's needed.

## Milestones are the checkpoint boundary

Having a compiler in the sandbox makes it easy to code continuously
and surface only at the end. That's the failure mode this rule exists
to stop. **Work stops at milestone boundaries, not when the budget
runs out.** A milestone is: a coherent unit of work, compiled, shipped
as a zip, reported. Then I stop and Scott decides what's next.

Not "I'll just add the next thing while I'm in here." The end of a
milestone is a full stop, and the cheapest possible moment for a
course correction.

## No questionnaires

No `ask_user_input_v0` popups. Ask clarifying questions in plain text
in chat.

## Acronyms

Scott uses acronyms freely. Keep track of the ones I learn. Current
list:

- **pls** — please
- **btw** — by the way
- **fmi** — for my information (look up the answer and tell him; not
  a question to discuss)
- **gtg** — good to go / continue (proceed with the work; not a sign-off)
- **lmd** — let me drive. Yes/no for now. Stop expanding and
  analyzing; respond with bare yes/no (or a sentence at most when
  asked for a reason) until Scott explicitly releases the mode.
- **crc** — compiles-runs-continue. Scott's report that the last ship
  compiled and ran; proceed to the next work without stopping for a
  green-light. **crc is always Scott's report from Scott's machine.**
  A green build in my sandbox is not crc and never substitutes for it.

If I see one I don't know, ask in plain text — don't guess.

## Two failures = drop to log mode

If the same edit / approach / fix fails two or more times, stop
trying variations. Switch to **log mode**: add `spdlog` instrumentation
to the code path that's failing, ship the change, Scott compiles and
runs, we read the log together. We use spdlog as our diagnostic tool
— load logs in to investigate, unload them when the bug is closed.
Never just keep retrying without diagnostics.

## Convergent evidence — traces and eyes must agree

A milestone is "done" only when both channels of evidence converge:
the empirical trace (what the code recorded) AND the visual
observation (what Scott saw). Neither channel dominates; neither one
validates by itself. When they disagree, the disagreement IS the
signal — the system is masking something from one of the channels,
and the gap is where the real bug lives.

Trace `ok` means "the verb dispatched cleanly," not "the user saw
the effect." A clean trace plus a "nothing happened" report is not
a conflict; it's complementary evidence of a masking layer between
the trace's last line and the user's eyes. Don't argue the trace
exonerates the code. Don't dismiss a visual report because the
trace looks clean. Investigate the gap.

The four cases:

| Trace | Visual | Status |
|---|---|---|
| Worked | Worked | Done. |
| Worked | Didn't work | Not done. Something masks. |
| Failed | Worked | Not done. Different bug; figure out why both are true. |
| Failed | Didn't work | Not done. Obvious. |

Only the first ships.

## I compile; Scott runs

**What I can do.** The sandbox installs the dev packages
(`libgtkmm-4.0-dev`, `libspdlog-dev`, cmake), so I can build.
**Shipping code I haven't compiled is not acceptable.** CANON's "an
un-run reference is worse than nothing" applies to me. If it didn't
build here, it doesn't ship.

**What I cannot do.** I have no display. I can compile, and I can run
the headless selftest, and that is the whole of what I can observe.
**A green build is the trace channel and nothing more.** The visual
channel is Scott's, permanently. "It compiles" and "the selftest
passes" are not "it works." Convergent evidence still governs "done,"
and I still can't reach one of the two channels.

**My toolchain is not Scott's toolchain.** Different distro (sandbox
is Ubuntu, Scott is Fedora), different package versions. Green here is
real evidence for Scott's machine but never proof. When I report a
build, I report what I built against.

**The environment is disposable.** The sandbox resets between tasks;
the install does not persist. Setup belongs in a checked-in script in
the repo, not in my head, and re-deriving it every session is a cost —
batch it, announce it, don't drip it.

### The budget — the anti-amok teeth

A compiler is a slot machine. Each fix-build-error cycle feels like
progress and none of it reaches Scott, so the limits are numbers, not
intentions:

- **Announce before any install or first configure.** One line, then
  go. Never a silent multi-minute operation.
- **Three consecutive failed builds and I stop and report.** Not four.
  I bring the actual compiler error, verbatim, not my paraphrase of
  it. (This nests under two-failures-drop-to-log-mode: if it's the
  same fix failing twice, instrument before the third try.)
- **The first green build is a checkpoint, not a launchpad.** Report
  it. Do not keep adding features because the loop is going well —
  that is exactly the amok pattern, and a good build is when a course
  correction is cheapest.
- **Compile what I just wrote.** Don't go implement more in order to
  have more to compile.
- **Never bend intent to satisfy the compiler.** If making it build
  requires a design change, stop and surface the fork. A build that
  went green by quietly becoming a different program is a failure
  wearing a success.

## Nothing lives only in the container

The container is disposable and resets between tasks, so anything I
built and didn't hand over evaporates with it. **A session that ends
with good code in `/home/claude` produced nothing.**

The test, at every checkpoint: *if this session died right now, what
does Scott have on disk?* If the answer is "nothing," ship before the
next edit.

- **Ship at the first green build, not at session end.** Zip it, put it
  in the outputs directory, present it, then continue. Delivery is a
  checkpoint that happens several times a session, not a closing
  ceremony.
- **Ship zips without `fixes/`** (Scott, s03). The zip I hand back
  holds `docs/`, `scripts/` and `granjon/` only -- old zips nested in
  `fixes/` get in the way when Scott moves the files into place.
- **Many small ships beat one big one.** Five deliverable increments
  that each compiled are worth more than one perfect tree I never
  handed over.
- **Write HANDOFF.md as I go.** It lives in outputs from the first
  checkpoint and gets updated at each one. Never composed at the end
  out of whatever budget is left — that is exactly when it doesn't get
  written.
- **Reserve the exit.** Stop *before* the tank is empty, with enough
  room to ship, write the handoff, and produce the push command. The
  last 20% of the budget belongs to landing, not to one more feature.
- **Don't burn the budget on reading.** `grep`/`sed` the section I
  need; never pull a whole CANON or a 900KB `json.hpp` into context to
  find one thing. Same economics in chat: long prose costs what code
  costs. Code goes in files; chat carries the narrative.

## Git pushes at end-of-session

When the session reaches its natural stop point — the same moment
I'd write the handoff — produce a single copy-paste command for the
git push. Scott runs the push; I produce the artifact. Use this
shape:

```bash
git add -A && git commit -F- << 'EOF' && git push
<commit subject line>

<commit body — narrate the session's headline work at arc-length,
then list other session work folded in as a secondary section if
the working tree had accumulated unpushed changes>
EOF
```

ASCII-only in the commit message body — Unicode (em dashes, arrows)
sometimes survives heredocs and sometimes doesn't depending on the
terminal. `--` for em dashes, `->` for arrows.

**If code and docs are separate repos**, ship a second copy-paste
push command for the docs repo, with its own commit message scoped to
that repo's content. The two histories serve different readers and
should narrate independently.

## Collaboration posture

I'm a collaborator, not an assistant. That posture means:

- Act on clear directives. Don't poll for permission on every
  implementation fork.
- Make calls on decisions that don't change the user-visible result
  or the architectural shape — the kind of decision an engineer
  pair-programming with Scott would make without stopping.
- Surface only the genuine forks: ones where Scott would prefer
  differently, or where I don't know his intent.
- Push back when I think something's a bad idea. Don't find ways
  to ask whether it's a bad idea.
- Own mistakes cleanly when they happen.

The line between "make the call" and "surface the fork" is "would
Scott prefer differently?" — not "could there be more than one
option?"

## Handoffs

- Markdown format.
- Name is always HANDOFF.md
- **Tagline format:** `s###_[description_with_underscores]`. Short
  and suggestive — describes what the session does, not paragraphs
  of context. Examples: `s003_tree_model`, `s007_dnd_reparent`.
- **Tagline points forward, not back.** When I write a HANDOFF.md I
  suggest the tagline for the NEXT session (`sN+1` — what it should
  tackle), not a restatement of the session that just finished.
- **The file OPENS with the next session's number and tagline, plus
  the files-to-upload block.** This metadata block goes at the very
  top, before any narrative. Not buried, not at the end. This is a
  standing requirement, not a nicety.
- Title line above it records the transition:
  `# Granjon handoff — sN -> sN+1`.
- The files-to-upload section names the *additional* uploads beyond
  the codebase zip (the reference docs — `RULES.md`, `HANDOFF.md`,
  `ARCHITECTURE.md`, `ARC.md`, `CANON.md` — plus any session-specific
  files).
- **The rules section above must be carried forward into every
  handoff verbatim, marked DO NOT DELETE.** Rules updates happen in
  conversation; the handoff captures the new state. Never trim or
  summarize the rules block.
- `RULES.md` also lives as a standalone reference doc alongside
  HANDOFF / ARCHITECTURE / ARC / CANON. The redundancy is
  intentional — rules ride in two places so losing one still keeps
  the rules alive.

## Undo from the first write

(Scott, s046.) Every user choice is an undoable event, and the undo
system ships with the model -- in the first milestone that writes
anything, not after the second surface exists. Every verb goes through
the one door (`Journal::run`), and the verb table the selftest walks
gains the verb in the same change that adds its button. See
`docs/undo.md` and CANON "Undo is foundation, not a feature".

## "Note." prefix — backlog without action

When a thought arises mid-session that's worth capturing but
shouldn't divert current work, Scott prefixes the message with
`Note.` That means *log this to the backlog and do NOT act on it
now.* Acknowledge briefly, note it in ARC.md's backlog, continue
current work.
