# CartOS patch notes

Changes are listed newest first. Each entry describes the user-visible changes and any important compatibility notes.

## 0.1.27 — Optional release: accessibility, random games, browser restore and recent apps

- Show launched OnionOS apps alongside games in Recent, with the newest first.
  Use A to reopen an app. Reopening an item moves it to the front without a
  duplicate. Recent app entries use the installed app's current icon and launcher.

- Remove the unnecessary direct A launch preference; A always previews artwork
  and START remains the quick-launch shortcut. Ignore the retired saved key.
- Enlarge the selected cartridge from its exact displayed rectangle in one
  smooth curve, including search and off-centre selections. Remove the initial
  size jump and keep preview text, B return and insertion poses consistent.
- Use the supplied accessibility icon and grey out Interface sound volume
  while navigation sounds are disabled.
- Route the new quiet navigation tick through OnionOS's existing audio service
  using the bundled SDL OSS driver. Preview the tick when enabling sounds.
- Match the control-recovery hint style and draw visual feedback using four
  inset filled strips so the left screen edge is included.
- Keep enlarged artwork bounds consistent when preview labels appear,
  including search and the transition into the game-launch animation.
- Document the Miyoo rumble actuator has fixed strength; keep timed on/off
  feedback without a nonfunctional strength setting.
- Add internal Apps > CartOS Accessibility with four sections and its own
  accessibility icon. No separate OnionOS app is installed.
- Add persistent text-size presets, high contrast, reduced motion, UI dimming,
  safe action/D-pad remapping and optional game-launch confirmation (OFF by
  default, including START). Keep normal A previews and START quick launch.
- Add larger artwork/controls, optional secondary-information hiding,
  bounded scroll-speed presets and enhanced selection outlines.
- Add optional navigation tones, independent interface sound volume, static
  visual button feedback and capability-checked, timed vibration feedback.
  Sounds and vibration start OFF; unavailable vibration has no toggle.
- Add a confirmed accessibility-only reset and physical SELECT+START control
  recovery. Preserve reserved OnionOS modifier buttons and shortcuts; START
  quick launch acts on release without adding a second launch step.
- Preserve unrelated and unknown settings.ini entries. Use safe defaults,
  atomic saves and no write when configuration is unchanged. Keep all current
  random-game and browser-restore behaviour below.
- Fit enlarged text/artwork within safe layouts and retain gallery, primary
  image restoration, settings, lists, favourites and normal OnionOS handoff.

- Fix Select > Play random game choosing only from the current list. Choose
  uniformly from all unique ROM paths in discovered system libraries instead.
- Include systems hidden from vertical navigation, exclude apps and utility
  entries, and avoid extra weighting from Recent/Favourite/custom-list copies.
- Use the selected game's system and the existing preview/launch animation.
  Show No games available safely when the library has no eligible ROMs.
- Add Restore browser position to CartOS settings, OFF by default. Save the
  choice in settings.ini, turn it off with Reset settings, and ignore/discard
  any pending position while it is disabled.
- When enabled, remember the source list and highlighted ROM before a game launch or
  GameSwitcher handoff from a game list.
- Restore that list and centre the same game in the normal browser when
  CartOS returns, using list keys and ROM paths rather than changing indexes.
- Keep ordinary cold starts on Recent; fall back to Recent when saved state,
  the original list/game, or a visible source system is unavailable.
- Store the pending position on the SD card so returning after a power-off
  game resume can still restore it. Consume each pending position once.
- Reuse the existing v0.1.25/v0.1.26 startup and GameSwitcher return hook.
  Search launches return to their system list; search and artwork-page state
  are outside this release's scope.

Optional release. The standard v0.1.26 release is unchanged.

## 0.1.26 — Faster artwork loading and START quick launch

- Open browser options when SELECT is released alone. SELECT combinations
  suppress CartOS actions so OnionOS brightness shortcuts do not also open
  options or move through the browser.
- Save a lightweight extra-artwork filename index for each system between
  sessions. Enlarged previews use the saved index instead of rescanning the
  artwork folders; images continue to load on demand.
- Extend X refresh to rebuild the artwork indexes and clear cached artwork,
  making added, removed, or renamed images available after refreshing.
- Rebuild missing, incompatible, or corrupt indexes automatically. The first
  index build and X refresh still scan artwork filenames.
- Press START on a valid ROM in the main browser to play the existing launch
  animation and launch through OnionOS without pausing for a second A press.
- Preserve A's enlarged preview, double-tap A, B's return animation, image
  ordering, and START's contextual functions in menus and search.
- Keep the existing v0.1.25 boot and GameSwitcher hooks; no hook update is
  needed when upgrading from a working v0.1.25 installation.

