# Combine Duplicate Furniture

A Windows x64 Mewgenics mod that batch-combines duplicate furniture from the furniture screen. The interface supports Chinese and English; Chinese is the default.

中文说明见下方的 [中文](#中文) 部分。

## Features

- Press `F8` in furniture mode to scan all deterministic pairs of identical furniture at the same rarity.
- One confirmation dialog covers the entire batch.
- Two ordinary copies become one native Rare item with 2x base attributes.
- Two Rare copies become one persistent enhanced Rare item with 4x base attributes and room effects.
- Furniture that the game marks as unable to become Rare, such as the Food Box, is skipped.
- The active furniture list is not rebuilt during the batch. Leave furniture mode and enter it again once to remove consumed copies and refresh the kept items' attributes without restarting the game.
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

## Language configuration

Language is changed outside the game. Close Mewgenics, open:

```text
Mods/CombineDuplicateFurniture/config.json
```

Chinese is the default:

```json
{
  "hotkey": "F8",
  "language": "zh-CN"
}
```

For English, change only the language value:

```json
{
  "hotkey": "F8",
  "language": "en-US"
}
```

The supported values are `zh-CN` and `en-US`. The configuration is loaded when the mod starts, so restart the game after editing it.

## Usage

1. Enter furniture mode.
2. Press the configured hotkey (`F8` by default).
3. Review the batch summary and confirm.
4. Wait for the completion notice.
5. Leave furniture mode and enter it again once. Consumed copies disappear and the kept items immediately show their new attributes.

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

- 在家具界面按 `F8`，扫描所有稀有度相同的同款家具并生成确定性的合并组合。
- 整批操作只弹出一次确认窗口。
- 两件普通家具合并为一件原生 Rare 家具，基础属性为 2 倍。
- 两件 Rare 家具合并为一件持久化的强化 Rare 家具，基础属性和房间效果为 4 倍。
- 游戏标记为不能变成 Rare 的特殊家具（例如食物箱）不会参与合并。
- 合并过程中不会强制重建当前家具列表。合并完成后退出家具界面并重新进入一次，多余家具会消失，保留家具会立即显示新属性，无需重启游戏。
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

## 切换语言

语言不在游戏内切换。先退出游戏，然后打开：

```text
Mods/CombineDuplicateFurniture/config.json
```

默认中文配置：

```json
{
  "hotkey": "F8",
  "language": "zh-CN"
}
```

切换为英文时，只修改 `language`：

```json
{
  "hotkey": "F8",
  "language": "en-US"
}
```

目前支持 `zh-CN` 和 `en-US`。配置会在 MOD 启动时读取，修改后重新启动游戏即可生效。

## 使用方法

1. 进入家具界面。
2. 按配置的快捷键，默认是 `F8`。
3. 查看批量合并摘要并确认。
4. 等待完成提示。
5. 退出家具界面并重新进入一次。多余家具会消失，保留家具会立即显示新的属性。

日志位置：

```text
Mods/CombineDuplicateFurniture/logs/CombineDuplicateFurniture.log
```

## 致谢

本项目由 GPT-5.6 完成。感谢所有参与测试并提供反馈的 Mewgenics 玩家。
