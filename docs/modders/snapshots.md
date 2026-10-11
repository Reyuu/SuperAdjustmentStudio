# Snapshots

A snapshot saves a whole setup (where the camera is, what the objects look like, what is selected, what is lit) and puts it back later.

**Snapshots** is in the menu bar of the SAS window, with two entries: **Save snapshot...** and **Load snapshot...**

## Save a snapshot

1. Set the scene up the way you want it: camera, transforms, poses, lights, grade, whatever is part of the shot.
2. Choose **Snapshots > Save snapshot...**
3. Type a name in the **Snapshot name** box.
4. Tick the sections you want to include. Anything with nothing in it is greyed out and cannot be ticked.
5. Press **Save**.

**Expected result**: a toast confirms *"Snapshot 'name' saved."*

### The sections

| Section | What it saves |
|---|---|
| **Camera position** | Where the freecam is. |
| **FOV / Roll** | The camera's field of view and roll. |
| **Screen filter** | The tint and strength. |
| **Color LUTs** | The LUT stack, its order and its settings. |
| **Animation** | Which animation is playing on which pawn, and whether it loops. |
| **VFX** | The active effects and their settings. |
| **Bones** | Bone poses and offsets. |
| **Selection** | Which object is selected. |
| **Lights** | The lights SAS is tracking and their settings. |
| **Prefabs** | Spawned prefabs. |
| **Particles** | Active particles and their loop settings. |
| **Camera post-process** | The camera's lens, depth of field, bloom and colour adjustments. |

**Selection** is the one to think about. Loading it selects the object the snapshot was saved on, which is what makes the other object sections land on the right things.

> **Note**
> A snapshot does not capture the level itself. Load the same level, in roughly the same state, before loading a snapshot; objects that are not there cannot be restored onto.

### Naming

- **Empty name merges into the last loaded snapshot.** If you leave the name box blank after loading a snapshot, the panel tells you which one it will merge into. That is the fast path for iterating: load, tweak, save.
- Saving over an existing name shows *"A snapshot with this name already exists. Save again to overwrite it."*. Press **Save** a second time to confirm.
- With no name and no previously loaded snapshot, SAS asks you to enter one first.
- If there is nothing to save, it says so rather than writing an empty file.

## Load a snapshot

1. Choose **Snapshots > Load snapshot...**
2. Pick a snapshot from the list on the left. Each entry shows the sections it contains as icons next to the name.
3. In the list on the right, untick anything you do not want applied. Selecting a snapshot ticks everything it contains.
4. Press **Load**.

**Expected result**: a toast confirms *"Snapshot 'name' loaded"* and the window closes.

The refresh icon at the top-left of the window rescans the snapshot folder, so use it if you added or renamed files outside SAS.

**Delete** removes the selected snapshot. **Cancel** closes the window without applying anything.

> **Note**
> Snapshots are tied to the game they were made in. Loading one saved in a different Mass Effect does nothing except tell you: *"Snapshot 'name' was saved for X and cannot be loaded in Y."* Snapshots from another game are not even listed.

## Where snapshots live

In `%APPDATA%\SuperAdjustmentStudio\snapshots`, for example `C:\Users\<you>\AppData\Roaming\SuperAdjustmentStudio\snapshots`. Copy them between installations, or between machines, by copying that folder.

That makes them a convenient way to keep a lighting setup, a camera framing or a character pose and reuse it, including across different levels, as long as the objects you are restoring onto are actually there.

## Next

- When a loaded snapshot is not what you expected → [troubleshooting](troubleshooting.md)
