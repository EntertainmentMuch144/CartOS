# CartOS Accessibility — v0.1.27 optional release

Open **Apps > CartOS Accessibility** inside CartOS. This is an internal
screen, with the supplied accessibility icon; it does not add another
application to OnionOS. Use Up/Down to choose a row, A to open a section,
Left/Right or A to change a setting, and B to go back. On the section chooser,
A opens the highlighted section. Settings apply immediately and save in
`App/CartOS/settings.ini`.

## Visual accessibility

- **Interface text size:** Small (90%), Default, or Large (125%). Applies to
  browser labels, menus, search, details and both settings interfaces. Long
  labels fit with an ellipsis; larger help text wraps where necessary.
- **High contrast:** black interface backgrounds, white text and borders,
  yellow emphasis and selection outlines. Temporarily hides the custom
  background and takes precedence over Invert text colour. Artwork remains
  in its original colours. Turning it off restores the chosen background.
- **Reduced motion:** skips startup animation, snaps between games, opens the
  enlarged preview immediately, returns immediately on B, and skips the
  insertion animation when launching. Artwork browsing and game launching
  still work. The normal animation preferences are retained for later use.
- **UI dimming:** None, Light, Medium or Strong. Dims the custom background
  and neighbouring artwork, keeping foreground text and selection clear.
  This does not change the handheld's screen brightness.

## Controls and navigation

- **Confirm game launch:** OFF by default. Adds a Launch/Cancel dialogue for
  normal A launches, double-A quick launches, START, search results and random
  games. Cancel is selected initially. START cannot accept this dialogue;
  choose Launch and press the currently assigned Confirm button.
- **Button mappings:** assign Confirm/Open, Back, Refresh and Search among
  A/B/X/Y; assign the four navigation directions among the D-pad directions.
  Current assignments are shown beside each action and in control hints.
  Assigning a used button swaps the two actions, so mappings cannot conflict.
  Face buttons and directions stay in separate groups to keep both usable.
- **Reset button mappings:** asks for confirmation and resets only controls.

A always opens the enlarged artwork preview. START remains a single-press quick launch in the main browser by default.
It acts on release so holding START with L2/R2 for an OnionOS shortcut does
not also launch a game. In search START still shows results, and in Create
List it still creates the list. The existing double-A interval is unchanged
and is not configurable.

MENU, SELECT, START and the shoulder buttons are reserved from remapping.
CartOS suppresses its own actions while an OnionOS modifier chord is held;
OnionOS's keymon continues to receive the physical buttons. In particular,
SELECT brightness shortcuts, START volume-boost shortcuts and MENU shortcuts
remain system-controlled. This does not add new shortcuts to OnionOS.

### Recover controls

**Hold physical SELECT, then press START while in CartOS.** This immediately
restores the default button mappings, saves them, closes a pending preview
or dialogue, and opens CartOS Accessibility. It works independently of the
current mappings. It leaves every other accessibility setting unchanged.

If settings cannot be saved, the recovery still restores controls for the
current session. A saved malformed or conflicting mapping falls back to safe
defaults when CartOS loads it. You can also remove the `a11y_bind_` lines from
settings.ini on a computer to restore default mappings on the next start.

## Display preferences

- **Artwork size:** Default or Large. Enlarges artwork where space permits,
  on top of each console's existing size preference. A safe display area
  limits portrait/landscape images so labels remain clear. Proportions are
  preserved and no original image is modified.
- **UI element size:** Default or Large. Gives settings/options rows, app
  tiles and supported keyboard keys more room; long lists scroll to retain
  access to every row.
- **Hide secondary information:** hides the browser count/battery, duplicate
  system labels, search metadata and enlarged-image filenames/counters.
  Game names, list names, navigation hints, loading messages and the full
  Game Details screen remain available. Other show/hide settings are retained.
- **Scrolling speed:** Slow, Default or Fast for held browser navigation and
  scrolling in CartOS settings and Accessibility. Repeats are bounded at
  280, 180 or 110 milliseconds and never catch up by skipping many entries
  after a slow frame. Shoulder-button letter jumps remain immediate.
- **Enhanced selection indicator:** a strong four-pixel outline around the
  selected artwork, app tile, menu row or keyboard key. High contrast also
  supplies an outline when this preference is off.

## Audio and feedback

- **Navigation sounds:** OFF by default. Enables a short, quiet synthesised
  40 ms navigation tick, previewed immediately when enabled. On the handheld,
  the bundled SDL OSS driver uses OnionOS's existing libpadsp/audio service
  instead of trying to take over its MI_AO device. Audio opens only when
  needed and closes before any game/app handoff. No additional sound library
  or sound file is required.
- **Interface sound volume:** 0%, 25%, 50%, 75% or 100% (default 50%). Changes
  the amplitude of CartOS's tone only; does not change the system or emulator
  volume. The row stays greyed out and inactive until Navigation sounds is
  enabled. 0% is silent and releases the audio device. If audio cannot open,
  sounds turn off and CartOS reports that audio is unavailable.
- **Visual button feedback:** a brief static outline at the screen edge when
  a button action registers. It does not cover text or artwork, and has no
  movement or fade when Reduced motion is enabled. Four inset filled strips
  keep the left edge visible with the Miyoo renderer.
- **Vibration feedback:** OFF by default and shown **only** when SDL reports
  a working rumble actuator. Uses a short 25 ms SDL rumble request. Failed
  requests disable it; handoff, reset and shutdown stop the actuator.
  An unavailable device shows an explanation instead of a nonfunctional
  switch. The Miyoo backend treats every nonzero strength as simply ON; it
  cannot vary motor intensity, so there is no misleading strength control.

### Vibration compatibility decision

The bundled, pinned Miyoo SDL2 backend implements joystick/controller rumble
through the Mini's GPIO actuator interface and reports capability only after
successful GPIO setup. OnionOS also uses this actuator; see its
[rumble implementation](https://github.com/OnionUI/Onion/blob/main/src/common/system/rumble.h).
CartOS uses SDL's capability and timed rumble APIs rather than adding its own
GPIO code or dependency. Input shortcuts were checked against the
[OnionOS shortcut reference](https://onionui.github.io/docs/apps/shortcuts).

The target build links these APIs successfully. The supplied icon is packaged
unchanged in res/icon/accessibility.png. Preview artwork uses consistent
bounds before and after its text appears; revealing text does not move or
resize the image.

The matching SDL source archive and OSS build flags are included under
Source/SDL2-Miyoo for the updated runtime. The included enumeration patch
lets SDL probe OnionOS's virtual /dev/dsp without requiring a kernel device
node. It does not create a device node or alter OnionOS.

Actual vibration timing,
audio output and interaction with keymon still require the Miyoo Mini V4
checks in TESTING.md. Host tests verify capability gating and failure paths;
they cannot prove physical actuator or audio behaviour on another device.

## Reset and configuration

**Reset Accessibility Settings** is on the main Accessibility screen. It
requires confirmation, with Cancel selected initially. It restores all the
options above and the default controls. It leaves backgrounds, normal CartOS
settings, list order, collections, favourites, ROMs and saves alone. The
ordinary CartOS Reset settings action likewise retains accessibility prefs.

All preferences use `a11y_` keys in the existing settings.ini. Missing or
invalid values use safe defaults. Saves retain unrelated preferences,
comments and unknown keys, replace the file atomically, and skip writing
when nothing changed. A failed settings write is reported and a UI change
is rolled back. Recovery controls still take effect for the current session
if the card is not writable.

No accessibility option changes firmware, emulators, saves or OnionOS files.
Keep settings.ini when updating to retain these preferences.
