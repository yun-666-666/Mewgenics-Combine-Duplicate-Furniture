# Combine Duplicate Furniture

Independent Windows x64 DLL mod for the current local Mewgenics build.

- Enter furniture mode and press `F8` (configurable to `F1`-`F12` in `Mods/CombineDuplicateFurniture/config.json`).
- The mod scans current furniture records and builds every deterministic pair of identical furniture at the same rarity.
- It shows one batch summary and asks for confirmation once.
- For ordinary pairs it sets the kept instance's native `0x2` Rare flag and consumes the duplicate through the game's native deletion path.
- For Rare pairs it marks the kept instance as an enhanced Rare, consumes the duplicate, displays 4x base attributes, and counts 4x base effects in rooms. The enhanced flag is stored on that instance and survives saving/reloading.
- The active furniture screen is left untouched while the batch runs. After leaving furniture mode and entering it again, the mod requests one rebuild from the current live UI so consumed duplicates disappear and kept furniture attributes use their new rarity flags without restarting the game.
- Stored duplicates are consumed directly. Room furniture is identified from its native room field and all room materials are queued for native recall in the confirmation frame, before the modal focus change closes furniture mode.
- After the batch confirmation closes, the mod scans and rebuilds the plan again so room-component changes during the dialog cannot leave stale work queued.
- Special furniture that the game marks `can_be_rare false`, such as the Food Box, is never combined.
- Completion and no-candidate notices close automatically and do not block the game thread.

The independent log is written to `Mods/CombineDuplicateFurniture/logs/CombineDuplicateFurniture.log`.

Build with `powershell -ExecutionPolicy Bypass -File tools/build.ps1 -Configuration Release` and deploy with `powershell -ExecutionPolicy Bypass -File tools/deploy.ps1 -Configuration Release` after the game has exited.
