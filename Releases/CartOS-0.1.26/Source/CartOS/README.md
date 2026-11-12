# CartOS v0.1.26

CartOS is a cartridge-style game browser for OnionOS handhelds. Version 0.1.26 has been tested on the Miyoo Mini V4.

## Version 0.1.26

- Release SELECT alone for options; hold SELECT with another button for
  OnionOS shortcuts without triggering browser actions.
- Persist extra-image filename indexes in `App/CartOS/cache/artwork` between sessions.
- Use direct indexed lookups when opening a gallery; decode only the displayed PNG.
- Rebuild all active artwork indexes with the existing X refresh.
- Press START on a game in the main browser to run the existing launch animation
  without waiting for a second A press. Contextual START controls are preserved.

The first preview for a system builds a missing or corrupt index. Adding or
removing artwork requires X refresh. Index build/load timings are printed to standard output.

## Features

- Browse multiple PNG artwork images for a game during the enlarged launch preview.
- Look up a game's extra images only when its enlarged preview opens, including when the game is opened from Recent, Favourite, or a custom list.
- Keep the normal OnionOS artwork first, then show CartOS alternatives. A single extra image can use the plain ROM title, such as `Pokemon Emerald.png`; additional views can use labelled names such as `Pokemon Emerald - Gameplay.png` in `Imgs/CartOS`. The earlier per-ROM folder layout still works.
- Press Left/Right to browse the artwork and then a Game Details page with the title, system, playtime, and ROM filename. A continues launching, and B returns to the centered game list.
- Always use the main cartridge image for the launch animation, including when A is pressed from an alternate image or Game Details. Extra-art discovery is indexed to avoid scanning the full artwork folder for every ROM during startup.
- Keep the two-step launch preview enabled; press START or double-tap A from the game list to launch directly with the normal animation.
- Save the GameSwitcher return request on the SD card when launching a game, so a game resumed after a power-off can still return to CartOS.
- Use L/R shoulder buttons for immediate jumps between starting letters in alphabetized system ROM lists. Recent, Favourite, and custom lists keep normal scrolling.
- Reorder lists from the Select menu or by editing the numbered `list_order_`
  entries in `App/CartOS/settings.ini`. Older `list-order.txt` files are
  imported automatically once.
- Keep the settings menu in a predictable order, with per-system options at
  the bottom and wraparound game browsing enabled by default.
- Keep artwork browsing temporary to CartOS; OnionOS artwork is not modified, and there is no Y action to save a default image.

See the release `INSTALL.txt` for the alternate-image folder layout, installation steps, and startup-hook details.

## Source layout

- `src/` — launcher, input, artwork discovery, and rendering code.
- `assets/` — interface artwork and font assets.
- `scripts/` — source packaging and Miyoo build helpers.
- `tools/` — toolchain support and asset-generation utilities.
- `packaging/OnionOS/` — OnionOS packaging resources and optional startup hooks.

CartOS Settings can hide a system from the vertical browser list without deleting its ROMs or removing games from search and saved lists. Backgrounds named `BG.png` or `BG_<name>.png` can be selected in settings. The browser also includes search, game details, playtime information, battery level, configurable cartridge sizing per system, and custom lists.

CartOS-authored code is covered by the MIT License. Third-party assets and libraries retain their own licenses; see `LICENSE` and `THIRD-PARTY-NOTICES.md`.
