# Extended stats (mod)

An opt-in mod, off by default: what King's Field IV keeps about the player and never shows, written
into the game's own menus, and the real numbers of every hit. It only **reads** the game's state: on or
off, the game runs exactly as without it (no patch in `patches/` belongs to it).

- **Turning it on:** the overlay's Mods page, "Extended stats in the game's menus" (the first switch of
  its "Extended stats" section), `settings.json`'s `"mods"`, or `--extended-stats` for one run. That is
  all: from then on the menus show it whenever they are open.
- **In the game's menus** (on with the mod): lines in the empty places of the menu screens, in a serif
  and the grey of the game's own text, fading in and out with the screen (below).
- **The stats panel** (optional): F6 shows and hides a free-floating panel with all of it and more
  (spell levels, gauges, recovery). It is rebindable on the Controls page ("Show / hide the stats panel
  (mod)", listed while the mod is on). With the overlay open (F1) it can be moved; its place is kept in
  `overlay.ini`.
- **The hit log** (optional): at the bottom left, each hit the player deals and takes, with the real
  damage and the HP left, for five seconds. At the top, the health bar of the creature last hit, for six
  seconds.

| `settings.json` `"mods"` key | default | |
|---|---|---|
| `extended_stats` | `false` | the mod |
| `extended_stats_menus` | `true` | its lines in the game's menus |
| `extended_stats_panel` | `true` | the stats panel, on its key |
| `extended_stats_damage` | `true` | the hit log and the target's health bar |
| `extended_stats_durability` | `true` | the equipment's durability (the menus and the panel) |

The parts count only while `extended_stats` is on. `extended_stats_status_screen` (the panel opening
with the Status screen, which the menus' lines replaced) is no longer read, and is dropped from the file
when it is written.

## In the game's menus

| Screen | Where | What |
|---|---|---|
| Main menu | under Condition, in the box of Exp ... Gold | Next level (the experience to it, x2 with the Fortune), Next Physical (weapon hits to the next point), Next Magical (spells to the next point), Weapon level N (hits to the next level), each with a thin bar of its progress; the ailments running, with their time left |
| Status | under Condition, in the left box | the same |
| Equipment | in the box of the figure, around it | above: the weapon's level (hits / 180 or 480) and the ailments; the defences against them when any; left: each worn piece's durability; right: the hidden effects of what is worn, and recovery or drain |
| Item, Arrange | at the foot of the box of the item's model | what the item under the cursor does (below) |

Nothing is drawn over the game's text: the places are the menus' own empty ones, and on Main and Status
the ailment icons beside Condition (one for each ailment running) are kept clear of, line by line. The
lines hide while the game's detail panel (Triangle) or the equipped list (Square) is over the place. The
text is fitted to its place: the largest size (up to about the game's row height) at which every row
fits, else smaller, down to a floor; at 1080 rows the text is 17 to 34 pixels high, at 1440 rows 23 to 45.

### Where the places are

The menu screens are made of parts, `menu/*_para.dat` (`Menu_LoadAllParts`, in game RAM as `main_parts`,
`status_parts`, `equip_parts`, `item_parts`, `recol_parts`): sprites with their corners in GS
coordinates. `MenuSprite_Draw` draws them where they say, so a part at (X, Y) is at (X - 1728,
(Y - 1936) * 2) of the menus' 640 x 448 picture (their XYOFFSET, and the field's rows doubled; the
window's pictures confirm it). The places (`port/overlay/stats_menus.cpp` names the parts each comes from):

| Screen | Place (640 x 448) | from |
|---|---|---|
| Main | x 352-574, y 309-364 | the labels' column (part 38), the values' right edge (30), Condition (31), the bottom border (39); the ailment icons, parts 0-5 |
| Status | x 82-302, y 321-372 | the same in `status_para.dat` (69, 51, 62, 8); icons 10-15 |
| Equipment | above x 334-608, y 44-78; left x 334-404, right x 552-608, y 88-330 | the box (part 0) inside its borders (21, 3, 2), the weight's band (9); the figure stands in the middle (about x 448-525, its hands with a weapon and a shield at x 409-547) |
| Item, Arrange | x 395-617, at most y 180-250 | the model's box (`item_para.dat` part 1, `recol_para.dat` 23) |

The window shows the 640 x 448 picture whole at 4:3 (or stretched), and with widescreen squeezed into
the middle three quarters of its width (the GS front end, README "Widescreen"); the same places then
land on the same parts with the native GS, `--native-draw`, `--renderer modern` and paraLLEl-GS, at
either presenter (checked at 1440 x 1080, 1920 x 1080 and 2560 x 1440).

### Which screen is up

The game's menu (`Menu_Run(0)`, `menu_mode` 0, `menu_step` not 0) runs one screen handler at a time,
`menu_screen_handler`. The handlers are the game's own static functions, which cannot be named from the
port, so `track_menu` (`game/stats.c`) tells them apart by what they do (`ui/menu_screens.c`):

- only `Menu_UpdateMain` moves `main_menu_state` off 0 (1 while it takes input, 2 and 3 while it fades
  out), and a screen it opens finds it back at 0: the handler while it is not 0 is the main menu's;
- another handler is the screen of the line chosen (`main_menu_screens`: 0 Item, 2 Equipment, 3 Status,
  5 Arrange), `main_menu_line` staying on that line until the menu closes (`Menu_CutModelChain`);
- the Item screen's sub-screen of the keys (`Menu_UpdateKeyItems`) is told from it by `use_item_frames`,
  which `Menu_UpdateUseItem` alone steps each time it runs.

A screen's parts fade in and out together, by a tenth of their alpha a frame, so `screen_part_alphas[0]`
against its part's alpha is how far it has faded in; the lines follow it. The item under the cursor is
`item_list_cursor[0]` (Item) or `owned_list_current[0]` (Arrange); `detail_panel_open` and
`equipped_list_open` say the game's panels are up.

### What an item does

Everything comes from the code that uses the item (`Player_UseItem`, `Player_ApplyItem`) and its row of
`param/item_cmn.prm` (`item_common_defs`, read in game RAM); names (the item a vial leaves, the spell a
crystal teaches, the weapon that shoots an arrow) are the game's own text, read at runtime.

- **Type 0** (used up, `use_mode` 0): `hp_now += hp_heal`, `mp_now += mp_heal`, each capped at the most;
  999 or more is shown as "all" (`Player_RecomputeStats` caps `hp_max` and `mp_max` at 999). Each bit n of
  `timed_flags` clears `timed_flags[n]`, curing that ailment ("Cures every ailment but poison" for
  0x3E). `use_mode` 2 gives one of `item` back ("Leaves a ..."). On the US disc this gives: the Herb 50
  HP, Herbal Liquid 180, Forest Fruit all HP, Antidote 5 HP and poison, Forest Root every ailment but
  poison, Dried Amphibia every ailment, Mushroom 80 MP, Dragon Flower all HP and MP and every ailment,
  the three Healing Waters 500 HP / 160 MP / 500 HP and 150 MP, each leaving the Crystal Vial.
- **Its own code** (`Player_ApplyItem`): `ITEM_POWER_SEED` and `ITEM_WIDDA_NEEDLE` start a timer of 1800
  frames (60 s) in which Physical or Magic Power is a tenth more (`Player_RecomputeStats`); the fruits
  (`ITEM_WISE_MANS_FRUIT`, `ITEM_FOOLS_FRUIT`) raise Magic or Physical Power by 1 for good, up to 999;
  `ITEM_MAGIC_CRYSTAL` raises the most MP by 1 (and restores its 50 MP by its row); the crystals of the
  six schools teach the school's next spell not yet known (`Player_LearnSpellInRow`; its name, effect
  `(school * 5 + slot) * 3 + 1`) and raise the most MP by 1 (71-76, restoring 50) or 5 (77-82, restoring
  all); the Poison Water (item 18) poisons for 1800 frames whatever the defence
  (`Player_ApplyPoisonWater`).
- **Arrows** (145-147): the menu never uses them (`Menu_UpdateUseItem`); a weapon shoots while there is
  one of its `PlayerItemDef.ammo_item_index` (`Player_UpdateActions`): "Shot by" that weapon.
- **Types 1, 4, 5**: used on the map object in front (`MapObject_UseItem`), used up with `use_mode` 0;
  **type 2** (the wands): used standing on a symbol of passage (`MapChange_StartPassage`); **type 6**:
  opens its map (`MapViewer_OpenForItem`); **type 3** (keys) are never listed.
- **Type 8**: "Nothing happened". The Earth Stone pays the smith for each upgrade
  (`Smith_UpdateListWithConfirm` takes one); bones, scales and claws pay for the trades whose items
  have `upgrade_material_kind` 1 to 3 (`Trade_UpdateTakeAmount`). The rest (the Crystal Vial, the
  Icons of Healing and Ice, the Rock of Life, the Forest Dew, the Strange Egg) show "Nothing happens when
  used": what they are for is a script of the person or object they are used on (`item_script_id`,
  `item_use_script`), not the item's own.
- **Weapons and worn items** (Arrange): the weapon's level and hits, its durability (bows and rings,
  bracelets, amulets do not wear), the ailments the piece guards against (`worn_ailment_defence`), its
  two data bonuses and its effect in the code (the tables below).

