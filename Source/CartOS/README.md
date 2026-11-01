# CartOS source

This folder contains the C++ and SDL2 source code, assets, and packaging resources for the CartOS OnionOS handheld app. CartOS has been tested on the Miyoo Mini V4.

## Source layout

- `src/` — launcher, input, artwork, and drawing code.
- `assets/` — interface artwork and font assets.
- `scripts/` — source packaging and Miyoo build helpers.
- `tools/` — toolchain support and asset-generation utilities.
- `packaging/OnionOS/` — OnionOS app template, optional combined boot-start/GameSwitcher hook, and runtime restore script.
- CartOS Settings can hide a system from the vertical browser list without deleting its ROMs or removing games from search and saved lists.
- Background images named `BG.png` or `BG_<name>.png` can be selected in CartOS Settings; names in parentheses are shown without the filename prefix or parentheses.
- The search keyboard includes a space key and working punctuation.
- The browser has a configurable cartridge intro that can be skipped with input and is skipped on GameSwitcher return by the optional startup hook.

For a normal installation, use the ready-to-copy `App/CartOS` folder and follow the installation guide at the repository root.

CartOS-authored code is covered by the MIT License. Third-party assets and libraries retain their own licenses; see `LICENSE` and `THIRD-PARTY-NOTICES.md`.
