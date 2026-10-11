# Spawning

**Spawn** is where things enter the scene: objects by class, prefabs, particles, lights and characters. **Game packages** is where extra content comes from. One selection-side panel, **Selection > Other properties**, lets you edit anything the game exposes on the selected object.

> **Caution**
> The Spawn panel carries its own warning: *"WARNING: Spawning an invalid actor can and will cause a crash!"* The object list contains every class the game has loaded, including classes that were never meant to be placed in a level. Stick to classes you can recognise as real content, and see [troubleshooting](troubleshooting.md) when something goes wrong.

## Spawn an object from a class

1. Open **Spawn > Object list**.
2. Type into the search box next to the magnifying glass. It matches both the class name and the package it lives in, so searching `SFXStatic` or a package name both work.
3. Click a class. **Selected class:** shows the full name above the list. The list is grouped by package, with the package name as a grey heading.
4. Set where it should appear in **Spawn > Transform**. Press **Reload from selected** to copy the current selection's transform if that is a convenient spot.
5. Press **Spawn selected**.

**Expected result**: a toast names the class and the object it became, the object appears in the world, and it becomes the selected object, so you can immediately move it, pose it or light it.

**If it does not work**: some classes do nothing, hang the game or crash it, whatever properties you set. That is an engine limitation, not something you can fix by choosing different values. Check the log file, `SuperAdjustmentStudio.log`, to see which class was attempted and how far it got.

### Setting properties before the spawn

**Spawn > Other properties** lists the editable properties of the selected class, read from the class itself. Anything you change here is applied when the object is spawned. The line at the bottom reports how many properties were applied.

Use it when the object needs a value at creation that you cannot change afterwards. A class with no editable properties shows *"(class not loaded or no editable properties)"*.

The refresh icon next to the search box re-reads them from the class.

### Diagnose a class before spawning it

If you are not sure a class will spawn, press **Diagnose class** next to **Spawn selected**. SAS writes a report for that class to the log file. The toast tells you to check there. The report covers whether the class resolves, what its defaults are, and why it would be refused.

