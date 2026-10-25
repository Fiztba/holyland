# Weapon sets (mod)

An opt-in mod, off by default: a second weapon and shield ("set 2") kept beside the ones in hand, swapped
with them on one key, so that a sword and shield and a bow are one press apart. With it off the game runs
exactly as without it.

- **Turning it on:** the overlay's Mods page ("Weapon sets"), `settings.json`'s `"mods"` `"weapon_sets"`, or
  `--weapon-sets` for one run.
- **The key:** R on the keyboard (both layouts) and a controller's right stick press (R3, which the game never
  reads); rebindable on the Controls page, "Swap weapon sets (mod)", listed while the mod is on.
- **Choosing set 2:** on the equipment screen, Up from Weapon reaches two more rows, **Shield 2** and then
  **Weapon 2**: the order is Shortcut, Weapon 2, Shield 2, Weapon 1, Magic, Shield 1, Helm, ..., so the armour is
  no further down than before. Left and Right walk the same items as the game's rows; Cross keeps the item for set
  2 (the "E" marks the one kept) instead of wearing it. The figure keeps showing what is worn. The rows' "2" (and
  the "1" on the game's Weapon and Shield), and a line naming set 2 under the row's name, are drawn by the overlay
  in the menus' style (as the extended stats mod's lines are).
- **In play:** one press swaps the weapon and shield in hand with set 2. A short line shows what is in hand now.
- **Saved per playthrough:** set 2 is kept with each save, as the automap keeps its explored maps (below).

## The equipment screen

`Menu_UpdateEquip` (ui/menu_screens.c) shows one row at a time. A row is a `MenuItemList`: its `item` is the
cursor in a ring of the owned items of one group (`Menu_BuildItemRings`), and the rows are a ring of their own
(`Menu_Init`: Up follows `next`, Down `prev`; Weapon, Magic, Shield, Helm, Armour, Gauntlets, Boots, Ring,
Bracelet, Amulet, Shortcut). Left and Right move the row's cursor (`Menu_MoveEquipCursor`), the models of the item
and of its neighbours (up, down, left, right) are loaded from the rows' `next`/`prev` and the items' `next`/`prev`
(`Menu_SetEquipModelNames`), the detail panel (Triangle) reads the item's group, and the name of the row is the
word sprite of its group (`equip_group_glyphs`).

Weapon 2 and Shield 2 are two more `MenuItemList`s (`game/weapon_sets.c`) with groups 0 and 2, over the same
rings of items, linked into the ring of rows between Shortcut and Weapon when the rings are built. So everything
the screen does with a row works on them as on the game's: the models, the cursor, the detail panel, the row's
name. Four places of `Menu_UpdateEquip` and `Menu_BuildItemRings` needed a word (in `patches/game/ui.patch`, one
call each, all marked `PC:`):

| Where | What |
|---|---|
| `Menu_BuildItemRings`, at its end | `WeaponSets_LinkLists`: links the two rows (cursors on set 2's items) while the mod is on, unlinks them when it is off |
| `Menu_UpdateEquip`, after Up and Down | `WeaponSets_TrackEquip`: `equip_group_shown` is the row's group (for the game's rows it always is; the count Up and Down keep would be off by the two rows), and notes the screen for the overlay |
| the "E" (`Menu_IsEquipped`) | `WeaponSets_ShownIsEquipped`: on the two rows, set 2's item |
| Cross | `WeaponSets_Choose`: on the two rows the item is kept for set 2, by the screen's own rules, with its sound |

The rules of Cross (`Menu_UpdateEquip`) are kept for set 2: a weapon that is not held in one hand
(`PlayerItemDef.action_handler_index` 1, the two-handed ones, and 2, the bows and crossbows) clears the shield, and
a shield clears such a weapon.

## The swap

`WeaponSets_Poll` runs once a game frame from `Quick_PollGame`, right after `GameLoop_OpenRequestedMenu`, and takes
the press only where the game would let its menu open:

