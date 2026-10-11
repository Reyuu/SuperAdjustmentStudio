# Getting started

## Requirements

SAS is an `.asi` plugin. It needs the **ME3Tweaks ASI loader** in your game installation.

1. Install the ME3Tweaks ASI loader for your game.
2. Copy `SAS_SuperAdjustmentStudio.asi` into the `ASI` folder in your game's directory. For Mass Effect 2 that is usually `<Mass Effect 2>\ASI\`.
3. Start the game and load a level.

SAS has no configuration step and no separate launcher. If the overlay does not appear, the loader is not seeing the file. Check that the `.asi` is in the `ASI` folder and that the loader itself is installed.

## Open the overlay

Press `F10`.

That is the default key. To change it, open **Settings** from the menu bar and use **Overlay hotkey**. See [Settings](#settings) below.

While the overlay is open, the game input is captured by the panel, and the system cursor is visible. Close the overlay to play again.

## Tour of the window

SAS is one tall window. Everything in it is a collapsible section; nothing is a tab, so you keep the same layout between sessions.

From the top down:

| Section | What it is for |
|---|---|
| **Controls** | The top-level switches: Pause, Hide all game UI, Click to select, Advanced selection, float the selected pawn, and the array expansion option. |
| **Freecam** | The photo camera, plus the lens, depth of field, bloom and colour adjustments. |
| **Selection** | Everything you do to the object you have selected: Target, Transform, Animation, VFX, Bones, NPC/Pawn, Other properties. |
| **Spawn** | Everything you add to the scene: Object list, Transform, Lights, Prefabs, Particles, NPCs, Other properties. |
| **Game packages** | Search, load and preview the game's own content packages. |
| **Photo guides** | Composition overlay: grid, aspect mask, safe frame, histogram. |
| **Screen filter** | A tint and vignette drawn into the frame. |
| **Postprocess** | The container that holds the LUT and Custom shaders panels. |
| **Screenshot** | Capture options and the Capture button. |

The sections are documented in their own files:

- **Controls**, **Freecam**, **Selection > Target** → [camera-and-selection.md](camera-and-selection.md)
- **Selection > Transform**, **Animation**, **Bones** → [transform-and-animation.md](transform-and-animation.md)
- **Spawn**, **Game packages**, **Selection > Other properties** → [spawning.md](spawning.md)
- **Lights** → [lighting.md](lighting.md)
- **Photo guides**, **Screen filter**, **Postprocess** → [photo-and-grading.md](photo-and-grading.md)
- **Screenshot** → [screenshots.md](screenshots.md)

### The menu bar

Above the sections, the menu bar has three menus:

| Menu item | What it does |
|---|---|
| **Settings** | Language, font size, theme, overlay hotkey, update check. |
| **Snapshots** | **Save snapshot...** and **Load snapshot...**, see [snapshots.md](snapshots.md). |
| **Debug** | **Metrics**, **Debug Log** and **ID Stack**. |

**Debug Log**, **Metrics** and **ID Stack** are ImGui's own developer tools, not SAS's. **Metrics** shows frame timings and counters, **ID Stack** shows the widget stack under the cursor, and **Debug Log** is ImGui's internal log. They help only when the SAS interface itself is misbehaving.

SAS's own messages go to a log file instead, `SuperAdjustmentStudio.log`, written where the game runs. When something does not work and the UI gives you no explanation, that file is where the explanation is.

## Where SAS keeps its files

| What | Where it is |
|---|---|
| LUT images | `SAS_LUTs`, in the same folder as `SAS_SuperAdjustmentStudio.asi` (your `ASI` folder, for example `<Mass Effect 2>\ASI\SAS_LUTs`). Drop `.png` files in it, and `.fx` MLUT pairs with a matching name. |
| Custom shaders | `SAS_Shaders`, in the same folder as the `.asi` (for example `<Mass Effect 2>\ASI\SAS_Shaders`). Drop `.hlsl` files in it. |
| Screenshots | `BioGame\ScreenShots` under the game install (for example `<Mass Effect 2>\BioGame\ScreenShots`). SAS finds it by walking up from the game executable until a `BioGame` folder appears. Change it in the Screenshot panel. |
| Settings | `%APPDATA%\SuperAdjustmentStudio`, which is `C:\Users\<you>\AppData\Roaming\SuperAdjustmentStudio` by default. |
| Snapshots | `%APPDATA%\SuperAdjustmentStudio\snapshots`, in the same folder as the settings file. |

Both asset folders are read when SAS starts and when you press **Rescan** in the corresponding panel. See [grading and LUTs](photo-and-grading.md#add-a-lut) and [custom shaders](photo-and-grading.md#custom-shaders).

## Settings

Open **Settings** from the menu bar. Your edits are staged until you save them, and the panel shows *"(unsaved changes)"* while the staged values differ from the saved ones.

| Control | What it does |
|---|---|
| **Language** | Interface language. Only English is translated so far; choosing another language saves the choice for later and the interface stays in English. |
| **Font size** | Interface font size, as a slider. Useful on 4K. |
| **Theme** | Interface colour theme. |
| **Overlay hotkey** | The key that opens and closes SAS. Press **Bind key**, then press the key you want; `ESC` or **Cancel** stops the capture. |
| **Version** | The SAS version you are running. |
| **Check for updates** | Asks GitHub for the latest release. A press always answers with a toast: *"New version available: `<tag>`"*, *"You are running the latest version"*, or *"Could not reach GitHub. Check your connection."* When a newer release exists, its tag appears beside the button with an **Open release page** button. SAS also checks once a day when it starts, silently, and only reports a newer release. |

Two icon-only buttons at the bottom save and revert the changes: the floppy disk writes them and shows *"Settings saved!"*, the arrow loads the saved values back and shows *"Settings reverted!"*. Settings live in `%APPDATA%\SuperAdjustmentStudio`.

## Next

- Frame a shot → [camera-and-selection.md](camera-and-selection.md)
- Move and pose what is already there → [transform-and-animation.md](transform-and-animation.md)
- Add what is not there → [spawning.md](spawning.md)
