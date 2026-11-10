# CartOS v0.1.27 optional release validation

This optional release includes CartOS Accessibility while
preserving its optional browser restore and whole-device random selection.
The new accessibility build still needs the real-device checks below.

## Automated checks

- Checked the random candidate pool includes every discovered system,
  including hidden systems, with one entry per normalized ROM path and no
  extra weighting from Recent/Favourite/custom lists or shared configurations.
- Checked app, utility, empty-ROM, and missing-launcher entries are excluded.
  Exercised the real Select-menu action with its only eligible ROM outside
  the current list and verified the existing preview animation is used.
- Checked empty-library handling without launching an app or changing lists.
- Checked the setting defaults OFF, including with older/missing/malformed
  settings, saves and reloads both choices, resets to OFF, and ignores old
  pending state while disabled. Checked per-system settings rows after the
  added general option.
- Saved/restored system, Recent, Favourite, and custom-list positions by list
  key and ROM path, including names containing spaces and quotation marks.
- Restored after reordering lists and inserting games ahead of the selection;
  the selected ROM, centred position, and remembered selection remained correct.
- Checked ordinary cold starts/manual opens, one-time state consumption,
  missing lists/games, hidden systems, normalized history paths, missing,
  empty, truncated, oversized, and malformed state, and a failed write.
- Re-ran the v0.1.26 artwork-index, SELECT-shortcut, and A/START input checks
  with AddressSanitizer and UndefinedBehaviorSanitizer.
- Built the OnionOS handheld app with the pinned Miyoo SDL2 runtime.
- Checked package contents and confirmed the v0.1.26 release is unchanged.

The emulator/app handoff logic and Startup/CartOS.sh /
Return/Restore-OnionOS.sh are unchanged. CartOS's launch script now chooses
OSS audio and its SDL runtime includes OSS support for OnionOS libpadsp. Renderer and input
changes for accessibility are described below. The existing return flag
still gates optional browser restoration; no extra settings file is added.

## Accessibility automated checks

- Built and linked the OnionOS ARM app with the existing Union toolchain and
  pinned Miyoo SDL2 prefix, including SDL audio and rumble capability APIs.
- Ran tests/accessibility_checks.cpp with AddressSanitizer and
  UndefinedBehaviorSanitizer. Checked safe defaults, invalid configuration,
  duplicate/conflicting mappings, 2,000 mapping changes, atomic saves, no
  write for unchanged configuration, and preservation of unknown preferences
  and comments. Checked a failed write rolls back a UI preference change.
- Checked confirmed accessibility reset, safe initial Cancel, independent
  general-settings reset, and recovery using physical SELECT+START.
- Checked controller, raw joystick and keyboard translations, mapped direction
  release, START on release, modifier suppression, each launch-confirmation
  entry path, cancellation and confirmation, reduced
  motion, and bounded held-scroll rates without burst catch-up.
- Checked the internal Accessibility app is discovered without creating a
  separate OnionOS app folder. Checked unavailable vibration has no switch
  and a failed rumble request disables vibration.
- Used SDL dummy audio to test queued tones, silence at 0%, separate amplitude
  choices and repeated open/close without leaking an audio-subsystem reference.
  An unavailable audio driver disables sounds with a message.
- Rendered 33 captures: browser, search, both settings screens, controls,
  options, create list, delete confirmation, details, enlarged preview and
  launch/reset confirmations. Checked the largest text, larger controls and
  artwork, high contrast and enhanced selection together at 752x560.
- Exercised 48 text/contrast/size/secondary-information combinations across
  browser, search and controls. Checked background-dimming pixels and the
  static feedback outline. Rendered large Accessibility at 640x480 through
  the same logical-size scaling used by CartOS.
- Re-ran the existing release_checks.cpp suite, including 5,000-image indexing,
  disk reuse, corrupt-cache recovery, A/START, SELECT shortcuts, all library
  random selection and optional browser restoration. No game or OnionOS app
  was started during host tests.

Host rendering and dummy audio are development checks, not a supported
computer version of CartOS or proof of performance on the handheld.

## Accessibility Miyoo Mini V4 checklist

1. Keep a stable App/CartOS copy. Install this optional release and retain
   settings.ini and BG*.png. Open Apps > CartOS Accessibility; check there is no additional
   Accessibility app in OnionOS itself.
