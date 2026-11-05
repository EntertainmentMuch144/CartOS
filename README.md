# CartOS
CartOS is a cartridge-style game browser for OnionOS handhelds. The current
release is version 0.1.26; CartOS has been tested on the Miyoo Mini V4.

## RECENT BUG FIX

Since v0.1.25, CartOS keeps the GameSwitcher return marker on the SD card when
it launches a game. If the handheld is powered off while a game is running
and OnionOS resumes that game on the next boot, leaving GameSwitcher returns
to CartOS. This requires the optional CartOS startup hook. When upgrading from
an earlier hook, replace both `App/CartOS` and `.tmp_update/startup/CartOS.sh`
with the supplied files. Start the handheld once to update the runtime hook,
then restart to load the change. A working v0.1.25 hook needs no replacement
for v0.1.26. See [`INSTALL.txt`](INSTALL.txt) for the update steps.

## Features

- Press START once or double-tap A to launch the selected game with the
  cartridge animation. Press A once to open the artwork preview first.
- Finds configured game systems that contain ROMs and displays their cartridge
  artwork.
- Browses Recent, Favourite, and custom game lists, with search and game details.
- Previews a game's extra artwork and details in the enlarged view, including from Recent, Favourite, and custom lists.
- Saves extra-artwork indexes between sessions for faster previews; X refresh
  rebuilds them after artwork is added, removed, or renamed.
- Searches with the on-screen keyboard, including spaces and punctuation.
- Opens with a brief, configurable cartridge startup animation that can be skipped with input.
- Shows playtime when Activity Tracker is installed, plus the handheld's
  battery level.
- Browses installed OnionOS apps and hands games and apps back to OnionOS to
  launch.
- Customizes the cartridge carousel, launch animation, game title, counters,
  and control hints.
- Chooses custom backgrounds such as `BG.png` and `BG_(Forest).png`, with text
  options for light backgrounds.
- Hides systems from the vertical browser list without removing their games.
- Uses the shoulder buttons for immediate jumps between starting letters in
  alphabetized system game lists. Recent, Favourite, and custom lists keep normal scrolling.
- Reorders lists from the Select menu or by editing the numbered `list_order_`
  entries in `App/CartOS/settings.ini`.
- Keeps the general settings in a fixed order, with per-system options at the
  bottom; game browsing wraps at the ends by default.

## Install

Download and extract `CartOS-0.1.26.zip` from
[`Releases/CartOS-0.1.26`](Releases/CartOS-0.1.26). From the extracted folder,
copy `App/CartOS` into the `App` folder on the OnionOS SD card. See
[`INSTALL.txt`](INSTALL.txt) for installation, updating, custom backgrounds,
optional boot and GameSwitcher integration, and uninstall instructions.

The optional `Startup/CartOS.sh` hook enables CartOS boot start and return from
GameSwitcher. `Return/Restore-OnionOS.sh` reverses the hook. Neither script is
needed for a normal app installation. When updating an installation that uses
the optional hook, use the startup script supplied with the release. The
working v0.1.25 hook also supports v0.1.26 and needs no repeat installation.

## Current release

CartOS 0.1.26 saves additional-artwork filename indexes between sessions and
adds START quick launch from the main browser using the existing animation.
Press X after changing artwork to rebuild the indexes. The first index build
and X refresh still scan artwork filenames. Existing preview controls and
OnionOS game launching are retained. See [`CHANGELOG.md`](CHANGELOG.md) for
the full patch notes.

## Repository contents

- `Releases/CartOS-0.1.26/App/CartOS/` — ready-to-copy OnionOS app for the
  current release.
- `Source/CartOS/` — CartOS source code and packaging resources.
- `Releases/` — versioned release ZIPs and extracted packages.
- `Startup/CartOS.sh` — optional boot-start and GameSwitcher return hook.
- `Return/Restore-OnionOS.sh` — script to reverse the optional hook.
- `INSTALL.txt` — installation, update, background, and uninstall guide.
- `BUILD-INFO.txt` — version and matching Miyoo SDL2 backend revision.
- `LICENSE` and `THIRD-PARTY-NOTICES.md` — project and third-party license
  information.


This software was made with the use of ChatGPT Codex, it is all my idea and my feature choices, AI was just my coder.
