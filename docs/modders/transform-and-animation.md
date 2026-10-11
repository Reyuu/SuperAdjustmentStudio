# Transform, animation and bones

Three things you do to the object you have selected: move it, make it do something, and change its pose by hand. See [camera and selection](camera-and-selection.md#select-a-target) for choosing the target first.

## Move, rotate and scale a selected object

**Selection > Transform** edits the selected object. The panel says so itself: *"Applies live to the selected object every 250ms."* You do not need an apply button. Edit a value and it is already on the object a quarter of a second later.

1. Select the object in **Selection > Target**.
2. Open **Selection > Transform**. The fields fill in with the object's current transform.
3. Under **Position**, set `X`, `Y` and `Z`. Under **Rotation**, set `RX`, `RY` and `RZ` in degrees. Under **Scale**, set `X`, `Y` and `Z`.
4. Each field is a drag control: drag left and right to change the value, or click and type an exact number.

**Expected result**: the object moves as you edit, without a confirm step.

To go back to what the object is really at (after a mistake, or after something else moved it), press **Reload from selected** at the top of the panel. A toast confirms which object was reloaded from.

> **Note**
> `Y` is up in Unreal units, `Z` is into the screen. If an object moves the wrong way, this is why.

### Setting a spawn position

**Spawn > Transform** is the same editor, used for the position a new object will appear at instead of an existing one. It is separate from the Selection panel and it does not touch anything already in the scene.

1. Open **Spawn > Transform**.
2. Press **Reload from selected** to copy the currently selected object's transform into it.
3. Set the position you want.

**Expected result**: the next object you spawn appears there.

## Play a custom animation

**Selection > Animation** plays any animation the game can see for the selected pawn, whether or not it is in the pawn's normal animation set.

1. Select a pawn in **Selection > Target**. It must have a skeleton. Characters do; props and lights do not.
2. Open **Selection > Animation**. The panel warns that the object must have bones.
3. Type in the search box next to the magnifying glass to filter by name.
4. Click a name in the list. The **Selected animation:** line below confirms it.
5. Press **Play**, or **Play (loop)** to have it repeat until you change something.
6. Press **Reset** to return the pawn to its normal state.

The refresh icon at the top of the list rescans the animations available for the current selection. The list only changes when the selection changes or when you refresh it.

**Expected result**: the selected pawn performs the animation you picked.

**If it does not work**: animations can stay broken after playback, in which case the character can remain stuck part-way through a pose. The fix is to press **Play** and then **Reset** again.

> **Note**
> **LE1**: custom animation playback is not available. Mass Effect 1 pawns have no animation slots, and the game resolves animations through an internal animation-set map that the plugin cannot reach. The Animation panel shows that explanation instead of the player controls. LE2 and LE3 both play custom animations; which engine call is used to do it differs, but that is not something you see or set.

### Why an animation is not in the list

Only animations the game has actually loaded are listed. If an animation is not referenced anywhere in the current scene, by an animation set or any other route, there is nothing for SAS to play.

The two ways to change that are outside SAS: get the content loaded by the game itself, or load its package with [Game packages](spawning.md#load-a-game-package) and then refresh the list.

### Showing everything

Tick **Include all animation sets** to include every animation set on the pawn rather than the default subset. The list gets much longer and is rebuilt when you toggle it.

## Pose bones

Bone posing moves individual bones of a character. It is the tool for a pose the animation system has no clip for.

**Do this first**: tick **Pause** in **Controls**. Posing a moving skeleton fights the animation, and the offsets are applied relative to whatever the animation did last.

1. Select a character and tick **Pause**.
2. Open **Selection > Bones**.
3. Choose the mesh under **Target mesh:**, **Body** for the skeleton or **Head** for the head. **Head** is only offered on characters. Switching the mesh rebuilds the bone list.
4. Type in the search box to find a bone. Each list entry shows the bone name and its parent after a `<`, so `jaw < head` means the jaw inside the head.
5. Click the bone. The **Selected bone:** line below the list confirms it.
6. Under **Position offset**, set `X`, `Y` and `Z`. This moves the bone from where the pose put it. Under **Rotation**, set `RX`, `RY` and `RZ`. Under **Scale**, set `S`.
7. The pose applies live as you edit.

**Expected result**: the bone holds the offset you set, and the character keeps that pose.

The count at the bottom of the list tells you how many bones the mesh has. An empty list with *"(no bones found in the mesh)"* means the mesh on this pawn has no skeleton.

Three buttons under the editor:

| Button | What it does |
|---|---|
| **Reload from bone** | Re-reads the bone from the mesh and zeroes the position offset, keeping the rotation and scale you set. |
| **Reset from pose** | Returns the whole mesh to the pose it was in when you selected it, discarding your edits. |
| **Absolute reset** | Clears everything SAS has applied to this mesh, in absolute terms. |

> **Caution**
> **Absolute reset** is the most sensitive control in SAS. Absolute mode is very easy to overshoot, and a pose that looks wrong after an absolute reset often cannot be recovered by adjusting the fields. The character can end up somewhere you cannot drag it back from. Use **Reset from pose** for anything you want to undo; reach for **Absolute reset** only when you need everything gone.

**If it does not work**: scaling bones in a non-uniform way gives wrong results. The scale is forced to uniform, which is why there is a single `S` slider rather than three: what looks like it should scale along one axis only cannot be done here.

## Next

- Put something new in the scene → [spawning](spawning.md)
- Light it → [lighting](lighting.md)
