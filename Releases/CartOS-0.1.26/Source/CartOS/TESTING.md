# CartOS v0.1.26 validation

Version: `0.1.26`. Based on v0.1.25 and the tested v0.1.26 Beta 1.

The device owner reported that the beta works on the Miyoo Mini V4 and
approved promotion to a stable release. The stable build uses the same
artwork-index and launch implementation, plus SELECT release handling for
OnionOS shortcut combinations.

## Completed on the development machine

- Built the installable OnionOS executable using the existing Union toolchain
  and the same pinned Miyoo SDL2 runtime as v0.1.25.
- Ran C++ checks with AddressSanitizer and UndefinedBehaviorSanitizer.
- Checked bare and labelled PNG names, case-insensitive matching, legacy ROM
  folders, nested ROM paths, multiple roots, empty results, and image order.
- Checked a library with 5,000 unrelated PNGs. Index build took 47.64 ms;
  loading the same saved index took 5.30 ms. The index was 154,209 bytes.
  These are host measurements, not Miyoo performance claims.
- Proved that a valid saved index loads without scanning or statting the
  artwork folder by temporarily moving that folder out of reach during a test.
- Checked newly added and removed artwork after a forced rebuild, missing
  indexes, truncated/corrupt indexes, a changed artwork root, a missing artwork
  folder, and inability to save the cache.
- Checked gallery lookup through normal games and saved-list copies, including
  a normalized OnionOS history path and index reuse by another launcher session.
- Checked the real input handler: A still opens step one; another A confirms;
  START selects the same ROM and animation with step two already confirmed.
  Search, settings, option menus, app entries, missing ROMs, and empty lists
  do not accidentally trigger the new browser quick launch.
- Verified that the renderer, animation update loop, game-launch function,
  app launch script, runtime libraries, and startup/return hooks match v0.1.25.

## SELECT shortcut update

Automated input checks cover SELECT down/release and repeat events, L2/R2
trigger combinations, keyboard aliases, raw Miyoo buttons, suppression of
A/START/navigation during a chord, contextual search behaviour, and recovery
after focus loss. Actual brightness adjustment is handled by OnionOS keymon
and should be checked on the handheld: tap SELECT alone for options, then
hold SELECT and tap each trigger to adjust brightness without opening options.

## Optional handheld regression checks

1. Keep a copy of your stable App/CartOS folder, then install this release using
   INSTALL.txt. Keep settings.ini and your background PNGs.
2. Open a system with additional images. Press A and check the first preview,
   then B and open other games in that system. Check image order and Game
   Details, and that A/B restore the primary cartridge image as before.
3. Restart CartOS. Open the same system's preview, then another system. Check
   galleries from Recent, Favourite, custom lists, and search as well.
4. Add one extra PNG and remove another while the handheld is off. Start
   CartOS and press X; verify that the gallery reflects both changes. Check
   that refreshed ROM lists, search, favourites, and collections still work.
5. To check recovery, close CartOS and remove or damage only a file inside
   App/CartOS/cache/artwork. Open that system's preview and verify recovery.
6. Press START once on a game in the main browser, including while the row
   is moving. Compare the animation with double-A: same centering, enlargement,
   pause, movement, sound if present, and correct game launch. Test A, then A,
   and A, then B too. Test START in search, menus, settings, and the Apps list.
7. Check exit from games, GameSwitcher return, and a game resumed after a
   power-off, using the existing v0.1.25 startup hook where installed.

## Limits and diagnostics

- Automated checks do not establish performance or compatibility on every
  handheld. The device confirmation is for the Miyoo Mini V4; the checklist
  above supports further regression testing on other OnionOS installations.
- A system's index is generated lazily on its first preview if absent or
  invalid. That first generation and an X refresh still scan PNG filenames;
  later sessions reuse the index. Artwork changes require X refresh.
- PNGs are decoded only when displayed. The existing limit of 20 images per
  game remains. Index scans are bounded to 100,000 PNGs and 16 directory levels;
  persisted indexes are limited to 16 MiB. A failed/oversized scan is not saved.
- If the cache cannot be saved, it works in memory for that session and is
  rebuilt next session. Cache deletion is safe while CartOS is closed.
- The app's existing log now includes lines such as
  `Artwork index [GB]: loaded, 5009 PNGs, 5.30 ms` or `rebuilt`/`scan failed`.
  These are printed to CartOS standard output; capture depends on OnionOS
  runtime logging. No separate log file or gallery image data is written by
  this diagnostic.

To return to your previous version, shut down and replace App/CartOS with the stable folder
saved before testing. The optional v0.1.25 startup/return scripts are unchanged.
