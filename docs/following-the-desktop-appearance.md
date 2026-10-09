# Following the desktop's appearance preference

The app came up light on a dark desktop. Nothing in the window was at fault —
no stylesheet, every chrome widget stock, all of them already wearing whatever
GTK handed them. The app had simply never **asked**.

This page exists because the bug has now been hit four times in four codebases
(Plunk, Slate, delr, Cairn) and the fix is the same file every time. Copy
`Appearance.hpp` / `Appearance.cpp` and call `follow_system()`; that is the
whole of it. What follows is why each guard in that file is there, so nobody
tidies one away.

## Why the app has to ask at all

A GTK3 app got dark for free, because the preference **was** the theme: one
string, `gtk-theme-name`, and picking a dark theme was how you asked. GTK4 split
the two. The desktop now expresses "I want dark" as a separate preference, and
the toolkit deliberately leaves acting on it to the application — libadwaita is
where that acting usually happens (`AdwStyleManager`), and none of these apps
link libadwaita.

So the behaviour didn't regress. A responsibility moved, silently, across a
major version, and the app kept doing exactly what it always did — which was
nothing.

## Where the preference lives, and why not the obvious one

Two places claim to hold it. The obvious one is wrong:

- **`org.gnome.desktop.interface color-scheme`, via GSettings.** Readable, and
  what the GNOME control panel writes. It is *GNOME's private key*. Reading it
  directly works on GNOME and is blind on KDE, Xfce, Sway and everything else,
  and it hard-codes another desktop's schema name into a tool that has nothing
  to do with that desktop.

- **`org.freedesktop.appearance color-scheme`, via the XDG desktop portal.**
  The cross-desktop interface every desktop is expected to answer, and what
  GNOME's own guidance points at. Works unsandboxed, works inside Flatpak, same
  call everywhere.

Portal first; GSettings only as a guarded fallback, because the portal is
D-Bus-activated and an app launched early enough can start before it is up.

## The guards, and the failure each one is a scar from

**GSettings aborts the process on a missing schema.** `g_settings_new()` on a
schema that is not installed does not return null — it kills the process. The
obvious two-line fallback therefore turns "no GNOME schemas installed" into a
crash on startup, which is strictly worse than the light window it was written
to fix. So the schema is looked up (`g_settings_schema_source_lookup`) and its
key checked before anything opens it.

**Peel the variant, don't count the layers.** The portal wraps its answer, and
how many layers depends on which method answered: the older `Read()` returns a
variant holding a variant holding the number; the newer `ReadOne()` returns the
number. Encoding that difference at the call site is how a working app breaks
against a portal version nobody tested. `unwrap_u32` peels until it reaches
something that isn't a wrapper.

**Read, then subscribe — in that order.** Subscribing first is a race the wrong
way round: a change arriving between subscribe and read gets applied, then
overwritten by the stale startup value.

**Hold the proxy forever.** The `SettingChanged` subscription lives on it.
Releasing it silently demotes this to a startup-only read — which is correct
right up until the user flips the toggle to test it, i.e. always.

**Tri-state, not boolean.** `0 = no preference, 1 = prefer dark, 2 = prefer
light`. **No-preference is not light.** It means the desktop declined to say,
and the honest response is to leave GTK's own default alone rather than
override a theme the user may have set by other means (`GTK_THEME`, a
`settings.ini`). This is the guard most likely to be "simplified" into `!= 1`
by a later reader, which silently turns *prefer light* into *no preference* —
which is why the enum is named instead of left as bare integers.

## Where the call goes

`App::on_activate()`, **before the first window is constructed.** Not after —
the difference between starting dark and starting light and repainting is a
flash the user sees on every launch.

Two call-site judgments that don't travel automatically with the file:

- If `on_activate` can run more than once (an app that deletes its window on
  hide and rebuilds on re-activation), the call belongs inside the
  first-activation branch. `follow_system()` subscribes; calling it twice
  stacks a second proxy and a second subscription for no gain. One call follows
  forever.
- It goes **ahead of any icon-theme work**. A symbolic icon is recoloured to the
  theme foreground, so it should inherit the answer rather than be painted
  before the question was asked. (That is also the argument for shipping one
  symbolic SVG instead of a light and a dark PNG.)

## What it must never grow

An opinion about content. Chrome follows the desktop; the app's own painted
surfaces do not. Slate's cloth is green in a dark room, and stays green. If
this file ever starts deciding what colour *data* is drawn in, it has left its
job.

## Cost

One `.cpp`, one `.hpp`, one line in `on_activate`, one line in `CMakeLists`.
**No new dependency** — the D-Bus and GSettings calls are GIO, which gtkmm-4.0
already brings in. Every failure path logs what it found under the `App` area
and leaves GTK's default in place: a desktop with no portal, no GNOME schemas
and no opinion is a supported configuration, not an error.
