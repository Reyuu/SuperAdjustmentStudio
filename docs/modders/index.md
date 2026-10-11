# SuperAdjustmentStudio: modder guide

SAS is a live scene adjuster for Mass Effect. You move the camera, move and pose what is already in the level, spawn what is not, light it, grade it and take the shot, all inside the running game, without opening LEXplorer.

This guide is written as recipes. Every entry in the inventory below says what you want to do and links to the procedure that does it.

Start at [getting-started.md](getting-started.md) if this is your first time with SAS.

## Game support

SAS builds per game and some features do not exist everywhere. Recipes are written so they work on any game; the note under a recipe tells you where behaviour differs.

| Capability | LE1 | LE2 | LE3 |
|---|---|---|---|
| Move / rotate / scale actors, select, gizmos, freecam, screenshots, snapshots | Yes | Yes | Yes |
| Custom animation playback | **No**<sup>1</sup> | Yes | Yes |
| Bone posing | Yes | Yes | Yes |
| Object / prefab / particle / VFX spawning | Yes | Yes | Yes |
| Character (NPC) spawning | Yes<sup>2</sup> | Yes | Yes |
| Morph face | Yes | Yes | Yes |
| LifeCrust | Yes | Yes | **No**<sup>3</sup> |
| Package load / preview | Yes | Yes | Yes |
| LUTs, depth gates, custom shaders, photo guides | Yes | Yes | Yes |

This release is a **beta**. Treat it as such.

* <sup>1</sup> LE1 has no animation playback system. You can pose a character by hand, but you cannot play a custom animation on it.
* <sup>2</sup> LE1 has a limited character index. Some characters cannot be spawned, and some will not have their morphs applied correctly.
* <sup>3</sup> LE3 has no LifeCrust system, so you cannot spawn a character with it.

## Task inventory

### Common

What most sessions start with, or come back for every time.

| I want to… | Recipe |
|---|---|
| Open the overlay for the first time | [getting-started.md](getting-started.md#open-the-overlay) |
| Find a control, or work out where SAS keeps its files | [getting-started.md](getting-started.md#tour-of-the-window) |
| Get a clean, controllable view of the scene | [camera-and-selection.md](camera-and-selection.md#get-a-clean-view) |
| Frame a shot with a camera I control | [camera-and-selection.md](camera-and-selection.md#move-the-camera) |
| Pick the thing I want to edit | [camera-and-selection.md](camera-and-selection.md#select-a-target) |
| Move, rotate or scale an object | [transform-and-animation.md](transform-and-animation.md#move-rotate-and-scale-a-selected-object) |
| Play a custom animation on a pawn | [transform-and-animation.md](transform-and-animation.md#play-a-custom-animation) |
| Pose a character by hand, bone by bone | [transform-and-animation.md](transform-and-animation.md#pose-bones) |
| Take a screenshot | [screenshots.md](screenshots.md#take-a-screenshot) |
| Spawn a character | [spawning.md](spawning.md#spawn-a-character) |
| Change what a spawned character looks like | [spawning.md](spawning.md#change-a-spawned-characters-appearance) |
| Spawn an object that is not in the level | [spawning.md](spawning.md#spawn-an-object-from-a-class) |
| Add a light and tune it | [lighting.md](lighting.md#add-a-light) |
| Save a setup and get it back later | [snapshots.md](snapshots.md#save-a-snapshot) |
| Grade the final image (LUT, filter, depth of field) | [photo-and-grading.md](photo-and-grading.md#grade-the-image) |
| Frame a shot with composition guides on | [photo-and-grading.md](photo-and-grading.md#composition-guides) |

### Uncommon

Real work, but not every session.

| I want to… | Recipe |
|---|---|
| Spawn a prop together with its lights | [spawning.md](spawning.md#spawn-a-prefab) |
| Put an effect on a specific bone | [spawning.md](spawning.md#attach-a-vfx) |
| Add a particle effect to the scene | [spawning.md](spawning.md#spawn-particles) |
| Edit any actor or component property live | [spawning.md](spawning.md#edit-actor-and-component-properties) |
| Load extra content from a package into the running game | [spawning.md](spawning.md#load-a-game-package) |
| Find the name of a prefab or animation | [spawning.md](spawning.md#preview-a-package) |
| Build a character from specific meshes before spawning it | [spawning.md](spawning.md#spawn-a-customized-character) |
| Put a face on a character | [spawning.md](spawning.md#apply-a-morph-face) |
| Hide a helmet, visor or face plate on LE1 armour | [spawning.md](spawning.md#hide-armour-parts-on-le1) |
| Capture a high-resolution tiled image | [screenshots.md](screenshots.md#tiled-high-resolution-shots) |
| Change the overlay key, language, font size or theme | [getting-started.md](getting-started.md#settings) |
| Move a character through walls and geometry | [camera-and-selection.md](camera-and-selection.md#move-a-pawn-through-geometry) |
| Change the look of the lights that come with a prefab | [lighting.md](lighting.md#edit-lights-inside-a-prefab) |

### Rare

Advanced, and worth reading the caution first.

| I want to… | Recipe |
|---|---|
| Write my own post-process shader | [photo-and-grading.md](photo-and-grading.md#custom-shaders) |
| Grade only part of the frame, by depth | [photo-and-grading.md](photo-and-grading.md#grade-by-depth) |
| Drive the depth buffer directly for a custom effect | [photo-and-grading.md](photo-and-grading.md#depth-inspector) |
| Fine shadow quality on a single light | [lighting.md](lighting.md#shadows) |
| Rebuild the character index, including modded content | [spawning.md](spawning.md#rebuild-the-character-index) |
| Set a character's hair, headgear, visor or face plate by hand | [spawning.md](spawning.md#mesh-slots) |
| See what a failing class actually is, before I try to spawn it | [troubleshooting.md](troubleshooting.md#diagnose-before-you-spawn) |
| Give SAS a file to report a crash with | [troubleshooting.md](troubleshooting.md#what-to-include-in-a-bug-report) |

## Topic files

| File | Covers |
|---|---|
| [getting-started.md](getting-started.md) | Requirements, opening SAS, every panel in the window, where files live, settings. |
| [camera-and-selection.md](camera-and-selection.md) | View control, freecam, choosing what you are editing, drawing helpers. |
| [transform-and-animation.md](transform-and-animation.md) | Moving and scaling objects, custom animation playback, bone posing. |
| [spawning.md](spawning.md) | Objects, prefabs, particles, VFX, characters, game packages, live property editing. |
| [lighting.md](lighting.md) | Creating lights, shadow and bloom settings, editing lights that already exist. |
| [photo-and-grading.md](photo-and-grading.md) | Composition guides, depth of field, colour grading, LUTs, custom shaders. |
| [screenshots.md](screenshots.md) | Capture options, output formats, tiled high-resolution shots, where files land. |
| [snapshots.md](snapshots.md) | Saving and restoring a whole setup, and what a snapshot can contain. |
| [troubleshooting.md](troubleshooting.md) | Known problems, dangerous options, diagnostics, what to send in a bug report. |

## Conventions used in these recipes

- Controls are named exactly as they appear in the English UI, so you can find every one of them.
- Some buttons have no text, only an icon. Those are described by their position, for example "the trash icon to the right of the search box".
- Every recipe ends with an **Expected result** line describing what you should see.
- Where behaviour differs between Mass Effect 1, 2 and 3, a **Per-game notes** block follows the steps. If there is no such block, the recipe behaves the same on all three.
- **If it does not work** appears only for failure modes the project documents. For anything else, check the log file, `SuperAdjustmentStudio.log`. Most questions about behaviour are answered there.
