# CartOS v0.1.28

CartOS is a cartridge-style game browser for OnionOS handhelds. This release
includes the v0.1.27 Optional features and has been tested on the Miyoo Mini V4.

## Cached game catalog

The first launch scans the ROM folders and saves a game catalog in
`App/CartOS/cache/game-catalog-v1.txt`. Later launches read that catalog.
Press **X** in CartOS to scan the ROM folders again and update the catalog
after adding, removing, or renaming games. X also refreshes lists and artwork.
If the catalog is missing or damaged, CartOS rebuilds it on launch. The catalog
is disposable and contains no ROM or save data.

On the tested Miyoo Mini V4, the cached launch initialized CartOS in 0.93
seconds versus 1.15 seconds with a fresh scan. The change is too small to
notice during a full device boot because OnionOS starts before CartOS.

## Text background

CartOS Settings includes an optional text background with a padded backing at
90% opacity. With Invert text colour enabled, the backing switches from
translucent black to translucent white so the darker text remains readable.
This leaves the wallpaper and search keyboard panel unchanged.

## Recent games and apps

Apps opened from CartOS appear in Recent alongside games. Reopening an item
moves it to the front. Select a recent app with A to open it again.

## CartOS Accessibility

Open **Apps > CartOS Accessibility** for four sections of persistent settings:
visual accessibility, controls and navigation, display preferences, and audio
and feedback. Includes text-size presets, high contrast, reduced motion,
background dimming, safe button remapping, optional launch confirmation,
larger artwork/controls, secondary-information hiding,
scrolling speed, stronger selection, optional navigation sounds with separate
volume, visual feedback, and capability-checked vibration.

Defaults preserve normal browsing and START quick launch. Hold physical
**SELECT and press START** to recover default controls. A confirmed reset
restores only accessibility settings. See [ACCESSIBILITY.md](ACCESSIBILITY.md)
for every option, defaults, control recovery and hardware-testing limits.

## Random games from the whole device

Select > Play random game now chooses from every discovered system library.
Each unique ROM path has one equal chance, even when it appears in Recent,
Favourite, custom lists, or more than one system configuration. Hidden
systems are included; apps and utility entries are excluded. The selected
game uses the existing enlarged preview and cartridge launch animation.

## Remember your browser position

Enable **Restore browser position** in CartOS settings to use this feature.
It is **OFF by default** and is saved in `settings.ini`. Reset settings turns
it off again. With it off, returning from a game opens Recent.

When enabled, launching a game remembers the current list and ROM. When CartOS returns from
GameSwitcher, the game is centred in that list in the normal browser. This
also works when opening GameSwitcher directly from a game list.

Ordinary cold starts open Recent. A missing game/list or hidden source system
also falls back to Recent. The pending position survives a power-off while in
a game and is consumed once when CartOS opens again. Search launches return
to the underlying system list; search text and artwork pages are not restored.

The existing v0.1.27 Optional boot-and-GameSwitcher hook works with this
release; no hook update is needed. See `INSTALL.txt` for installation and
rollback instructions.

## Features

- Browse multiple PNG artwork images for a game during the enlarged launch preview.
- Look up a game's extra images only when its enlarged preview opens, including when the game is opened from Recent, Favourite, or a custom list.
- Keep the normal OnionOS artwork first, then show CartOS alternatives. A single extra image can use the plain ROM title, such as `Pokemon Emerald.png`; additional views can use labelled names such as `Pokemon Emerald - Gameplay.png` in `Imgs/CartOS`. The earlier per-ROM folder layout still works.
- Press Left/Right to browse the artwork and then a Game Details page with the title, system, playtime, and ROM filename. A continues launching, and B returns to the centered game list.
- Always use the main cartridge image for the launch animation, including when A is pressed from an alternate image or Game Details. Extra-art discovery is indexed to avoid scanning the full artwork folder for every ROM during startup.
- By default, use A for the two-step artwork preview, or press START / double-tap A from the game list to launch directly with the normal animation.
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
