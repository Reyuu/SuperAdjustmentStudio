# Screenshots

The **Screenshot** panel is at the bottom of the SAS window. It captures what the post-process chain produced, so the image you get is the graded image, not the raw frame.

## Take a screenshot

1. Open **Screenshot**.
2. Press **Capture**.

**Expected result**: the status line below the button reports *"Captured ..."* and then *"Saved to ..."* with the full path.

The whole panel is disabled while a capture is running, so the button greys out until it finishes.

### Where the file goes

**Save directory** shows the current path. By default SAS walks up from the game executable until it finds a `BioGame` folder and saves into `BioGame\ScreenShots`, the same place the game's own screenshots go.

To change it:

| Button | What it does |
|---|---|
| **Browse** | Opens a folder picker. The chosen folder is remembered in the settings. |
| **Revert to default** | Goes back to `BioGame\ScreenShots`. |

### Format

**Format** is `PNG`, `JPEG` or `BMP`. PNG by default; JPEG only if you are reducing file size for something that does not need to be lossless.

Changing the format saves it to your settings, so it stays.

## Tiled high-resolution shots

A tiled capture renders the frame in several pieces and stitches them, giving an image larger than your resolution.

| Control | What it does |
|---|---|
| **Multiplier** | How many tiles across. `1` is a single normal shot. |
| **Overlap** | How much the tiles overlap, which hides the seams. |

1. Set **Overlap** before raising **Multiplier**. The seams are much easier to hide with an overlap set.
2. Raise **Multiplier** as far as your frame rate and video memory allow. The capture is taken over several frames and the status line reads *"Capturing tiledshot..."* while it works.
3. Press **Capture**.

**Expected result**: one large stitched image in the output folder.

**If it does not work**: if the tiled capture produces no file, the status line says so. Drop the multiplier back a step and try again.

### Extra unlit shot

**Extra unlit shot** takes a second capture with the scene lighting removed, alongside the lit one. Useful as a reference pass for compositing or for checking which parts of an image depend on the level's lights.

With it ticked the status line reads *"Capturing unlit tiledshot..."* for that pass.

## What is in a saved shot

A capture is taken after the post-process chain has run, so a saved image contains:

- the LUT stack, including any depth-gated LUTs. The panel states *"Baked shots include LUTs, depth gates included."*
- the custom shader chain. *"Saved shots include the custom shaders."*
- the screen filter tint and vignette, because the filter is drawn into the frame rather than on top of it

Composition guides, the histogram and the depth inspector are **not** in the capture. They are drawing aids over the frame.

To turn the grade off for one capture, disable the LUT and shader entries (their enable checkboxes are in [photo and grading](photo-and-grading.md#grade-the-image)) rather than deleting them from the stack.

## Status messages

The status line reports what happened, in order:

| Message | Meaning |
|---|---|
| `Capturing tiledshot...` | Working. |
| `Capturing unlit tiledshot...` | Working on the extra pass. |
| `Tiledshot produced no file` | The capture failed. See above. |
| `Captured <file>` | The raw capture is done. |
| `Converting <file>...` | Being converted to your chosen format. |
| `Failed to convert <file>` | The conversion to JPEG or BMP did not work. |
| `Converted <file>` | Conversion done. |
| `Saved to <path>` | Final file location. |
| `Screenshot failed!` | The capture itself failed. |

## Next

- Save the setup the shot came from → [snapshots](snapshots.md)
- When something does not work → [troubleshooting](troubleshooting.md)