Every item the Item screen lists is covered; none was left to a guess.

## Where the code is

| File | |
|---|---|
| `game/stats.c` | built with the game code: reads the game's state into a snapshot (`port_stats_sample`: with the menu screen up and what the item under its cursor does), and watches the hits and the menu once a vblank (`port_stats_track`) |
| `port/overlay/stats_menus.cpp` | the lines in the game's menus: their places, fitting and drawing (Dear ImGui, no window) |
| `port/overlay/stats_panel.cpp` | draws the panel, the hit log, the health bar and the Mods page's section (Dear ImGui) |
| `port/overlay/stats_mod.c` | the switches and their `settings.json` keys |
| `port/include/port/stats.h` | the interface between them |

The hooks into shared files are one line each: `overlay.cpp` (the Mods page, the menus' font, the
vblank's tracking and drawing, whether the overlay draws this vblank, and the `menu@V` test picture), `settings.c` (read and write the keys),
`main_host.c` (`--extended-stats`), and `controls` (the panel's key).

Names of items and spells are read from the game's own tables in game RAM (`item_name_defs`,
param/item_nam.prm; `fx_table`, param/magic.prm), decoded from the game's text code; nothing of the
game's text is in the source.

`KF4_STATS_LOG=1` prints, on stderr, every change of the counters the panel shows (`[stats]
weapon_use_count=... power_base=...`), every hit, and every creature as it is first seen (its record,
HP and place): what the checks below were read from. `KF4_STATS_PANEL_AT=VBLANK` opens the panel at
that vblank, so that `KF4_OVERLAY_SHOT=closed@N` takes its picture.

The menus' lines are checked with pictures: `KF4_OVERLAY_SHOT=menu@V,...` writes the window (overlay
closed) at each vblank V as `overlay_menu_V.png`, and a pad script opens the screens. From a quick save
loaded on the title screen (`--quick-load`, the memory card notice passed with `100 CROSS 4`), Circle at
500 opens the menu; Cross opens the Item screen and Down walks its list; Down, Down, Cross opens
Equipment (Right picks a piece, Cross wears it, Down goes to the next slot); Down x3, Cross opens
Status. `--cheats all` before a quick save gives every item to look at; using the Poison Water from the
Item screen gives an ailment to see beside Condition.