- `Player_Update`'s menu button: the player in control (`player_control`, not `update_hold_flags` bit 0), not
  dying, no item being used (`bit6`, `item_use_cooldown`), no other menu asked for; not paused, no message open;
- not with the weapon out: `item_action_step` 0 and `frames_since_item_action` at least 2, as the game's own reload
  of the arm models waits for (`GameLoop_OpenRequestedMenu`, `game_status.bit14`). The game itself would open the
  menu mid-swing and cut the swing short (`Player_LoadArmModels`); the mod ignores the press instead;
- `GameLoop_OpenRequestedMenu`'s waits (the frame buffer half, the sound banks loading, a map load at steps 2 to 4,
  the files locked): the press waits for them for up to a second, then is dropped.

A press that is refused is dropped (`[weapon sets] ... the swap key ignored: <why>` on stderr), and a press in a
menu does nothing. Falling, running and charging a spell do not stop it, as they do not stop the menu.

The swap is what the equipment screen and the menu's closing do:

1. The weapon, then the shield, into `PlayerState.weapon` and `equipment[EQUIP_SHIELD]` (the screen's Cross; a
   weapon not held in one hand leaves no shield); the ones they replace become set 2.
2. `Player_RecomputeStats`, and HP and MP capped at the new most (the screen's Cross).
3. `Player_LoadArmModels`: the held weapon's model, motions, sound bank and trail (the menu's closing).
4. When the weapon changed, the strength gauge emptied and the weapon's magic set charging (the menu's closing).

Durability and the weapon's level are the item's own (`weapon_level_records`, `worn_item_level_records`, by item),
so they go with it: nothing is copied. A bow's arrows are counted by item (`item_counts[ammo_item_index]`), as
always.

If an item of set 2 is no longer owned (sold, dropped, with the smith: not in the ring the equipment screen would
list), it is dropped from set 2 when the rings are built and before a swap.

## Per save

`port/platform/weapon_sets.c` keeps set 2 as the automap keeps its explored maps:

- `weapon_sets/live.txt` in the data folder is the playthrough's, written at each change;
- a quick save also writes `weapon_sets/quick_<file>.txt`, a memory card save (the game's file `BASLUS-20318Gnn`
  opened for writing) `weapon_sets/card_nn.txt`;
- loading a quick save or a memory card save brings its copy back; a save made without one has an empty set 2. A
  new game (play reached from the title without a load) starts with an empty set 2.

## Where the code is

| File | |
|---|---|
| `game/weapon_sets.c` | the game's side: the rows, the swap |
| `port/platform/weapon_sets.c` | the switch, the key's press, the files |
| `port/overlay/weapon_sets_view.cpp` | the rows' numbers, set 2's line, the swap's message, the Mods page's section |
| `port/include/port/weapon_sets.h` | the interface between them |

The hooks into shared files are one line each: `patches/game/ui.patch` (above), `game/quicksave.c` (the poll and the
title), `port/sdk/controls.c` and `controls.h` (`ACTION_WEAPON_SWAP`), `port/overlay/settings.c`, `overlay.cpp` (the
Mods page, visibility, drawing), `main_host.c` (`--weapon-sets`), `port/platform/quicksave.c` and `port/sdk/libmc.c`
(the files).

## Tests

`KF4_WEAPON_SETS_SWAP_AT=V,V,...` presses the key at those vblanks (a pad script has no PC keys). Every swap and
refusal is written on stderr with the weapon, the shield, set 2, Physical, the attack and defence values, the
weight, the weapon's level and hits, its durability, the arrows and the shield's durability.

Checked on a copy of the owner's save (`--cheats all` for a bow): Weapon 2 set to the Long Bow on the equipment
screen; the swap took the Fiery Sword and Buckler (attack 37 8 29, defence 33 26 29, weight 36.1) to the Long Bow
(attack 0 0 81, defence 27 17 21, weight 34.6), the bow drew and shot (arrows 99 to 98), the swap back restored the
sword and the shield with the same level, hits (61) and durability (94%); presses mid-swing and with the bow drawn
were ignored; a quick save taken after it brought set 2 (the Long Bow) back on loading.
