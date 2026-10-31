# CartOS

CartOS is a cartridge-style game browser for OnionOS handhelds. This release
has been tested on the Miyoo Mini V4.

## Features

- Finds configured game systems that contain ROMs and displays their cartridge
  artwork.
- Browses Recent, Favourite, and custom game lists, with search and game details.
- Shows playtime when Activity Tracker is installed, plus the handheld's
  battery level.
- Browses installed OnionOS apps and hands games and apps back to OnionOS to
  launch.
- Customizes the cartridge carousel, launch animation, game title, counters,
  and control hints.
- Chooses custom backgrounds such as `BG.png` and `BG_(Forest).png`, with text
  options for light backgrounds.
- Hides systems from the vertical browser list without removing their games.

## Install

Download and extract the latest release ZIP from [`Releases`](Releases). Copy
`App/CartOS` into the `App` folder on the OnionOS SD card. See
[`INSTALL.txt`](INSTALL.txt) for installation, updating, custom backgrounds,
optional boot and GameSwitcher integration, and uninstall instructions.

The optional `Startup/CartOS.sh` hook enables CartOS boot start and return from
GameSwitcher. `Return/Restore-OnionOS.sh` reverses the hook. Neither script is
needed for a normal app installation.

## Latest release

CartOS 0.1.21 adds a short first-launch handoff delay for OnionOS Music Player.
See [`CHANGELOG.md`](CHANGELOG.md) for the full patch notes.

## Repository contents

- `App/CartOS/` — ready-to-copy OnionOS app.
- `Source/CartOS/` — CartOS source code and packaging resources.
- `Releases/` — versioned release ZIPs and extracted packages.
- `Startup/CartOS.sh` — optional boot-start and GameSwitcher return hook.
- `Return/Restore-OnionOS.sh` — script to reverse the optional hook.
- `INSTALL.txt` — installation, update, background, and uninstall guide.
- `BUILD-INFO.txt` — version and matching Miyoo SDL2 backend revision.
- `LICENSE` and `THIRD-PARTY-NOTICES.md` — project and third-party license
  information.
