# Photo and grading

Everything that changes how the image looks: the composition overlay, the lens, the colour and the post-process chain.

All of it lives in three panels: **Photo guides**, **Screen filter**, and **Postprocess**, the last of which holds the **LUT** and **Custom shaders** panels.

## Composition guides

**Photo guides** draws an overlay on top of the game frame.

1. Open **Photo guides**.
2. Tick **Show composition guides**. The panel reminds you: *"Guides stay visible when the SAS overlay is hidden."* You can close SAS and keep framing.
3. Under **Grid:**, choose `None`, `3x3`, `4x4`, `Crosshair`, `Diagonals`, `Phi Grid` or `Golden Triangles`.
4. Under **Aspect mask:**, choose `None`, `16:9`, `21:9`, `1:1`, `4:3`, `5:4`, `3:2` or `9:16`. Anything other than `None` darkens everything outside the mask, and **Mask opacity:** appears to control how dark.
5. **Line color / thickness** sets the guide colour and its weight.
6. Tick **Center dot**, **Safe frame (90%)** and **Readout label** as needed.

**Expected result**: the guides draw over the frame and are still there when you press `F10` to hide the overlay.

### Histogram

**Histogram (RGB)** draws the channel histogram of the frame.

- **Shadow threshold:** marks the black point.
- **Highlight threshold:** marks the white point.
- The two triangle markers on the plot light up when a threshold is set, and the readout below reports the current FOV, the frame size in pixels, and what percentage of the image is clipped at each end.

Move the thresholds to judge exposure before you commit to a grade.

## Photo adjustments

**Freecam > Photo adjustments** is the camera's own look. Tick the panel heading to open it.

**Lens**: **Depth of field** turns on depth of field, then:

| Control | What it does |
|---|---|
| **Focus distance** | How far away the sharp plane is. |
| **Focus inner radius** | Size of the fully sharp region around that distance. |
| **F-stop** | Depth of field strength. |
| **DoF intensity** | How strongly it blurs. |

**Bloom**: **Enable bloom**, then **Bloom threshold** (how bright a pixel has to be to bloom) and **Bloom intensity**.

**Color**: **Enable color**, then **Contrast**, **Brightness** and **Saturation**.

