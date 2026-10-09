# ARCHITECTURE.md — what jot is

## What jot is FOR (Scott + Claude, Oct 4 2026)

Every task app answers "what do I do next?" -- OmniFocus best of all, but it
is a ledger of intentions: excellent containers, the METHOD left to a book on
the shelf, and no idea whether a piece of work is actually finished. A
project closes when the last box is ticked, and the boxes were only ever a
guess. ("Useful but incomplete -- it just checklists it, and what goes into
a package is left open, with no strategy." -- Scott.)

jot answers a different question: **what does this work need, and what is
missing?** It can, because a jot todo IS a note -- it holds the document, the
prose, the links and the history.

- **Done-when.** A piece of work says up front what finished means -- what it
  must produce or contain. Steps fall out of what is missing.
- **Finished is checkable.** jot can see whether the done-when is satisfied
  (attached, ticked, filed), so "done" is a fact, not a feeling.
- **Strategies are built in** -- shapes of work jot knows how to help with:
  *packet* (gather the parts, send once), *deadline* (work back from the
  date, pull the feeders forward), *errand run* (bunch by place), *routine*
  (it repeats; the history is kept).
- **One place, one time.** The documents live with the work they are for,
  and sending is one act, not a scavenger hunt.

OmniFocus's road (Inbox through notification actions) was the FLOOR. This is
the point. Next actions still matter; they come from what is missing rather
than being the whole model.

## The app in one paragraph

jot is a scribble pad that files itself. You open it and type into a
note; the organising comes after. Notes are markdown, arranged in a
tree where any node can parent any other, with images and media
embedded inline in the markdown body rather than existing as separate
node types. Tasks live inside that markdown as checkbox lines and are
gathered into GTD-shaped views. Nodes link to each other by identity,
so a link survives a rename and a move. GTK4 / gtkmm, C++23, Linux.

**The front door is a blank note, not a tree.** That follows from
"scribble pad": the capture path has to be the shortest path in the
app. Filing is a second act.

**And filing includes choosing where the notes live.** jot with no jots
folder is not an empty app — it is a scratch buffer you can type into
at once, and the question of where it should live is asked when you
leave, not when you arrive. s005 built the other order first and it was
wrong; the rule is strong enough to catch a whole milestone's design,
which is why it is restated here rather than left implied.

---

## Current state (s017)

jot's own surfaces are standing on top of the Cairn spine, reading a
folder of jots through `core::NodeSource`.

| Surface | Files | What it gives us |
|---|---|---|
| Persistence | `core/Project.{hpp,cpp}` | `core::Project`: a `NodeSource` over a jots folder. Atomic writes, deferred bodies, orphan recovery from front-matter ids. |

| Surface | Files | What it gives us |
|---|---|---|
| Node model + the seam | `core/Nodes.{hpp,cpp}` | `Node`, `NodeSource` (reads by id, writes that can refuse, kinded change notification), `MemoryNodes`, the fixtures and the stress generator. GTK-free. |
| The tree | `TreePane.hpp`, `TreePane.cpp`, `TreePane_dnd.cpp` | Pole B flattened: one row per visible node, full timed rebuild, collapse, three-zone drag (above / into / below) with a live-refusing indicator. |
| The note | `EditorPane.{hpp,cpp}` | Markdown body that styles itself as you type, write-through, click-to-tick checkboxes, a protected tell under the body. (The title entry was cut in s006; the name lives in the tree and the drawer.) Three views since s022: Source, Live Preview (the same buffer, marks tagged invisible except where the cursor is (line-level marks by line; inline ones by run since s026, `core::Run`; a bullet / task mark stays drawn on the cursor's line unless the cursor is AT it since s027, `core::mark_touched`), bullets/boxes/rules/pictures/Copy drawn over it in `widgets::DrawTextView`'s snapshot layer -- `core::live_view`), Reading (a second, read-only buffer from `core::render`). |
| Capture | `core/Nodes.{hpp,cpp}` (`capture_split`, `capture`), the header line in `Shell_zones/_helpers`, `App::on_command_line` | Text in, an unfiled top-level note out, nothing lost and nothing else moved. One split shared by the header box and `jot --capture`. |
| The pending spool | `core/Pending.{hpp,cpp}`, `App::on_command_line`, `Shell::drain_pending` | Where a capture waits when jot is NOT running: `~/.local/share/jot/pending/`, one front-matter file per thought, written atomically and exiting without a window. Drained oldest-first on the next launch, and ONLY into a jots folder -- the spool file is the only copy until a durable one exists, so flush comes before delete and a crash mid-drain costs a duplicate rather than a note. |
| The task model | `core/Tasks.{hpp,cpp}` | `availability()` derived (parent status, sibling order, done, defer), inheriting due/defer, `next_action()`, date parse/format, `core::TaskIndex` (membership only), grouping by parent; s032 the Logbook (`logbook()`, `group_by_day()`, `day_label()`) over `Task::finished`, which `MemoryNodes::set_task` stamps. GTK-free. |
| Repeats (s033) | `core/Repeat.{hpp,cpp}` | `Repeat` rule on Task; parse / text; calendar `repeat_add`; `repeat_next` (fixed or from done). The roll itself is in `MemoryNodes::set_task`: log a `LogRecord` to `history()`, move the dates, untick. GTK-free. |
| The desktop projection | `core/Projection.{hpp,cpp}`, `Desktop.{hpp,cpp}` | `core::project`: which todos reach the GNOME calendar drop-down and what each says, as a pure function of the model and an instant. `Desktop`: the only file that includes libecal — creates a local EDS task list, wipes rows with the `jot-` uid prefix, writes the set. One way, always. Optional at build time. |
| Due notifications | `core/Notify.{hpp,cpp}`, `Shell_helpers.cpp`, `install-desktop.sh` | `core::due_announcements`: which todos deserve an INTERRUPTION, as a pure function of the model and an instant -- and it filters by availability where `core::project` deliberately does not. A 60s tick; `Prefs::announced` remembers what has been DELIVERED (s015 -- not what was sent) and survives a restart. Click lands on `app.goto-node`. Needs the installed `.desktop` entry, icon and D-Bus service, or GNOME drops it silently. |
| The notification receipt | `core/Notify.{hpp,cpp}` (`Outbox`), `Notifier.{hpp,cpp}`, `Shell_helpers.cpp` | The only file that puts a notification on the bus, and the one thing `g_application_send_notification()` cannot do: come back and say whether it arrived. `Notifier` calls `org.gtk.Notifications.AddNotification` -- the same request GLib's own gtk backend sends, but as a method with a reply -- and answers three-valued: Delivered / Refused / NoService, told apart by D-Bus error code. `core::Outbox`: the pure ledger of the gap between asking and being answered (in flight, tries, prune), so one key is never asked twice at once and five refusals end it. `Shell::on_notify_receipt` is the ONLY writer into `Prefs::announced`, which makes the 60s tick the retry loop. NoService takes the unreceipted `send_notification` road and the status line says the confirmation is missing. |
| Residency | `core/Lifecycle.{hpp,cpp}`, `Shell.cpp` (the close handler), `Shell_helpers.cpp` (`request_quit`), `App.cpp` (`app.quit`) | `core::on_close` / `core::on_quit`: what the X means and what Quit means, as a pure truth table -- three answers where there used to be one, and two of them differ only in whether unsaved notes are about to be lost. Off by default. A vetoed close plus `g_application_hold()`; the Shell, the store and the 60s timer all survive the window. `app.quit` is MANDATORY, not decoration: GNOME's Background Apps Quit activates it over D-Bus and SIGKILLs an app that has not got one. |
| Inbox | `core/Inbox.{hpp,cpp}`, `InboxPane.{hpp,cpp}`, `Node::inbox`, `Shell::queue_inbox_refresh` / `on_clean_up` / `on_toggle_inbox` | s028. A MARK, not a place: every capture road sets `Node::inbox` on a note it makes (jot.json `"inbox": true`, only when set). `core::inbox_members` (tree order), `inbox_state` (Waiting / Filed = has a parent / Done = ticked todo), `clean_up` (clears processed marks, moves nothing). The pane is the third left tab, Notes \| Inbox N \| Today; processed rows stay dimmed until Clean Up (`win.clean-up`, Ctrl+Shift+K, greyed when nothing is processed). Keep clears one waiting row; the note menu's In Inbox toggles any note. Overturns s007's "there is no Inbox". |
| Tags as contexts | `core/Tags.{hpp,cpp}`, `TagsPane.{hpp,cpp}`, `Shell::on_show_tag` / `on_show_tags` / `queue_tags_refresh` | s035. The body is the source; every answer is a query over `core::scan`. `tag_key` (ASCII case fold), `tag_under` (nesting, downward only), `tag_list` (chips, implied parents, distinct-node counts), `tag_members` (available / waiting / notes, tree order; done counted not listed), `waiting_reason`. No inheritance. s035b: the TAG LINE (`find_tag_line`, `tag_add_edit` / `tag_remove_edit`, `clean_tag_name`) -- Note details' field writes it through `EditorPane::apply_outside_edit` (one undo); `md-tagline` bands it in Live / Reading. The fourth left tab; chips are ToggleButtons in a FlowBox, pressed = the filter. Refresh debounced 400 ms on any change (keystrokes too) and only while showing. Reached from the tab, View menu, `win.show-tags` (Ctrl+Shift+T), the drawer's tag chips (`DrawerPane::signal_tag`), Ctrl+click / Reading click on a `#tag` (`EditorPane::signal_tag_activated`). |
| Today | `TodayPane.{hpp,cpp}` | The report: Overdue pinned, then Today / Available / Flagged grouped by parent with the parent note's first line as the why. s039: Forecast, the fifth view -- `core/Forecast.{hpp,cpp}` `forecast(src, idx, now, ndays)` -> `days` (today + ndays-1, empty kept) and `later` (only days with something), each `ForecastDay{day, due, starts}`; effective due (>= now) and effective defer (> now), remaining todos only; days stepped by `next_day` (day_end + 1 s, DST-safe). The pane keeps a built-once strip of 8 ToggleButtons (relabelled per refresh), `.jot-tight` padding so five view buttons do not widen the side pane. |
| The markdown scan | `core/Markdown.{hpp,cpp}` | `core::scan`: blocks, inline runs, task checkbox ranges, byte↔codepoint offsets. GTK-free, 39 selftest checks. |
| The capture hotkey | `core/Hotkey.{hpp,cpp}`, `Keybinding.{hpp,cpp}`, `PreferencesWindow.{hpp,cpp}` | A global shortcut that runs `jot --capture`, set from jot's own preferences. `core::Hotkey`: which chords are fit to be grabbed desktop-wide, and the list algebra for adding jot's slot to -- and taking it out of -- gnome-settings-daemon's `custom-keybindings` without disturbing anyone else's. `keys`: the GSettings performing half plus an advisory conflict scan; every access goes through a schema LOOKUP first, because `g_settings_new` on a missing schema kills the process. GNOME-only, and it says so on any other desktop. |
| Outline undo (s045) | `core/Undo.{hpp,cpp}` (`Journal`, `snapshot`, `apply`), `NodeSource::restore`, `Shell::undoable`, `TreePane::journaled` | Every navigator verb runs through `Journal::run(src, label, ids, subtrees, op)`: before / after pictures of the touched nodes (subtrees for delete and move), undo / redo make the model match one side, writing only what the step changed. `restore` brings a deleted node back under its own id (Project: body file too). Keys are the tree's (capture phase, never during a rename); the Shell's actions are `win.undo-nav`, `win.redo-nav`, `win.new-sibling`, `win.indent`, `win.outdent`, `win.move-up`, `win.move-down`. New navigator verbs MUST go through `undoable()` / `journaled()`. |
| Packets (s044, J2) | `core/Packet.{hpp,cpp}`, `Node::packet`, `DrawerPane::fill_packet` | A note marked as a packet; its checkbox lines are the required items, each IN when a file (attachment or linked) sits on its line or it is ticked. `packet_state` derives everything from the scan -- no stored item list -- and `packet_line` words it. Gather, nudges and next year (later milestones) read the same state. |
| Todo cards (s042) | `core/RowLook.{hpp,cpp}`, `TaskCard.{hpp,cpp}`, `Appearance.cpp` (the stylesheet) | The ONE builder for a todo row (Today, Forecast, Tags, Find -- s043). Note-shaped rows (Find's note hits, the Inbox) wear the same `jot-card` class with `st-note` / `st-inbox`. `core::summarize` / `summary_text` (s043) is the line under each Today view's title. `core::row_look` decides what a card says -- a single `RowState` by precedence, the short due chip on calendar days, project / estimate / repeat -- and is pure and selftested; `task_card` only lays it out and adds `jot-card st-<state>` classes. Colours are per state, twice (light / dark), in Appearance's sheet; greys are the text colour at low alpha. A grey state also gets a word, so colour is never the only carrier. New lists use `task_card`, never a row of their own. |
| Notification actions (s041) | `core/NoticeAction.{hpp,cpp}`, `App.cpp` (`app.notice`), `Shell_helpers.cpp` (`notice_act`), `Notifier.cpp` (buttons, `withdraw`) | Every button on a notification is ONE application action, `app.notice`, whose parameter is a verb and a target: `done:0:<key>`, `snooze:60:<key>`, `remind:1:<key>`. The target is the announce key (`<id>@<due>`), and `check_notice_target` rebuilds it from the model before anything is written -- so a stale notice (rescheduled, a repeat rolled on, ticked, gone) changes nothing. Done goes through `set_done` (the one door: repeats roll, the Logbook stamps). Snooze / Remind touch only the notice: the key leaves `announced` and is parked in `Prefs::snoozed`; `due_announcements` skips a parked key. A press never presents the window; a jot started for one leaves after (an unrealized window is detached in `finish_quit`, since GTK4's close() ignores it). The packet's nudges (J2) are meant to add a key shape and a button list here, not a new action. |
| Preferences | `PreferencesWindow.{hpp,cpp}` | The ONLY place a setting lives (s016a, Scott's call). Four headings -- Capture, Notifications, Desktop calendar, When the window closes -- each check box with a note saying what it does. Rows drive `win.*` stateful actions rather than owning a bool. Scrolls. The Today footer keeps the STATUS lines (reports of what another process did) and a Preferences... button, and no switches. |
| Format bar, list typing | `core/Format.{hpp,cpp}`, `EditorPane` (`build_format_bar`, `apply_format`) | s024. `core::format(body, sel, Fmt)` -> one `FmtEdit` (codepoint range + replacement + new selection); GTK-free, selftested. Every verb toggles. EditorPane applies it as one user action (one undo). Buttons named `editor.fmt.*`, above the stack, hidden in Reading, insensitive when not editable. Ctrl+B/I/K on the body's own key controller (not app accels -- s016c). Text glyphs, no icons (has_icon lied under Xvfb). s025: `core::enter` / `core::indent` (list continuation, Tab / Shift+Tab nesting) in the same file; EditorPane `on_list_key` on a CAPTURE key controller, all edits through `apply_edit` (one undo; a no-op touches nothing). s027: numbered lists renumber -- `FmtEdit::then` carries the renumber patches, applied in the same user action; `core::renumber` after a text-view erase that spans a newline (idle; not during undo / redo); `core::backspace` takes a drawn bullet's mark in Live Preview. |
| CLI list / append | `core/Nodes` (`capture_list`, `append_tasks`, `capture_append`, `append_text`, `find_list_note`), `core/Pending` (`list:` / `append:` headers), `App` (`-l`, `-a`; the NAME is the first plain word), `Shell::capture_list` / `capture_append` + `drain_pending` | s025c. `jot --list NAME item...`: task lines on the note NAME (trimmed, ASCII case-insensitive, first in tree order, protected skipped) or a new top-level note. Live -> filed and answered on stdout; cold -> spooled with `list: NAME`, drained through the same function. |
| Reading view | `core/Render.{hpp,cpp}`, `EditorPane` (stack, anchors, clicks), `Shell` (`win.toggle-reading`, `on_read_link`, the image resolver) | s021. `core::render(body)` = the source minus its `Style::Mark` bytes, glyphs for bullets / boxes, one U+FFFC per rule or image, and `src_cp` (rendered cp -> source cp) for mapping clicks back. EditorPane holds Source and Reading as two pages of a Stack; the SOURCE buffer is the truth and Reading is re-rendered after each restyle. U+FFFC placeholders become child anchors in place (offsets unchanged). Box click -> `toggle_task_at` on the source; link click -> Shell; double-click -> Source at the mapped offset. Mode is `Prefs::reading`, Ctrl+E, written only by `apply_layout_state()`. Live Preview (Obsidian's third view) is meant to stand on the same map. s021b: a fenced block is ONE `Kind::Code` anchor (code + lang) drawn as `code_bubble()` with a Copy button; Source tints fenced lines with `md-codeblock` instead of Code. |
| Import | `core/Import.{hpp,cpp}`, `Shell::import_files` / `on_import_markdown`, `TreePane::attach_file_drop` | s021b. A .md/.markdown/.txt becomes a note: body as written (BOM, CR gone; UTF-8 enforced), title = first H1 else the stem, relative image targets that resolve become `file://` (linked enclosures). Two roads, one helper: `win.import-md` (Ctrl+Shift+M, multi-select, top level) and a GdkFileList drop on the tree list in CAPTURE phase (row under pointer = parent). s021c: `win.import-md-folder` + folders in a drop -> `core::plan_folder_import` (an `ImportItem` tree; hidden, empty and symlinked folders skipped; 2000-file cap) -> `Shell::import_item` makes the folder a parent note. Both Import items sit in the main menu beside Open. |
| Enclosures | `core/Enclosures.{hpp,cpp}`, `core/Project` (adopt, jot.json v3), `EditorPane` (drop, paste), `Shell_handlers/_helpers` (place), `DrawerPane` (the section) | Images as the first enclosure kind (s016b). The markdown `![label](attachments/name)` is the truth; `core::enclosures()` DERIVES the list from the body and joins on metadata (`AttachStore`: a dir plus filename -> mode/source/size/added). A jots folder keeps files in `attachments/` and metadata in jot.json; the scratch buffer stages files in `<data>/jot/scratch-attachments/` and keeps metadata in memory. `Project::adopt(from, att, move)` carries referenced files (move from scratch, copy on Save As), renaming on collision and rewriting bodies BEFORE they are written. The editor recognises (a capture-phase `GdkFileList` drop target; `paste-clipboard` when the clipboard is a picture and no text); the Shell copies first, references second. Embedded only; nothing is ever deleted. s017: each present row has a menu -- Open, Show in Files, Copy Image, Save a Copy -- on drawer-owned `encl.*` actions; the Shell acts (GtkFileLauncher, clipboard texture, GtkFileDialog + `core::copy_out`); every name is resolved through `core::enclosure_path`, which refuses escape. s018: ANY file is an enclosure; `core::enclosure_markdown` writes `![..]` for an image and `[..]` for anything else, so `is_image_filename` is the kind switch, not a gate. A non-image row shows its content-type icon and description (guessed from the name, nothing read); Copy Image is on pictures only. s019: LINKED mode -- a `[..](file:///abs)` link is an enclosure too, keyed in jot.json by its canonical URI (`core::linked_key`), mode "linked" with size + mtime; status OK / Missing / Modified by stat, nothing read. `enclosure_path` resolves a linked key to its own path. Verbs `encl.accept` (restamp) and `encl.relink` (GtkFileDialog, `core::relink`, then `Shell::retarget_everywhere` rewrites every note through the model). `retarget_references` is the general rewrite for either kind of key. s019b/c: a drop links or copies by `Prefs::drop_links`, flipped by SHIFT, which reaches jot on GNOME Wayland only as the MOVE action (Ctrl+Shift never does); the target accepts every action but never finishes a flip as MOVE, so the source deletes nothing; `tools/drag_source.cpp` drags a file under Xvfb. s020: CONVERT -- `encl.embed` ("Embed a Copy", linked+present rows: `core::embed_copy` = ingest_file of the linked path) and `encl.link` ("Link Instead", embedded rows whose meta.source is an absolute path -- `core::original_of` -- including a Missing embed; straight to the original if it exists, else a GtkFileDialog in its old folder; `core::link_instead`). Both rewrite THIS note only (`Shell::retarget_current`), not every note: a convert is a choice about how one note carries a file, a relink is a fact about the file. A protected note is refused BEFORE anything is copied. Nothing is deleted either way. |
| The drawer | `DrawerPane.{hpp,cpp}` | Everything about the note that is not the note. Name pinned; below it, collapsible sections declared in ONE TABLE (`kSections`: key, heading, default open, hide-when-empty) -- Todo, Structure, Links, Linked from, Tags, Enclosures, File, Identity. Headers carry counts. Open/closed is app-wide in `core::Prefs::drawer_open`, storing only what the user chose. A new property goes in a section or gets a table row; there is no bottom to append to. Enclosures (s016b) shows each attached file: a thumbnail for an image (decoded at 2x, cached on mtime+size) or the type icon and description for any other file (s018), size, origin, Missing. Links no longer lists attachments. |
| Menus | `Menus.{hpp,cpp}`, `Shell_zones.cpp` (`build_menu`), `core/Shortcuts.*` | Two menus (s016c). MAIN (hamburger): app-level only -- jots files, save, View and Diagnostics submenus, Preferences, shortcuts, About, Quit. NOTE (⋮ beside it): one model in `Menus.cpp`, shared with the tree's right-click; toggles are stateful actions drawn as check items, ticked by `update_note_actions()`. Settings are in no menu. No app-wide shortcut may be a text-editing chord (`core::steals_text_editing`, swept by the selftest); delete-note is plain Delete bound on the tree list, not an app accel. |
| Review + Projects | `core/Review.{hpp,cpp}`, `ProjectsPane.{hpp,cpp}`, `DrawerPane` (Structure › Review), `Shell` (`win.show-projects`, `win.mark-reviewed`) | s037. `core::is_project` (a todo directly under it, or a Children order / non-Active state), `review_list` (reviewable = Active / On hold, nothing Completed / Dropped above; due by end of today; oldest first), `projects_by_state` (strongest state in the chain), `project_counts`, `mark_reviewed` / `set_review_every` through `set_task`, `review_when` -- the words both surfaces use. `Task::review` (Repeat, off = weekly) + `Task::reviewed`; `set_task` leaves `modified` alone when only those changed; `make_task(false)` keeps them. ProjectsPane is the fifth left stack page ("projects"): Review / All projects as joined toggles, refreshed debounced only while showing. |
| Estimates (s040) | `Task::estimate` (minutes, 0 none; jot.json `estimate` when set), `NodeSource::set_estimate`, `core::parse_estimate` / `format_estimate` (Tasks), `DrawerPane` Time row (`m_est`, `estimate_picker`, `commit_estimate`), Today `when_line` ⏱, Search `TermKind::Est` (`est:N` at most N min, `est:none`; remaining todos only), funnel Time section (`win.find-est`, `query_est` / `query_set_est`) | A todo field: un-making a todo clears it. No project totals yet. |
| Find | `core/Search.{hpp,cpp}`, `SearchPane.{hpp,cpp}`, `Shell` (`m_find`, `m_notes_stack`, `win.find`), `EditorPane::select_range` | s038. `core::search(src, query, cap)` -> ranked `SearchHit`s (id, in_name, snippet, cp_start/len). The Notes page = SearchEntry over a Stack of the tree and the results. Results refresh debounced while a query is up. s038b: the field takes a query language -- `parse_query` -> `Query` (AND of clauses, each an OR of `Term`s: Word / Tag (prefix on tag keys) / State (`is:`) / Due (`due:`, "by", remaining todos only); `or` joins neighbours, `-` negates); `describe_query` words it for the summary; a filter-only hit's line is `status_line` (a todo's availability + due, a note's first line). PERSPECTIVES = `Prefs::perspectives` (name + query, app-wide, sorted), `perspective_save / _remove / _for / _named`. The funnel (`shell.find_menu`, `jot-filter-symbolic`) is a Gio menu over `win.find-show` / `find-due` (radio strings) / `find-flagged` / `find-open(name)` / `find-save` / `find-delete`; `sync_find_actions()` READS their state from the field's text (`query_show / _due / _flagged`) and the handlers WRITE the text (`query_set_*` -> `set_find_text`). `PerspectiveDialog` names one. Ctrl+J = `win.perspectives`. |
| The window | `Shell*` | `Paned` tree\|note, note actions greyed by selection, the model→surface callback. |
| Where the jots live | `Shell_{zones,helpers,handlers}.cpp`, `JotsFolderDialog.*`, `core/Project.*` | No default folder: a scratch buffer until named. `<name>.jots` folders, the header title menu button carrying name + path, adopt (scratch → folder), rename and relocate — both a rename of one directory, because nothing inside holds a path. |

`Shell::m_store` is typed as the INTERFACE; `Shell::m_project` is the
concrete handle, and exists only for the two things a jots folder can
do that a `NodeSource` cannot: flush, and name its own directory.
Constructing it is the swap point, and it was the whole of what s003
had to touch.

What the spine underneath carries, unchanged from s001:

| Surface | Files | What it gives us |
|---|---|---|
| `Named<W>` + live registry | `include/widgets/`, `Registry.*` | Mandatory widget names, dotted (`shell.sidebar`), `registry::dump()` for the live tree. `unregistered_t` ctor for per-row content. |
| Split class by category | `Shell.hpp` + `Shell_{zones,bindings,handlers,helpers}.cpp` | The header-as-index discipline, installed on day one rather than retrofitted. |
| Per-area logging | `Log.*` | spdlog named loggers, runtime-toggleable. Areas: App, Shell, Registry, Recents, Io. |
| Actions + parameterised actions | `Shell_bindings.cpp` | Gio action wiring, including the per-item recents action group. |
| JSON persistence pump | `core/Recents.*` | Encode/decode adjacent in one file. The pump discipline, with a working instance. |
| Forward/inverse text mapping | `core/TextMap.*` | Flattened-line ↔ codepoint-range mapping with a round-trip test. Directly relevant to the markdown editor. |
| Resource pipeline | `resources/`, `App.cpp` | Symbolic icon compiled into the binary, recoloured by theme. |
| Dialog lifetime | `AboutWindow.*` | Hide-on-close singleton, built lazily, owned by Shell. |
| Desktop appearance | `Appearance.*` | Follows the desktop light/dark preference via the XDG portal, with the GSettings fallback guarded. |
| Shortcut registry | `core/Shortcuts.*`, `ShortcutsDialog.*` | One list, two consumers: App wires accels from it, the dialog renders from it. Can't drift. |

**Cairn's demo pages are gone**, replaced by the real UI in s002. The
recents menu and its JSON pump survive without a job: they become
"recent vaults" the day D1 is answered, and a live exemplar is worth
more than a deleted one.

**Not present, and needed:** non-image media drop and linked (not copied)
enclosures; sibling reorder (drop-between);
backlinks; the GTD query views. GTK-side, still untouched:
`ListView`/`ColumnView` and the factory stack (Pole A — see D6),
`DrawingArea`, `Popover`.

### Build

```
./build.sh
./build/jot_selftest     # headless core, currently 464 pass / 0 fail
./build/jot
JOT_STRESS=5000 ./build/jot   # the D6 instrument: a synthetic vault
```

Deps: `libgtkmm-4.0-dev libspdlog-dev cmake g++`. Optional, for the
desktop projection: `libecal2.0-dev libedataserver1.2-dev` (Fedora:
`evolution-data-server-devel`). The PROBE additionally needs the running
daemon (`evolution-data-server`), not just the headers. `pkg_check_modules` is NOT REQUIRED for
it — without it the app builds whole and the feature reports itself
unavailable, which is what keeps an optional dependency optional. The
first dependency outside gtkmm/spdlog, and the reason there is a
`jot_desktop_probe` target: the writing half cannot be observed without
a session bus, so it has its own instrument rather than being taken on
trust. BOTH halves of the `#ifdef` are built before any ship
(`cmake -B build-noecal -DJOT_DESKTOP=OFF`); s009 shipped a fallback
branch nothing had ever compiled. Verified against
gtkmm 4.10.0 / g++ 13.3 / cmake 3.28 in the sandbox (Ubuntu); Scott
builds on Fedora, so sandbox-green is evidence, not proof.

For notifications: `./build.sh --install-desktop`, once. App id casing
is now LOAD-BEARING -- the `.desktop` basename must match it exactly or
the notification daemon drops everything jot sends.

App id: `io.github.scott8420.Jot`. GResource prefix mirrors it:
`/io/github/scott8420/Jot/icons/scalable/apps`. Both move together in
a one-pass rename. Verify the join with
`gresource list build/jot` after touching either.

---

## The model, as the requirements imply it

Recorded so the shape is visible while the forks below are still open.
**The node and the move are shipped (s002); links, tasks and the GTD
views below are still a sketch.**

### Nodes

**Shipped in s002 — this is code now, not a sketch.** The sketch below
is what got built, with `body` as a plain string and the flags reduced
to one (`protect`).

A node is the only entity. Every node can parent any number of
children; there is no folder/document distinction. (Notr had one, and
its whole drag-and-drop limitation traced back to it.)

```
Node {
  id          stable identity, generated once, never reassigned
  parent_id   empty for roots
  title
  body        markdown; images and media referenced inline
  created / modified
  ...per-node flags (a protect/lock flag is worth carrying from Notr)
}
```

**The tree is a projection of a flat node set**, reconstructed by
parent lookup. Notr had this right and then didn't use it: its drop
handler copied a node, appended the copy, and deleted the original,
which is why it could not move a subtree and would have broken every
inbound link the moment links existed.

**A move is one field write: `parent_id`** (plus a position among the
new siblings, since s002's drop-above/drop-below gesture has to mean
something the store can write down). Children come along because they
never referenced anything but their parent. This is
CANON's "tracking is an illusion; the model is the truth," and with
links in the picture it stops being a matter of taste — identity has
to survive a drag.

### Links

Links target **id, never title or path.** Titles get renamed and tree
position changes on every drag; a title-keyed link rots silently.

Candidate syntax `[label](jot:<id>)` — valid markdown for any outside
tool, human-readable label, stable target. If notes become files on
disk, the id must also live in front-matter so the file carries its
own identity.

**Backlinks are the feature.** A forward link is a convenience; the
reverse index ("what points at this note") is what makes a linked
structure worth having. One pass at load, maintained on save.

### Tasks

**Shipped in s007. The sketch this section used to carry — "checkbox
lines anywhere in any body are tasks, and the GTD views are queries over
every checkbox line" — was WRONG and is recorded as overturned rather
than quietly replaced.**

What it got right: the views are queries, not a second store. What it
got wrong: a checkbox line has no identity, and **you cannot hang a
field off something with no identity.** A due date, a defer date and a
flag need something to belong to.

So a **todo is a NODE** (D2), with optional task fields:

```
Task {
  is_task   the presence bit -- this node is a todo
  done
  due       epoch seconds, 0 == none
  defer     do not surface it before this
  flagged   "this one, today"
  status    None | Sequential | Parallel -- how this node's CHILDREN run
}
```

A node is a todo when `is_task` is set and a note when it is not — the
same no-split principle as requirement 7, applied again. Everything
built since s002 comes free: ids, links, backlinks, children, a markdown
body, the drawer, tags, drag-reparent, protection.

**Task state lives in `jot.json`** (D4), alongside the title and the
parent. The note files on disk carry none of it.

**No priority field.** Numeric priorities rot — within a month
everything is a 1, which is why OmniFocus dropped them. jot uses the
three things that do not: sibling order (already model state since
s002), `flagged`, and `defer`.

**Availability is DERIVED and never stored** — a pure function of
(parent status, sibling order, done flags, defer dates), living in
`core::Tasks` under the selftest. A stored availability flag is a cache,
and a stale one hides a task, and **a task wrongly hidden is a task you
do not do** with nothing on screen to say so.

`core::TaskIndex` holds membership in document order and no verdicts.
Today, Available, Flagged, Overdue and Scheduled are filters over it.

A checkbox line in a body (`- [ ] milk`) is still a real thing and still
ticks — it is an INFORMAL note to yourself that the report ignores. Two
todo systems that both look real, where only one reaches the report, is
how you stop trusting the report.

### Widget naming vs node identity — two vocabularies, kept apart

Widgets get dotted names (`shell.sidebar`, `editor.body`) in the live
registry. Nodes get ids. **These are different systems and should stay
visibly separate** — a future session must not try to address a note
through the widget registry. Notr blurred this line and it's part of
why its drag handler reached for tree-control APIs instead of the
model.

Per-row widgets in the tree view take `unregistered_t`: named for the
Inspector, not entered in the registry, because a `ListView` rebuilds
row content on every scroll and reorder.

Worth building early, alongside `registry::dump()`: a **model-side
dump** that prints the node tree with ids and link targets. Same
debugging payoff, other layer, and it's what you want the first time
a link resolves to nothing.

---

## Open architectural decisions

These are unresolved and everything else waits on the first one. See
ARC.md for status.

0. **(answered by looking, not yet decided)** D1 below is now the
   gating question for s003 and is answerable from the running app.

1. **ANSWERED (s003). Where the markdown lives** — one document (Notr's shape,
   modernised: one file, tree inside it, markdown as node bodies) vs
   a folder of markdown files (one `.md` per note, tree membership in
   front-matter or directory structure). Import/export and "images
   dropped into markdown" both push toward the vault; atomic save and
   simplicity push toward the single document.
2. **ANSWERED (s007). Task representation** — checkbox lines harvested by query
   (above) vs tasks as nodes with subtasks. Both are defensible;
   having both without deciding which is canonical produces two
   half-task-systems.
3. **ANSWERED (s004). libadwaita or hand-rolled GTK4** — no
   libadwaita, and the reason is the binding rather than the look:
   there is no official C++ binding and none planned, so adopting it
   from C++ means calling the C API by hand or vendoring an unstable
   third-party one. Folio has never used it either — its `adw_*`
   strings are variable names over ~1,700 lines of hand-written
   stylesheet. That leaves adw offering only its stylesheet, which is
   the one thing Scott has declined for a decade of sessions. Full
   reasoning in the s004 handoff. Original text below.

   **(original)** libadwaita or hand-rolled GTK4 — "modern GNOME look" visually
   *is* libadwaita (`AdwNavigationSplitView`, `AdwToastOverlay`,
   `AdwPreferencesDialog`, boxed lists). No prior project links it;
   `Appearance.cpp` exists precisely because they don't. A
   sidebar-plus-content notes app is the strongest case yet for
   adopting it.
4. **GTD state location** — inline in the markdown
   (`- [ ] call Bob @waiting due:2026-10-14`) keeps the file portable
   and is where Obsidian/Logseq converged; a sidecar index queries
   faster and keeps prose clean. Inline plus a rebuilt-on-load cache
   is the likely answer but it should be decided, not discovered.
5. **ANSWERED (s004). Does Folio's markdown editor lift?** No — Folio
   has no markdown editor. Its `Editor` is WYSIWYG over a
   `GtkTextBuffer` serialized to HTML, and markdown lives only at its
   import/export boundary with no inline parsing in either direction.
   jot's storage format IS markdown, so the HTML round-trip is exactly
   the layer we don't want. jot's scanner is its own
   (`core/Markdown.*`); what lifted from Folio was the idle-restyle /
   double-guard / never-fight-the-user discipline of its screenplay
   classifier. Full reasoning in ARC.md.
