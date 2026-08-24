# Combine Duplicate Furniture

Independent Windows x64 DLL mod for the current local Mewgenics build.

- Enter furniture mode and press `F8` (configurable to `F1`-`F12` in `Mods/CombineDuplicateFurniture/config.json`).
- The mod scans current furniture records and selects one deterministic pair of identical ordinary furniture.
- It shows the native Rare attribute preview and asks for confirmation.
- On confirmation it sets the kept instance's native `0x2` Rare flag and consumes exactly one duplicate through the game's native deletion path.
- Room and storage duplicates are consumed through the game's corresponding native deletion paths.
- It never automatically processes a second pair in the same action.

The independent log is written to `Mods/CombineDuplicateFurniture/logs/CombineDuplicateFurniture.log`.

Build with `powershell -ExecutionPolicy Bypass -File tools/build.ps1 -Configuration Release` and deploy with `powershell -ExecutionPolicy Bypass -File tools/deploy.ps1 -Configuration Release` after the game has exited.