**Hide**: the four visibility checkboxes for hiding the player, party, other pawns and vehicles. See [hide what you do not want in the shot](camera-and-selection.md#hide-what-you-do-not-want-in-the-shot).

> **Note**
> These are camera controls and they are part of what a snapshot saves. The per-light bloom in [lighting](lighting.md#bloom-and-shafts) is a different setting. Change whichever one matches the problem you are looking at.

## Screen filter

**Screen filter** is a colour tint over the whole frame. The panel says it is *"Drawn into the frame, visible on screenshots"*; unlike the composition guides, it is part of the image.

1. Open **Screen filter**.
2. Tick **Apply screen filter**.
3. Set the **Tint color / strength:**, a colour picker and a strength slider on the same row.

Two more controls sit above it:

| Control | What it does |
|---|---|
| **Disable fog** | Turns off the game's atmospheric fog. |
| **Disable lens flares** | Turns off lens flares. |

**Expected result**: the whole frame takes the tint, and the tint is in the saved screenshot.

> **Note**
> There is no vignette control in this panel. The bundled example shader implements one. See [custom shaders](#custom-shaders).

## Grade the image

The post-process chain is in **Postprocess**, which contains the **LUT** panel first and the **Custom shaders** panel second. Anything enabled in either runs on every frame, including the frames that end up in a screenshot.

### Add a LUT

1. Drop your `.png` files into the `SAS_LUTs` folder next to the `.asi` (your `ASI` folder). `.cube`-style MLUT sets work as a matching `.fx` file with the same name.
2. Open **Postprocess > LUT**. The folder the panel is reading is shown at the top.
3. Press the refresh icon (**Rescan**) if you added files after SAS started.
4. Press **+** to open the add popup. It shows `Add LUT (n/max)` and the current stack usage.
5. Type in the search box to filter, click an entry, and it is added to the stack.

With no LUTs found, the panel tells you to drop `.png` files (and MLUT `.fx` pairs) into the folder above. Entries that failed to parse are listed with a `(!)` suffix and cannot be added.

Once a LUT is in the stack, it has its own controls:

| Control | What it does |
|---|---|
| **Blend** | How strongly this LUT applies. |
| **Chroma** | Colour-only application, leaving luminance alone. |
| **Luma** | Luminance-only application. |
| Enable checkbox | Turns the entry on and off. |
| Drag grip | Reorder the stack. The stack order is the order they apply in. |
| Trash icon | Removes the entry. |

**Expected result**: the graded LUT applies to the frame and to your captures.

> **Note**
> The stack has a fixed number of slots. When it is full the panel says *"Stack is full."* Remove an entry before adding another.

### Grade by depth

Each LUT entry can be masked by depth, so the grade applies only to part of the frame, so a corridor can be graded differently from the sky behind it.

1. Expand the LUT entry in the stack and tick **Depth gate**.
2. Set **Focus**, the depth the gate is centred on, and **Range**, how far from it the gate reaches.
3. Set **Blend min** and **Blend max** to control how the gate fades in at the near edge and out at the far edge.
4. Tick **Preview depth gate** to see the mask in game, and **Heatmap** to see it as a colour overlay instead of as the actual LUT.

Depth gate needs the depth buffer, which is captured by **Enable depth capture** at the top of the LUT panel. Without it the gate has nothing to read. The panel notes that saved shots include LUTs and depth gates.

### Depth inspector

**Depth inspector** shows the captured depth buffer directly, useful for finding out what a depth-based effect is actually seeing.

| Control | What it does |
|---|---|
| **Enable depth capture** | Turns depth capture on. |
| **Depth source:** | Which depth surface to read. Press the refresh icon to rescan the available surfaces. |
| **Freeze** | Holds the current depth so you can inspect it without it updating. |
| **Linearize** | Converts the raw depth to a linear distance. |
| **Invert** | Inverts the depth. |
| **Near** / **Far** | The range shown. Both are logarithmic. |
| **Heatmap** | Shows the buffer as a heatmap rather than greyscale. |
| **Show selected depth texture in-game** | Draws the selected surface in the world. |

### Custom shaders

A custom shader is your own HLSL post-process pass. It is the most powerful thing in SAS and the one that takes the most setup.

1. Drop `.hlsl` files into the `SAS_Shaders` folder next to the `.asi`. The panel lists the folders it is reading at the top.
2. Open **Postprocess > Custom shaders**. If the folder is empty it says so and offers nothing to add.
3. Press **+** to add one. The popup shows `Add shader (n/max)`, a search box, and the shaders found.
4. Or press **Add file...** at the bottom of the popup to pick a `.hlsl` from anywhere on disk.

Each slot in the chain has an enable checkbox, a drag grip to reorder, and a trash icon. **Recompile** rebuilds every shader; the per-slot **Recompile** button rebuilds just that one. **Reset to defaults** puts the slot's parameters back to the values the shader declares. The folder icon opens the shaders folder.

A slot that has not been compiled says *"Not loaded yet"*; once it compiles, the line above it reports how many widgets it produced, for example *"Loaded: N float, N int, N color widgets"*.

**Expected result**: your shader runs after the LUT stack, and appears in saved shots.

#### Writing the shader

A shader declares its own interface in comments. One file is one slot, and the entry point must be named `PS`.

```
// sas: slider(Strength, 0, 1, 0.45)
// sas: toggle(Tint on, 1)
// sas: radio(Blend mode, Normal, Multiply, Screen, 0)
// sas: dragInt(Bands, 1, 8, 1)
// sas: color(Tint, 1.0, 0.85, 0.7)
```

Those five lines are what produce the widgets in the panel. The panel is generated from the shader, not the other way round. Every annotation begins with `// sas:` and names its widget first, and that name is what keys the stored value, so names must be unique in the file.

| Kind | Arguments | Notes |
|---|---|---|
| `slider` | `Name, min, max, default[, step]` | Float. Needs `max > min`; an omitted `step` becomes `(max - min) / 100`. |
| `drag` | `Name, min, max, default[, step]` | Float, same arguments as `slider` but a drag widget. |
| `input` | `Name, default[, step]` | Free float entry, no bounds; an omitted `step` is `0.01`. |
| `dragInt` | `Name, min, max, default` | Integer. Needs `max > min`. |
| `inputInt` | `Name, default[, step]` | Free integer entry, no bounds; an omitted `step` is `1`. |
| `toggle` / `checkbox` | `Name, default` | Boolean; a non-zero `default` starts it on. |
| `radio` / `combo` / `listbox` | `Name, item1, item2, ..., defaultIndex` | One or more labels, then an integer default index from `0` to label count `- 1`. The shader receives the selected index. |
| `color` | `Name, r, g, b[, a]` | Components clamped to `0..1`; include `a` for an alpha picker. |

A malformed annotation, an unknown kind, a duplicate name, or a default outside its range fails the whole parse, so the widget indices never shift silently.

Bindings you get:

| Name | Contents |
|---|---|
| `gParams[]` | Float widget values, in annotation order. |
| `gIntParams[]` | Integer widget values. Optional: if you do not declare it, integer widgets take the `gParams[]` slots after the float ones and their values arrive as floats. |
| `gColors[]` | Colours. |
| `gTime` | Elapsed time. |
| `gResolution` | Frame size. |
| `gFlags` | Bit 0: depth is bound. Bit 1: last pass. Bits 8-15: pass index. |

The array sizes are injected at compile time as `SAS_NUM_FLOATS`, `SAS_NUM_INTS` and `SAS_NUM_COLORS`, so there is no hard cap on how many widgets you declare. `#include` resolves relative to the file, then to the shaders folder; paths that escape above it are rejected.

The compiled target is `ps_5_0`, falling back to `4_0` on hardware without feature level 11_0. Compile errors are shown in the panel and written to the log file.

**The shipped example** is `custom_shader_example.hlsl`, a vignette and tint with switchable blend modes. It is the fastest way to see what a shader looks like, and it is where the vignette from the screen filter section comes from.

> **Note**
> The shader chain runs **after** the LUT stack, in slot order. A shader that ignores `gFlags` bit 0 and samples depth unconditionally will misbehave whenever depth capture is off.

## Next

- Capture the result → [screenshots](screenshots.md)
- Save the whole setup → [snapshots](snapshots.md)
