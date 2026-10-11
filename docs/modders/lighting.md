# Lighting

**Spawn > Lights** creates and edits lights. Two other routes into lighting are at the end: the lights that come with a prefab, and any light component in the level.

> **Note**
> Not everything can be lit or relit. The game's lighting is a mix of baked and dynamic light. Baked light is stored in the level's lightmaps and cannot change at runtime, so surfaces lit that way will not respond to anything here. SAS creates and edits movable (dynamic) lights, which add dynamic light and change dynamic light components, but they do not rebake the level.

## Add a light

1. Open **Spawn > Lights**.
2. Set the position in **Spawn > Transform**. See [setting a spawn position](transform-and-animation.md#setting-a-spawn-position).
3. Press **Add light**.

A movable spot light is created at that position and becomes the selected light.

**Expected result**: a new light in the world, listed under **Active:** and selected.

To aim it, rotate it with the **Rotation** fields in **Spawn > Transform** before adding it, or afterwards with [the Selection transform editor](transform-and-animation.md#move-rotate-and-scale-a-selected-object) once the light is selected as a target.

### The active light list

**Active:** lists every light SAS is tracking, each as `Type: Name` with two buttons:

| Button | What it does |
|---|---|
| **Select** | Makes that light the selected one, so the editor below applies to it. |
| Trash icon | Removes the light. |

The refresh icon at the top of the list rescans for lights. With no lights it reads *"(no active lights)"*.

## The selected light

**Selected light:** names the light being edited. Four collapsible blocks follow.

### Light

| Control | What it does |
|---|---|
| **Color:** | The light's colour. |
| **Intensity:** | Brightness. |
| **Enabled** | Turns the light off and on without deleting it. |

Changes apply as you make them.

### Shape

Offered for point lights, which includes spot lights.

| Control | What it does |
|---|---|
| **Radius:** | How far the light reaches. |
| **Falloff exponent:** | How quickly brightness drops off with distance. Low values spread far, high values stay tight. |
| **Shadow radius multiplier:** | Softens the shadow edges cast by this light. |

For a **spot** light, three more appear inside the same block:

| Control | What it does |
|---|---|
| **Inner cone angle:** | Angle of the fully lit centre of the cone. |
| **Outer cone angle:** | Angle where the cone falls off to nothing. |
| **Light shaft cone angle:** | Angle of the visible volumetric shaft, which is what you see in a dusty room. |

### Shadows

| Control | Options |
|---|---|
| **Cast shadows** | On or off. |
| **Cast dynamic shadows** | On or off. Shadows from objects that move. |
| **Projection technique:** | `Default`, `PCF`, `VSM`, `BPCF Low`, `BPCF Medium`, `BPCF High`. |
| **Filter quality:** | `Low`, `Medium`, `High`. |
| **Mode:** | `Normal`, `Modulate`, `Modulate better`. |

**Filter quality** is the one that matters most for stills: raise it on the light that matters and leave the rest alone. `BPCF High` with **Filter quality** `High` is the sharpest, most expensive combination. It is the last thing to turn up and the first to turn down if the frame rate suffers.

### Bloom and shafts

| Control | What it does |
|---|---|
| **Render light shafts** | Whether the visible shaft in the air is drawn. |
| **Bloom scale** | How much the light blooms. |
| **Bloom threshold** | How bright the light has to be before it blooms. |
| **Bloom screen blend threshold** | How much the bloom spreads across the screen. |
| **Bloom tint** | The colour of the bloom. |

Bloom controls here affect the light. The global bloom on the *camera* is a separate control under [photo adjustments](photo-and-grading.md#photo-adjustments). Change the one that matches what you are trying to fix.

## Edit lights inside a prefab

Prefabs come with their own lights. Those are edited together under **Spawn > Prefabs > Attached lights**, and that block only appears once you have selected a prefab in the **Active:** list.

- Each light has a **Select** button and a trash icon.
- Four bulk sliders, **Brightness (all)**, **Radius (all)**, **Color (all)** and **Expansion (all)**, act on every light of the prefab at once.

See [spawn a prefab](spawning.md#spawn-a-prefab) for how those lights get created in the first place.

## Edit a light that is already in the level

Lights the level shipped with are not in the **Active:** list. Two ways to change them:

**Via the light component**

1. Tick **Advanced selection** in **Controls** so lights appear in the target list.
2. Select the light in **Selection > Target**.
3. Open **Selection > Other properties**.
4. Pick the light component under **Target component:** and edit its brightness, colour and the rest.

**Via the light's actor properties**

The same panel, with *"(actor) NameOfLight"* selected instead of the component, exposes whatever the light actor itself defines.

**Expected result**: the level light changes as you edit.

> **Note**
> Not every component is editable. For some of them that is deliberate. Making them editable would risk infinite recursion and race conditions. If a value does not respond, it is one of those. See [troubleshooting](troubleshooting.md).

## Next

- Frame and grade the lit scene → [photo and grading](photo-and-grading.md)
- Capture it → [screenshots](screenshots.md)