2. Check default A preview, gallery Left/Right, A launch, B return, double-A,
   START quick launch, search, favourites/custom lists, alphabet jumps, random
   selection, app handoffs and GameSwitcher return.
3. Enable Large text + High contrast + Large artwork + Large UI controls +
   Enhanced selection together. Browse portrait/landscape artwork, long list
   names, search keyboard/results, details, all menu rows and both settings
   interfaces. Repeat at 640x480 on a compatible device if one is available.
4. Test each text preset and dimming level with light/dark backgrounds. Check
   high contrast temporarily hides BG and overrides inverted text, and turning
   it off restores the background. Check system brightness stays unchanged.
5. Remap Confirm/Back and swap D-pad directions. Verify hints/current assignments
   and key releases follow the maps, including held scrolling. Hold physical
   SELECT and press START to recover. Test the two confirmed reset choices;
   ensure ordinary settings, backgrounds, lists, favourites and saves remain.
6. Enable launch confirmation. Check Cancel is initially selected for A/A,
   double-A, START, search and random games. Cancel must not queue a game.
   Choose Launch and confirm; ensure the primary cartridge image is used.
7. Check A always opens the artwork preview and START launches directly,
   including after loading an old settings.ini with a11y_simple_navigation=1.
   Verify the removed preference is absent from the interface.
8. Check Reduced motion suppresses startup, browser easing and launch/return
   transitions. Check Slow/Default/Fast held navigation in the browser and
   both settings interfaces; a long pause must not produce a burst of moves.
9. Enable navigation sounds and try 0/25/50/75/100%. Check the system volume
   stays unchanged and games/apps have normal audio after handoff. Check the
   visual feedback edge with Reduced motion on and off.
10. If Vibration feedback is available, enable it and check a brief pulse,
    disabling it, reset, exit and game/app handoff. Check there is no continued
    vibration or input lag. If unavailable, confirm only an explanation appears.
11. Verify MENU/GameSwitcher, MENU+POWER screenshot, Mini SELECT+L2/R2 brightness
    and START+L2/R2 volume boost. Modifier chords must not also open options,
    quick-launch games or change CartOS settings. Check Mini Plus MENU+VOL
    brightness on that device when available.
12. Restart and verify persistence; refresh with X; test optional restore ON
    and OFF. Check cold boot time, browsing responsiveness, repeated long
    browsing and launches for memory growth, audio/display issues or crashes.

## Miyoo Mini V4 device checklist

1. Save your stable App/CartOS folder. Install this optional release, keeping settings.ini
   and your background images. Use your existing working CartOS return hook.
   First leave Restore browser position OFF (the default) and confirm a return
   from a game opens Recent. Then enable it in CartOS settings for steps 2-8.
2. In a system list, select a game well into the list and launch it. Open
   GameSwitcher and exit back to CartOS. Check the same list and game are
   shown, centred, with the primary cartridge image and normal browser UI.
3. Repeat from Favourite, a custom list, and Recent; test A/A, double-A and
   START launches. Recent may reorder when the launched game becomes newest;
   the selected game should still be found at its new position.
4. Open GameSwitcher directly from a game list and exit without launching a
   new game. Check that the previous browser list and selection return.
5. Launch a game, power off, let OnionOS resume it on the next boot, then
   leave via GameSwitcher. Check that the original list/selection return.
6. Shut down from the browser and boot normally. Check that Recent opens.
   Opening CartOS manually without a return request should also open Recent.
7. Launch from search. Returning should open that game's system list, without
   restoring the keyboard, search text, enlarged view, or alternate image.
8. Optionally change list/game ordering between sessions, remove the source
   list or game, or hide its system. A still-valid source should be restored;
   an unavailable source should fall back to Recent.
9. Disable Restore browser position, launch a game, and return. Check that
   Recent opens. Restart and confirm the setting stays OFF. Enable it again,
   restart and confirm ON persists; Reset settings should return it to OFF.
10. Confirm SELECT alone still opens options on release, SELECT brightness
   shortcuts leave options closed, and gallery/X-refresh controls still work.

11. From a system list, Favourite, and a custom list, choose Select > Play
    random game repeatedly. Confirm choices can come from other systems and
    use the normal preview/A-launch/B-back animation. Confirm no app or
    ~Filter/~Refresh roms entry is chosen. Hidden systems are eligible too.