## The stats, and how each was proved

The game runs 30 frames a second; every timer below counts those frames (two vblanks each).

### Level

- **Where:** `PlayerState.experience` and `.level`; the table `player_level_steps[100]` (rodata):
  the experience each level starts at and what reaching it adds to `hp_base`, `mp_base`,
  `power_base` and `magic_base`.
- **How:** `Player_AddExperience` takes level L once experience reaches
  `player_level_steps[L].experience`. The bar runs from this level's start to the next one's; "Next"
  is that next row's gains. Experience counts double while the Fortune bracelet is worn
  (`ITEM_FORTUNE`).
- **Checked:** a new game, level 1, shows "50 to level 2"; a kill worth 10 (`EntityParams
  .experience_reward`) made it "40 to level 2" (`KF4_STATS_LOG`: exp=10).

### Physical and Magic Power: the next point

- **Where:** `PlayerState.weapon_use_count` (0x0D4) and `.magic_use_count` (0x0D8), both saved.
- **How:** `Player_CountWeaponUse` adds one, modulo 100, and at 0 raises `power_base` by one ("Physical
  UP"), up to 999. It is called by `Attack_HitEntity` only for a weapon hit that **deals damage**
  (`damage > 0`): swings that miss, bounce or hit a map object do not count. `Player_CountMagicUse`
  does the same for `magic_base` ("Magic Power UP"). An attack spell counts once per shot, when the
  shot first deals damage (`magic_use_counted`); the spells that are not shots count when cast
  (`Player_CastMagic`), the healing ones only when HP was below the most, the cure only while
  poisoned. The bars show the count out of 100; "(N in effect)" is `PlayerState.power`/`.magic`, the
  base with what is worn and the timed effects (`Player_RecomputeStats`).
- **Checked:** a save with `weapon_use_count` 97: three club hits on a creature took it to 98, 99 and
  then 0 with `power_base` 30 to 31 (the game's "Physical UP" showed). A save with `magic_use_count`
  99: casts that missed spent MP (200, 195, 190 ...) and left the count alone; the cast that hit (130
  damage) took it to 0 and `magic_base` 30 to 31 ("Magic Power UP" on screen, the panel at 0 / 100).

### Weapon level

- **Where:** `weapon_level_records[weapon]` (`LevelRecord`, saved): `use_count` and `level_index`
  (0 to 2, the row of `PlayerItemDef.attack_by_level` in use).
- **How:** `Player_CountWeaponUse` (the same damaging hits) adds one to `use_count` up to 480; at 180
  and 480 `level_index` rises ("Weapon Level UP"). The panel shows level 1 to 3 and the hits toward
  the next. A weapon's second magic needs level 3 (`Player_UpdateStatus`).
- **Checked:** `use_count` 178: the second hit made it 180 and `level_index` 1 (level 2).

### Spell levels

- **Where:** `spell_levels[6][5]` (0: not known, else 1 to 3) and `spell_experience[6][5]`, saved.
- **How:** `Player_CountMagicUse`: the spell of school r, slot c at level L is effect
  `(r * 5 + c) * 3 + L`. Each counted use adds 1 (3 for the spells cast from the menu, `fx_table
  .item_menu_magic`, but for effects 0x10-0x12 and 0x37-0x39), up to 150; level 2 at 50, level 3 at
  150.
- **Checked:** the first spell at 49 of 50: the hit made it 50 and level 2 ("Magic Level UP"); the
  panel showed "2: 50 / 150".

### Gauges

- **How:** `Player_UpdateStatus` fills the weapon's gauge by `max(power, 20) * 42 / (weapon weight in
  tenths of a kilogram + 1)` a frame, to 5000 (weapons whose `action_handler_index` is 2, the bows, have
  none), and the magic gauge by `max(magic, 20) * 42 / (fx_table[charging_magic]
  .gauge_charge_divisor + 1)`. The panel shows the time from empty to full. A weapon hit's damage is
  scaled by the gauge (`damage * strength_gauge / 5000`, `Attack_HitEntity`): a full gauge is full
  damage.
- **Checked:** Magic 30 to 31 made the magic gauge's time 4.2 s to 4.0 s (125 and 120 frames: 5000
  over 30 * 42 / (divisor + 1) and over 31 * 42 / (divisor + 1), rounded up); the club's full-gauge hit dealt 85, the next ones, swung
  every 25 frames, 4 to 6.

### Weight

- **Where:** `PlayerState.weight` and `.max_weight`, tenths of a pound; the Status screen shows them
  too. `max_weight` is `(power * 3 + 80) * 2.2046`. Over it the player is slowed in proportion and
  cannot run (`Player_Update`); the panel says so.

### Equipment wear

- **Where:** `LevelRecord.level` of the weapon (`weapon_level_records`) and of each worn item
  (`worn_item_level_records`): the percent its values count at (`Player_RecomputeStats`,
  `Player_AddWornItemValues`: `(base + gained) * level / 100`), from 100 down to 50. `LevelRecord.exp` is
  the wear toward the next point, and `LevelDef.exp_per_level` (item_nam.prm byte 0x37) the wear a
  point costs.
- **How:** `Attack_WearWeapon`: a weapon hit wears the weapon by 1 (10% to 100% of the creature's most
  HP), 2 (under 10%) or 4 (no damage: it bounced); bows and the bare hand do not wear.
  `Attack_WearArmorFromHit`: a hit on the player wears the shield, helm, armour, gloves and boots
  (rings, bracelets and amulets never wear) by `damage * 10 / hp_max` (at least 1): with a shield up
  and a hit under 5% of the most HP only the shield, by 1; otherwise the shield double and the rest
  once; without a shield everything double and the piece hit four times. The smith mends it.
- **Checked:** the shield (`exp_per_level` 5) on a player with 300 HP took 10-damage hits: wear 1, 2, 3,
  4, then 99% with wear 0, and so on to 97% after fifteen hits. The club (`exp_per_level` 40) took 1
  for an 85-damage hit on a 105-HP creature and 2 for each 4-6 damage hit.

### Ailments

- **Where:** `PlayerState.timed_flags[6]`, frames left; `ailment_defence[6]`, percent; both by index.
  The game never names them; the panel's names are the port's, from what each does:

  | # | Name in the panel | What it does (code) | Lasts |
  |---|---|---|---|
  | 0 | Poison | 3 HP every 30 frames (every 90 while `ailment0_slow_timer` runs), green tint (`Player_UpdateStatus`) | 1800 |
  | 1 | Paralysis | no moving, turning, looking, attacking or casting (`Player_Update`) | 150 |
  | 2 | Darkness | the screen darkened (`Player_UpdateScreenTints`) | 3600 |
  | 3 | Curse | Physical and Magic Power halved, 1 HP and 1 MP every 30 frames, purple tint | 5400 |
  | 4 | Slow | no running, white tint | 1800 |
  | 5 | Magic seal | the magic gauge held empty | 18000 |

  Poison and paralysis are confirmed by the item data: the bracelet whose description promises to
  protect against paralysis has `worn_ailment_defence[1]` 255 (capped at 100), and the armband that
  protects against poison has `[0]` 100.
- **How:** a hit's chance of each is its rate less the defence, never under 5%
  (`Attack_RollPlayerAilments`); the defence is what is worn plus timed bonuses, at most 100
  (`Player_SumWornBonuses`, `Player_RecomputeStats`). They wear off 1 a frame; with Demon Armor
  (`ITEM_DEMON_ARMOR`) not on every third frame (1.5 times as long), with the Purity Amulet
  (`ITEM_PURITY_AMULET`) 2 on every third frame (0.75 times); the panel's seconds left take it in.
- **Checked:** a save with `timed_flags[0]` 1800: the hit log showed "-3 HP" every 60 vblanks (30
  frames) and the panel "50 s left" 300 frames on.

### Recovery and drain, hidden effects

- **Where:** `Player_SumWornBonuses` and `Player_AddItemBonuses` (the weapon's and every worn item's
  two `bonus_kinds`/`bonus_amounts`, item_wpn.prm 0x64 and item_pct.prm 0x1F): kind 2 a drain (Physical
  and Magic halved, 1 HP and 1 MP a second, as the curse), 3/4 HP up/down and 5/6 MP up/down (one point
  every `150 / total amount` frames), 7/8 a percent of the damage dealt back as HP/MP
  (`Attack_HitEntity`), 9/10 Physical/Magic Power plus the amount. Kind 1 sets `PlayerState.unkE8`,
  which nothing reads; kind 11 marks the items with effects of their own in the code.
- The effects the code ties to particular items, each listed in the panel while it is worn:

  | Item | Effect | Site |
  |---|---|---|
  | `ITEM_FORTUNE` | experience x2 | `Player_AddExperience` |
  | weapon 167 (0x11) | double damage against chr records 47, 50-66, 92-97, 164, 167, 168 (most of them undead) | `Attack_HitEntity` |
  | weapon 186 (0x24) | every 600 frames a 70% chance of poison, defence ignored | `Player_UpdateStatus` |
  | weapon 196 (0x2E) | breaks the map objects (def 0x201) other weapons bounce off | `Attack_HitMapObject` |
  | shields 221-222, boots 326-327 | the fire part of the damage taken +20% each | `Attack_HitPlayer` |
  | `ITEM_DEMON_ARMOR`, `ITEM_PURITY_AMULET` | ailments 1.5x / 0.75x as long | `Player_UpdateStatus` |
  | the whole Ancient set | the weight of the six counts a third | `Player_RecomputeStats` |
  | `ITEM_ANCIENT_SHIELD` without the set | no running | `Player_Update` |
  | `ITEM_SACRED_GLOVES`, `ITEM_LORD_MYUS_GLOVES` / `ITEM_BONE_HANDS` | the weapon's weight 3/4 / double | `Player_RecomputeStats` |
  | `ITEM_SILVERY_GLOVES`, `ITEM_POWER_BRACELET` | slash, strike, stab attack +10% (each) | `Player_RecomputeStats` |
  | `ITEM_VENOM_GLOVES` | the weapon's poison rate +70 | `Player_RecomputeStats` |
  | `ITEM_WIDDAS_RING` / `ITEM_ANGELIC_CROWN` / `ITEM_CLARITY_BRACELET` | most HP +20% / most MP +20% / most MP x5 (999 cap) | `Player_RecomputeStats` |
  | `ITEM_RING_OF_FIRE` ... `ITEM_RING_OF_WATER` | that element's attack +20 | `Player_RecomputeStats` |
  | `ITEM_FEATHER_BRACELET` | weight x3/4 | `Player_RecomputeStats` |
  | `ITEM_ICE_ARMOR` | the first school's spells (effects 1-15) cannot be cast | `Player_CastMagic`, `Player_HoldMagic` |
  | `ITEM_THIEVES_NECKLACE` | enemies notice and reach the player from half as far | `entity/entity_ai*.c` |
  | `ITEM_ABSORPTION_BOOTS` | no slowdown after a hard landing (fall damage is unchanged) | `Player_TakeFallDamage` |
  | `ITEM_DEMON_LEGS` | half speed, no running | `Player_AddItemBonuses`, `Player_Move`, `Player_Update` |
  | `ITEM_RING_OF_SIGHT` | lights the dark as the Light spell | `Player_AddItemBonuses` |
  | amulet 374 | breath under water lasts twice as long | `Player_UpdateBreath` |
  | `ITEM_SACRIFICIAL_MASK` | 1 in 451 a frame, a magic (effect 0x7C) of its own | `Player_Update` |
  | `ITEM_MYUS_NECKLACE` | some map objects glow | `MapObject_DrawAll` |

- The timed effects of spells and items are listed with the seconds left: the attack and defence
  bonuses per element (`physical_strength_bonus` ... with their timers), the ailment rate and defence
  bonuses, the Power Seed and Widda Needle (+10%), rage (`rage_timer`: Physical + 10/20/30, Magic 1),
  the light spell and the intimidation (enemies attack from half as far).
- **Checked:** a save with the weapon 192 (kinds 10 and 5: Magic +10, MP +3), the bracelet 362 (kind
  6: MP -5; paralysis defence), Demon Armor and the Widda's Ring: the panel showed Magic "30 (40 in
  effect)", "MP +1 every 1.7 s" (150 / 3 = 50 frames), "MP -1 every 1.0 s" (150 / 5 = 30 frames), most HP
  360 (300 + 20%), paralysis defence 100%, and the four items' effects.

### The hit log and the target's health

- **How:** the game never reports a hit, so `port_stats_track` watches, once a vblank, every entity on
  `active_entities_head`: `Attack_HitEntity` sets `Entity.hit_cooldown_frames` to 15 with
  `last_hit_damage` and takes the damage off `hit_points`, and no hit lands while the cooldown runs, so a
  cooldown that rose with the HP dropping is one hit, of `last_hit_damage`. That is the real damage,
  before the HP is clamped at 0 (a kill can show more than the HP that was left). The player's HP is
  watched the same way: a drop is a hit when `recent_damage_frames` was set with it
  (`Player_TakeDamage`), else HP lost otherwise (poison, the curse, the ground, a fall: "not a hit").
- **Checked:** the club's hits on a 105-HP creature: 85, 4, 6, 6, 6, killed (`hit_points` 20, 16, 10, 4,
  0); a cast: 130, killed; the creatures' hits on the player: 10 each with a shield (7 with Demon
  Armor); poison: 3 each 30 frames.

## What is not shown

- The game's own Status screen already shows the nine attack and defence values, most HP and MP,
  Physical and Magic (base and in effect), gold, experience, level and weight; the equipment screen
  compares equipment. The panel does not repeat them.
- A retail bug the panel does not paper over: against a hit, the slash part is measured against the
  strike defence (`defence_stats[1]`); the slash defence is never used (`Attack_HitPlayer`).