This is the cheapest way to find out whether a class is spawnable without risking a crash on it. See [troubleshooting](troubleshooting.md#diagnose-before-you-spawn).

## Spawn a prefab

A prefab is a ready-made group of actors from the game's content, such as a crate set, a console bank or a piece of set dressing. Prefabs carry their own lights.

1. Set the position in **Spawn > Transform**. See [setting a spawn position](transform-and-animation.md#setting-a-spawn-position). Prefabs are spawned there, not at your current selection.
2. Open **Spawn > Prefabs**.
3. Type into **Available prefabs:** or click a name. **Selected prefab:** confirms it. The refresh icon rescans.
4. Press **Spawn**, the **+** icon at the right of the search row.
5. Under **Active:**, spawned prefabs are listed. **Select** makes one the current prefab; the trash icon deletes it.

If a spawn produces no actors, the toast says so and the reason is in the log file.

### Spawning the lights

The light rig is a separate action from spawning the prefab itself.

1. First **Select** the spawned prefab in the **Active:** list. **Spawn lights** acts on that selection, and reports *"Select another object to be able to spawn the prefab!"* when nothing is selected.
2. Under **Light configuration**, pick what to create:
   - **Corner lights**: eight lights around the object.
   - **Face lights**: six lights facing it.
   - **Corner and face lights (heavy!)**: both. Use this only when you need it; it is marked heavy for a reason.
3. Press **Spawn lights**. A toast reports the number of actors or lights created.

### Materials

**Force update material** re-applies the prefab's materials. Use it when the prefab has spawned but is rendering with the wrong or missing materials.

### Editing the lights inside a prefab

Under **Attached lights**, the lights belonging to the prefab selected in the active list are listed individually. Each row has **Select** and a trash icon, and each selected light gets the same **Color:**, **Intensity:** and **Enabled** controls described in [lighting](lighting.md#light).

Above the rows, four bulk sliders act on every light of the selected prefab at once:

| Slider | What it changes |
|---|---|
| **Brightness (all)** | Light component brightness. |
| **Radius (all)** | Point light radius. |
| **Color (all)** | Light colour. |
| **Expansion (all)** | The expansion the spawn used, for resizing the whole rig. |

**Expected result**: changing one bulk slider changes every light of that prefab.

## Spawn particles

1. Select the object the effect should belong to in **Selection > Target**. Particles are spawned against the current selection. With nothing selected, the **+** button does nothing.
2. Set the position in **Spawn > Transform**.
3. Open **Spawn > Particles**.
4. Find the effect under **Available particles:**. Type in the search box, or press the refresh icon to rescan.
5. Click a name to select it, then press the **+** icon at the right of the search row to spawn it.
4. Tick **Loop** to repeat it. **Loop delay:** and **Playback duration:** control the gap between repeats and how long it plays.
5. Under **Active:**, spawned particles are listed. **Select** makes one current; the trash icon removes it.

**Expected result**: the effect plays at the spawn transform, belongs to the selected object, and repeats if **Loop** is ticked.

## Attach a VFX

VFX is different from particles: it attaches to the object you have selected, and it can follow a bone. **VFX** is in the **Selection** section, not **Spawn**, because it acts on the current target.

1. Select the object to attach to in **Selection > Target**.
2. Open **Selection > VFX**.
3. Tick **Show bone selection** to attach to a specific bone, then choose it under **Bone:**. Leave it unticked to attach to the object itself.
4. Find the effect under **Available VFX:** and press the **+** icon to spawn it on the selection.
5. Tick **Loop**, and set **Loop delay:** and **Playback duration:** as needed.
6. Tick **Ignore camera movement** to keep the effect in the world rather than following the camera.
7. Under **Active:**, press **Select** on an entry to edit it, or the trash icon to remove it.

**Expected result**: the effect is attached to the selected object, or to the chosen bone when **Show bone selection** is on.

## Spawn a character

Characters are spawned from **Spawn > NPCs**, and can be done two ways: pick a ready-made one, or build one from parts.

### Pick a character

1. Open **Spawn > NPCs**.
2. The first block is **Characters**. Type into **Search characters...** to filter, and tick **Loaded only** to hide characters whose content is not in memory.
3. Click a name, then press **Spawn**.

**Expected result**: a toast names the pawn you spawned and it becomes the selected object.

> **Note**
> **LE1**: characters in Mass Effect 1 are not pawn classes. Each entry in the list builds a pawn from that character's own type instead of spawning a class, and the spawned pawn stays fully editable with the same mesh and bone controls. The panel says so above the list on LE1.

### Spawn a customized character

For a specific combination of parts (a body from one source, a head from another, a particular face), use the build-up path. Everything for it is under **Spawn > NPCs > Advanced**, which starts expanded.

1. Tick **Spawn with no body mesh** if you want to place the head only. A pawn with no body mesh is invisible, so this is only useful when you intend to add a body afterwards.
2. Under **Meshes**, pick the parts: **Body mesh (skeletal)**, **Head mesh**, **Hair mesh**, **Head gear mesh**, **Visor mesh** and **Face plate mesh**. Each has its own filter box. See [mesh slots](#mesh-slots) for what the filters do.
3. Under **Face and effects**, pick a **Morph face** and a **LifeCrust VFX**, and set **LifeCrust lifetime:**.
4. Under **Object list**, pick the pawn class to use as the base.
5. Press **Spawn customized NPC**.

**Expected result**: a toast names the pawn, and the pawn appears built from the parts you picked.

**If it does not work**: a character class the game refuses is replaced by a fallback rather than failing outright, and the toast tells you what you actually got versus what you asked for. A pawn class that has no component for a slot leaves that slot unchanged and says which one.

> **Note**
> **LE3** has no LifeCrust support: the LifeCrust picker and its lifetime control are not shown. LE1 and LE2 both have it.

### Rebuild the character index

The character list comes from an index built from the game's files. If a character you expect is missing:

1. Open **Spawn > NPCs > Characters**.
2. Tick **Include mods** to include modded content in the scan.
3. Press **Rebuild the index from the game files**, the button labelled **Generate index**.

The scan runs in the background and reports progress as *"Scanning N / M packages, K characters found so far..."*. It can be cancelled. This takes a while on a large installation; a character whose package is not installed reports that the package is missing.

## Change a spawned character's appearance

**Selection > NPC/Pawn** edits a character that already exists, including one that came with the level.

1. Select the pawn in **Selection > Target**. If it is not a character, the panel says *"Select a BioPawn to edit its appearance."*
2. Tick **Show all assets (including ones the game never uses here)** to widen the asset lists. Leave it unticked for the shorter, safer list.
3. **Target pawn:** confirms which pawn you are editing.
4. Open **Customization** and edit the mesh slots, the morph face and the materials, the same pickers used before spawning, applied live.
5. Press **Dump selected pawn** at any point to write a full report of the pawn to a text file (the toast names it, `npc_dump_<pawn>_<timestamp>.txt`): its components, its current mesh in every slot, its morph, its LifeCrust and its class-specific fields. This is the first thing to attach to a bug report.
6. Under **Spawned NPCs:**, the characters SAS spawned are listed. Characters that were already in the level are not. SAS does not track ownership of those.

**Expected result**: the character changes appearance as you edit.

### Mesh slots

Six slots, each with its own asset list and its own filter box:

| Slot | Notes |
|---|---|
| **Body mesh (skeletal)** | The skeleton. Changing it changes the whole body and the bone list. |
| **Head mesh** | Only on characters; the panel says so when the pawn has no head component. |
| **Hair mesh** | Merged into the head mesh. On **LE1** hair is its own component and is not merged. |
| **Head gear mesh** | Helmet and similar. |
| **Visor mesh** | |
| **Face plate mesh** | |

Two things to know about the lists:

- By default a list shows only the assets the game itself uses in that slot. Tick **Show all assets (including ones the game never uses here)** to see everything the game has loaded. A wrong pick can leave the pawn looking broken or invisible. That is what the checkbox is for.
- Each list is capped at 100 entries. When it hits the cap it says *"(showing 100, type to filter the rest)"*, so use the filter box rather than scrolling.
- When a new mesh has a different number of bones than the one it replaced, SAS warns you that the mesh probably does not belong in that slot. That warning is the signal to pick a different asset, not to continue.

If a pawn class has no component for a slot, the pick is still offered and goes through the game's own fallback.

### Apply a morph face

Under **Customization > Morph face**, pick a face from the list. The panel reminds you that *"The morph face is a shared asset - changes affect every pawn and NPC using it."*

Read that before you edit: changing the morph changes the face on **every** pawn and NPC in the level that uses the same morph asset, not just the one you selected.

- **Re-apply morph** pushes the morph to the pawn again. Use it after changing morph parameters, or if the pawn's face has not updated.
- Face shaping parameters are not reachable at runtime. Colours and materials still apply.

> **Note**
> The morph is applied through a different engine call in each game, and the two games that use a material-instance rebuild do not include LE3. In practice: pick a morph and it applies, on all three games. LE3 does not rebuild the pawn's material instances, so a material change on LE3 can need a re-select to show.

### Materials

**Materials** lists the material slots on the pawn. Pick a material per slot, or *"(use the mesh default)"* to leave one alone. Slots the pawn does not have are not listed.

Two small buttons sit at the header: one re-reads the list, the other resets the filter.

### Hide armour parts on LE1

On **LE1**, spawned armour comes with its helmet, visor and face plate already showing. **Selection > NPC/Pawn** gives you three checkboxes, **Helmet**, **Visor** and **Face plate**, to hide them on the selected pawn.

A checkbox is greyed out when the pawn has no such component. They spawn up, so untick what you do not want.

## Edit actor and component properties

**Selection > Other properties** exposes the properties the game itself defines on the selected object, including its components. Edits apply live as you make them.

1. Select the object in **Selection > Target**.
2. Open **Selection > Other properties**.
3. Under **Target component:**, choose *"(actor) NameOfObject"* for the object itself, or one of its components, each listed as `ComponentName[ClassName]`.
4. Type in the search box to narrow the property list, or press **Reload properties** to re-read it.
5. Edit the values in the table.

The line above the table reports how many properties were on the target.

This is the general escape hatch, the same place you go to change a light's brightness on an existing light, or any field a dedicated panel does not cover.

**If it does not work**: some components are not editable. For several of them that is deliberate: making them editable would risk infinite recursion and race conditions.

> **Caution**
> **Allow array slack expansion** in **Controls** allows writing past the end of an array by growing it, which needs a reallocation. SAS shows a warning toast when you enable it. Leave it off unless you are deliberately working on array properties and you know what you are doing.

## Load a game package

Content that is not loaded cannot be spawned. **Game packages** lets you load it.

1. Open **Game packages**.
2. Type in the search box to find the package, or press the refresh icon to rescan. The panel reports *"(scanning...)"* while it works and refreshes itself in the background.
3. Click a package. **Selected package:** shows its name and `Path:` its location on disk.
4. Press **Load**. The toast confirms when the package is loaded.
5. Prefabs and animations from that package become available. Open the prefab list and press its refresh icon to pick them up.

**Expected result**: a toast reports *"Loaded package ..."*, and the content can now be spawned or played.

> **Note**
> Load the package *before* looking for the prefab or animation. Nothing appears in the lists until it is in memory.

### Preview a package

To find out what is inside a package before loading it, which is how you find the exact name of the prefab or animation you want:

1. Select the package in **Game packages**.
2. Press **Preview**.

A separate **Package preview** window opens, showing the file, the export and import counts, and the object tree: nested objects as expandable nodes, imports as a flat list at the bottom. Type in its search box to filter the tree down to the name you are after.

**Expected result**: the object names inside the package are readable without loading it into the game.

If the package cannot be parsed, SAS says so rather than opening an empty window.

## Next

- Light what you just spawned → [lighting](lighting.md)
- Grade and capture it → [photo and grading](photo-and-grading.md), [screenshots](screenshots.md)
