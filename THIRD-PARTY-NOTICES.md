# Third-party notices

The MIT license in `LICENSE` covers CartOS-authored source code only. These
third-party components and names keep their own terms:

- **DejaVu Sans** — bundled font source in `assets/fonts/DejaVuSans.ttf`; see
  `assets/fonts/DejaVu-LICENSE.txt`.
- **Miyoo SDL2 backend** — built from
  [XK9274/sdl2_miyoo](https://github.com/XK9274/sdl2_miyoo). Its GPL-3.0
  source is publicly available at the exact source revision linked from the
  release ZIP's `BUILD-INFO.txt` and `Source/SDL2-Miyoo-SOURCE.txt`. SDL2's
  Zlib license and the backend's GPL license are included in the app folder.
- **CartOS Miyoo compatibility library** — `tools/neon_compat.c` provides the
  SDL2 backend's memcpy and pixel-scaling entry points using standard C and
  libc. It is CartOS-authored and covered by the MIT license in `LICENSE`.
- **OnionOS and Miyoo** are third-party project and product names. This
  project is an independent community project, not an official OnionOS or
  Miyoo product.
- **OnionOS wordmark** — `assets/onionos_logo.png` and the copy in the app
  template identify the Main UI entry. The wordmark belongs to the OnionOS
  project and is not covered by CartOS's MIT license; replace it with an
  officially permitted asset if you redistribute the project in a context
  that requires permission.
- **Game ROMs and box art** are not included. CartOS loads artwork already
  present on the user's own SD card.

The launcher was visually inspired by the cartridge browsing concept in
[Slot CFW](https://slot-cfw.fyi/).

## Accessibility assets and updated SDL audio configuration

The accessibility icon was supplied by the CartOS maintainer and is included
unchanged as assets/accessibility.png and res/icon/accessibility.png.
Navigation ticks are synthesised by CartOS; no third-party sound recording is
used. The release's SDL runtime enables its existing OSS audio driver for the
OnionOS audio service. Matching SDL source and build notes are included in
Source/SDL2-Miyoo; the original SDL/backend license notices still apply.
