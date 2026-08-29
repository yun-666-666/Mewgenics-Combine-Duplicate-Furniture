# Combine Duplicate Furniture

A Windows x64 Mewgenics mod that batch-combines duplicate furniture from the furniture screen. The interface supports Chinese and English; Chinese is the default.

中文说明见下方的 [中文](#中文) 部分。

## Features

- Press `F8` in furniture mode to seal all deterministic pairs of identical furniture at the same rarity.
- External dialogs are disabled by default, so `F8` seals the batch immediately without leaving the game window.
- The known-stable external confirmation and status dialogs can be restored with `"show_dialogs": true`; the mod does not install a custom SWF or shared game-UI hook.
- Two ordinary copies become one native Rare item with 2x base attributes.
- Two Rare copies become one persistent enhanced Rare item with 4x base attributes and room effects.
- Furniture that the game marks as unable to become Rare, such as the Food Box, is skipped.
- No furniture is modified while the furniture screen is active. Leave furniture mode after pressing `F8`; after the batch finishes, the mod keeps retrying the game's native list-rebuild request while the screen is closed, so the updated attributes are ready on the next entry.
- The hotkey can be changed to `F1`-`F12` in the external configuration file.

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

Settings are changed outside the game. Close Mewgenics, open:

```text
Mods/CombineDuplicateFurniture/config.json
```

Chinese is the default:

```json
{
  "hotkey": "F8",
  "language": "zh-CN",
  "show_dialogs": false
}
```

For English, change only the language value:

```json
{
  "hotkey": "F8",
  "language": "en-US",
  "show_dialogs": false
}
```

The supported language values are `zh-CN` and `en-US`. With `show_dialogs` set to `false` or omitted, confirmation, completion, information, and error windows are suppressed and outcomes are written to the mod log. Set it to `true` only if you want the stable external dialogs back. The configuration is loaded when the mod starts, so restart the game after editing it.

## Usage

1. Enter furniture mode.
2. Press the configured hotkey (`F8` by default).
3. With the default configuration, no window appears. Leave furniture mode to let the sealed batch start safely.
4. Wait briefly for the batch to finish, then enter furniture mode again. Consumed copies are gone and the kept items are rebuilt with their new attributes.

The log is written to:

```text
Mods/CombineDuplicateFurniture/logs/CombineDuplicateFurniture.log
```

## Acknowledgements

This project was completed with GPT-5.6. Thank you to all Mewgenics players who tested the mod and shared their feedback.

## Building

Run from PowerShell with Visual Studio 2022 and its C++/CMake tools installed:

```powershell
powershell -ExecutionPolicy Bypass -File tools/build.ps1 -Configuration Release
```

The release layout is generated under `dist/Release`.

---

## 中文

这是一个 Windows x64 Mewgenics MOD，可以在家具界面批量合并同款家具。界面支持中文和英文，默认使用中文。

## 功能

- 在家具界面按 `F8`，扫描所有稀有度相同的同款家具并封存确定性的合并组合。
- 默认关闭所有外部弹窗；按下 `F8` 后只封存批次，不会切出游戏窗口。
- 如确实需要，可通过 `"show_dialogs": true` 恢复已知稳定的外部确认和状态窗口；本 MOD 不再安装自定义 SWF，也不再挂接共享游戏 UI。
- 两件普通家具合并为一件原生 Rare 家具，基础属性为 2 倍。
- 两件 Rare 家具合并为一件持久化的强化 Rare 家具，基础属性和房间效果为 4 倍。
- 游戏标记为不能变成 Rare 的特殊家具（例如食物箱）不会参与合并。
- 家具界面仍打开时不会修改任何家具。按 `F8` 后先退出家具界面，MOD 会在界面完全关闭后重新核对并执行批次，并在界面关闭期间持续重试游戏原生列表重建请求，让再次进入时直接显示新属性。
- 可以在游戏外的配置文件中把快捷键改为 `F1` 至 `F12`。

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

设置不在游戏内修改。先退出游戏，然后打开：

```text
Mods/CombineDuplicateFurniture/config.json
```

默认中文配置：

```json
{
  "hotkey": "F8",
  "language": "zh-CN",
  "show_dialogs": false
}
```

切换为英文时，只修改 `language`：

```json
{
  "hotkey": "F8",
  "language": "en-US",
  "show_dialogs": false
}
```

语言目前支持 `zh-CN` 和 `en-US`。当 `show_dialogs` 为 `false` 或缺省时，确认、完成、普通信息和错误窗口都会关闭，结果只写入 MOD 日志；只有希望恢复稳定的游戏外弹窗时才设为 `true`。配置会在 MOD 启动时读取，修改后重新启动游戏即可生效。

## 使用方法

1. 进入家具界面。
2. 按配置的快捷键，默认是 `F8`。
3. 默认配置下不会显示任何窗口。按键后退出家具界面，让已封存的批次安全开始执行。
4. 稍等批次完成，再重新进入家具界面。多余家具会消失，保留家具会显示新的属性。

日志位置：

```text
Mods/CombineDuplicateFurniture/logs/CombineDuplicateFurniture.log
```

## 致谢

本项目由 GPT-5.6 完成。感谢所有参与测试并提供反馈的 Mewgenics 玩家。