To roll back, shut down and restore the stable App/CartOS folder. The optional
startup and return scripts need no replacement for this optional release or its rollback.

## Limits

- This restores the list and ROM that you left in CartOS. Switching to a
  different game within OnionOS GameSwitcher does not change that snapshot.
- Search query/results, per-list positions other than the source list, menus,
  and enlarged/gallery state are not persisted.
- State is saved only for game-list handoffs. Failed writes or missing/invalid
  state result in Recent; the emulator handoff remains unchanged.
- New physical audio, actuator timing, frame rate/memory use, system shortcuts,
  actual device input, emulator exit, and cold/resume boot behaviour still
  need the handheld checks above. Automated checks do not establish support
  for every OnionOS device or configuration.

## Accessibility source changes

Added:
- src/accessibility.hpp and src/accessibility.cpp: preferences, safe config
  updates, input translation/hints, bounded repeat presets and queued audio.
- src/accessibility_ui.cpp: dedicated four-section interface, internal icon
  routing, feedback, reset/recovery and consistent launch confirmation.
- tests/accessibility_checks.cpp and ACCESSIBILITY.md.

Updated:
- src/launcher.hpp / src/launcher.cpp: internal app discovery, persistence,
  input integration/modifier safety, repeat navigation, reduced motion,
  confirmation, proportionate artwork, secondary labels and readable layouts.
- src/draw.hpp / src/draw.cpp: scaled/measured text, contrast palette,
  selection outline, accessibility icon and safe artwork bounds.
- CMakeLists.txt: compile the two new implementation files.
- tests/release_checks.cpp: physical recovery and START-release assertions.
- scripts/package-handheld.py: include ACCESSIBILITY.md in the release ZIP.
- README.md, INSTALL.txt, CHANGELOG.md and this file.

The release App/CartOS/bin/cartos and ZIP are rebuilt from these sources.
No stable release, SD card content, startup/return hook, emulator or save was
modified during implementation.

## Accessibility feedback corrections

- Replaced the drawn icon with the exact supplied PNG in assets/accessibility.png
  and App/CartOS/res/icon/accessibility.png; image proportions are preserved.
- Removed the unnecessary direct A launch setting; A always opens the preview
  and START remains quick launch. The retired preference is ignored and cleaned
  up on save. The dependent sound-volume row is visibly grey
  and rejects changes while sounds are OFF, including with High contrast.
- Rebuilt the same SDL revision with OSS enabled and selected dsp audio for
  CartOS, so tones use OnionOS's libpadsp/audio service. The direct MI_AO audio
  path can conflict with that service. Added a smooth 40 ms tick, immediate
  preview on enabling, amplitude checks, and underlying-error logging.
- Matched recovery and footer hint font/capitalisation. Checked all four
  feedback strips by sampling their pixels, including the left edge.
- Checked identical selected-image pixel bounds immediately before and after
  preview labels appear, from both browser and search, and at step-two commit.
- Verified the Miyoo SDL rumble backend reduces any nonzero magnitude to ON;
  no strength setting is exposed. Physical audio and edge rendering still
  need the handheld checks above.
- Bundled the corresponding SDL source archive/build notes under Source/SDL2-Miyoo.
  Renderer/input source files are unchanged. OSS is enabled as a configure
  option; the included small SDL audio-enumeration patch permits OnionOS's
  virtual /dev/dsp endpoint without creating or modifying device nodes.

The follow-up also updates assets/accessibility.png, App/CartOS/launch.sh,
the bundled SDL library and its source/build notes. The original icon file
is byte-for-byte identical to the supplied attachment. Vibration strength
remains fixed by the backend; no intensity control was added.

## Smooth enlargement correction

- Capture the selected image's actual browser/search rectangle before launch.
  Centre off-centre selections without resizing, then use one smooth curve
  to the enlarged preview. Already-centred selections start growing immediately.
- Fit both endpoints before interpolation instead of switching viewports on
  the first launch frame; this removes the small initial enlargement jump.
- Preserve alternate-image proportions, stationary preview metadata, primary
  artwork insertion and B's shrink to the centred browser image.
- Check the first rendered launch frame matches the browser pixel bounds,
  growth is monotonic/bounded at 60 Hz, and both ends of the B transition match
  the preview and normal browser, including search and off-centre selection.
