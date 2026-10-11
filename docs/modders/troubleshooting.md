# Troubleshooting

Most problems SAS reports are either visible in the UI as a message or written to the log file. This page collects the known ones and tells you where to look for the rest.

## Known issues

These are documented limitations, not things you are doing wrong.

### A character stays stuck after an animation

After an animation has played back and the character has been reset, animations can stay broken. The character can remain part-way through a pose or stop animating.

**Fix**: press **Play** and then **Reset** again.

### Spawning does nothing, hangs or crashes

Trying to spawn some objects does nothing, hangs the game or crashes to desktop, whatever properties you set. The spawn panel warns about this: some classes in the list were never meant to be placed in a level.

There is no property you can change to fix it. Use [Diagnose class](#diagnose-before-you-spawn) before spawning something unfamiliar, and be ready to lose the session.

### Bone scale looks wrong

Scaling bones in a non-uniform way gives wrong results. The scale is forced to be uniform, which is why **Scale** is a single `S` field rather than three axes. If you need a bone scaled along one axis, you cannot get it from this panel.

### A property will not change

Some components are not editable. For several of them this is deliberate. Making them editable would risk infinite recursion and race conditions.

A property that does not respond is usually one of those, not a broken build. Check the log file before assuming otherwise.

### An animation is not in the list

Only animations the game has loaded are listed. An animation that is not referenced anywhere in the current scene, by an animation set or any other route, is not available. Load its package with [Game packages](spawning.md#load-a-game-package) and then refresh the animation list.

### "SDK globals not initialized"

The Spawn section reports *"SDK globals not initialized! Check log file for more info."* and shows no class list. SAS cannot enumerate the game's objects, so it cannot spawn anything.

Check the log file for what it says at startup. In practice this means SAS loaded too early or the game's object array was not ready. Restart the game and open the overlay in a loaded level rather than on the main menu.

### An object will not spawn where you want it

Objects are placed at the **Spawn > Transform** values, not at the position of your current selection. Press **Reload from selected** in that panel to copy the selection's transform across.

## Dangerous options

### Allow array slack expansion

In **Controls**, **Allow array slack expansion** lets SAS write past the end of an array by growing it, which requires a reallocation of memory that the game did not allocate. SAS shows a warning toast when you enable it for this reason.

Leave it off unless you are deliberately working on array properties in [Selection > Other properties](spawning.md#edit-actor-and-component-properties) and you know what the array is. A mistake here is a crash or corrupted state, not a wrong value.

## Diagnose before you spawn

**Diagnose class**, next to **Spawn selected** in the Spawn section, writes a report for the selected class to the log file. The toast tells you to check there.

The report covers whether the class resolved, its defaults and flags, and why it would be refused. Use it whenever:

- a class you expected is not in the list, or the list shows *"(no classes match)"*
- you are about to try a class from a package you just loaded
- a spawn produced nothing and you want to know whether the class or the transform was the problem

It costs nothing and cannot crash the game the way a spawn can.

## The log file

SAS's own messages go to a log file, `SuperAdjustmentStudio.log`, written where the game runs. It records what SAS asked the engine to do and what came back: classes that resolved, animations that were found or not, prefabs and packages, light changes, snapshot activity, NPC dumps and shader compiles.

Open it before doing the thing that went wrong if you can, or read it afterwards: the entries are timestamped and in order.

The messages the UI shows you are summaries of what is in it. When a message tells you to check the log, the log file has the detail.

## The Debug menu

**Debug** in the menu bar holds ImGui's own developer windows, not SAS's messages. They help only when the SAS interface itself is misbehaving.

### Metrics

Frame timings and counters. Useful when something is slow, such as a shadow setting at maximum quality, a heavy prefab light rig or a large tiled capture.

### ID Stack

The ImGui widget stack under the cursor. Only useful for reporting a problem with the SAS interface itself, such as a control that cannot be clicked.

### Debug Log

ImGui's internal log. It is normally empty, and nothing SAS does is written to it.

## What to include in a bug report

The more of these you can supply, the faster it can be reproduced:

| What | Where it comes from |
|---|---|
| What you did, step by step | The recipes are written as steps. Follow them exactly and note where it diverges. |
| What you expected and what happened | Including the exact wording of any toast. |
| The game and its version | LE1, LE2 or LE3, and the patch. |
| The SAS version | Shown in **Settings > Version**. |
| The log file | `SuperAdjustmentStudio.log`, from before the problem onwards. |
| A pawn dump, if a character is involved | Select the pawn and press **Dump selected pawn** in [Selection > NPC/Pawn](spawning.md#change-a-spawned-characters-appearance). It reports every component, the mesh in each slot, the morph and the class fields. |
| A class diagnosis, if a spawn is involved | **Diagnose class** before spawning. |
| A screenshot | From the game, not from SAS, if it shows the problem. |
| Metrics, if it is a performance problem | **Debug > Metrics**. |

For shader problems, also include the `.hlsl` and the compile errors from the **Custom shaders** panel.

## Related

- [Spawning](spawning.md): the panel whose warning this page refers to
- [Transform, animation and bones](transform-and-animation.md): poses and playback
- [Getting started](getting-started.md): where the Debug menu lives
