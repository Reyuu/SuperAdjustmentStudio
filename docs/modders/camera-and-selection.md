# Camera and selection

Everything in this file is in the top two sections of the window: **Controls**, and **Freecam** / **Selection > Target**.

## Get a clean view

Before anything else, get the game's own interface out of the shot and stop the world moving.

1. Open the overlay with `F10`.
2. In **Controls**, tick **Hide all game UI**. The HUD, health bars and dialogue chrome disappear.
3. In **Controls**, tick **Pause**. Game time stops and animations are paused with it.

**Expected result**: you are looking at the level with no HUD and nothing moving.

> **Note**
> **Pause** also pauses animations, which is what makes bone posing stable. See [pose bones](transform-and-animation.md#pose-bones). If you tick **Hide all game UI** and it appears not to take effect immediately, toggle it again; the change is applied on the game's render thread.

## Move the camera

The game's own photo camera is replaced by SAS's freecam, which is not limited to photo-mode moments. It also works during conversations.

1. Open **Freecam**.
2. Tick **Enable freecam (photo camera)**.
3. Right-click anywhere outside the overlay and drag to orbit around the focal point.
4. Hold `SHIFT` **first**, then right-click and drag to pan left, right, forward and backward.
5. Hold `SHIFT` **first**, then left-click and drag to lift the camera up and down.
6. Use **Move speed** to change how fast pans and lifts move. **Field of view** changes the lens. **Roll** tilts the horizon.
7. Press **Reset to player** to snap the camera back to the player.

The line under the controls reads `RMB look, SHIFT+RMB pan, SHIFT+LMB lift (hold SHIFT first); freecam off = native orbit`, and the **Status:** line tells you whether the camera is free, still attaching, or still detaching.

**Expected result**: the camera moves independently of the player, and the status line reports *free*.

> **Note**
> The order matters on the modifier combinations. Hold `SHIFT` before you press the mouse button. **Field of view** and **Roll** are per-camera and are part of what a snapshot saves; see [snapshots](snapshots.md).

### Hide what you do not want in the shot

Under **Freecam > Photo adjustments > Hide** there are four checkboxes:

| Checkbox | Hides |
|---|---|
| **Player** | The player character. |
| **Party** | Your squad. |
| **Other pawns (NPCs, enemies)** | Every other character in the level. |
| **Vehicles** | Vehicles. |

**Photo adjustments** itself has to be ticked for this block to appear. See [photo and grading](photo-and-grading.md#photo-adjustments) for the rest of that panel.

### Freecam off

Untick **Enable freecam (photo camera)** and you are back on the game's native orbit camera. Nothing else in the scene changes.

## Select a target

Everything under **Selection** acts on the current target. Choose it in **Selection > Target**.

1. Open **Selection > Target**.
2. Press **Target player** to select the player character, or click a name in the list below.
3. To narrow a long list, type into the search box, the magnifying glass icon at the left of the row. It matches anywhere in the name.
4. To select something that is not in the list, see [click to select](#click-to-select) or [advanced selection](#advanced-selection).

**Expected result**: the **Selected object:** line under **Selection > Animation** and the panels below it now act on that object.

The row above the list has three icon-only controls:

| Control | Position | What it does |
|---|---|---|
| Search box | Left of the row, after the magnifying glass | Filters the list by name. |
| Trash icon | Right of the search box | Deletes the selected object, but only if SAS spawned it. A game object like the player is never deleted. |
| Refresh icon | Right of the trash icon | Rescans the level and rebuilds the list. |

> **Note**
> The trash icon does nothing for objects that were already in the level. SAS only removes what it spawned itself. If the icon appears dead for a level object, that is expected.

### Click to select

1. In **Controls**, tick **Click to select**.
2. With the overlay open, click directly on an object in the game.

The click picks whatever is under the cursor, provided it has collision. The clicked object becomes the current selection, and SAS adds it to the selection list if it is not there already.

> **Note**
> Click to select runs only while the SAS overlay is open, and only for clicks that land on the game world rather than on the panel. With it off, clicking in the game does not select anything; you choose the target from the list instead. Turn it on when the list is too long to scan, so you can click objects directly.

### Advanced selection

By default the list contains only Pawns, meaning characters and anything else that walks.

1. In **Controls**, tick **Advanced selection**.
2. Open **Selection > Target** again.

Every actor in the level is now listed: lights, props, level geometry, volumes. This is how you select the lights and props that the level already contains.

Two consequences:

- The list gets much longer, so use the search box.
- The trash icon stops working. Deleting is restricted to SAS-spawned objects precisely because deleting arbitrary level actors is destructive.

## Drawing helpers

The lower table in **Controls** controls the helpers drawn in the world. They are display-only except where noted.

| Checkbox | What it draws |
|---|---|
| **Show orientation gizmo** | A small axes marker showing which way the selected object faces. |
| **Draw gizmos always on top** | Draws the helpers over everything, including geometry that would normally hide them. |
| **Draw tracer** | A line from the selected object to the player, useful when the object is behind a wall or off screen. |
| **Highlight selected** | Outlines the selected object so it is obvious which one it is in a crowded scene. |
| **Bone pivot gizmo** | Marks the bone you have selected in **Selection > Bones**. See [pose bones](transform-and-animation.md#pose-bones). |

**Expected result**: the helpers appear in the world while the overlay is open.

## Move a pawn through geometry

Characters are blocked by level collision. To place one exactly where you want it:

1. Select the pawn in **Selection > Target**.
2. In **Controls**, tick **Float selected pawn (ignore collisions)**.

The checkbox only appears once a pawn is selected.

**Expected result**: the pawn passes through walls, floors and props, and stays where you put it.

> **Note**
> This is also the fastest way to move a character to an exact spot. Combine it with the **Position** sliders in [transform and animation](transform-and-animation.md#move-rotate-and-scale-a-selected-object).

## Next

- Change where the selected object is, or what it is doing → [transform and animation](transform-and-animation.md)
- Put something new in the scene → [spawning](spawning.md)
