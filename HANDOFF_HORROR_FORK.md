# Hand-off: forking the Begotten engine into a single-player horror game

For the Claude instance that takes over the fork. Written at engine version **0.15.4** (simulation revision 13). Everything
below was checked against the source at that version; where something was not checked it says "verify".

The job: a copy of this engine that is **single-player only and has one playable character**. Multiplayer (network and
split-screen) and the multi-class system's *plurality* come out. Everything else (lighting, scripting, personalised damage,
monsters, voxel props, weather, palette, HUD, save system) stays and is the foundation of the horror game.

Read sections 1-3 first (orientation), then 6-7 (the work). Sections 4-5 are the inventory you work from.

---

## 1. What this engine is

* **Lineage.** A fork of **dsda-doom** (itself a successor of PrBoom+), GPL-2.0-or-later. Source in
  `prboom2/src`; Begotten's own code is mostly in `prboom2/src/dsda/bg*.c` and `dsda/begotten.c`, plus small hooks in the stock files
  (`p_user.c`, `p_inter.c`, `p_pspr.c`, `g_game.c`, `m_menu.c`, `r_things.c`, ...). It is a **standalone game**, not a mod: it ships as
  `begotten` (binary) + `begotten-data.wad` + `begotten.wad` (the game's content). Doom's own assets are not used; the placeholder art is original.
* **Platforms.** The user develops and tests on Linux (AppImage) and Windows (built by MSVC in GitHub Actions from the source zip; see
  `.github/workflows`). Anything you write must compile with **MSVC (C99-ish, no VLAs, declarations at the top of blocks, `strings.h` guarded)**; this has broken the
  Windows build before. GL rendering was removed (0.9.9): software renderer only.
* **Documentation to read.** `BEGOTTEN_ENGINE.md` (the full change log, oldest first, one section per version: the best record of *why* things are the way they are; search it before changing a
  subsystem), `LIGHTING.md`, `docs/CONTENT_TREE.md` (how the content wad is built from a folder tree), `docs/LICENSING.md`, `docs/RELEASE_CHECKLIST.md`,
  `docs/udmf.md`, `docs/things_and_specials.md`, `docs/Begotten_ACS_Manual.html`, `udb/README.txt` (Ultimate Doom Builder config).
* **The user's workflow** (keep it): they build maps in Ultimate Doom Builder (UDMF format, config in `udb/`), the engine is built in CI and by you in `build2`-style out-of-tree
  builds; they test and report back. Working style they asked for: work unattended, ask only about real art/design decisions, and finish with a report that lists what is **untested** (Windows
  CI build of new code, real hardware appearance/feel, gamepad on real hardware, real-network play, GL mode unsupported).

### 1.1 Repo map

| Path | What |
|---|---|
| `prboom2/src/*.c` | the stock engine (play simulation `p_*`, renderer `r_*`, game `g_*`, menu `m_menu.c`, HUD `st_stuff.c`/`hu_*`) with Begotten hooks |
| `prboom2/src/dsda/` | dsda-doom's own modules (`console.c`, `configuration.c`, `excmd.c`, `damage_kind.c`, ...) **and** Begotten's `bg*.c` |
| `prboom2/CMakeLists.txt`, `prboom2/src/CMakeLists.txt` | version (`project("Begotten" VERSION x.y.z)`) and the source list (new `.c` files must be added here) |
| `game/` | content: `begotten.wad` (built), `vehicles/`, `voxel_props/`, face art, licence texts |
| `tools/` | Python content generators and map generators (`make_*`, `art_*`, `pack_wad.py`, `wad_put_vehicles.py`, ...) |
| `udb/` | Ultimate Doom Builder game config (`Begotten_Doom2Hexen.cfg`, `Includes/Begotten_*.cfg`: things, linedefs, lights, monsters) |
| `docs/`, `BEGOTTEN_ENGINE.md`, `LIGHTING.md` | documentation |
| `spec/`, `patch_notes/` | dsda-doom leftovers (ruby specs, upstream notes); ignore |

### 1.2 Build and test (what works in the cloud container)

```
cd /home/claude/work/build2 && ninja          # incremental build (cmake . after adding files)
```
Run the game headless-ish with Xvfb: `Xvfb :N -screen 0 1280x720x24 &` then
`DISPLAY=:N SDL_AUDIODRIVER=dummy ./begotten -iwad <iwad.wad> -file <map.wad> -class 2 -window -nomusic -nomonsters -skill 4 -warp 1`. Screenshots: `import -window root x.png`.

**Self tests** (environment variable `BEGOTTEN_SELFTEST`): `3` runs the data tests (classes, poses, weapons, pickups, monsters, ending, split-screen, network, progress, voxel);
`8` save/load round trip (add `BEGOTTEN_LOCALPLAYERS=n` for split-screen); `9` co-op rules; `4` live monster/lighting test (use `-warp 2`); `7` dark room with every light kind; `S`/`BEGOTTEN_NETTEST=host:n|join:addr` network sync test.
When you remove a subsystem you must remove its tests, otherwise the runs report failures; see 7.

**Developer aids** (env): `BEGOTTEN_TELEPORT="x y angle"`, `BEGOTTEN_VEHTEST="20:use 30:fwd=50 ... jump=0"` (scripted player 1 input; `jump=` with no value means 1),
`BEGOTTEN_VEHDEBUG=1`, `BEGOTTEN_DUMP=file`, `BEGOTTEN_ACSLOG=1`, `BEGOTTEN_DAMAGELOG=1`, `BEGOTTEN_GOD=1`, `BEGOTTEN_AUTOWALK=tic`, `BGVOX_STATS=1`, `BGLIGHT_*`.

### 1.3 Determinism rules (do not break them while cutting)

The play simulation is **deterministic**: demos, saves, rewind/key frames and (today) the network lockstep all depend on every machine computing the same thing. So: integer / fixed-point math only in game logic
(floats are fine in the *renderer* and the light code), no use of `rand()` (use `P_Random(pr_class)`), nothing in the simulation may depend on the display or the local machine, and the order of RNG calls matters.
After you remove multiplayer the lockstep requirement disappears, **but demos and saves still need determinism** (and so do the self tests). Keep the rule.

---

## 2. How the engine is put together (the parts you will touch)

1. **Tic loop.** `D_DoomLoop` -> `TryRunTics` (`d_client.c`) -> `G_Ticker` (`g_game.c`) -> `P_Ticker` (`p_tick.c`) -> `P_PlayerThink` (`p_user.c`) per player and `P_RunThinkers` (all mobjs, `p_mobj.c`).
   Begotten adds hooks: `bgveh_PostTick` after the thinkers, checkpoint saves between tics, weather/light ticks.
2. **Player input.** `G_BuildTiccmd` (`g_game.c`) builds a `ticcmd_t`; Begotten extends it with the **ex-cmd** (`dsda/excmd.c`, fields `actions`, `actions2`, `track_l/r`, ...: jump, alt-fire, look, tank tracks).
   The ex-cmd is also what the network/demos carry (`G_WriteDemoTiccmd` buffer size, `bgnet.c` PutCmd/GetCmd).
3. **Players.** `player_t` (`d_player.h`) has Begotten fields: `bgclass`, pose clocks (`bgatktic`, `bgparrytic`, `bgruntic`, ...), parry (`bgparry`, `bgdeflect`), blade (`bgcharge`, `bgstabdmg`, `bgstabspd`), `bgaltprev/cur`, ...
   `mobj_t` has `bg_cd`, `bg_aux`, `bg_stun`, `bg_dmgscale`, `bg_rx/ry/rz`, `bg_guide*`. Saves archive the whole struct, so adding or removing a field changes the save format (fine: nothing is shipped yet, but old saves stop loading).
4. **Data lumps** (text, in `begotten.wad`; read at start-up, later wads override earlier ones): `BGCLASS` (player classes), `BGWEAPON` (slots/ammo), `BGMOBJ` + `BGMONSTER` (monster roster numbers, wind-up sounds), `DMGKINDS` (per monster damage vulnerabilities),
   `BGLIGHT` (lights, weather, sun, fog), `BGVOXEL`/`BGVOXELV` (voxel props, vehicles), `BGPAL` (palette per map), `BGSCALE` (art pixels per unit), `BGMUSIC`, `BGPROGRESS`, `UMAPINFO` (maps, endings), `DEHACKED`, `ANIMATED`, ...
   `docs/CONTENT_TREE.md` describes `tools/pack_wad.py`. **Never run `pack_wad.py` on a wad that already contains UDMF maps** (it drops them); edit such wads with `tools/wad_put_vehicles.py`-style lump insertion.
5. **Maps.** UDMF (zdoom namespace) and Hexen-format maps are supported; ZDBSP builds nodes (`/tmp/zdbsp/b/zdbsp` in the cloud container; rebuild from source if missing). MAP01 is both the first level ("Gallery of Lights") and the script lab; it has a vehicle hall and a rail course.
6. **Lighting** (`dsda/bglight.c`, 5,000 lines, `LIGHTING.md`): software-renderer lights that brighten Doom's colormap choice; placeable light things (20101-20108 stationary, baked with line-of-sight shadows at level start; 20201-20208 movable), projectile/explosion/muzzle lights, flashlight cone, fog, halos, sun, weather (rain/snow/lightning), dithering, shadowed things.
   **This is the heart of a horror game's look; leave it alone while you cut.** The only multiplayer-aware bits are loops over `players[]`/`playeringame[]` for muzzle flashes and flashlights (harmless with one player).
7. **Scripting.** Hexen-style **ACS** (compiled with `acc`; numbering rules in `BEGOTTEN_ENGINE.md` 0.10.48: scripts started by line specials are numbered 0-255), ZDoom-style line specials in UDMF, `tools/make_script_lab.py` (191 demo rooms), `tools/check_script_lab.py`, plus dsda's in-game console (`dsda/console.c`).
   Begotten's own line specials: **174** checkpoint (single player: auto-saves a "checkpoint" save; co-op: tickets), **212/213** music change/stop (binary maps), **300** grind rail (UDMF).
8. **Personalised damage** (`dsda/damage_kind.c`): every weapon/projectile has a damage *kind* (blade, bullet, shell, belt, fire, tempest, spirit, hellfire) in a *category* (melee, firearm, magic, special); `DMGKINDS` sets what each monster takes per kind;
   `bg_ClassDamagePct` lets a class modify it; `bg_StunDamage`, parry and riposte rules sit on top (`dsda/begotten.c`).
9. **Monsters.** `dsda/bgroster.c` builds a **new roster that REPLACES Doom's monsters** at start-up from copies of Doom's mobj types and state chains (so DEHACKED edits carry over). Tunables are named values overridable by `BGMOBJ`. `bgmonster.c` is the wind-up-sound lump.
10. **Voxels / vehicles / rails** (`bgvoxel.c`, `bgvehicle.c`, `bgrail.c`): voxel props drawn by a splat renderer with LODs; vehicles (motorcycle, mecha, tank, turret) are voxel props with a `vehicle` keyword in `BGVOXELV`; rails are line special 300.
    These are action-game features. Whether they stay in a horror game is a **design decision for the user**; they are self-contained (see 4.3) and can be left in, disabled, or deleted at the end.

---

## 3. What the user wants (and does not)

* **Keep:** lighting, scripting (ACS, line specials, UDMF), personalised damage, monster roster, voxel props, weather/fog, palette system, checkpoints/saves, HUD, content tooling, the console, demos.
* **Remove:** all multiplayer (network lockstep, split-screen, co-op rules, lobby/menus, player names, friendly fire as a *multiplayer* concept) and the **multiple playable characters** (class choice, unlocks, per-class content for other characters).
* **Do not** invent a new design. The protagonist's stats and weapons are the user's call; until they say otherwise, keep one existing class as a placeholder protagonist (see 6) and say so in your report.
* Ask the user only for true art/design decisions. List these early (see section 9) and carry on with sensible defaults in the meantime.

---

## 4. Inventory: what is where

### 4.1 Multiplayer

| Component | Files | Notes |
|---|---|---|
| **Network lockstep over UDP** (host/join, lobby, late join with snapshot, desync detection, relay) | `dsda/bgnet.c` (3,632 lines), `dsda/bgnet.h` | `BGNET_SIM_REV` (13) and `PKT_VERSION` guard same-build play; the content hash is computed here too. Also has network self tests (`dsda_BgNetSelfTest`, `BEGOTTEN_NETTEST`). |
| Net hooks in the stock loop | `d_client.c` (`bgnet_Poll`, `bgnet_Lockstep`, `bgnet_ReadyTics`, `bgnet_PreTic/AfterTic/LateTick`, `bgnet_TestCmd`), `g_game.c` (4), `d_main.c` (banner/status drawing), `e6y.c` (3), `wi_stuff.c` (player names on the intermission), `m_random.c` (`bgnet_tracing` RNG trace), `r_things.c` (`bgnet_NoteWeaponDraw`), `dsda/skill_info.c`, `dsda/begotten.c` (3) | grep `bgnet_` |
| **Split-screen** (2-4 local players, per-player controllers, view rectangles) | `dsda/bgsplit.c` (698), `dsda/bgsplit.h` | hooks in `d_main.c`, `d_client.c`, `g_game.c`, `st_stuff.c`, `hu_stuff.c`, `m_menu.c`, `dsda/settings.c`, `dsda/pause.c`, `dsda/begotten.c`; the renderer draws `bg_ViewCount()` views |
| **Co-op rules** (tickets, rucksacks, respawn costs, boss tid 30000-30999, "everyone dead = level restarts") | `dsda/bgcoop.c` (434), `dsda/bgcoop.h` | hooks: `g_game.c` (8), `p_inter.c` (7), `p_map.c` (3), `dsda/save.c` (2), `dsda/death.c`, `st_stuff.c` (ticket HUD), `dsda/begotten.c` (24: `bg_Checkpoint` forks on `netgame`), `r_things.c`, `gl_main.c` |
| **Menus** | `m_menu.c`: the multiplayer menu tree (`mp_*`, host/join/lobby/level select with per-player "completed" ticks, controller assignment), `bgnet_*`/`bg_Split*` calls (about 60 lines) | `M_NetMessage`, `mp_session`, `mp_mode`, `mp_players`, `MainMenu[0] = bgnet_InGame() ? end_game : new_game` |
| **Config** | `dsda/configuration.h/.c`: `begotten_friendly_fire`, `begotten_local_players`, `begotten_class_p2/p3/p4`, `begotten_pad_player`, `begotten_net_class/port`, `begotten_join_address`, `begotten_player_name(_p2/p3/p4)`, `begotten_net_delay`, `begotten_split_layout` | plus `m_misc.c` entries if present; verify with grep |
| **Per-player controller routing** | `dsda/game_controller.c/.h`, `bgsplit.c` (`bg_SplitBuildCmd`, `bg_SplitPadPlayer`) | one controller per extra player; keep the *first* controller path |
| **Net-only pieces of the ex-cmd** | `dsda/bgnet.c` PutCmd/GetCmd (+3 bytes for tank tracks/style), `d_ticcmd.h` | demos use `G_WriteDemoTiccmd`; keep the ex-cmd, drop only the packet code |
| **Tests / tools** | `dsda_BgNetSelfTest`, `dsda_BgSplitSelfTest`, co-op tests (`BEGOTTEN_SELFTEST=9`), `BEGOTTEN_SELFTEST=S`, `bgnet` RNG trace | delete with their subsystems |
| **Docs** | `BEGOTTEN_ENGINE.md` sections 0.8.6-0.8.10, 0.10.14, 0.10.18-19, co-op sections | keep as history; add a "removed in the fork" section |

Things that look like multiplayer but are **stock Doom** and fine to keep: `playeringame[]`, `players[MAX_MAXPLAYERS]`, `consoleplayer`/`displayplayer`, deathmatch/coop *spawn flags* in maps, `netgame`/`deathmatch` globals (they stay false).
You do not need to shrink the arrays. Shrinking `MAX_MAXPLAYERS` is optional, risky (saves, demos) and gains nothing.

### 4.2 Multiple characters (classes)

| Component | Files | Notes |
|---|---|---|
| **Class definitions and rules** | `dsda/bgclass.c` (1,574), `bgclass.h` | built-in default text for `soldier`, `witch`, `swordsman` (lines ~99-175) plus every `BGCLASS` lump; per-class health/armor/speed/jump/air control/damage by kind/spread/fire rate/swap speed/health caps/sounds/poses/art/face/weapons; `bg_player_class[]`, `bg_ClassReborn`, `bg_ClassAvailable` |
| **Unlocks and progress** | `dsda/bgprogress.c` (437), `bgprogress.h` | "unlock campaign": a class opens when the campaign is finished (`dev_unlock` config overrides). Also records completed maps (used by the multiplayer level select). |
| **Class-flagged map things** | `p_mobj.c` (`bg_ClassAllowsThing`, `bg_ClassAllowsPlayerStart`), `dsda/bgclass.c` (`bg_StashStart`, `bg_FlushStarts`), UDMF `class1/2/3` flags | things with none of the three flags are for everybody; in a single-protagonist game these filters become a no-op or must be removed |
| **Class art/sprites** | `r_things.c` (`bg_ClassSprite` for body and weapon), `dsda/bglight.c` (2 call sites), `st_stuff.c` (class face prefix, face background), `dsda/bgface.c` (faces from large pictures) | `game/face_witch`, class sprite lumps in the wad |
| **Class sounds** | `bg_ClassSfx` calls in `p_mobj.c`/`p_inter.c`/`p_user.c` | verify with grep |
| **Class mechanics baked into the simulation** | `p_inter.c` (melee-only pickup rules, health caps), `p_pspr.c` (spread, tics scaling, swap speed, blade damage), `p_user.c` (movement scaling, air control, jump), `dsda/begotten.c` (swordsman blade: tap/charge/stab/lunge/riposte, parry and deflect, stun) | the **swordsman is the most entangled**: blade state chain, `bgcharge/bgstabdmg/bgstabspd`, parry window, deflect, rails (`bgrail.c`), `bgveh_Use` refusal |
| **Menus** | `m_menu.c` (about 15 `bg_Class*` uses: class select in New Game, lobby class pick, unlock text), `mp_LocalClass`, `-class` argument (`dsda/args.c`) | |
| **Intermission/HUD** | `st_stuff.c`, `wi_stuff.c` | face set per class |
| **Weapons** | `dsda/begotten.c` (16 weapon modes over 7 slots, 7 ammo types, primary/secondary fire), `BGWEAPON` lump | weapons are shared by classes through `weapons ...` lines in BGCLASS; **they stay** (the user decides which the protagonist keeps) |
| **Tests** | `dsda_BgPoseSelfTest`, class tests in `bgclass.c` (~line 1070-1200) | adjust, do not just delete: the pose and jump tests are useful for the one remaining class |

### 4.3 Self-contained systems whose fate is a design decision (not part of this job)

* Vehicles: `bgvehicle.c` + voxel models + `BGVOXELV` + Vehicle Hall in MAP01 (`tools/make_vehicle_hall.py`). Swordsman-only rails: `bgrail.c`, special 300, `tools/make_rail_demo.py`.
* The boss marker (`bg_IsBoss`, thing tid 30000-30999) lives in `bgcoop.c` and, at 0.15.4, is used only there (co-op boss rewards). If the horror game wants "boss" things (music, a health bar), move it out before deleting the file.

---

## 5. Recommended design for "one character"

Two options. **Option A is recommended.**

**A. Keep the BGCLASS machinery, but fix the game to exactly one class.** The class system is the way the engine expresses *the protagonist*: health, speed, jump, damage multipliers, weapons, sounds, poses, face. Deleting it means hard-coding all of that back into stock
Doom player code. So: keep `bgclass.c` as the **protagonist definition**; remove the *choice*: delete the class menus, `-class`, the unlock system and `bgprogress.c`'s unlock part, make `bg_player_class[0]` a constant, drop the other classes' text from the built-in default and from the wad
(`BGCLASS`), and drop class-flagged thing filtering. Pick which of the three existing classes becomes the placeholder (the **soldier** is the most conventional: firearms; the user will draw a new character later, and a class is just a text block + art lumps, so replacing it is cheap).
Remove the other classes' *mechanics* only where they are cheap to remove; the swordsman's blade/parry/rail code can stay dormant (class data does not enable it) and be deleted in a later pass, or now if the user says the protagonist is never a swordsman.

**B. Remove the class system completely.** Hard-code the protagonist into `p_user.c`/`p_pspr.c`/`p_inter.c`, delete `bgclass.c` (1,574 lines), `bgprogress.c`, and every `bg_Class*` call (about 290 references in 24 files, 90 of them inside `bgclass.c` itself; the `bg_weapons ? bg_Class...(...) : stock` pattern means the stock value is already the fallback, so most call sites simply collapse to the stock expression).
More code removed, more risk, and the protagonist's tuning (jump height, speeds, sounds, poses, weapon hands) has to live somewhere new. Only do this if the user asks.

Ask the user (once, early) which class is the placeholder and whether the protagonist keeps jumping, the parry/blade, and the alt-fire; until answered use Option A with the soldier and say so.

---

## 6. The plan (stages with gates)

Work in stages; **build and run the tests after every stage**; do not start the next stage on a red gate. Keep each stage in its own commit if the fork has git (this working copy is not a git repository: make one, `git init`, as step 0).

**Stage 0: baseline.** Copy the tree, `git init`, build with the existing `build2` recipe, run `BEGOTTEN_SELFTEST=3` (all PASSED except the split-screen test *only if* `BEGOTTEN_LOCALPLAYERS` is set in your environment: unset it), `8`, and one scripted level run. Record the output as the baseline. Rename the project/version in
`prboom2/CMakeLists.txt` and the window title/AppImage name if the user gave a name (ask for the title; default: keep "Begotten" until told).

**Stage 1: single-player lock (no code deleted yet).** Make multiplayer unreachable without removing code, so a regression shows up as "it still works":
1. Hide the multiplayer menu entries (`m_menu.c`: the host/join/split-screen items and `mp_*` flow) and any multiplayer command-line options (none named `-net`/`-host`/`-join` exist at 0.15.4: hosting and joining are menu-driven, the 2-4 local players come from the `begotten_local_players` config and the `BEGOTTEN_LOCALPLAYERS` environment variable); force `bg_split_request = 1`, `begotten_local_players = 1`.
2. Make `bgnet_State()` always `BGNET_IDLE`, `bgnet_InGame()` and `bgnet_Lockstep()` false (temporary stubs).
3. Gate: new game, play a level, die, load a save, take a checkpoint (line 174 saves), finish the level, intermission, next level; all as before.

**Stage 2: delete multiplayer.**
1. Delete `bgnet.c/.h`, then fix every caller (`grep -rn "bgnet_\|BGNET_" prboom2/src`): `d_client.c` (restore the stock single-player `TryRunTics` path: dsda-doom's own version is the model; look at what the stub branches do and keep that), `g_game.c`, `d_main.c`, `e6y.c`, `wi_stuff.c`, `m_random.c`
   (remove `bgnet_tracing`), `r_things.c` (`bgnet_NoteWeaponDraw`), `dsda/skill_info.c`, `dsda/begotten.c`, `m_menu.c`. Remove the net content hash, the sim revision, the net config keys, and the 3 ex-cmd network bytes **only in the packet code** (keep the ex-cmd and the demo writer).
2. Delete `bgsplit.c/.h` (and `bg_ViewCount`-based multi-view drawing: the renderer must always draw one view; check `d_main.c`, `r_main.c`, `st_stuff.c`, `hu_stuff.c`, `dsda/pause.c`, `dsda/settings.c`). Keep `game_controller.c` for the one controller.
3. Delete `bgcoop.c/.h`: ticket HUD in `st_stuff.c`, rucksack and respawn code in `p_inter.c`/`g_game.c`/`p_map.c`/`dsda/death.c`, co-op save fields in `dsda/save.c`. In `bg_Checkpoint` keep only the single-player branch. Single-player death must behave as stock does (reload last save / restart level) unless the user asks for a different horror rule
   (that is a design decision; propose, do not invent).
4. Remove the multiplayer tests and `BEGOTTEN_SELFTEST` branches `9`, `S`, `P` and the net/split parts of `3`/`8`; remove `BEGOTTEN_NETTEST`, `BEGOTTEN_LOCALPLAYERS`, `BEGOTTEN_VEHTEST2`, `BEGOTTEN_TELEPORT2`, `BEGOTTEN_CLASS2`.
5. Remove the CMake entries (`prboom2/src/CMakeLists.txt`) and the Windows socket linking (`target_link_libraries(... ws2_32)`, around line 585) if only bgnet used it (verify: grep for `socket`, `WSA`, `SDL_net` first).
6. Gate: build clean (also check there are no warnings from unused statics), selftests 3 and 8 pass, the stage-1 play-through passes, **and the recorded demo plays back identically** (record a demo with `-record`, play it with `-playdemo`, compare the final state / use `BEGOTTEN_DUMP`).

**Stage 3: reduce to one class (Option A).**
1. Decide the placeholder class with the user's answer (default: soldier). Edit the built-in default text in `bgclass.c` and the `BGCLASS` lump source (`game/` / `tools/data/` or `tools/make_placeholder_content.py`; verify where the lump text lives) so only that class exists, at index 0.
2. Remove the class menus in `m_menu.c`, `-class` in `dsda/args.c`, the config keys (`begotten_class_p2..p4`, `begotten_net_class`, unlock keys), `bg_player_class[]` (make it a constant 0 or delete and use 0), and the unlock logic in `bgprogress.c` (**keep** the completed-maps record if the user wants a level-select or an ending flag; otherwise delete the file and its `d_main.c`/`g_game.c` hooks).
3. Remove the class-flag filtering of map things (`bg_ClassAllowsThing`, `bg_ClassAllowsPlayerStart`, `bg_StashStart/FlushStarts`), so every thing and every player start spawns; UDMF files with `class1/2/3` flags still load (the flags are simply ignored: **verify** the map loader does not reject them).
4. Delete the other characters' content: their `weapons`, sprites, faces (`game/face_witch`), hands art, class sounds, `bgface` entries, pose placeholders (`tools/make_pose_placeholders.py`) as far as they are unused; keep the *mechanism* (poses, faces, hands) because the new character will use it.
5. Optional now, otherwise later: delete the swordsman-only mechanics: blade states, parry/deflect/riposte (`dsda/begotten.c`), `bgrail.c` and its hooks in `p_user.c`/`bgclass.c`/`bglight.c`, the stun system if only the blade used it. Each of these is entangled with weapons the protagonist might keep, so ask before deleting any.
6. Gate: selftest 3 (pose, jump, weapon, pickup tests pass for the one class), a play-through, a saved game round trip (selftest 8), demo round trip.

**Stage 4: clean-up and Windows.**
1. Remove dead strings from the menus, the help screens and `BEGOTTEN_ENGINE.md`'s user-facing parts; add a section "Fork: single-player horror" to the log describing what was removed.
2. Run a compiler with `-Wall -Wunused` and clear everything your changes introduced. Re-read every file you edited for MSVC problems (mixed declarations, `strings.h`, C99 features the existing code avoids).
3. **You cannot build for Windows or run real-hardware tests.** The user does the MSVC build from the source zip and tests. Say so in your report.
4. Package: `bash pack.sh build2 <out.AppImage>` and `zip -qr ... -x '*/build*' -x '*/.git/*' -x '*/__pycache__/*'` as in the previous releases (the scripts live in `/home/claude/work`; copy them into the fork).

---

## 7. Pitfalls (each one has already bitten this project once)

* **Do not use `tools/pack_wad.py` on a wad that contains UDMF maps** (it silently drops them). Insert lumps directly (`tools/wad_put_vehicles.py`).
* **Windows/MSVC:** no declarations after statements inside the fork's new code in a way MSVC's C mode rejects, guard `strings.h`, no VLAs, no GNU statement expressions, `ssize_t` is not available; unprototyped functions are warnings on gcc and errors elsewhere. Add every new `.c` to `prboom2/src/CMakeLists.txt`.
* **`player_t` / `mobj_t` layout** is part of the save format and of `p_saveg.c`; if you delete a `bg*` field, delete it from the archive code too (grep the field name) and from `bgnet.c`'s state hash (which disappears with it).
* **Index-based references in maps:** when you edit UDMF by text append, existing sector/line/thing indices must not move (specials and ACS refer to them by id and index).
* **`bg_weapons` flag:** most Begotten behaviour is behind `bg_weapons` (set when the Begotten weapon/class rules are on). The fork should leave it permanently on (or delete the flag and keep only the `true` branches: a cleanup for later, not now).
* **The ex-cmd and demos.** `G_WriteDemoTiccmd`/`G_ReadDemoTiccmd` carry the ex-cmd; changing its layout breaks demo playback of anything recorded earlier (acceptable) but a mismatch between writer and reader breaks all demos (not acceptable: test it).
* **Scripted-input test aid cannot be recorded into demos** (`bgveh_TestCmd` runs after the command is recorded); use real `-record` runs with the keyboard replaced by a recorded demo from the user, or the selftests.
* **Sprite/frame guards.** `R_ProjectSprite` skips non-existent frames (the Windows crash `createPatch: Unknown patch format S_START` came from reading past a flat picture); keep these guards when removing class sprite hooks.
* **`bg_ClassSprite` returns the original sprite when a class has no replacement:** with one class this is the normal path; check body and weapon sprites still draw after Stage 3.
* **Time budget.** The two largest files (`bgnet.c` 3.6k lines, `begotten.c` 6.2k lines) are where the time goes. Read `begotten.c` by section; it mixes weapons, damage, parry/blade, checkpoints, tests and developer teleports in one file.

---

## 8. Verification checklist (copy into your final report)

1. Builds clean in the container (`ninja`), no new warnings.
2. `BEGOTTEN_SELFTEST=3`, `8` pass; the tests you removed are listed.
3. A scripted/real play-through: new game -> play -> die -> reload -> checkpoint -> finish -> intermission -> next map.
4. Save/load round trip, quick save and checkpoint save.
5. Demo record + playback is identical.
6. The menus: no leftover multiplayer or class entries; no crashes opening every menu page (use the keyboard in Xvfb).
7. Lighting/weather/script-lab maps load and look as before (compare screenshots of MAP01 hall and the dark-room test `BEGOTTEN_SELFTEST=7`).
8. **Not verified (state it):** Windows MSVC build, real-hardware feel and appearance, gamepad on real hardware.

---

## 9. Questions for the user (ask early, in one message; proceed with the defaults meanwhile)

1. Which existing class is the placeholder protagonist until the new character exists? (Default: soldier.)
2. Does the protagonist keep: jumping, the alt-fire/secondary modes, the parry/blade mechanics? (Default: jumping yes; everything else stays dormant and is deleted only on request.)
3. Is there a title/name for the horror game (window title, AppImage and binary name, `PROJECT_VERSION`)? (Default: keep "Begotten".)
4. Death rule in single player: stock Doom (restart/reload) or checkpoint reload? (Default: checkpoint saves already exist for single player; keep stock death handling.)
5. Do vehicles and rails stay? (Default: leave in, untouched.)
6. Keep the campaign-progress record (`bgprogress.c`) for something else (level select, ending flag)? (Default: delete the unlock part, keep the record.)

---

## 10. Quick reference: greps that find everything

```
grep -rn "bgnet_\|BGNET_\|bg_net"                 prboom2/src          # network
grep -rn "bg_Split\|bg_split\|bg_View"            prboom2/src          # split-screen
grep -rn "bg_Coop\|bg_Ticket\|bg_Rucksack\|bg_IsBoss" prboom2/src      # co-op rules
grep -rn "bg_Class\|bg_player_class\|->bgclass"   prboom2/src          # classes
grep -rn "bgprogress\|bg_Campaign\|bg_RecordMap\|dev_unlock" prboom2/src
grep -rn "netgame\|deathmatch\|consoleplayer"     prboom2/src/dsda/bg*.c prboom2/src/dsda/begotten.c
grep -rn "begotten_local_players\|begotten_class_p\|begotten_net\|begotten_join\|begotten_player_name\|begotten_split\|begotten_pad_player\|begotten_friendly" prboom2/src
grep -rn "BEGOTTEN_NETTEST\|BEGOTTEN_LOCALPLAYERS\|BEGOTTEN_VEHTEST2\|BEGOTTEN_TELEPORT2\|BEGOTTEN_CLASS2" prboom2/src
```
Sizes for planning (lines): `begotten.c` 6,170 · `bglight.c` 5,066 · `bgnet.c` 3,632 · `bgroster.c` 1,587 · `bgclass.c` 1,574 · `bgvoxel.c` 1,570 · `bgvehicle.c` 1,247 · `bgsplit.c` 698 · `bgprogress.c` 437 · `bgcoop.c` 434 · `bgrail.c` 431.