Tested on the Miyoo Mini V4. See INSTALL.txt for updating and artwork refresh.

## 0.1.25 — Navigation, list ordering, and settings

- Fix returning to CartOS from GameSwitcher after a game resumes from a power-off by preserving the game-session marker on the SD card; update the startup hook in place and clear the marker after CartOS is queued.
- Reorder CartOS Settings to: Animation speed, Game title size, Show game counter,
  Show battery level, Show control hints, Hide game title, Carousel spacing,
  Startup animation, Background, Invert text colour, and Reset settings.
- Remove the Wrap game browsing setting; game browsing continues to wrap at the ends.
- Keep per-system cartridge sizing and visibility options after the general settings.
- Omit macOS Finder metadata files from the install ZIP.
- Make L/R shoulder-button jumps to the previous or next starting-letter group immediate in alphabetized system ROM lists, without cartridge animation.
- Keep letter jumps unavailable in Recent, Favourite, and custom lists.
- Show the `L/R LETTER` control hint only while browsing a system ROM list.
- Add Move list up and Move list down actions to the Select menu, preserving the selected list and game.
- Save the vertical order in `App/CartOS/settings.ini` so updates only need the settings file and background images preserved; new lists are appended automatically.
- Import the order from older `list-order.txt` files automatically on first launch.
- Order the Select menu with favourite/list creation actions first, then list movement, details, random play, and game deletion.

## 0.1.24 — Per-game artwork preview

- Speed up startup artwork discovery by indexing each extra-art folder once and looking up each game's images directly.
- Defer extra-image lookup until a game reaches its enlarged preview, and load the same artwork from Recent, Favourite, and custom lists.
- Resolve Recent entries through normalized ROM paths so their extra artwork folder is found even when history stores an alternate path form.
- Always use the primary cartridge image for the launch animation, even when A is pressed from Game Details or an alternate image.
- Accept a plain ROM-title PNG (for example, `Pokemon Emerald.png`) in `Imgs/CartOS` as one extra image; use names such as `Pokemon Emerald - Gameplay.png` for additional views. Continue supporting earlier per-ROM folders and show the plain-title image before labelled views.
- Restore the primary cartridge image in the game list after leaving the enlarged artwork preview; alternate images remain preview-only.
- Add a Game Details page after the artwork images in the enlarged launch preview, showing the title, system, playtime, and ROM filename.
- Let Left/Right browse artwork in the enlarged first launch step, A continue, and B return to the centered game list.
- Remove the Y set-default action from the artwork preview; artwork browsing is temporary to CartOS and OnionOS's artwork files are never changed.
- Keep two-step launch enabled, with double-tap A available for direct launch.
- Use one artwork discovery path for system lists, Recent, custom lists, and search.

## 0.1.23 — Cartridge startup animation

- Add a short startup sequence: the selected cartridge rises into view, the side cartridges slide into place, then the interface text appears.
- Add a CartOS Settings option to disable the startup animation; it remains on by default.
- Skip the animation automatically when CartOS is relaunched after GameSwitcher, using a one-time app-folder marker as well as a launch flag, and when the player presses a button or moves a direction.

## 0.1.22 — Search keyboard space and punctuation

- Add a visible SPACE key to the on-screen search keyboard.
- Make every displayed search key insert its character, including hyphen, apostrophe, and period.

## 0.1.21 — Music Player first-launch handoff

- Add a one-second settle delay only before launching OnionOS Music Player (GMU), which switches from OnionOS audio to its own SDL 1.2 stack.
- Keep every other OnionOS app launch command unchanged.

## 0.1.20 — Per-system visibility

- Add a saved Show/Hide visibility control beside each system's cartridge size setting.
- Skip hidden systems in the vertical system carousel and its up/down labels, while keeping their games available in search and other lists.
- Restore all systems to shown when CartOS settings are reset.

## 0.1.19 — Settings list organization

- Move per-system cartridge size controls to the bottom of CartOS Settings so the general options remain near the top as more systems are enabled.
- Keep each system's existing saved cartridge size and reset behavior unchanged.

## 0.1.18 — Background selection

- Add a Background option to CartOS Settings that cycles through `BG.png` and named `BG_<name>.png` images.
- Show the selected filename label in settings, save it to `settings.ini`, and reload the selected image after ROM refresh.
- Accept names such as `BG_(Forest).png` and display them as `Forest`.

## 0.1.17 — Darker inverted browser text

- Make all main-browser text use a high-contrast dark palette when `Invert text colour` is enabled, including secondary headers, counters, arrows, and control hints.

## 0.1.16 — Browser text display options

