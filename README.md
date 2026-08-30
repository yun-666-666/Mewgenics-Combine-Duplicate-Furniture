# Combine Duplicate Furniture

A Windows x64 Mewgenics mod that batch-combines duplicate furniture already stored in the furniture inventory. Chinese is the default language; English is also supported.

中文说明见下方的 [中文](#中文) 部分。

## Features

- Press `F8` in furniture mode to seal deterministic pairs of identical inventory furniture at the same rarity.
- Furniture still placed in a room is ignored. Move every copy you want to combine back into the furniture inventory before pressing `F8`.
- Two ordinary copies become one native Rare item with 2x base attributes.
- Two Rare copies become one persistent enhanced Rare item with 4x base attributes and room effects.
- Furniture that the game marks as unable to become Rare, such as the Food Box, is skipped.
- External dialogs are disabled by default. The mod does not install a custom SWF or shared game-UI hook.
- Hook and patch locations are resolved from the loaded game executable at runtime instead of being restricted to one exact RVA table or game-version whitelist. If an optional capability cannot be resolved after a game update, that capability is disabled and reported in the log instead of rejecting the entire game version.

The stable `v0.6.15` inventory merge and attribute-refresh flow has been confirmed by player testing.

## Installation

1. Install a compatible Mewjector loader (`version.dll`, loader API v3 or newer).
2. Download the latest release archive.
3. Extract the archive directly into the Mewgenics game directory.
4. Confirm that these files exist:

   ```text
   Mewgenics/
   ├─ Mods/CombineDuplicateFurniture.dll
   ├─ Mods/CombineDuplicateFurniture/config.json
   └─ Mewtator/mods/CombineDuplicateFurniture/description.json
   ```

## Configuration

Close Mewgenics and edit:

```text
Mods/CombineDuplicateFurniture/config.json
```

```json
{
  "hotkey": "F8",
  "language": "zh-CN",
  "show_dialogs": false
}
```

- `hotkey`: `F1` through `F12`.
- `language`: `zh-CN` or `en-US`.
- `show_dialogs`: `false` suppresses confirmation, completion, information, and error windows. Set it to `true` to restore the stable external dialogs.

The configuration is loaded when the mod starts, so restart the game after editing it.

## Usage

1. Manually move every duplicate you want to combine out of its room and back into the furniture inventory.
2. Enter furniture mode and press the configured hotkey (`F8` by default).
3. With the default `show_dialogs: false`, no window or on-screen notification appears. This is intentional; the hotkey result is written to the mod log.
4. Leave furniture mode. The sealed batch is revalidated and then processed outside the furniture screen.
5. Wait briefly and enter furniture mode again to view the updated items and attributes.

Furniture that remains placed in a room does not enter the batch and is never moved automatically by this mod.

A batch is processed one pair at a time. If a later pair fails a safety check, earlier completed pairs remain combined and the remaining pairs are left unchanged. The log records the completed and planned pair counts.

The log is written to:

```text
Mods/CombineDuplicateFurniture/logs/CombineDuplicateFurniture.log
```

Useful success markers include `hotkey press captured`, `batch sealed`, `batch pair complete`, and `batch complete`.

## Game updates

The mod discovers required code locations from the currently loaded executable rather than checking for one exact supported Mewgenics build. This allows address-only game updates to continue working without a new RVA table. A game update can still change functions or object layouts; unresolved required hooks or disabled optional capabilities are reported in the log so they can be repaired when an actual incompatibility appears.

## Building

Run from PowerShell with Visual Studio 2022 and its C++/CMake tools installed:

```powershell
powershell -ExecutionPolicy Bypass -File tools/build.ps1 -Configuration Release
```

The release layout is generated under `dist/Release`.

---

## 中文

这是一个 Windows x64 Mewgenics MOD，用于批量合并已经收回家具栏的重复家具。默认使用中文，也支持英文。

## 功能

- 在家具界面按 `F8`，封存家具栏内稀有度相同、同款家具的确定性合并组合。
- 仍摆放在房间里的家具会被忽略。请先手动把所有想要合并的家具移回家具栏，再按 `F8`。
- 两件普通家具合并为一件原生 Rare 家具，基础属性为 2 倍。
- 两件 Rare 家具合并为一件持久化的强化 Rare 家具，基础属性和房间效果为 4 倍。
- 游戏标记为不能变成 Rare 的特殊家具，例如食物箱，不会参与合并。
- 默认关闭外部弹窗；本 MOD 不安装自定义 SWF，也不挂接共享游戏 UI。
- 挂钩和补丁位置会从当前加载的游戏 EXE 中动态解析，不再依赖单一版本的固定 RVA 表或游戏版本白名单。游戏更新后，如果某个可选能力无法解析，只会关闭该能力并写入日志，而不是因为版本不同直接拒绝整个 MOD。

稳定版 `v0.6.15` 的家具栏合并和属性刷新流程已经由玩家实际确认正常。

## 安装

1. 安装兼容的 Mewjector 加载器（`version.dll`，加载器 API v3 或更高版本）。
2. 下载最新 Release 压缩包。
3. 把压缩包直接解压到 Mewgenics 游戏目录。
4. 确认以下文件存在：

   ```text
   Mewgenics/
   ├─ Mods/CombineDuplicateFurniture.dll
   ├─ Mods/CombineDuplicateFurniture/config.json
   └─ Mewtator/mods/CombineDuplicateFurniture/description.json
   ```

## 配置

退出 Mewgenics 后编辑：

```text
Mods/CombineDuplicateFurniture/config.json
```

```json
{
  "hotkey": "F8",
  "language": "zh-CN",
  "show_dialogs": false
}
```

- `hotkey`：支持 `F1` 至 `F12`。
- `language`：支持 `zh-CN` 和 `en-US`。
- `show_dialogs`：设为 `false` 时不显示确认、完成、普通信息和错误窗口；设为 `true` 可恢复稳定的游戏外弹窗。

配置在 MOD 启动时读取，修改后需要重新启动游戏。

## 使用方法

1. 手动把所有想要合并的重复家具从房间移回家具栏。
2. 进入家具界面，按配置的快捷键，默认是 `F8`。
3. 默认配置为 `show_dialogs: false`，所以不会出现窗口或游戏内提示。这是预期行为；快捷键处理结果会写入 MOD 日志。
4. 退出家具界面。MOD 会重新核对已封存的批次，然后在家具界面外逐组执行合并。
5. 稍等片刻，再进入家具界面查看更新后的家具和属性。

仍放在房间里的家具不会加入批次，本 MOD 也不会自动把它们收回家具栏。

批次会逐组处理。如果后面的某一组没有通过安全检查，之前已经完成的组会保留合并结果，后续尚未执行的组不会被修改。日志会记录已完成数量和计划总数。

日志位置：

```text
Mods/CombineDuplicateFurniture/logs/CombineDuplicateFurniture.log
```

常见的成功日志包括 `hotkey press captured`、`batch sealed`、`batch pair complete` 和 `batch complete`。

## 游戏更新兼容

MOD 会根据当前加载的游戏 EXE 查找所需代码位置，不再检查某个唯一受支持的 Mewgenics 版本。因此，仅仅改变地址的游戏更新不需要重新维护固定 RVA 表。游戏更新仍可能改变函数或对象结构；如果届时确实出现不兼容，无法解析的必需挂钩或被关闭的可选能力会写入日志，再针对实际问题修复。

## 构建

安装 Visual Studio 2022 C++/CMake 工具后，在 PowerShell 中运行：

```powershell
powershell -ExecutionPolicy Bypass -File tools/build.ps1 -Configuration Release
```

发布目录生成在 `dist/Release`。
