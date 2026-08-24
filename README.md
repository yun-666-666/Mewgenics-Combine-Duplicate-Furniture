# Combine Duplicate Furniture

Independent Windows x64 DLL mod for the current local Mewgenics build.

- Enter furniture mode and press `F8` (configurable to `F1`-`F12` in `Mods/CombineDuplicateFurniture/config.json`).
- The mod scans current furniture records and builds every deterministic pair of identical ordinary furniture.
- It shows one batch summary and asks for confirmation once.
- On confirmation it processes one pair per game frame, sets each kept instance's native `0x2` Rare flag, and consumes each duplicate through the game's native deletion path.
- Stored duplicates are consumed directly. The first time a batch needs a room furniture material, the mod asks once, then recalls all remaining room materials to the furniture inventory before consuming them.
- Completion and no-candidate notices close automatically and do not block the game thread.

The independent log is written to `Mods/CombineDuplicateFurniture/logs/CombineDuplicateFurniture.log`.

Build with `powershell -ExecutionPolicy Bypass -File tools/build.ps1 -Configuration Release` and deploy with `powershell -ExecutionPolicy Bypass -File tools/deploy.ps1 -Configuration Release` after the game has exited.