- Add a saved `Invert text colour` setting for the main cartridge browser to improve readability over light backgrounds.
- Add a saved `Hide game title` setting to remove the large game title from the main browser.
- Keep both options off by default and include them in the settings reset.

## 0.1.15 — Direct custom-background rendering

- Draw opaque custom backgrounds without texture alpha blending or an extra dark overlay.
- Record the SDL renderer's background draw result in `App/CartOS/background-debug.log`.

## 0.1.14 — Custom background loading diagnostics

- Retry `BG.png` from the CartOS app folder if the configured background path cannot load.
- Record each background path attempt and its load result in `App/CartOS/background-debug.log` for troubleshooting.
- Keep the existing background tint and display style unchanged.

## 0.1.13 — Cleaner game lists

- Hide OnionOS utility entries named `~Filter`, `~Refresh roms`, and `Search` from console, Recent, custom-list, and search-result game lists.
- Keep CartOS's Y-button search feature available.

## 0.1.12 — Reliable OnionOS app handoff

- Fix OnionOS app launching by matching its expected app-directory command format and restoring the framebuffer to its top page before returning control to an app.
- Confirmed repeated launches of Activity Tracker and Video Player on a Miyoo Mini V4.
- Remove temporary app-handoff and launcher startup diagnostic logging from the release build.

## 0.1.11 — OnionOS app launch compatibility

- Match OnionOS MainUI's app handoff: change to the app directory, make its configured launch target executable, and start it with OnionOS's `libpadsp` preload. This restores the launcher environment used by apps such as Video Player and supports apps that depend on their working directory.

## 0.1.10 — OnionOS app launch compatibility

- Stop forcing OnionOS's audio preload library onto every app. CartOS now hands the configured app launcher to OnionOS as-is so app packages can use their own audio setup and launcher environment.

## 0.1.9 — OnionOS app launch compatibility

- Match OnionOS's app command format when handing off an installed app. The configured launch target is now the first quoted executable path, without a prepended directory change or forced `/bin/sh`, so OnionOS can identify and launch app packages using their own interpreter, working directory, and environment setup.

## 0.1.7 — OnionOS discovery and launch compatibility

- Discover systems from OnionOS emulator configuration files instead of a fixed six-system list. Use each system's configured ROM folders, file extensions, artwork folder, and launch script; omit systems without ROMs and scan nested ROM folders.
- Discover OnionOS apps using the app's configured `launch` target, with the conventional `launch.sh` fallback for older packages. Run shell scripts through the system shell and set their app working directory.
- Flush the GameSwitcher and return markers before handing control back to OnionOS, and stop discarding a normal Menu press based on a short release-time cutoff.
- Describe CartOS as a cartridge launcher for OnionOS devices, tested on the Miyoo Mini V4.

## 0.1.8 — Ready-to-install handheld package
- Combine optional CartOS boot start and GameSwitcher return in `Startup/CartOS.sh`; it preserves queued OnionOS game launches, makes a guarded marked runtime change, and includes `Return/Restore-OnionOS.sh` to reverse that change without a backup.
- Reformat the installation guide with consistent section breaks and clearer hidden-file instructions.
- Update the installation guide layout, point to the ZIP-root `Startup` folder, and clarify the hidden-file steps.

- Add a downloadable ZIP containing the OnionOS app folder, install and uninstall guide, CartOS source, license notices, and patch notes; pin the exact Miyoo SDL2 source revision used for the bundled runtime.
- Include the optional boot-start hook in a simple `Startup/CartOS.sh` folder at the ZIP root, and document how to enable and disable it.
- Replace the public build walkthrough with direct download and installation instructions.
- Replace the upstream NEON helper with a CartOS-authored MIT-licensed library implementing the backend's memcpy and pixel-scaler interface.
- Build and package the compatibility library alongside the Miyoo SDL2 runtime, without downloading the upstream NEON helper.
- Include the SDL2 and backend license files with the app and provide a link to the exact matching backend source revision.
- Restore the accidentally truncated launcher methods so the source builds from a clean checkout.
- Rename the project source folder and the source copy in the release ZIP to `CartOS/`.

## 0.1.6 — Open-source source release

- Added the C++/SDL2 launcher source, Miyoo build helpers, and OnionOS app template in the source folder.
- Added beginner-friendly build and installation instructions, including custom background setup and controls.
- Added an MIT license for CartOS-authored source and separate notices for third-party assets and dependencies.
- Documented that CartOS has been tested on the Miyoo Mini V4.
- Excluded compiled device libraries, ROMs, box art, and personal SD-card data.

Future fixes should add a dated or versioned entry here describing the change and its user impact.
