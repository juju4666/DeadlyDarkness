
## 0.8.5d — Windows (MSVC) build fixes
- `strings.h` does not exist on MSVC: guarded the include in begotten.c, bgclass.c, bgprogress.c, damage_kind.c (doomtype.h maps strcasecmp/strncasecmp to _stricmp/_strnicmp).
- bgnet.c: includes p_maputl.h (P_AproxDistance), silences Winsock deprecation warnings; renamed internal `trace` ring to `bgtrace` (clashed).
- If the Windows build still fails, send the output of the "Show Windows build errors" step.

## 0.8.5e — Windows build log
The Windows job now saves the whole build output and, on failure, the "Show Windows build errors" step prints only the lines containing errors. Open that step and paste it (a few lines).

## 0.8.6
- **Same build required.** Joining a network game now also compares a simulation revision (`BGNET_SIM_REV` in bgnet.c, currently 6). Raise it whenever a change alters movement/damage/weapons. Machines on different revisions are refused at joining, instead of drifting apart minutes into the game. The log shows `build 0.8.6, simulation revision 6, content hash ...` at the start of every game; compare these lines on all machines. (A test confirmed: one machine with the 0.8.5 wall-running fix and one without desync within seconds.) The game version is now 0.8.6 (was a leftover 0.2.0).
- **A player who falls out of sync is removed** (host, 3+ players, when the other players still agree with the host). The rest carry on. With two players there is no way to tell who is wrong.
- Page Up / Page Down (next/previous level), the restart key, New Game, Load Game and the level table are blocked in network games.
- Windows: fixes for MSVC (strings.h, strndup, log file buffering), line endings no longer converted on the Windows build (.gitattributes + workflow), CI prints only the error lines.
- Test mode `BEGOTTEN_NETTEST_DUEL=1`: the players stand and shoot at each other.

## 0.8.7 — multiplayer workflow
- **Split-screen players in network games.** A machine owns one or more consecutive player slots (`peer_t.nlocal`, `my_nlocal`, `OwnerOf()`/`IsMine()` in bgnet.c). The HELLO packet carries the number of local players and their classes; the host hands out consecutive slots and relays everybody's commands; a machine that leaves, times out or desyncs takes all of its slots with it. `PKT_VERSION` is now 4: builds older than 0.8.7 cannot join (they just get no answer).
- **Menu.** MULTIPLAYER: START LOCAL GAME / HOST NETWORK GAME / JOIN / LOCAL PLAYERS (1-4) / FRIENDLY FIRE / PLAYER SETUP / NETWORK SETUP. Add the split-screen players first, then pick local or network. JOIN with no saved address asks for it, then joins. The first player's class for all multiplayer games is set in PLAYER SETUP (and in the lobby); the class menu is skipped for them.
- **PLAYER SETUP:** class of each local player (with the sprite of the highlighted class drawn at the right), which player the first controller drives, and the layout for two players (`begotten_split_layout`: 0 horizontal split = one above the other, 1 vertical split = side by side).
- **NETWORK SETUP:** host address and player name. Both are saved the moment they are entered or left with Esc (the config file is written at once, not only on exit), and the address is used by JOIN.
- **Lobby:** every player's sprite (class art when the class has some, via `bg_ClassPortraitLump`) in the bottom right corner, `>` marks your own players, extra local players are named "NAME 2" ...
- **Windows:** the settings, log and saves go to `%APPDATA%\begotten` unless a `begotten.cfg` already sits next to the exe (then that folder keeps being used). A new download no longer starts with empty settings.
- Test mode: `BEGOTTEN_NETTEST_LOCAL=<n>` gives that instance n local players (use `host:<total slots>`).
- Class art: the player sprite of a class comes from its `sprite PLAY xxxx` line in BGCLASS; menus and lobby use the same mapping as the game.

## 0.8.8 — multiplayer menus follow the flow chart
- **Saved settings really are saved now.** None of the Begotten settings (join address, names, classes, local players, friendly fire, split layout...) were in the config file's table in m_misc.c, so they were never written. They are listed now (the "Begotten multiplayer" block of `cfg_defs`) and flushed at once when changed in the menus.
- **Menus** (m_menu.c, "MULTIPLAYER screens"): MULTIPLAYER = HOST GAME / JOIN GAME / LOCAL SPLITSCREEN. HOST GAME (server name, IP address, local players 1-3) -> CONTROLLER SETUP (only with 2+ local players) -> PLAYER SETUP -> GAME SETUP. JOIN GAME (address) -> PLAYER SETUP -> waiting room. LOCAL SPLITSCREEN -> CONTROLLER SETUP (2-4 players) -> PLAYER SETUP -> GAME SETUP.
- Friendly fire, the two-player split layout, difficulty and level (with a box per player showing who finished it) live only in GAME SETUP. The old NETWORK SETUP and lobby screens are gone.
- PLAYER SETUP: one row per local player, ENTER types the name, LEFT/RIGHT changes the class. Names of all local players travel to the host in HELLO. New settings: `begotten_player_name_p2..p4`, `begotten_server_name` (shown to joiners as the title of the waiting room; LOBBY packet).
- A join that fails (wrong address, full, other version) returns to the JOIN GAME screen with the reason. An open network session is closed again whenever the menus go back to MULTIPLAYER / HOST / JOIN or are closed (`M_MpWatch`).
- Split-screen HUD: sits fully inside each view (it was placed in screen pixels, so it was cut off at 1080p), on a dark strip, with the player's name and the ammo of the weapon in hand.
- `PKT_VERSION` 5.

## 0.8.9 — leaving, ending, names
- **Leaving is announced.** Quitting the program (menu, window close, Ctrl+C) now sends the leave message (`bgnet_Leave()` in `I_EssentialQuit`), so the host drops that player at once instead of waiting for the timeout; a quitting host sends everybody back to the title screen with "The host left the game". The timeout for a connection that just vanishes is 6 s (was 10 s).
- **Main menu in a network game:** NEW GAME is replaced by END GAME (leaves the game for everybody concerned, back to the title screen). MULTIPLAYER refuses to open another game; `G_DeferedInitNew` refuses to replace a running network game (this was the "new game with voodoo dolls" mess: the local-game start was reachable while the network game ran).
- **Banner:** blue text at the top of the screen while a lobby is open ("WAITING FOR PLAYERS", then "N PLAYERS CONNECTED"; joiners see "CONNECTING TO THE HOST..." / "CONNECTED - WAITING FOR THE HOST"), also drawn behind the menus (`bgnet_DrawBanner`).
- **Names** moved to HOST GAME (a NAME row per local player, appearing as LOCAL PLAYERS grows) and JOIN GAME. PLAYER SETUP only has the classes. The "(UP TO 3)" note is gone; hosting loops 1-3 local players.

## 0.8.10 - per-view palette flashes in split-screen
- `ST_PaletteIndex(const player_t*)` (st_stuff.c) picks the damage/pickup/radiation palette for any player; `ST_NormalPalette()` resets the global palette while split is active.
- `V_TintRect(x,y,w,h,pal)` (v_video.c) remaps the pixels of one view rectangle through PLAYPAL page `pal` (software mode only; GL has no split).
- d_main.c tints each split view after it is rendered, so every player sees only their own flashes.
- Test aid: `BEGOTTEN_TESTFLASH=1` forces a damage flash on the first local player.
- Stick lag: analog sticks are sampled once per game tic (35 Hz) in `I_StartTic`, same as the mouse; no code defect found. Needs symptom details.

## 0.8.11 - monster wind-up sound (BGMONSTER) and fullbright colormap tool
- New lump **BGMONSTER**: lines `<doomednum> <sound lump>` (e.g. `9 DSWNDSGT`; `9 -` removes an entry). Built-in defaults use Doom's own sounds as placeholders (shotgun cock for the humans, etc.); every BGMONSTER lump in the wad is read after them. A sound that is not a known sfx but exists as a lump is registered automatically.
- `bg_MonsterWindup()` (dsda/bgmonster.c) is called from `A_Chase` the moment a monster switches into its missile or melee state. It plays only if the monster's target is a player, and in a network game only on the machine that owns that player. Split-screen: the sound is positional relative to the displayed view.
- Test aids: `BEGOTTEN_WINDUPLOG=1` prints each wind-up; `BEGOTTEN_WAKE=1` alerts all monsters every second. Selftest group "monsters".
- `tools/make_colormap.py WAD --fullbright 200-207 -o COLORMAP.lmp` builds a COLORMAP where those palette indices map to themselves in all 32 light levels (always bright). `--check` lists the current ones (the wad's COLORMAP already has 0 and 235). Put the result in begotten.wad as COLORMAP; this is the palette item at the end of the engine list.

## 0.8.12 - per-class player sounds
- BGCLASS gets `sound <pain|death|xdeath|oof|noway> <lump>` (`-` restores the stock sound). `bg_ClassSfx()` (dsda/bgclass.c) is called from `S_StartSound`: when a stock player sound (plpain, pldeth, pdiehi, slop, oof, noway) is made by a mobj that is a player, the class' sound replaces it. Unknown lump names are registered as new sfx (`bg_SfxFromLump`, shared with BGMONSTER).
- Class sprites (`sprite PLAY ...`) and faces (`face XYZ`) already existed (0.5.x). The four co-op player colours are still Doom's player translation by slot, independent of class: a class sprite gets the colour swap only if its swappable parts use the stock green ramp (palette indices 112-127).
- Weapon firing sounds are not per class: they belong to BGWEAPON/the weapon states.

## 0.8.13 - episode ending, black menu background, licensing notes
- **Episode ending** (dsda/bgending.c). When an episode's finale starts and the wad has the lumps, the stock finale is replaced by: the picture at full brightness (4 s), dimmed to roughly 40% over 2 s, the text scrolling up over it, then back to the title (the network session is left too). Same for every episode.
  - `BGPIC<n>` picture for episode n, else `BGPIC`: a patch or a raw 320x200 screen. No picture lump: black background.
  - `BGTXT<n>` text for episode n, else `BGTXT`: one line per line, a line starting with `#` is a heading (gold), an empty line is a gap, body text is gray, uppercase hud font, centred.
  - If neither a picture nor a text lump exists the stock finale plays as before.
  - A button during the picture moves on to the text (after 1 s). Holding a button while the text scrolls speeds it up. After the last line, a button or 3 s returns to the title.
  - Dimming uses the COLORMAP rows, so it is software-renderer only (OpenGL shows the picture undimmed).
  - Test aid: `BEGOTTEN_ENDTEST=1` jumps to the ending 10 tics into a level. Selftest group "ending".
- **Menu background**: new setting `begotten_title_art` (default 0): the title/credits/demo pages are plain black and the `M_DOOM` logo is not drawn. Set it to 1 in begotten.cfg to bring the wad's pages and logo back.
- `docs/LICENSING.md`: GPL obligations, what can stay closed, Steam options, release checklist.

## 0.8.14 - ending music, AppImage library cleanup
- `BGMUS<n>` / `BGMUS`: music lump played (looping) during the episode ending; without one the stock intermission music keeps playing. Any music format the engine plays (MIDI, MUS, OGG ...).
- `pack.sh` removes libreadline (GPLv3), libtinfo and libdb from the AppImage. All three are present on ordinary distros / not needed; see docs/LICENSING.md.

## 0.8.15 - licence files in the packages
- `game/THIRD-PARTY-NOTICES.txt` and `game/AUTHORS-PrBoom-DSDA.txt` are installed with every package (every `game/*.txt` is). The notices file has a `[SOURCE URL]` placeholder to fill in before release. `docs/RELEASE_CHECKLIST.md` lists the licensing steps.
- Checked the 0.8.14 Windows zip: begotten.exe has the libraries statically linked (only system DLLs imported).

## 0.8.16 - class jumping
- BGCLASS: `jump <height>` = how high the class' jump rises in map units (0 = cannot jump); `sound jump <lump>` optional jump sound. Built-in: soldier 28, witch 37, swordsman 56. `bg_JumpSpeedFor()` (dsda/bgclass.c) turns the height into the upward speed with integer maths (same on every machine); measured in-game: exactly 28.00 / 37.00 / 56.00 units with stock gravity (sector gravity still scales it).
- Jumping no longer depends on the "Allow Jumping" setting (removed from the menu); `dsda_AllowJumping()` is true unless the map's UMAPINFO forbids it. Jump key: JUMP (Right Alt default) / controller B; split-screen pads: B. XC_JUMP already travels in the network ticcmd.
- A wad's own BGCLASS lump replaces the built-in numbers, so it needs the `jump` lines too (data/lumps/bgclass.lmp and BGCLASS.txt have them).
- Test aid: `BEGOTTEN_JUMPTEST=1` jumps at tic 20 and prints the height reached.

## 0.8.17 - player poses (jump / run / shoot / parry / per-weapon sprites)
- BGCLASS `pose <stand|run|shoot|jump|parry> <SPRITE|-> [frames [tics]] [weapon <name>]` and `edit <class>`; see data/lumps/bgclass.lmp. Resolver: `bg_PlayerPose()` (dsda/bgclass.c) called from R_ProjectSprite in r_things.c and gl_main.c; state is advanced per tic by `bg_PoseTick()` from P_PlayerThink (fields bgatktic/bgruntic/bgparrytic/bglaststate in player_t). Purely cosmetic and derived from simulated state, so every machine draws the same; PKT_VERSION unchanged.
- 8 unmirrored directions need no engine change: provide lumps A1..A8 (16 rotations also work).
- tools/make_pose_placeholders.py builds begotten_poses.wad (numbered placeholder sprites + a BGCLASS lump using `edit`). Self-test `BEGOTTEN_SELFTEST=3 ... -file begotten_poses.wad` (now run after P_Init because sprite tables exist only then).


## 0.8.18 - content tree tool, BGCLASS order
- tools/pack_wad.py + docs/CONTENT_TREE.md: folder tree <-> begotten.wad (unpack / pack / check). PNG pictures with grAb offsets, WAV sounds, one .wad per map, textures.txt for TEXTURE1/PNAMES, sprite rotation check. Verified by unpack+pack round trip of the whole wad (textures identical, pictures pixel-identical, sounds sample-identical) and by running the engine self-tests / a 2-player net test on the rebuilt wad.
- dsda_InitBgClasses: the engine's own BGCLASS (begotten-data.wad) is now read first, then all other BGCLASS lumps in load order. Before, a BGCLASS lump inside begotten.wad itself was overridden by the engine's later one.
- pack_wad.py opens a window when run without arguments (tkinter); .github/workflows/wad_packer.yml builds BegottenWadPacker for Windows/Linux with PyInstaller.

## 0.9.1 - the roster REPLACES Doom's monsters; piercing revolver
- **The new monsters took over Doom's monster slots** (same editor numbers, so existing maps just change). The old monsters no longer exist. Mapping (editor no. -> monster): 3004/9/84/65 pistol/shotgun/SMG/chaingun troopers (unchanged slots, retuned by BGMOBJ) | 68 shock trooper (was arachnotron) | 3002 werewolf (was demon) | 3001 fire elemental (imp) | 66 mad wizard (revenant) | 69 apprentice (hell knight) | 3003 minister (baron) | 67 golem (mancubus) | 64 mindeater (arch-vile) | 58 aegis (spectre) | 3005 bore (cacodemon) | 71 conjurer (pain elemental) | 3006 hatred (lost soul) | 16 mecha (cyberdemon) | 7 giant beetle (spider mastermind) | 88 articulator core (boss brain). Two have no Doom counterpart and are new types: 21001 rocket trooper, 21016 articulator heart.
- **Renamed in code**: MT_SERGEANT -> MT_WEREWOLF, MT_TROOP -> MT_FIREELEM, MT_UNDEAD -> MT_MADWIZARD, MT_KNIGHT -> MT_APPRENTICE, MT_BRUISER -> MT_MINISTER, MT_FATSO -> MT_GOLEM, MT_VILE -> MT_MINDEATER, MT_HEAD -> MT_BORE, MT_PAIN -> MT_CONJURER, MT_SKULL -> MT_HATRED, MT_CYBORG -> MT_MECHA, MT_SPIDER -> MT_BEETLE, MT_BOSSBRAIN -> MT_CORE, MT_SHADOWS -> MT_AEGIS, MT_BABY -> MT_SHOCKTROOPER; state prefixes S_SARG_ -> S_WWLF_ and so on; sprite names SARG -> WWLF, TROO -> FELM, SKEL -> MWIZ, BOS2 -> APPR, BOSS -> MINI, FATT -> GLEM, VILE -> MNDE, HEAD -> BORE, PAIN -> CNJR, SKUL -> HTRD, CYBR -> MECH, SPID -> BEET, BBRN -> CORE; UMAPINFO actor names (bossaction), cast-call captions. The wad's sprite lumps were renamed to match (placeholder colour shifts of Doom's art, `tools/make_monster_placeholders.py`; it also removes the Doom sprites that are no longer used). Leftovers: the arachnotron's states/sprites (BSPI) and the Doom projectile types stay in the tables but nothing uses them (DEHACKED frame numbers must not move).
- Monsters whose slot already had the right body are edited IN PLACE (their own states); shock trooper, aegis, rocket trooper and heart are copies of another chain (SS, lost soul, shotgun guy, boss brain) in their slot or a new type.
- **BGMOBJ lump** (data/lumps/bgmobj.lmp): `<name|ednum> <field> <value> ...`, later lines win; names: rockettrooper shocktrooper werewolf fireelemental madwizard apprentice minister golem mindeater aegis bore conjurer hatred mecha beetle heart core (the old editor numbers work too: `3002 health 160`).
  - all things: `health speed radius height mass pain reaction damage` (damage = projectile damage)
  - stock hitscan monsters (3004 9 84 65): `foedamage <percent>`, `foespread <percent>` (spread is also scaled by the target's class `foeaim`)
  - rockettrooper `windup recover`; shocktrooper `volley gap windup recover`; werewolf `leapmin leapmax leapspeed leapflight leapcooldown leapchance crouch landing`; fireelemental `volley gap clawbase windup recover`; madwizard `seekerchance windup recover clawbase drain`; golem `rockspeed rockscatter arm volleys`; aegis `reach giveup search range nipbase`; bore `bitebase`; conjurer `hatredchance` (of 256) and `cap` (shared by Hatreds and Aegises, default 60); hatred `chargespeed explodepct windup blast blastradius`; mecha `rockets seekers flames flamerange seekerchance windup`; beetle `volleys shots recover gap fan windup`; projectiles by key (`trooper_rocket blast 60 blastradius 72`, `mecha_flame life 18`).
- **Behaviours**: the Aegis raises a corpse with doubled health and a boost flag (double damage, double movement as two normal steps), is used up by it and does not count as a kill; with no corpse it drifts at the player and nips. Revived monsters count as kills again (`totallive`). The Conjurer leaves no skulls when it dies. Hatreds charge; 50 % of charges end in a blast (radius/damage tunable) that kills the Hatred. Drain bolts heal the shooter; a parried bolt hurts the shooter's side for double and heals the Swordsman at half rate. Werewolf leaps. Golem rocks are lobbed (gravity). The hearts and core carry `A_BossDeath`, so UMAPINFO `bossaction` works by the new names (Beetle, Core, ... and `heart`).
- **Vulnerabilities**: DMGKINDS lines per faction on the NEW editor numbers (Modern firearm 115 / magic 80; Medieval magic 125 / firearm 90; Golem firearm 35 / magic 150; Mecha firearm 50; Beetle magic 40). The old test lines for cacodemon/lost soul/revenant were removed (those slots are other monsters now).
- **Revolver secondary** (slot 2 alt): one bullet, `(1..mod) x damage` (20/40/60), passes through up to `hits` monsters (default 5), each taking `falloff` percent (85) of the one before; recovery ~41 tics; uses 2 bullets. BGWEAPON line `pierce damage 20 mod 3 hits 5 falloff 85 spread 0`. Engine: `bg_PierceAttack()` in p_map.c.
- **Engine hooks**: `p_mobj.h` gained `bg_flags bg_drain bg_cd bg_aux`; `p_inter.c` (boost doubling, drain), `p_enemy.c` (boost's second step, hitscan scaling), `p_map.c`/`p_mobj.c` (skull impact => explosion), `name.c` (actor names).
- **UDB**: `udb/Includes/Begotten_monsters.cfg` (included by Begotten_Doom2Hexen.cfg after the Doom things) re-lists the replaced numbers under the new names in four categories. Not tested inside UDB itself.
- **Tests**: `BEGOTTEN_SELFTEST=4 ... -warp 2 -nomonsters` spawns every roster monster in a real map and checks leap, lob, summon cap, Aegis drift and revive, boost speed, drain and parried drain, Hatred burn-out, piercing; selftest 3 and the net test are unchanged and pass.
- Note: in this engine at complevel mbf21 the vanilla 20-Lost-Soul limit is inactive (`comp_pain` off), so the cap of 60 is new.
- Vanilla MAP07 / E1M8 style `A_BossDeath` triggers now key off the renamed types (golem, shock trooper, mecha, beetle ...).

## 0.9.2 - fix: missing sprite crash
- The sprite name table still listed "TROO" for sprite 0 (my rename missed the first entry), so the fire elemental had no sprite lumps and R_ProjectSprite stopped with "Missing spriteframes 0 : 12" (a gibbed imp) as soon as one was drawn; on Linux the same error closed the game with no window. Fixed ("FELM"). The live test now checks that every state's sprite frame exists in the loaded wad (known stock gaps excepted). The wad is unchanged from 0.9.1.

## 0.9.3

- **Parry window is 7 tics** again (BGCLASS `parry 7`, swordsman). Selftests follow the class value.
- **Chaingunner (65) longer wind-up**: the aim-up frame is now 24 tics (was 12). Tune it with a BGMOBJ line `65 windup <tics>`; `windup` now works on any stock gunner too (e.g. `84 windup 20`, `9 windup 14`).
- **SS trooper rotations**: the SSWV shooting frames E/F/G were front-only; they are now 8 separate numbered placeholder rotations each (`tools/make_rotation_placeholders.py`), and so is the shock trooper copy (STRP). The chaingunner (CPOS) already had every rotation.
- **Music lines for Hexen-format maps** (UDMF maps already had Music_ChangeSong 2703 / Music_Stop 2704 with the lump name):
  - special 212 *Music Change*: arg1 song number, arg2 1 = only the activator hears it, arg3 1 = play once (0 loops).
  - special 213 *Music Stop*: arg1 1 = only the activator.
  - Song numbers come from the new `BGMUSIC` lump (`<number> <LUMPNAME>` per line). A different song starts from its beginning; crossing the line while that song is already playing does nothing.
  - UDB: `udb/Includes/Begotten_linedefs.cfg` (category "Begotten"), included by `Begotten_Doom2Hexen.cfg`.
  - Two songs cross-fading at once is not implemented (needs a second music stream in the mixer).

### 0.9.3 (second batch)

- **Chaingun guy = the SS-sprite monster (84).** The longer wind-up (24 tics before the first shot) moved from 65 to 84; 65 is back to stock + 2. `84 windup N` in BGMOBJ tunes it.
- **Revolver swapped.** Primary (pistol, slot 2): one bullet, 10 x (1-3), not piercing (BGWEAPON `pistolshot damage 10 mod 3 spread 40`). Secondary (`dualpistols`): weaker (`pierce damage 6 mod 3 hits 5 falloff 85`), slow (41 tics), pierces. Both cost 1 bullet.
- **Pistol troopers (3004) fire the same rule** as the player's primary (10 x (1-3), then their `foedamage` percent). They don't pierce.
- **Music lines fade out.** Music Change fades the old song out (default 35 tics = 1 s, volume ramp), then starts the new one from its beginning; Music Stop fades and then stops. Applies to UDMF and Hexen-format lines. BGMUSIC `fade <tics>` sets the length (0 = cut at once). Level start or any other music change cancels a running fade.

### 0.9.4

- **Witch starts with the fireball** too: `weapons fist pistol fireball`, `ammo fire 20` (5 shots; the fireball costs 4).
- **Network weapon-change log.** Every weapon-change command a machine builds in a network game is written to `begotten.log` (first 300): `WEAPONCHANGE maketic .. gametic ..: <slot key | next/previous weapon | toggle key | out of ammo> -> weapon N (ready R, pending P)`. Added to find what makes the weapon switch back and forth in multiplayer.
- `BEGOTTEN_NETTEST_IDLE=1` makes the net test bots send empty commands.

## 0.9.5

- **Witch fireball fixed.** 0.9.4's `weapons fist pistol fireball` was rejected by the parser (the engine word is `missile`); the line is `weapons fist pistol missile` now, and the BGCLASS/BGWEAPON parsers also accept the in-game names knife, rifle, scattergun, machinegun, fireball, revolver, thunderclap. She starts holding the fireball (the best weapon she has ammo for).
- **Weapon change speed per class**: BGCLASS `swapspeed <category> <pct>` scales how fast the class raises and lowers weapons of that category (melee / firearm / magic / special). Soldier: firearm 150, magic 75. Witch: magic 150, firearm 75. Swordsman: normal.
- **The instant pistol raise is gone**: the old DEHACKED `Frame 12 / Unknown 1 = 1` (the pistol's raise state setting the sprite position) is ignored by the engine (`S_PISTOLUP` misc values cleared) and removed from the wad's DEHACKED.
- **Field diagnostics in `begotten.log`** (network games): `WEAPONODD` when my weapon sprite jumps more than 12 units in a tic (with state, bob, momentum), and `NETSTAT` every 10 seconds (stalls, long gaps, bursts, interval, lead).

## 0.9.6

- Network pacing: when a tic runs late (the other player's commands were late, or a slow frame) the engine no longer
  runs the missed tics in a burst. The next tic starts one full interval later and the lead adjustment makes up the
  time gradually. Bursts moved the camera two tics in one frame while the weapon sprite stayed put, which looks like
  the weapon jittering. The net test now shows 0 back-to-back tics (was 5-6 per 1100).
- Diagnostics in begotten.log (network games): `WEAPONLOWER` (every time any player's weapon is put away, with the
  reason fields) and `WEAPONSTAT` every 350 tics. On Windows the log is `begotten.log` next to the exe.

## 0.9.7

- Spy mode ("WATCH TEAMMATE", co-op only, cycles through the players' views) now defaults to V instead of F12. A saved
  begotten.cfg keeps its old binding: rebind it in Options > Key Bindings > Game, or delete `input_spy`.
- Options menu: DEMOS and WEAPONS (priority list, unused by Begotten's own weapon logic) are gone; COMPATIBILITY is now
  GAMEPLAY (skill, casual play, friendly fire, dev unlock); the overflow-emulation and mapping-fix pages are gone.
- Key Bindings: Movement (incl. jump, look up/down/center, free look, autorun), Weapons, Game, Interface, Automap,
  Menus. Removed: Misc, Toggles, Raven (inventory), Cheats, Scripts, Build. The actions keep their defaults and stay
  settable in begotten.cfg.
- General/Display: removed v1.1 pitch effects, movement-toggle sounds, ENDOOM, rewind settings, mouse strafe divisor,
  floor-jolt fix, translucent ghosts.

## 0.9.8 - Options menu rework

Options: VIDEO (tabs Video, Display), AUDIO, CONTROLS (tabs Mouse, Controller, Key Bindings), DEMOS (Options, TAS, Keys),
GAMEPLAY (always pistol start, friendly fire, dev unlock), LEVEL TABLE. Automap, Weapons priority and Compatibility
screens are gone from the menu (code left in place, unreachable).

- Display tab: hide weapon, view/weapon bobbing, gamma (now a slider), crosshair options.
- Key Bindings is one scrolling list: Movement, Toggles, Look, Weapons, Map, Game, Screen, Menus.
- New defaults: jump Space / pad A; use E / pad X; secondary fire Z / right mouse button / left trigger; free look M;
  console `; next/previous weapon on the mouse wheel; turn left/right on the arrow keys (E and Q no longer turn);
  forward and strafe lose their old mouse buttons; spy V.
- New defaults for hidden settings: weapon attack alignment centred, announce map on, locked doors blink, secrets
  only after entering, mute when out of focus, crosshair "cross".
- `begotten_menu_rev` in begotten.cfg: when a config older than this revision is read, every setting that is no
  longer in a menu goes back to its default and the re-defaulted key bindings are reset. Runs once.
- Sliders take their range from the setting (gamma 0-4, volumes 0-15).

## 0.9.9 - menu polish, OpenGL removed, weapon-draw diagnostics
- Options: "Video" is now "Screen"; the Audio screen has a single title; Controls tabs are Key Bindings, Mouse, Controller.
- Host/Join/Player Setup reordered (Join: Join, name, IP address, blank, Back; Host: Continue first, Back last; Player Setup: Continue, one "name - class" row per local player, Back).
- Game Setup: the bottom "N players connected" line is gone; the top banner stays for network games only.
- OpenGL: removed from the menu and the video mode is forced to Software on load (config revision 2).
- Diagnostics in begotten.log: `WEAPONDRAW jump` dumps the last ~32 rendered weapon frames when the drawn weapon position jumps while the simulation's did not; `NETSTAT` now has `true-stalls` (only counts real waiting, not early peer commands).

## 0.9.11
- Multiplayer menus: the cursor goes back to Join / Continue after editing a name or address and when returning to those screens.
- Waiting room (clients) now shows the host's selected level, the difficulty and each player's finished-level checkbox (lobby packet carries level index, skill and a completion bitmask).
- begotten.log is written next to the .exe (Windows) or next to the .AppImage (Linux); if that folder is not writable it falls back to the config folder (%APPDATA%\begotten on Windows).

## 0.9.12
- Network rendering: bgnet_Frac (the between-tics position used to smooth the view) can no longer step backwards within a tic and no longer underflows while catching up; this was a candidate for the "flickering gun" on the Windows host.
- Diagnostics: NETSTAT shows `frac-back N/M`; WEAPONDRAW also fires if the drawn weapon moves within a single tic.

## 0.9.13 - all placeholder content
- begotten.wad no longer contains Freedoom art, sounds, music or maps. Every sprite / menu / HUD graphic has the same name, size and hot spot as before but is a labelled coloured block (name, frame, rotation; first-person weapons are see-through blocks with the weapon name). Fonts and digits are plain glyphs in the original palette colours so text and colouring still work.
- Textures: GREY1 RED1 BLUE1 GREEN1 YELLOW1 BROWN1 DOOR1 DOORTRAK LIFT1 STEP1, switches SWX1/SWX2 (exit) and SWS1/SWS2 (secret exit), SKY1, and WALL01..WALL32 (one labelled wall per map). Flats: FLOOR1-3, CEIL1-2, LIQUID1/2, LAVA1, F_SKY1.
- Maps: MAP01..MAP32 are one 512x512 room (odd maps have an open sky ceiling) with 4 player starts, 4 deathmatch starts and an exit switch; maps with a secret exit in UMAPINFO (and 15, 31) also have a secret-exit switch. Names are "Placeholder NN".
- Sounds: every name the engine's sound table knows is a 60 ms silence. Music: every D_* name is a silent MIDI. SOUND_MAP.md lists which lump is used for what.
- Kept (tables, not art): PLAYPAL, COLORMAP, GENMIDI, DMXGUSC, colour-map / translucency tables, DEHACKED, BGCLASS. Removed: Freedoom's own text lump, PC-speaker sounds, DBIGFONT, Freedoom music lumps in data.
- Tool: tools/make_placeholder_content.py OLD_TREE NEW_TREE (needs Pillow; reads the engine's sounds.c/.h for the name lists).

## 0.10.0 — swordsman blade, stun bonus, class air control

- **Blade (BGCLASS `charge`)**: for a class with a held attack the attack button works as follows. A click (released within 9 tics) is the normal slash (`blade`, swordsman now 45-55). Held longer, the sword is drawn back (only the tip shows, PUNG frame E) and charges; releasing stabs. Stab damage runs `charge <tics> <min> <max>` = 70 tics, 1 .. 100 (a hold just past the click threshold does 1-5, weaker than a slash). On release the player is held still for 3 tics (on the ground only), then lunged forward at `lunge <min> <max>` = 10 .. 24 map units/tic by charge. In the air the lunge adds to the existing momentum (a running jump + stab goes much further). A hit pushes the target with the player's momentum (heavy monsters move less, mass-scaled, capped) and the player keeps 25% of his speed.
- **Riposte (BGCLASS `riposte 200`)**: a click on a stunned monster within 192 units is a riposte instead of a slash: small step back (the stun is held open while winding up), then a hard lunge/stab for 200 damage (300 with the stun bonus) that ends the stun and pushes the target back.
- **Stun bonus**: a stunned monster takes 150% damage from any player (`BG_STUN_DAMAGE_PCT`, `bg_StunDamage`, applied in `P_DamageMobj`). Damage does **not** end a stun (so one player can parry and others shoot); it runs out or a riposte spends it.
- **Air control (BGCLASS `aircontrol <pct>`)**: percent of ground acceleration while airborne: soldier 15, witch 30, swordsman 80. Steering never makes the player faster than a running one but keeps any speed he already has (lunge) - only its direction changes.
- New PUNG sprite frames: E (charge, tip only), F (thrust), G (riposte wind-up). A class whose art stops short of the last frame draws its last frame instead of aborting (`R_DrawPSprite`).
- Player fields `bgcharge`, `bgstabdmg`, `bgstabspd`; simulation revision 6 -> 7 (builds from before cannot play together with this one).
- Tunables are `#define`s next to `A_BGBladeStart` in dsda/begotten.c (tap threshold, lock tics, stab ticks, reach, riposte seek range and wind-up, momentum kept) plus the BGCLASS numbers above.

## 0.10.1 — menus, default keys and display, 8-direction sprites, weapon-draw hardening
- Menu font: the placeholder STCFN glyphs are white with a dark drop shadow (a flat single-colour font collapsed every text colour ramp to its dark end).
- Default keys: W/Up forward, S/Down back, Left/Right turn, A/D strafe, Left Alt strafe modifier, Space jump, E use, Left Shift run, Backspace 180 turn. Existing configs are migrated once (`BG_MENU_REV 3`): keys, 1280x720, fullscreen on, exclusive fullscreen off.
- Menu flow: title picture with no menu; the menu opens by itself after 5 s (once). Enter advances, Esc goes back and only closes the menu at the top of the main menu. Controller: A advances, B/Start go back (Start and Esc share one input).
- Placeholder sprites: all 8 rotations, no mirrored lumps, with the facing dial.
- First-person weapon interpolation is now tracked per player AND per psprite layer, and by gametic instead of the display's global `realframe` flag (a shared slot between the weapon and flash layers, and a flag one frame could consume for another, could make the drawn weapon jump between two positions in network games). Sim is unchanged (sim revision stays 7).

## 0.10.2 — stab damage follows speed, menus eat controller buttons (sim revision 8)
- Charged stab: the damage is scaled by how fast the player is moving at the moment of impact, relative to the lunge's launch speed (`lunge`): full while she is still carried (always in the air, where nothing slows the lunge), falling as the ground slows her, never below 25% (BG_STAB_MIN_PCT) so touching an enemy at a crawl still hurts. A riposte is always the full hit. The target's pushback uses the same impact speed (floor 2 units/tic), divided by the target's mass as before.
- Controller: a button pressed while a menu (or any non-level screen) was up is hidden from the game until it is released (`held_in_menu` in game_controller.c). Confirming "End Game" with A used to close the menu with A still held, which the game read as a jump. Verified with the virtual-pad test (`BEGOTTEN_PADTEST_EAT=1`, build with -DBEGOTTEN_PADTEST): fails without the mask, passes with it.

## 0.10.3 — class weapon hands, class speeds, werewolf landing (sim revision 9)
- Speeds (BGCLASS `speed`, percent of the original game's): soldier 90, witch 110, swordsman 120.
- Werewolf: the crouch before the leap is 9 tics (was 14, a third shorter) and the landing no longer stops: it bites at once if the target is in reach, otherwise it is straight back in the chase (`landing` default 0, `crouch` default 9 in the roster's tuning values).
- First-person weapons per class: the soldier and the witch have their own lumps for every held weapon (soldier SKNF SSAW SPIS SSHG SSH2 SCHG SMSG SPLG SBFG, witch WSNP WSAW WPIS WSHG WSH2 WCHG WMSG WPLG WBFG; the swordsman keeps PUNG), mapped by `sprite <stock> <class>` lines in the wad's own `data/BGCLASS.txt`. Placeholders: soldier = one dark-skinned hand block, witch = two light-skinned hand blocks that wander a little from frame to frame (`make_placeholder_content.py`, `do_class_weapons`; skin colours SOLDIER_SKIN / WITCH_SKIN). Muzzle flashes are shared. The weapons' state chains (which frame, how many tics) are still the stock ones, so a class can redraw frames but cannot yet have extra or different frames: that needs per-class weapon state tables (not done).

## 0.10.4 — palette family, animated pickups, nine keys (sim revision 10)
- **Palettes.** `PLAYPAL`, `COLORMAP`, `TRANMAP` and the sector colour maps are now original (made by `tools/make_palette.py`; the Freedoom palette is gone). The base palette is 256 colours built as 15 ramps of 16 shades plus a short glow (fullbright) ramp, so the light levels in `COLORMAP` slide a colour along its own ramp. `make_palette.py TREE --base base --variant 1=crow --variant 2=dusk` writes the base palette and alternative themes into `PLAYPAL1..9`; a theme is a list of hues and saturations per ramp (`THEMES` in the script), so "inspired by" palettes are made by choosing those. Themes keep the same ramp structure, so all art recolours by index and the light maps stay valid. 256 is the limit: art is 8-bit indexed and the engine does not load PNG lumps as truecolour, so more colours would need a different renderer; per-episode palettes are how to get more variety.
- **Per-map palette.** The wad's `data/BGPAL.txt` (`dsda/bgpalette.c`) picks the palette: `default N`, `map A[-B] N` (N = 0 for PLAYPAL, 1..9 for PLAYPAL1..9). The generator ships map 1-8 = base, 9-16 = crow, 17-32 = dusk as an example. Everything is recoloured, HUD and menus included. Caveats: the translucency table (`TRANMAP`) is built from the base palette, so a variant's translucent things are only approximately right; pain/pickup flashes still use the palette's own 14 entries.
- **Pickups.** Every weapon pickup (SHOT slot 3, MGUN 4, LAUN 5, PLAS 6, BFUG 7, SGN2 8; slots 1-2 are start weapons) and every key loops over eight frames A..H of its sprite (4 tics each, no rotations; a sprite with fewer frames loops over what it has). Code: `dsda/bgpickup.c`.
- **Nine keys.** Blue, yellow and red keep their things (editor numbers 5, 6, 13); green 22001, cyan 22002, magenta 22003, black 22004, grey 22005, white 22006. Sprites BKEY YKEY RKEY GKEY CKEY MKEY KKEY AKEY WKEY (frames A..H). The skull keys (38-40) are retired. `player->cards[]` slots: 0 blue, 1 yellow, 2 red, 6 green, 7 cyan, 8 magenta, 9 black, 10 grey, 11 white (`key_c` added, NUMCARDS 12). Locks in Hexen / UDMF-format maps: 1 red, 2 blue, 3 yellow, 7 green, 8 cyan, 9 magenta, 10 black, 11 grey, 12 white; 100 = any, 101 = all nine. Doom-format maps only know red / blue / yellow. In a netgame keys stay in the level for everyone, as in Doom.
- **HUD.** The status bar has a 3 x 3 key grid (cell per colour, only owned keys drawn); the HUD `keys` component does the same. Icons are the wad's `BGKEY0`..`BGKEY8` (blue yellow red green cyan magenta black grey white, 8 x 8).
- **Tests.** The pickup self test (`bg_PickupSelfTest`, part of `BEGOTTEN_SELFTEST=3`) and the live test (every key is spawned, touched and its lock opened; 8-frame loops).

## 0.10.5 — episode palettes, no invisibility sphere (sim revision 11)
- Palette themes for the three episodes (`make_palette.py`): `crow` (episode 1, urban night: midnight blues, bruised purples, orange fire), `golden` (episode 2, medieval villages at golden hour: warm, saturated, amber highlights, violet shadows), `alien` (episode 3, ship and alien planet: blues to violet, yellows to acid green, reds to crimson, muted with violet shadows). `PLAYPAL1/2/3` and `data/BGPAL.txt`: maps 1-8 = 1, 9-16 = 2, 17-32 = 3 (edit the ranges to match the real map numbers). `dusk` stays in the script as an extra theme but is not shipped.
- The Partial Invisibility sphere (editor number 2024) is retired like the skull keys. The Spectre monster is unchanged.

## 0.10.6 — palette index 0 is black, palette switch at maps 11 and 21
- **Splash flash fixed.** The palette made in 0.10.4 had white at index 0 (the neutral ramp ran light to dark). The engine treats index 0 as black (screen fills, borders, wipes, the startup screen), so those showed white. The neutral ramp is now stored dark to light (index 0 = black, 15 = white); `make_palette.py` handles that in the colour maps (`ramp_index`). Rule for any new theme: index 0 must stay black.
- `data/BGPAL.txt`: maps 1-10 = PLAYPAL1 (crow), 11-20 = PLAYPAL2 (golden), 21-32 = PLAYPAL3 (alien). Title, menus and intermission use PLAYPAL (base).

## 0.10.7 — art at more pixels per map unit (BGSCALE), tall pictures
- **BGSCALE** (`dsda/bgscale.c`, data lump `BGSCALE`): `sprites N`, `textures N`, `weapons N` say how many picture pixels make one map unit (default 1; fractions such as 2.5 are fine). Sprites, wall textures and first-person weapons can then be drawn at a higher resolution while the map, the physics and the collisions stay exactly as they are (a 56 unit tall player is 56 units, whatever the size of its picture). Everything that is measured in map units (`textureheight`, offsets, sprite offsets) is converted from picture pixels, so the sim does not change. HUD, menus, intermission and the sky are not scaled. Sprite hot spots and sizes are in picture pixels.
- **Flats**: a flat lump of 128x128, 256x256, 512x512 or 1024x1024 bytes covers the same 64x64 map units as a 64x64 one (no setting needed).
- **Tall pictures**: `pack_wad.py` now writes pictures up to 1024 rows (DeePsea tall patches, which the engine reads) instead of 254.
- **Test wad**: `BEGOTTEN_ART_SCALE=2 make_placeholder_content.py OLD NEW` writes every sprite, weapon, wall texture and flat at 2x the pixels (nearest neighbour) plus `data/BGSCALE.txt`; the game must look exactly like the 1x one. Screenshot helpers: `BEGOTTEN_SELFTEST=6` puts a row of pickups in front of the player.
- Known limits: the sky is not scaled; per-sprite scales are not supported (one factor per group); a hi-res weapon's frames must all use the same factor. The live test (`BEGOTTEN_SELFTEST=4`) is random-seeded and some roster sections (golem, aegis, pierce) occasionally fail when an earlier section leaves the player next to a wall; run it again.

## 0.10.8 — the placeholder art is hi-res
- `make_placeholder_content.py` now makes everything at 2.5 pixels per map unit by default (`BEGOTTEN_ART_SCALE=1` gives the old 1x art; any other number works too): sprites and first-person weapons are enlarged with their labels redrawn sharp, wall textures and the switches are drawn at the new size, flats are 128x128 (64 units wide, a power of two is needed). `data/BGSCALE.txt` is written to match. The sky, HUD, menus and intermission pictures are unchanged.
- Why 2.5: the player's eye is 41 units high, so 2.5 gives a camera about 100 pixels up.
- MEDIA0 (the medikit) is a detailed hand-drawn style sprite (`detailed_medikit`) and MAP01 has one at (170, 230), ahead and to the right of the player start. It is a sample of the resolution, not final art.

## 0.10.9 — every placeholder asset is now drawn (identifiable stand-ins)
- No engine change. `make_placeholder_content.py` now draws the placeholder art instead of labelled blocks. It is still stand-in art, made so you can read what each thing is; none of it is meant to ship.
- **How**: `tools/art_render.py` is a small ray-caster (ellipsoids, boxes, tapered cylinders; orthographic camera 12 degrees down; outlines; 3x supersampling). A model is built once in its own frame and turned to any of the eight directions, so the eight rotations of a sprite are one model, never mirrored. `art_rig.py` is a posable humanoid (walk, aim, pain, fall); `art_monsters.py` / `art_monsters2.py` the roster; `art_classes.py` soldier, witch, swordsman in every pose the engine uses (their player-colour parts use the palette ramp 112-127, so the co-op colours still work); `art_guns.py` the 11 weapons as 3D models, shared by pickups, first-person views and the players' hands; `art_props.py` pickups, ammo, keys, scenery; `art_fx.py` projectiles, explosions, fogs, flashes; `art_fp.py` first-person weapons (drawn into the same box and hot spot the placeholder had); `art_tex.py` wall textures, flats, sky (all tile); `art_hud.py` status bar, faces, key icons, menu skull and logo, title / help / credit / intermission / boss screens; `art_apply.py` walks the tree and replaces every sprite a drawing function knows.
- Names, frame letters, rotations and hot spots are unchanged, so everything else keeps working. A family with no drawing function keeps its labelled block.
- Weapon and key pickups turn 45 degrees per frame (A-H). The unused stock families (POSS, SPOS, SSWV, CPOS) are drawn as the nearest roster monster.
- The map wall textures now look like their episode (brick, stone blocks, metal panels) and carry a plaque with the map number.
- Run time: about 20 minutes on two cores for the whole tree (set `BEGOTTEN_ART_PROCS`). Everything is deterministic.

## 0.10.10 — characters, hands and the triple shotgun (placeholder art, one small engine change)
- **Engine**: the snap (slot 7 primary) now has its own first-person sprite, `SNPG` (A ready, B pinch, C snap, D open hand), with its own raise / lower / ready states. Before, it shared `PUNG` with the knife / sword, so the soldier's snap showed a knife. `BGCLASS` maps it per class: `sprite SNPG SSNP` (soldier, one hand) and `sprite SNPG WSNP` (witch, both hands). The witch's slot 1 is now `WPUN` (bare fists, guard on frame D). The parry still uses `PUNG` D.
- **HUD faces**: one set per character, all women: soldier (`SLD`, a black woman), witch (`WCH`, blonde, white), swordsman (`SWD`, black hair, Asian). `face SLD / WCH / SWD` in each `edit` block of BGCLASS. `STFB0-3` (player backgrounds) are drawn too.
- **Weapons (art)**: pistol slot is a revolver; rifle and scattergun are one 3-barrel shotgun (SHTG: fires the top barrel, the cluster turns; SHT2: all three, break, shells out, reload, close). Fireball and devil's breath are cast from bare hands (fire ball between the palms / a jet of fire); thunderclap is a clap with rings; snap is finger snaps. The swordsman's parry is the blade held across the body in both hands (first person `PUNG` D, third person `XPAR`).
- **Muzzle flashes** are placed at the muzzle: every first-person weapon is drawn on one fixed canvas (`art_fp.py`: the grip sits at a fixed screen position, the hot spot comes from where the origin lands), and each flash lump is centred on the muzzle of the firing frame. Flashes are shared by all classes, so the guns sit at the same place for every class.
- **Pickups**: slot 3 shows the triple shotgun, slot 2 the revolver; slots 5-8 (fireball, storm, spirit, hellfire) are spell tomes with differently coloured gems.
- **Third person**: the witch casts bare-handed (no wand), the soldier has an open-faced helmet and the revolver / triple shotgun, the swordsman a ponytail and the guard pose. Slimmer proportions.
- Limits: arc lightning (slot 6 alt) draws with the shotgun sprites and tornado with the devil's breath ones (they share states with them).

## 0.10.11 — weapon sizes, own sprites for hellfire and arc lightning, see-through flashes, the new parry
- **Weapon art** is smaller and lower (the guns sit in the bottom quarter of the view, like Doom's), with a flatter angle. `art_fp.py`: `PPU_OF`, `ANCHOR` and `FACE_OF` set the scale, the screen position of the grip and the camera yaw per weapon.
- **Hellfire (slot 8)** has its own first-person sprite `HLFG` (A ready, B firing) and muzzle flash `HLFF`: an alien rocket launcher (also its pickup, `SGN2`). It no longer shares the fireball's hands. `BGCLASS` maps `HLFG` per class (`SHLG`, `WHLG`).
- **Arc lightning (slot 6 alt)** has `ARCG` (A charge, B bolts, C fade): bare hands, no gun and no muzzle flash. Per class `SARC`, `WARC`.
- **Muzzle flashes** are drawn translucent (the flash layer uses the translucency table when it is on).
- **Swordsman parry** (first person, `PUNG`): D = guard (the sword comes up at once); if nothing is parried the window closes and the sword swings slowly back through I and J (about 16 tics, she is open); a parried blow plays the bat-away swing K L M and leaves her guarding on the other side (H, the guard mirrored); the next parried blow in the window plays N O P (the swing mirrored), then K L M again, and so on. Ending a chain skips the recovery (one frame, J). The mirrored lumps are real lumps (H, N, O, P). `player->bgparryhit` now counts the parried blows.
- **Swordsman parry** (third person, `XPAR`, 3 frames): A raising, B held, C the outward swing, shown for a few tics after a parried blow only. A pose with three or more frames now works that way for `parry` (see bgclass.lmp).
- Limits: the tornado (slot 6 primary) still shares `PLSG` with the devil's breath.

## 0.10.12 — a projectile of its own for each weapon
Audit: before this, the fireball and hellfire rocket both used the stock rocket sprites, devil's breath and arc lightning used the plasma ball, and the tornado and the thunderclap reused other effects.
- **Fireball (slot 5)**: new mobj type (copy of the rocket) with sprite `FBAL`, a large fireball, fire explosion on impact. Action `A_BGFireball`.
- **Devil's breath (slot 5 alt)**: new mobj type (copy of the plasma ball) with sprite `DBRT`, a small tossed flame. Action `A_BGBreath`.
- **Tornado (slot 6)**: spinning sphere `TRNP` (A-D loop); on impact it becomes the funnel `TRNF` around the target, like the arch-vile's flames.
- **Arc lightning (slot 6 alt)**: bolt sprite `ARCB`.
- **Hellfire (slot 8)**: magenta alien rocket `HLFR` (8 rotations) with a magic explosion.
- **Thunderclap (slot 7 alt)**: a shock ring `CLAP` expands at chest height.
- Snap (slot 7) keeps the arch-vile flame (`MT_FIRE`) at the target.
- Limits: the tornado's first-person hands still share `PLSG` with the devil's breath.

## 0.10.13 — the fourteen items, Shield, screen flashes, tornado hands
- **Items** (the stock things keep their editor numbers; `p_inter.c` `bg_TouchItem`):
  first aid kit (MEDI) +15 health up to 100 · health potion (STIM) +40 up to 100 · health vial (BON1) +2 up to 200 · shield charge (BON2) +2 shield up to 200 · bulletproof vest (ARM1) 100 shield, absorbs 1/3 · shield potion (ARM2) 200 shield, absorbs 1/2 · health spell (SOUL) +100 up to 200 · life and protection spell (MEGA) health 200 and the blue shield · adrenaline shot (PSTR) double damage dealt, heals to 100 if below, 120 s (uses the `pw_strength` counter; the berserk x10 fist is gone) · immortality spell (PINV) 60 s · heat resistant boots (SUIT) 60 s · cartography spell (PMAP) whole level · infrared goggles (PVIS) 120 s · backpack (BPAK) doubles ammo capacity and gives some ammo. Class health caps (witch) still apply.
- **Armor is now Shield** in messages and HUD labels (`SHD`, `SH`).
- **Screen flashes** (`ST_PaletteIndex`, `bg_PaletteIndex`): PLAYPAL now has 26 palettes: 14-17 blue, 18-21 yellow, 22-25 purple (four strengths each; `tools/make_palette.py`). A wad with the old 14 palettes keeps the stock flashes. Damage flashes are halved (full if the player is dead). Health pickups flash blue, shield pickups yellow; the 100-point pickups (vest, potions, spells) are stronger and a little longer. Immortality tints the screen blue (no inverse colours any more), the boots yellow, adrenaline purple; each flashes when it is about to end.
- **Tornado** has its own first-person sprite `TRNG` (A ready, B/C whirl, D thrown), `STRN` / `WTRN` per class, and no muzzle flash.
- Item art redrawn (`art_props.py`).

## 0.10.14 — co-op pickups and keys
Applies only while more than one player is in a non-deathmatch game (split-screen or network). `bg_CoopItems()`, `bg_ItemShared()` and friends in `begotten.c`; the pickup in `P_TouchSpecialThing`.
- **Every pickup exists for every player.** The first player to take it sees it turn translucent (drawn per viewer in `r_things.c` / `gl_main.c`), the others still see it solid; it is removed once everybody in the game has taken it (`mobj_t.bg_taken`, a bit per player, saved with the mobj). A pickup that is not needed (full health...) is not taken. Placed weapons follow the same rule (the old "leave weapons forever in net games" is off), dropped ammo and weapons too.
- **Difficulty 4 (Ultra-Violence):** power-ups (the fourteen items, backpack included) are one shared pool: whoever takes one removes it for everybody. Weapons and ammo stay per player.
- **Difficulty 5 (Nightmare):** everything except keys and the weapons the map places is one shared pool (ammo, monster drops, dropped weapons, power-ups).
- **Keys, difficulty 1-3:** a key taken by one player is given to all players and removed. Dying keeps the keys.
- **Keys, difficulty 4:** only the player who takes a key gets it (only they open its door); it stays in the level, translucent for them, until the others take it too; it is never removed. Dying keeps the keys.
- **Keys, difficulty 5:** same pickup rule as 4, but dying wipes that player's keys (`G_PlayerReborn`), and the keys lie there solid for them again (`bg_ForgetKeys`).
- Difficulty here is the menu level 1-5 (`gameskill + 1`).

## 0.10.19 — input lag that appeared after a few minutes of network play

- **Cause** (found in a Windows `begotten.log` and reproduced with the two-instance net test plus a 1.3 s freeze of one instance): commands are made at 35 per second by the wall clock, but tics only run as fast as the slowest machine's commands. After a hitch (a respawn, a slow frame) every machine except the one that hitched ended up with its command counter several tics ahead of the game for good: the log showed `lead 2` becoming `lead 6` with the interval pinned at its 85% floor for ~4 minutes, which is ~170 ms of input delay instead of ~57 ms. The speed-up in `bgnet_AfterTic` cannot drain it because the game cannot pass the slowest machine's commands. The machine that hitched kept `lead 2`, so it felt fine, as in the report.
- **Fix:** `bgnet_CanMake()` makes no command while more than `target_lead + 1` are already waiting, and trims one command after two seconds of the lead sitting above the target. In the test the lead returns to 2 on both machines after the freeze (before: 5 on the other machine, forever), with identical state hashes at the end.
- `NETSTAT` lines in `begotten.log` now end with `trimmed N` (commands skipped by this rule). A healthy game shows a few; `lead` should return to the target within a couple of seconds of any hitch.

## 0.10.18 — lobby layout fix

- The waiting room's own-class body portrait stands left of the mugshot column instead of in front of it, and a long status line wraps before reaching the portrait. Engine only; the wad is unchanged from 0.10.17.

## 0.10.17 — the swordsman's stab in third person, player mugshots in the lobby

- New pose kinds in BGCLASS: `charge`, `chargerun`, `chargejump` (the sword drawn back beside her, pointing forward: still, walking, in the air) and `stab`, `stabair` (the lunge: feet on the ground, or legs flexed in the air). `bg_StabPhase()` (begotten.c) reads the blade's weapon state: charging / the pause before the lunge / the riposte wind-up = drawn back; lunge thrusts and their recovery tic = lunging. Classes without these poses fall through to the ordinary ones.
- Sprite families X5FI (1 frame), X6FI (6), X7FI (3), X8FI (1), X9FI (1), each in 8 rotations.
- First person: PUNG frame E (the drawn-back sword) now shows only the tip.
- The waiting room and Game Setup show a mugshot tile (player colour + class face) per player down the right edge; the waiting room keeps the full-body portrait of your own class.

## 0.10.16 — run and jump poses for every weapon, weapons on MAP01

- Soldier and witch now have a third-person **run** (6 frames) and **jump** (3 frames) sprite family per weapon, 8 rotations each (`S3xx`/`S4xx`, `W3xx`/`W4xx`), so a held knife, shotgun, fireball and so on stays in hand while moving or airborne.
- BGCLASS gains matching `pose run` / `pose jump ... weapon <name>` lines. `BG_MAX_POSES` raised 48 -> 96.
- MAP01 has a row of weapons on the floor for multiplayer testing: shotgun, super shotgun, chaingun, launcher, plasma, BFG (the chainsaw pickup is not placed).

## 0.10.15 — class bodies in the lobby and in the world, a third-person pose for every weapon
- **Class bodies**: BGCLASS gets `sprite PLAY WPLY` / `sprite PLAY XPLY` for the witch and the swordsman (the sprites were drawn but never mapped), so standing, pain and death are the class's own. Before, only the soldier had an idle picture, and the menus (which show frame A of the player sprite) drew the soldier for everybody.
- **Third-person poses per weapon** (`art_class_weapons.py`): sprite families `<S|W><1|2><code>` (1 = stand ready, 1 frame; 2 = shoot, 3 frames; code: FI fist, CS chainsaw, PI pistol, DP dualpistols, SG shotgun, SS supershotgun, CG chaingun, CF chainfire, FB missile, DB plasma, TN tornado, AL arclightning, SN snap, TC bfg, HF hellfire), all eight directions, wired in BGCLASS with `pose stand|shoot <SPR> ... weapon <name>`. The swordsman stands with the sword (`X1FI`); her attack and parry keep `XSHT` / `XPAR`. Running and jumping are still one set per class.
- **Lobby (waiting room)**: the waiting message moved below LEAVE; only the player's own class portrait is drawn (the portraits are hi-res, four of them covered the screen).

## 0.10.20

- **Parried missiles now go where the parrier aims.** The parry itself set the right heading, but a fast missile (the fireball) is moved in several sub-steps per tic and the sub-steps left after the parry still used the old heading, so the missile was carried sideways before turning. `P_XYMovement` now drops the rest of the old movement when a missile was redirected (`bg_redirected`). The live test has a three-player case (witch fires, swordsman parries, soldier is hit) at three guard angles.
- **Thunderclap (snap's secondary, hold alt on slot 7) costs 20 spirit, not 25.** A found bfg gives 20 spirit, so it could never be fired; with nothing else it failed silently. It also says "Not enough ammo for the secondary fire" once per press now.
- **Snap flame** stays on the target's body (no `A_Fire` slide to in front of where she faces) and is drawn 20 units toward whoever is looking (`bg_ViewShift`, software and GL renderers), so it is in front for every viewer, including each split-screen view.
- **Game Setup** shows the host's own body beside the mugshots, like the waiting room.

## 0.10.21

- **Thunderclap (slot 7 secondary) rebuilt.** No plasma ball, no ray, no projectile. Pressing the alt button starts a lead-up (three first-person frames of the hands drawn slowly apart, sparks between them, 6 tics each); on the clap (tic 18) `bg_ClapBlast` walks the things in the level and hurts every shootable one that is within 1280 units, within 55 degrees either side of where the player is facing at that instant, and in her line of sight (right beside her counts from any side of the front). So it follows her turning and cannot get lost in a big map. Damage is 80-120, falling to 60% at the edge of the range. Each target is thrown away along the line from her to it (`BG_CLAP_BACK` 1500 / mass, plus a little lift), so the blast spreads as a ring. Players are only hit with friendly fire on.
- **Recoil:** the clapper is thrown backwards: soldier 100%, swordsman 75%, witch 45% of hellfire's kick.
- **Cost 25, found weapon gives 30 spirit** (`bg_WeaponPickupClips`: the slot 7 weapon gives one and a half times the usual clips, so a found one is 30, a dropped one 10).
- **Art:** first person BFGG now has six frames (A clasped hands in prayer = ready, B-D hands apart with sparks, E the clap and rings, F after); the third person soldier/witch thunderclap shoot pose has five frames at 6 tics (`CLAP_FRAMES`), and the idle pose is the clasped hands. Wad changed.

## 0.10.22

- **Thunderclap lead-up doubled:** 36 tics (was 18). First person: six frames of 6 tics with the hands parting a little more each time (BFGG B-G), then the clap (H) and the aftermath (I). Third person: the lead-up frames are 12 tics each (`pose shoot ... 5 12`), the clap lands on tic 36.
- **Recoil and view shake (`bg_WeaponKick`)**, called once per shot from `P_SubtractAmmo` and `A_ConsumeAmmo`, keyed on the weapon being fired: chaingun secondary pushes back on every round and jitters the view by 0.25 degree, alternately left and right (about a pixel); scattergun (secondary) a heavy shove and the same shake at the same strength per shot; rifle a small push and half a shake; revolver (pistol's secondary) a small push; fireball a light push and a hint of shake. The witch is pushed 160% as much as the soldier. The shake alternates, so the aim does not drift.
- **Slot 5 swapped:** the devil's breath is the primary and the fireball the secondary (`slot 5 primary plasma alt missile`). The launcher pickup gives the slot's primary; the witch starts with the devil's breath (`weapons fist pistol plasma`).
- **Fireball lead-up:** 18 tics (the orb grows between the hands, MISG B-D), then the cast (E) with `A_BGFireball` and a light kick, then F. Third person: five frames of 6 tics.

## 0.10.23

- **Parry aim cone widened.** From a recording: the parried fireball left exactly along the screen centre because nobody was within 10 degrees of the crosshair (the other player stood about 35 degrees off). A parry now looks for a target within 35 degrees either side of the crosshair among everyone except the shooter (nearest to the crosshair wins); only if there is none does it fall back to the old 10 degree cone, which includes the shooter, and then to straight ahead. `BG_PARRY_AIM_WIDE` / `BG_PARRY_AIM_NARROW`. Players are still only candidates with friendly fire on. New test round: the other player 30 degrees off, the shooter 20 degrees off.
- **Level-end screen (coop and deathmatch):** every player gets their own class face (`<prefix>ST00`, `<prefix>DEAD0` in deathmatch) instead of one star for the local player over empty boxes, and their name is written beside the face in their lobby colour. Split-screen / non-network games show "PLAYER n".
- **Performance log:** the log now has GL_VENDOR / GL_RENDERER / GL_VERSION, and every ten seconds of play a line `PERF: <fps> fps average, worst frame <ms>, <software|OpenGL>, <n> textures built (<mb> MB)`. The default video mode is the software renderer at 1280x720, which is CPU bound.
- **Not changed, to remember:** cheat codes still work in network games and desync them (client-local); remove or gate them before release.

## 0.10.24

Swordsman parry now sends a missile exactly where the crosshair points.

- Cause: the parried missile restarted from where it touched the swordsman's collision box (on the shooter's side), and the parry knockback moved the swordsman as well, so a heading that was already correct landed well off the crosshair.
- Now the missile restarts on the swordsman's own aim line (centre, chest height) and flies along it, following freelook pitch. `bg_PlaceRedirected` applies the start position after the first successful move (`bg_rx/ry/rz`, `bg_redirected` in p_mobj).
- Autoaim is monsters only, within 8 degrees of the crosshair. Players are never auto-targeted, even with friendly fire on. The old 35 degree cone is gone.
- Live test: single-player flight is within 1/4 degree and 2 units of the crosshair line; the three-player test hits the other player only when she stands on the line (splash does not count).

## 0.10.25

Parry knockback no longer slides the crosshair off the line the ball left on.

- Cause (found from the 21:37 video): a parried blow shoved the swordsman along the direction the blow was travelling. With the guard turned away from the shooter that is a sideways shove of tens of units, so the crosshair line moved sideways while the ball kept the original line, and the ball looked several degrees off. The 0.10.24 launch was already exact.
- Now the shove is straight back from where she looks (missiles and hitscan alike), so she slides back along her own aim line and the ball keeps flying along the crosshair.
- Live test: after each parry the player's sideways drift from the aim line is within 1 unit.

## 0.10.26

Parried fireballs now stay on the crosshair for the first third of a second, and every parry is logged.

- Measured from the 2026-10-12 09:41 video: the ball leaves exactly on the crosshair (it starts inside the reticle), but the view then turns a couple of degrees while the ball flies, so by the time it lands the reticle is no longer where it was at the parry.
- `bg_GuideMissile` (called from `P_MobjThinker` before the move): for `BG_PARRY_GUIDE_TICS` (12) tics after a parry with no monster under the crosshair, every tic the ball steers to the point on the owner's *current* aim line that is as far out as the ball is. Pitch follows mouse look. Monster autoaim parries are not guided (they already go exactly to the monster). Players are never auto-targeted. After 12 tics it flies straight.
- Fields: `bg_guide`, `bg_guide_start` in p_mobj.h.
- begotten.log now has `PARRY: ball turned at tic N, facing, pitch, ...` at each parry and `PARRY: ball released, her aim turned X degrees since the parry` when the guidance ends.
- Live test: a 10 degree flick two tics after the parry is followed within 1 degree and 0 units to the side.

## 0.10.27

Lights for the software renderer (first version: point, spot and tube lights, player lantern).

- New module `dsda/bglight.c/.h`. A light makes the brightness table (colormap) a wall column, floor row or sprite would get brighter by a fraction that depends on the distance to the light. Walls are drawn in vertical runs of equal brightness and floors in horizontal ones, found by sampling every 8 pixels and refining where the step changes, so a small light low on a tall wall lights a rounded patch, not a strip.
- Hooks: `R_RenderSegLoop` and `R_RenderMaskedSegRange` (r_segs.c), `R_MapPlane` (r_plane.c), `R_ProjectSprite` (r_things.c), `R_RenderPlayerView` (r_main.c, collects the lights each frame). Software mode only; OpenGL mode ignores lights. Nothing here changes the game state, so lockstep play is unaffected.
- A light reaches only the sectors it can flood into through open two-sided lines (a closed door has no opening), and a wall is lit only on the side facing the light. Up to 8 lights (the nearest) per frame.
- Light things (map editor numbers): 20101 torch (flickers), 20102 lamp, 20103 spot (ceiling, cone 25/50 degrees, pointing down), 20104 strip lamp (tube 128 long along the thing's angle, hangs from the ceiling, wide cone down). The thing's angle sets the cone direction (yaw) and the strip's axis. Heights are above the floor (or below the ceiling with `ceiling`).
- `BGLIGHT` lump (optional, in any wad): `light <number> <name> radius R strength S height H [ceiling] [flicker|pulse] [spot INNER OUTER] [pitch DEG] [tube LENGTH]` and `lantern radius R strength S height H`. A later definition replaces an earlier one with the same number.
- Player lantern: one per player, on by default; menu option "Player Lantern" (config `begotten_lantern`).
- Not done yet: coloured lights, dithering between brightness steps, lighting the first-person weapon, OpenGL.
- Test: `BEGOTTEN_SELFTEST=7` opens the dark room with every light kind (env `BGLIGHT_BASE`, `BGLIGHT_ONLY`, `BGLIGHT_FWD/SIDE`, `BGLIGHT_NOLANTERN`). The live test (4) checks the falloff, facing, cone and tube shapes.

## 0.10.29 — dithered lighting

Light brightness is no longer rounded to the nearest colormap step per run. The exact brightness is sampled every 8 px and interpolated per pixel; each pixel is then rounded up or down by an 8x8 Bayer ordered-dither threshold, for both the colormap row and the colour-tint strength. Gradients (light falloff, spot cones) blend without banding and get a gritty, retro grain. Colour (which of the 125 tints) is still chosen without dither.

Menu: Setup > Compatibility > "Dither Lighting" (default on in 0.10.29; off from 0.10.31, see there). Off rounds to the nearest step as in 0.10.28. Self-test 7 accepts `BGLIGHT_DITHER=0|1`. Sprites are not dithered (one colormap per sprite).

## 0.10.30 — dynamic lights

Lights that follow the action, collected every frame like the placed lights (so they move smoothly with view interpolation):

- **Projectiles**: every missile in flight carries a light. Its colour is taken from its own sprite (the bright, saturated pixels of the first frame, mixed; cached per thing type), so fireballs glow orange, plasma blue and so on with no per-weapon setup.
- **Explosions**: a projectile that has hit (its death frames) glows brighter and wider, fading over the frames.
- **Muzzle flash**: while a player's flash sprite (`ps_flash`) is showing, a warm light sits just in front of them.

BGLIGHT lump lines (all optional, same keywords as `lantern`):

    missile   radius 230 strength 0.85 color auto
    explosion radius 420 strength 1.0  color auto
    muzzle    radius 340 strength 0.9  height 0 color warm

`color auto` uses the sprite colour; a colour name forces one. (0.10.31 removed the "Dynamic Lights" toggle: they are always on.) The frame now keeps the 12 lights nearest the view (was 8); beyond that the farthest are dropped.

Self-test 7: `BGLIGHT_DYN=1` (frozen projectiles and an explosion), `BGLIGHT_FLASH=1` (held muzzle flash). The live test also checks projectile, explosion and muzzle lights, and no longer removes a test monster that was already gone (that latent bug crashed once memory got reused).

## 0.10.31 — lighting follow-ups

- **Gun flashes no longer brighten the sector.** In Begotten, `A_Light1/2` and `A_FireOldBFG` do not raise `extralight` any more; the muzzle light does the job.
- **Dynamic lights are always on** (config `begotten_dynamic_lights` and its menu entry removed).
- **Dithering is off by default.** The config key is now `begotten_dither` (the old `begotten_light_dither` is ignored, so earlier saved "on" values do not carry over). Menu: "Dither Lighting (Retro Look)".
- **Flicker controls** in BGLIGHT: `flicker [rate N] [depth D]`, `pulse [period S] [depth D]` (see LIGHTING.md). Defaults match the old look.
- **Per-thing tuning in the editor**: a placed light's args set its radius (arg0, units of 8), strength (arg1, percent) and colour (arg2-4 = R, G, B).
- **Faster**: brightness is sampled every 16 px instead of 8 (it is interpolated between samples, so the picture is the same). A five-light test room went from ~84 to ~103 fps, with dither ~67 to ~82, on the cloud test VM (relative numbers only).
- New: `LIGHTING.md` (mapper/modder guide) and the Doom Builder configuration (`Begotten_lights.cfg`).

## 0.10.32 — baked stationary lights with true shadows

- **Stationary lights** (20101-20108 and any BGLIGHT `light` without `movable`) are baked at level start (the first frame after `P_SetupLevel`, `bg_LightsNewLevel` / `bg_LightsBake`): a grid over the floor and one over the ceiling around each light (8-unit spacing), plus a grid per wall seg it reaches (8 units along and up). Every grid sample is the light's contribution (distance, facing, cone, strength) after a 3D line-of-sight test against the map's lines: one-sided lines block, two-sided lines block outside their opening (steps, lowered ceilings, closed doors). Middle textures do not block. So pillars, walls and steps cast shadows.
- In the frame only a bilinear lookup remains, times the light's flicker/pulse. The mobj of a baked light is still watched: if the thing is removed, the light stops (breakable lamps).
- **Movable lights** (`movable` flag; built-in twins 20201-20208) are evaluated every frame as before (sector flood, no shadows). Projectile, explosion, muzzle and lantern lights are always movable. Lights spawned after the bake (tests, scripts) are movable until the next level.
- Sprites are lit by baked lights with the distance formula, but only where the light's floor grid reaches them (so things in a shadow stay dark).
- **Warnings** (`bg_BakeWarnings`, Hexen/UDMF maps): a log line for each stationary light inside or near a sector moved by a Door_/Floor_/Plat_/Ceiling_/Generic_/Elevator_ special.
- `BGLIGHT_NOLANTERN=1` now works in any map; `tools/make_light_test_map.py` builds a pillar-room test PWAD (`HEXEN=1` writes a Hexen-format map, but in my one attempt the map loader crashed in `P_CalculateLineDefProperties`; I did not investigate, it is probably my test file, so treat that option as unfinished).
- Performance: a lookup costs about what the old calculation did, so a baked scene ran at about the same speed as the same lights live in the test room (~100 fps either way on the cloud VM). The gain is the shadows.
- Tests: shadow rays on made-up geometry (pillar, step, closed door, low ceiling), a real bake, thing removal, movable twin, the door warning.

## 0.10.33

Windows (MSVC) build fixes; no gameplay changes.

* `bglight.c`: `<strings.h>` is only included outside MSVC; a variable named `far` (a macro on Windows) is now `farthest`.
* `begotten.c`: same `far` rename in the hellfire test; added the missing declarations for `G_PlayerReborn`, `R_SmoothPlaying_Reset`, `dsda_AddSpriteName` and `P_GiveWeapon`; fixed an int-to-short overflow in the self-test (a stun of 100000 did not fit a `short` and wrapped negative) and a pointer-difference-to-int warning.
* Not compiled with MSVC here: the fixes follow the reported errors.

## 0.10.34

The Windows link error, a fix for baked lights, power-up lights and the MAP01 showcase map.

* `strtok_r` (not in MSVC) now maps to `strtok_s` on Windows: this was the link error in the 0.10.33 Windows log.
* **Bug fix (present since 0.10.32): baked lights never showed in play.** The per-frame setup of the baked lights was skipped
  whenever any live light was in the frame, and the player lantern is a live light, so with the lantern on (the
  default) stationary lights did nothing. Earlier checks ran with the lantern off and missed it. Fixed.
* **Power-up lights:** floor power-ups glow with a slow pulse in their sprite's colour (BGLIGHT command `powerup`).
* **MAP01 is now the lighting showcase** ("Gallery of Lights", UDMF, `tools/make_showcase_map.py`; the finished map is
  `tools/data/MAP01_showcase.wad` and `make_placeholder_content.py` copies it in).
* Developer aids: env `BEGOTTEN_TELEPORT="x y degrees"` starts player 1 there, `BEGOTTEN_USE=tic` presses use then.
* The self-tests (`BEGOTTEN_SELFTEST=4` and the others) assume the plain placeholder room, so run them with `-warp 2`, no longer MAP01.

## 0.10.35

* **Puff lights:** bullet puffs and chainsaw sparks give off a small warm flash (BGLIGHT command `puff`).

## 0.10.36

* **Flashlight:** the light amplification pickup (still `pw_infrared`, same duration) is now a spot light along the
  player's view instead of the full-bright screen (BGLIGHT command `flashlight`, default radius 760, cone 13/32
  degrees, cool white). The old full-bright colormap is off whenever Begotten's rules are on. Its pickup text is the
  `GOTVISOR` string, rename it in DEHACKED if "Light Amplification Visor" is not what you want.
* Developer aids: `BEGOTTEN_TELEPORT` takes a fourth value (view pitch, turns free look on); `BEGOTTEN_POWER_INFRARED=tics`
  gives player 1 the flashlight.
* Known: the live test's "pierce" checks fail on roughly half of the runs (the first target is placed by a search that
  sometimes finds no spot); not caused by the lights, not fixed yet.

## 0.10.37

* **Fog:** BGLIGHT command `fog` (per level; colour, density, scatter, max), menu toggle "Fog (Where a Level Has It)"
  (`begotten_fog`, on by default). MAP01 (the Gallery of Lights) has a thin haze, written by `make_placeholder_content.py`
  into `data/BGLIGHT.txt`. See LIGHTING.md, "Fog". The lighting draw key became 64 bits to carry the haze.
* **Look limits:** looking fully up or down now stops at the limit and stays there instead of bouncing. The limit is
  applied where queued look commands are applied (`P_HandleExCmdLook`) rather than by clamping the per-tic delta in
  `g_game.c`, which let the accumulated angle overshoot and wrap. Checked by the live test; not reproduced on screen
  before the change, so it was found by reading the code.
* Fix: baked stationary lights were skipped in play whenever any live light existed (the frame prepared them only
  otherwise); they are now always prepared.
* The live test (`BEGOTTEN_SELFTEST=4`) gained a fog-curve check.

## 0.10.38

* **Sun:** BGLIGHT command `sun` (per level: angle, elevation, color, strength): a baked directional light with shadows;
  sky-ceilinged sectors are open to it, windows and doorways let it in through their openings. Walls facing it are lit.
  See LIGHTING.md, "Sun and moon". It is baked as one more stationary light (`bg_BakeSun`), so frame cost is a lookup.
* **Gallery of Lights** has a sun court (north of the first hall) with a room whose window and doorway let patches of sun
  onto the floor. `make_placeholder_content.py` writes the `sun` line into `data/BGLIGHT.txt`.
* The live test gained a sun-ray check on made-up geometry (windows, sill, lintel, solid ceiling, sun behind a wall).
* The live test's pierce failure message now prints the three targets' positions and flags.

## 0.10.39

* **Glows round lights:** BGLIGHT option `halo SIZE` on `light`, `missile`, `explosion`, `powerup`; built-in lights have
  them. Made as vissprites (`bg_halo` in `vissprite_t`, `bg_ProjectHalos` in `r_things.c`, drawn by `bg_DrawHalo`), so
  the engine's own sorting and clipping hide them behind walls and things. Menu: "Glow Around Lights" (`begotten_halo`).
* **Fog per sector:** `fog ... sector TAG` makes a fog zone for the sectors with that tag (up to 15 per level); the level's
  own fog applies to the others. `density 0` is a clear zone. The fog colour went into the colormap key (bits 32-35).
  A surface takes the thicker of its own zone's fog and the viewer's. Gallery of Lights: the Chamber of Colour (tag 5) is
  thick and violet, the Sun court (tag 6) is clear.
* The live test's fog check also covers the zone choice.

## 0.10.40

* **Coronas:** BGLIGHT options `corona PIXELS` and `coronafade NEAR FAR` on `light`, `missile`, `explosion`, `powerup`:
  a procedurally drawn star of fixed screen size that fades out close up. It shares the glow's machinery (`bg_HaloAdd`
  with a corona flag, vissprite sorting and clipping). Menu: "Coronas On Lights" (`begotten_corona`). Built-in
  torches, lamps, candles, coloured lights and missiles have one.

## 0.10.41

* **Checkpoint line:** line special **174** (`zl_bg_checkpoint`), activated by a player crossing it (set it to "player crosses",
  once or repeatable, in the map editor; Hexen-format and UDMF maps). Playing alone it writes a save named
  `checkpoint-<MAP>` (one rotating slot per map, shown as "checkpoint" on screen). In any multiplayer game (split-screen
  included) it does not save: it is the hook for the ticket rules (phase 2 of the co-op work). The save is queued and
  written between tics (`bg_Checkpoint` / `bg_CheckpointUpdate`), never from inside the line special. Not yet available
  as a Doom-format (binary Doom) special.
* **Use after death reloads the last save** when playing alone (`dsda_death_use_action` now defaults to 2, "reload"; the
  menu can still set it to default/nothing). If nothing has been saved yet, or in a multiplayer game, it respawns as before.
  The last save is whichever came last: quick save (F6), autosave or checkpoint.
* **Saves audited:** quick save/load already worked. New tests: `BEGOTTEN_SELFTEST=8` saves and loads through the real
  save file and compares a hash of the whole world (players, every thing, RNG, time) before and after; with
  `BEGOTTEN_LOCALPLAYERS=2` it does so for a split-screen game and checks the second player and the split view survive.
  The live test (`BEGOTTEN_SELFTEST=4`) also checks that the in-memory key frame restores exactly and replays identically.

## 0.10.42

Co-op respawn rules, phase 2 (`dsda/bgcoop.c`). They apply to any game with two or more players (split-screen or network),
never in single player or deathmatch.

* **Difficulty 1-3:** a dead player who presses Use comes back with health reset to the class's, the weapons, ammo and
  armor as they were, and no active power-ups. No rucksack, no tickets, unlimited deaths. A checkpoint line does nothing.
* **Difficulty 4-5:** one shared ticket pool. Each respawn costs a ticket (no tickets: Use does nothing and says so).
  Tickets are granted: one per player at every level start; one per player when a checkpoint line (special 174) is crossed
  (the line is then used up); one per player when a boss dies (`bg_IsBoss`: the Mecha, the Giant beetle, or anything with
  `MF2_BOSS`, or any monster the level marks as a boss: thing TID 30000-30999, the "Tag" field in the editor, in Hexen-format
  and UDMF maps; see 0.10.45). The pool is capped at 999 and carries over between levels.
* **Rucksack (4-5):** a dying player leaves a rucksack with all their ammo (a backpack sprite stands in for it).
  They respawn with their weapons but only the class's starting ammo; touching the rucksack gives the rest back, up to
  their capacity (what does not fit stays in it). On difficulty 4 only the owner can take it, and others see it translucent;
  on difficulty 5 anyone can.
* **Everyone dead and no tickets:** the level starts over for everybody from scratch (no weapons, no keys, no items), and
  the new level's tickets are granted.
* A player who is dead when the level ends comes back at the next level without paying a ticket, with the same carry-over.
* **Ticket counter:** a small gold number at the top right of the face on the status bar; in split-screen games a gold
  number at the bottom right of the first view's strip (on a 3 or 4 player screen the strips lose the name so it fits).
  Shown only on difficulty 4-5 in a multiplayer game. It uses the menu font.
* Saved with the game and part of the state hash. New mobj fields `bg_owner` and `bg_pack` (rucksack).
* Tests: `BEGOTTEN_SELFTEST=9 BEGOTTEN_LOCALPLAYERS=2` with `-skill 2`, `4` or `5` plays the rules through (dying,
  respawning, the rucksack, the 999 cap, checkpoint, no-ticket refusal, the wipe). Also run through two real instances
  over the network with `BEGOTTEN_NETTEST`. Developer aid: `BEGOTTEN_HUDTEST=<n>` shows a made-up ticket count.
* Not yet: the 5-second death screen followed by forced spectating (phase 3), joining mid-match (phase 4), per-player
  completion tracking.

## 0.10.43

Spectating, phase 3 of the co-op rules (`bg_SpectateList` in `dsda/bgcoop.c`).

* **Death screen:** a dead player sees their own cadaver view for 5 seconds (`BG_DEATH_SCREEN_TICS`) with a banner:
  "YOU DIED" and either "PRESS USE TO RESPAWN" (with the ticket count on difficulty 4-5) or the no-tickets notice.
  Pressing Use at any time before or after respawns them, as before.
* **Forced spectating:** once every player on a machine has been dead for 5 seconds, that machine watches the living
  players of the other machines until someone on it respawns. Use still respawns (or says there are no tickets).
  Display only: nothing here touches the simulation, so it cannot desync.
* **Split view:** it uses the split-screen renderer. One view per living player (1 to 3, laid out like a 1 to 3 player
  split screen), each with a strip with the player's name, health, armor and ammo; the views shrink as those players
  die. The gold ticket count stays at the bottom right of the first view. With nobody left alive to watch, the
  cadaver view returns.
* **Games on one machine** (split-screen only) never spectate: everyone is already looking at the same screen. A dead
  local player's view shows "DEAD - PRESS USE" as before. In a network game with several local players on a machine,
  the machine spectates only when all of them are dead.
* Sound and the on-screen palette follow the first watched player (`displayplayer`).
* Test: `BEGOTTEN_SELFTEST=S` in a three-player network game over two instances (host `BEGOTTEN_NETTEST=host:3`, joiner
  `join:<addr>` with `BEGOTTEN_NETTEST_LOCAL=2`; both with `-nomonsters`). `BEGOTTEN_SPECTEST_HOLD=1` pauses at each
  stage for a screenshot.

## 0.10.44

Joining a network game that is already running (phase 4 of the co-op rules), the automap background, and fixes found on the way.

* **Mid-match join.** A machine that "Joins" a host whose game has started is no longer refused. It becomes a
  **spectator** (the split view of the living players, "SPECTATING / PRESS USE TO JOIN AS <CLASS>", strafe left or
  right to change character), and pressing Use asks the host to put its player into the game. The player appears at a
  start point through the normal reborn. Entry is free on every difficulty (no ticket is spent). One player per machine.
* **How it works** (`dsda/bgnet.c`, search "Joining a game in progress"): still lockstep, and no command log is replayed.
  1. *Attach (the join barrier).* The host accepts the machine (`PKT_HELLO`), picks a free slot and a tic H (its command
     counter). Every machine stops at tic H for a moment (a gold "X IS JOINING..." line); the host stores a snapshot of
     the whole game at H (`dsda_StoreSnapshot`, a key frame without the demo parts), compresses it (zlib) and sends it in
     1000-byte pieces (`PKT_LATE`, `PKT_SNAP`, `PKT_SNAPACK`, go-back-N). The new machine starts a placeholder level, puts
     the snapshot in (`bgnet_LateTick`, state `BGNET_LATE`), sets its counters to H and sends its first `PKT_TICS`: the host
     lifts the barrier (`att_rel`). The whole stop is a fraction of a second on a LAN (a snapshot is about 5 KB compressed
     on the gallery map). If anything goes wrong (the joiner goes away, 40 seconds pass, the level ends at that moment) the
     barrier is lifted and the game goes on without it; the joiner is told why.
  2. *Spawn.* The observer's machine sends `PKT_JOINREQ` (the character); the host announces a tic J (its command counter);
     at J every machine runs `bg_CoopJoin` in `bgnet_PreTic`: the slot becomes a player and is reborn. From J on, the
     machines wait for that player's commands (`bgnet_ReadyTics`).
  The three events (attach, spawn, drop) are announced by the host in every `PKT_TICS`; a machine adopts a value only
  if it is newer than its own, so a slot can be reused after its player left. `PKT_VERSION` 6, `BGNET_SIM_REV` 12.
* **Completion.** Finishing a level counts (level table, so the secret class) only for players who were there when it
  began and still are: `bg_CoopCompletionCounts()` is false for a player who joined mid-level (`joined_late`, cleared
  at every level start) and for an observer. A player who joins between levels (the intermission) is simply a player
  of the next level.
* **Fixes.** Respawning on a map with one player start no longer errors (`G_RespawnStart`: players 2-4 use the first
  real start); a binary map after a UDMF map was read in the wrong format (`udmf_namespace` was never reset, so the
  next Doom-format map failed with "references a non-existent linedef"); in a network game a character that is locked
  on this machine but chosen by another player is no longer replaced by the first one (it made machines disagree);
  `bg_SplitNewGame` leaves the players of a running network game alone; the automap background is a dark green
  (palette index 255 is reserved for it in every theme; before, the stock default was a bright pink in this game's
  palette). Players who are not in the game yet have no body: the renderer, `P_SetupLevel` and `G_DoReborn` cope.
* Test (`tools/test_latejoin.sh`): host + client + late joiner (`BEGOTTEN_NETTEST=late:<addr>`, `_LATEMS` wall-clock
  delay, `_JOINAT` first tic at which the observer presses Use); all three must print the same hash and no desync.
  Also `BEGOTTEN_NETTEST_EXIT=<tic>` (everybody leaves the level), `_LEAVE=<tic>` (an observer goes away) and a second
  late joiner taking the freed slot.

## 0.10.45

* **Boss marking in level data.** A monster whose thing TID (editor "Tag"/"ID") is 30000-30999 is a boss for the ticket
  rules, whatever its type: one ticket per player when it dies. The Mecha, the Giant beetle and `MF2_BOSS` types still
  count without a tag. Only Hexen-format and UDMF maps carry TIDs; plain Doom-format maps have no way to mark one.
  The tag only affects tickets (no other boss behaviour: damage immunity and the like stay with the type). Use
  30001, 30002, ... for different bosses if a script needs to find them; the whole range counts.
* Each boss death logs `BGBOSS: a boss died (monster type N, TID N)` with whether tickets were on.
* Developer check: `BOSSTEST=1 python3 tools/make_showcase_map.py out.wad <zdbsp>` writes a MAP01 with one tagged and one
  untagged trooper at the player starts (never part of the shipped wad). Two-instance run: both machines logged the one
  tagged death, identical state hashes, 0 desyncs.

## 0.10.46

* **The piercing shot was fine; its test was not.** Two test faults, no change to the game:
  1. The first shot's knock-back pushed the middle target sideways, and a hitscan only meets things whose centre lies in a
     map block the line crosses (Doom links a thing into a single block). When the line ran along a block edge the
     middle target was missed and "stops after the hit limit" failed. Every pierce shot now starts with the three targets
     put back exactly on the line.
  2. The target search (`bg_SpawnTarget`) multiplied the angle before halving it, which overflowed and tried only half the
     circle.
* Each roster section of the live test (2-8) now puts the player back in the middle of the room first, so an earlier
  section cannot leave him at a wall.
* Run the live test with `-warp 2` (the plain 512 x 512 room). On MAP01 (the Gallery of Lights) the middle of the biggest
  sector is inside a pillar, so nothing can see a target; that is a property of the showcase map, not a bug.
* Pierce checks: 12 of 12 runs pass. Still occasionally failing (about 4 runs in 10, not related to piercing): the Aegis
  raising a corpse and the "boosted monster closes twice as far" check; both depend on where the random corpse and monster
  land. Not fixed.

### Editor bundle and manual (same release)

* `udb/` is now complete in the source tree (the lights file `Includes/Begotten_lights.cfg` was only in the earlier zips).
  New in it: line action 174 (checkpoint), the 14 items and the weapon pickups under their Begotten names, the boss-tag note
  in the Bosses category, `examples/MAP01_showcase.wad` and `examples/BGLIGHT_example.txt`. Not tested inside UDB.
* `docs/Begotten_ACS_Manual.html` is the ACS manual with a new Part 4, "Begotten in the editor" (setup, things, lights,
  BGLIGHT, fog, sun, glows and coronas, checkpoints, boss tags, co-op rules, music lines), and Appendix A (Thing_Spawn
  numbers) with the new monster and item names. The numbers did not change.

## 0.10.47 - weather

* New BGLIGHT command `weather map MAPxx rain|snow|none density D wind W thunder SECONDS` (see LIGHTING.md, "Weather").
  Rain and snow are specks made round the viewer (up to 600, about 200 live) that fall only through sky-ceiling sectors and
  die at the floor, at a roof or when they leave the area; each is a vissprite (`r_defs.h` `bg_weather`, `r_things.c`
  `bg_ProjectHalos`), drawn through the palette blend of the halos, so walls and things hide them. Drawing cost measured
  at 0.017 ms a frame.
* Thunder: a lightning flash (`bg_WeatherFlashLevel` feeds `bg_PaletteIndex` the blue palettes in a 14-tic flicker) and the
  sound `DSTHUNDR` 0.4-2.4 s later; `DSRAIN` plays every 70 tics while the player is under open sky. The placeholder wad
  has generated noise for both (`weather_wavs` in `make_placeholder_content.py`); MAP01 of the placeholder wad has rain
  and thunder in its sun court.
* Cosmetic only: own xorshift random numbers, nothing on the game state, called from `dsda_UpdateAutoSaves` (once per
  unpaused tic, so not in demos with `-timedemo`). Option "Rain, Snow And Lightning" (`begotten_weather`, default on).
* Checked in the running game: rain with walls hiding the drops, the lightning flash (screen brightness rose by half
  for the flash tics), snow; `BEGOTTEN_WXLOG=1` logs specks and cost, `BEGOTTEN_WXFIRST=<tic>` forces the first strike.
  Not checked: the Windows build, the sounds (the placeholder ones are noise, and the test ran without audio), split-screen,
  net play (nothing in it touches the network or the game state).



## 0.10.55 - fog is lit like everything else

* The fog colour used to be added at full strength whatever the light, so in a dark room the far surfaces faded to a blue-grey brighter than the near ones (the haze glowed by itself). Now the haze colour is scaled by the sector's light level: `am = dark + (1 - dark) * lightlevel / 255` (`bg_level_t.am`, a 4-bit step in the fog key, bits 36-39; `bg_FogTable` uses `zc * amb`). The lights' own scattering (`scatter`, `lv`) is unchanged. New fog option `dark F` (default 0.1; 1 = the old behaviour).
* The sector light is the sector's `lightlevel`, not the distance-dimmed colormap row (that one would also darken the haze with distance, which is the opposite of fog). Walls use their sector, flats the plane's light (`bg_LightSetPlane(height, lightlevel)`), sprites their sector.
* Tested here: a dark room (light 24) with fog and a red lamp, before and after: the far wall goes to near black; a lit room (light 192): about the same as before.
* Fog colours chosen for a dark map now show less. Set `dark 1` on a fog line to get the old look back.

## 0.10.54 - dark colours no longer go grey or green

* Every coloured light and every fog table matches a computed colour back to the palette through a lookup cube (`nearest_cube`). Two faults showed in the dark: the cube had 32 cells of 8 units a side, so dark colours (all channels under about 40) collapsed into a few cells and lost their hue (grey); and the search covered index 255, the spare dark green of every theme (12 40 22 in the crow palette), which about one in six dark cells picked (green). Now: 40 cells a side with edges at 255 * (l/40)^2 (about 3 units wide near black, 13 near white; `cube_q`, `BG_CUBE_AT`, `bg_Nearest`), and index 255 is never handed out. All four users (tint tables, fog tables, halos, weather) go through `bg_Nearest`.
* The cube is rebuilt whenever the palette changes (damage and pickup flashes): 64,000 cells instead of 32,000, so about twice as long as before; not timed on slow machines.
* Tested here: a dark room with fog colour 34 40 54 and a red lamp, old against new: the teal-green cast on the dark walls is gone.

## 0.10.53 - flashlight column fix, faster gyros with a glow

* **Wall flicker with the flashlight**: the world point of a wall column was worked out from the wall's rounded fixed-point `scale`, which jitters from column to column (a lot at a shallow angle), so the shadow ray of a column next to a shadow edge came out lit or dark at random: single-pixel stripes. `bg_ColumnPoint` now intersects the view ray with the wall's own line (`wall.ax/ay/dx/dy`), exact, with a fallback to the old value when the two disagree. It also sharpens fog distance on walls. Not reproduced here in my own test room (it showed no stripes either way), so this is the likely cause from the video, not a confirmed one.
* **Gyro speed**: default `spin 480` (was 180). A placed gyro's first thing argument sets the speed in tens of degrees a second (0 = default 480); for lights that spin, arg0 is no longer the radius (the BGLIGHT line sets it).
* **`glow RADIUS STRENGTH`** in a BGLIGHT `light` line: a second, round light at the same spot (placed-light path, so the light has to be movable). The gyros have `glow 320 0.6`, the small red (amber, blue) pool round the lamp. Each gyro now uses two of the 12 live-light slots.
* Editor config: gyro things' arg0 is "Turn speed".

## 0.10.52 - Windows build fix

* `near` is a macro in the Windows headers: a local called `near` in `bg_CollectOccluders` broke the MSVC build of 0.10.51 (renamed `in_reach`). Do not use `near`, `far`, `min`, `max` as local names in engine code.

## 0.10.51 - things cast shadows

* Solid things block the beam of shadow-casting live lights: `bg_CollectOccluders` (once a frame, every `P_MobjThinker` mobj that is MF_SOLID and not a missile, corpse or no-clip, within a shadow light's radius; at most `BG_MAXOCC` = 48) and `bg_ThingBlocks` (segment against an upright cylinder: radius, z..z+height) called from `bg_ShadowLit` after the wall rays. A thing the light is inside (the player's own body) or the target stands on never blocks. Sprites lit by the beam get the same test, so a monster behind another is dark.
* Menu "Monsters Block Flashlight" (`begotten_thing_shadows`); BGLIGHT `thingshadows off` (whole wad). Developer switch `BGLIGHT_NOTHINGSHADOW=1`.
* Shapes are cylinders, not sprite outlines. Cost measured in a small room with the flashlight on: 24 barrels in the beam added about 1.7 ms a frame to the 1.6 ms the wall rays take. Not measured with real monsters moving, nor on the Windows laptop.

## 0.10.50 - flashlight shadows

* **The flashlight casts shadows.** Live (movable) lights can now carry a `shadow` flag (`bg_light_t.shadow`, `bg_lightdef_t.shadow`): after `bg_Contribution` says a point gets light, `bg_AccLive` traces a ray from the light to the point (`bg_ShadowLit`, using `bg_Los`) through one level-wide line grid (`world_los`, built on the first frame that needs it, rebuilt per level by `level_serial`). The rays read the sectors' live heights, so doors and lifts change the shadows as they move. Same blocking rules as the baked lights (one-sided lines, steps, lowered ceilings, closed doors; middle textures do not block).
* The flashlight has it by default, switchable with the menu option "Flashlight Shadows" (`begotten_flashlight_shadows`) or `flashlight noshadow` in BGLIGHT. Other live types take `shadow` (lantern, missile...), and a `light` line takes `shadow` together with `movable`. Developer switch: `BGLIGHT_NOSHADOW=1`; `BEGOTTEN_FLASH=1` keeps the flashlight on.
* **Beam origin offset**: with shadows on the flashlight starts `side` units right of and `drop` below the eye (`flashlight offset 10 8`; `0 0` = eye). A light at the eye shows exactly what the eye sees, so each shadow would be hidden behind its caster. When the offset point is not reachable from the eye (a wall in between) the eye position is used.
* Sampling: `bg_Runs` samples every 16 pixels as before, but with a shadow light in play a piece whose brightness jumps by more than `BG_REFINE_JUMP` rows is split in halves down to 2 pixels (`bg_RunPiece`), so edges stay sharp. Measured in a small test room on the cloud VM: about 18,000 rays and 1.3 ms a frame with the flashlight on; a first version with a flat 4-pixel step needed 53,000 rays and 3.8 ms.
* Sprites use the same ray test, so a monster behind a pillar is not lit by the beam.
* Tested here: a room with two pillars, shadows on and off (a dark wedge on the floor beside the pillar with them on). Not tested: very large maps, coop with two flashlights, performance on the Windows laptop.

## 0.10.49 - gyro lights, switchable lights

* **`spin DPS`** in a BGLIGHT `light` line: a spot or strip turns round DPS degrees a second (negative = the other way), smooth between tics. It implies `movable`. The thing's angle is the starting direction. Built in: 20301 red, 20302 amber, 20303 blue beacon (cone 12/30, tilted 20 degrees down, hung from the ceiling, 180 degrees a second). Two things 180 degrees apart make the two-beam kind. Code: `dsda/bglight.c` (`spin` in the definition; added to the yaw of the movable path).
* **Lights follow the dormant state.** A placed light whose thing is dormant (Thing_Deactivate, or the Dormant flag set in the editor) gives no light, baked or movable; Thing_Activate brings it back. No new special: the existing Hexen ones.
* Editor config (`udb/Includes/Begotten_lights.cfg`) has the three gyro things; `LIGHTING.md` documents `spin` and the dormant switch.
* Tested here with a dark room: the red beam lights different walls at different times, and the beam and a baked red light both go dark on Thing_Deactivate and come back on Thing_Activate. Not tested: the Dormant flag at level start, or split-screen.

## 0.10.48 - script lab and flashlight smoothing
* **MAP01 is now also the script lab**: 191 rooms (one per built-in function or line special callable from ACS) behind a door in the Gallery of Lights.
  Generated by `tools/make_script_lab.py` from `tools/script_lab_demos.py`; the finished map is `tools/data/MAP01_showcase.wad`.
  `tools/check_script_lab.py` compares a run dump with the manifest (see below). 15 demos need a real line crossing; Print, sound and damage cannot be seen by the checker.
* **Flashlight smoothing** (camera lag): the beam trails the view angle (`BG_FLASHLIGHT_LAG`, 0.075 s, wall-clock based, `dsda_RealTimeUS`). Visual only, no effect on the simulation.
* **Engine fixes found by the lab:**
  * UDMF maps never loaded their BEHAVIOR lump, so no ACS ran in them (`P_UpdateUDMFLevelComponents`).
  * `P_FindLine` (SetLineBlocking / SetLineTexture / SetLineSpecial) found nothing in Doom-in-Hexen maps; it now also matches line ids and Line_SetIdentification.
  * **ACS specials kept stale arguments from earlier calls**: `Teleport(tid)` got a leftover sector tag, so it often went nowhere. `SpecArgs` is now cleared before every LSPEC call.
  * ACS specials always run as the front side (a script started from the back of a line could not teleport).
* **Script numbers:** a script started by a line special or ACS_Execute is truncated to a byte, so every such script must be numbered 0-255 (OPEN scripts: below 1000 with `acc -h`). ENTER scripts are not supported.
* **Developer aids (env):** `BEGOTTEN_DUMP=file` (per-tic change log of sectors, lines, things, polyobjs, player), `BEGOTTEN_ACSLOG=1` (script starts, line and special activations), `BEGOTTEN_DAMAGELOG=1`, `BEGOTTEN_GOD=1`, `BEGOTTEN_AUTOWALK=tic`.
* Flats named in the manual's examples (e.g. FLOOR4_8) do not exist in the placeholder wad.

## 0.10.48 (follow-up)
* **Command-line starts are single player.** `-warp` (and UDB's test button) used the saved "local players" setting from the split-screen menus, so a game could open in split screen. The saved count now only prefills the menus; use `BEGOTTEN_LOCALPLAYERS=n` for a split-screen start without the menus.
* UDB test parameters: no `+map` (that is GZDoom); use `-iwad "<exe folder>\begotten.wad" -file "<exe folder>\begotten-data.wad" "%F" -warp 1 -skill 4`.
* The "This level may have rendering errors in software mode" alert is no longer raised for polyobjects (they render correctly in the software renderer). It still appears for wrapped middle textures and scaled wall textures.
* Load time grows with map size (the baked-light line-of-sight pass): the 885-sector script lab bakes in about 150 ms here and 500-700 ms on a slow laptop, against about 25 ms for the old MAP01.
* **Editor wad** (`tools/make_editor_wad.py CONTENT_TREE begotten_editor.wad`): the same sprites, textures and flats at 1 pixel per map unit
  (sprites and textures divided by the BGSCALE values, 128-pixel flats made 64, hot spots scaled), without sounds, music and maps.
  Load it in UDB instead of begotten.wad so sizes and texture alignment match the game. Composite textures from `textures.txt` are not rescaled yet.
* `tools/bgnodes.py MAP.wad [OUT.wad]`: builds the nodes with ZDBSP outside the editor (UDB under Wine crashes in its own nodebuilder step). The engine does not build nodes itself.
* **Class-flagged player starts.** A Player start with only some classes ticked (Class 1 Soldier, 2 Witch, 3 Swordsman) is used by players of those classes; a start that fits nobody is kept as a fallback and used only when no fitting start exists, so a class can never be left without one (`bg_ClassAllowsPlayerStart` / `bg_StashStart` / `bg_FlushStarts` in bgclass.c). In co-op each player takes the start for his own class. Before this the flags were ignored on starts (the last start in the file won).
* **UDMF thing flags fixed:** the loader pre-set Boom's "not single / dm / coop" bits, which are the same bit values as Dormant / Soldier / Witch, so class flags in UDMF maps were scrambled and things without `singleplayer` came out dormant. The bits are no longer set. (Binary Hexen-format maps were not affected.) Lab re-check: all demos still pass.

## 0.10.56 - a smaller lantern, split-screen crosshairs, no stray glow pixels
* **Lantern** default is now radius 150, strength 0.7 (was 360): the carried light only reaches about a sword's length, as in Dark Souls. Maps need their own lights (or sector light) to be seen further away.
* **Split screen draws one crosshair per view**, centred in each player's own view (`HU_DrawCrosshairAt`, called from `bg_SplitDrawHud`); the single central one is drawn only when there is one view.
* **Packer**: floor, wall and sprite art pixels that happened to match the fullbright glow colours (palette indices 240-254) are no longer mapped to them, so they cannot sparkle in the dark (FLOOR1 had 437 such pixels). Index 255 is never produced. Exact glow matches are kept only for art packed with glow on.

## 0.10.57 - sector light 0 is pitch black
* The darkest colormap rows were not black (each palette ramp ends in a faint colour, about (16,12,15)), so walls stayed visible in a sector at light 0. At load, private copies of the colormaps now fade rows 23-31 to true black (`bg_FadeColormap`, called from `R_InitColormaps`). Row 31 is black. Walls, floors and sprites all use the same colormap, so every path agrees.
* The fade start can be tuned with env `BGLIGHT_BLACKROW` (default 22; a lower number darkens more of the range, 31 turns the fade off).

## 0.10.58 - Hexen lightning off
* A MAPINFO `lightning` line made sky sectors flash and swapped the sky (`P_InitLightning`). Begotten has its own weather, so that Hexen lightning is now ignored unless the env var `BEGOTTEN_HEXEN_LIGHTNING=1` is set. Rain/snow/thunder come only from BGLIGHT `weather` lines and the "Rain, Snow And Lightning" menu option.

## 0.10.59 - the dark fade keeps no blue residue
* The fade of the darkest colormap rows (0.10.57) matched dark results with a colour weighting that makes blue errors cheap, so dark browns and reds at distance snapped to the darkest bluish palette colour (index 159) and showed as flat dark-blue panels. Now: a result under 14 in every channel is index 0 (black), and the matching uses plain distance. Rows 28-31 are black for practically every colour.
* `BEGOTTEN_HOM=1` (developer aid): fills the 3D view with red before drawing, so any pixel the renderer does not draw shows up (hall of mirrors).

## 0.10.60 - gyro lights
* The beam is tilted 45 degrees down (`pitch -45`, was -20) and stronger (strength 1.7, was 1.0); the small point light at the lamp is weaker (`glow 320 0.25`, was 0.6).
* A turning beam's corona and halo now flash: they only show while the beam points at the viewer (within about 40 degrees, full at 8), so the lamp no longer shows a steady glow that gives the point light away (`bg_FlareFactor`; any light with `spin` and a cone).

## 0.10.61 - gyro lights, second pass
* Beam tilt -30 degrees (was -45); the cone is wider (`spot 18 42`, was 12 30); the lamp's point light is 20% smaller (`glow 256 0.25`, was radius 320). The corona flashes within 50 degrees of the beam.

## 0.10.62 - gyro reach
* With the beam tilted -30 degrees and the lamp near a 288-high ceiling, the beam's axis met the floor about 460 units away, at the very edge of the 480 radius, and the floor directly below was outside the cone: walls lit, floor dark. Gyro radius is now 720 (`light 2030x ... radius 720 strength 1.7`). Rooms with high ceilings can raise it further with the placed light's own radius.

## 0.10.63 - gyro lights cast shadows
* The gyro lights (20301-20303) and the point light at their lamp now carry the `shadow` flag, so walls, ledges, steps and doors block the beam the way they block the flashlight (the "Flashlight Shadows" menu option switches all shadow rays off). Before, only the flashlight cast shadows, and a ledge in front of a wall did not shade it.

## 0.10.64 - shock trooper approaches, lights ignore spawn flags
* **Fire range** (new mobjinfo field `firerange`, map units; 0 = unlimited): a monster with a range keeps chasing its target until it is that close before it starts a ranged attack (`P_CheckMissileRange`). The shock trooper (editor no. 68, the old arachnotron slot; its art is still the chaingunner placeholder) has 640. Tune it with a BGMOBJ line `shocktrooper firerange 800`.
* **Placed lights spawn whatever their tick boxes say**: skill, class and single/coop/deathmatch flags are ignored for the light things (`bg_IsLightEdNum` in `P_SpawnMapThing`), so a copy pasted in the editor with other flags still lights.
* `udb/README.txt`: how to make the Test button use the unsaved map (`%F`).
* **UDB monster list** (`udb/Includes/Begotten_monsters.cfg`): UDB keeps the first definition of a thing number, which was Doom's (68 stayed "Arachnotron"). The new monsters are now written into Doom's own `monsters` category, so number 68 shows as "Shock trooper [Modern]" with its own sprite and size, and so on for the other renamed slots. Two sprite names that do not exist in the wad (`MWIZA1D1`, `MNDEA1D1`) became `MWIZA1` and `MNDEA1`.

## 0.10.65 - spilled light (bounce), -warp MAPxx
* **Bounce**: a light with `bounce S` (BGLIGHT; the gyros have 0.3) colours every sector it reaches. Each sector holds a coloured ambient value (`sglow`) that jumps up when the light hits a sample point of the sector (five spots on the floor, middle and ceiling, with the light's shadows) and decays by 0.9 per tic; walls, floors, ceilings and things in the sector are lit with it as one more light without a position (`bg_GlowAt`). Menu: "Light Bounce (Coloured Spill)"; env `BGLIGHT_NOBOUNCE=1` for tests. This is an approximation, not radiosity: it ignores which surface the light hit.
* `-warp MAP01` (the lump name UDB passes as %L) now means `-warp 1`.

## 0.10.66

- Light bounce: a turning spot (gyro) no longer lets the room fall back to black between passes of the beam. The sector glow now also takes 60% of the light's omni reach (same shadow test), so the wash dips as the beam leaves and rises as it returns, but never goes out.
- Several lights on one sector are combined with a "screen" blend per colour channel, `1 - prod(1 - c)`: two equal lights are brighter than one, red + blue gives purple, and the result never exceeds 1. It is not a median or an average.

## 0.10.67

- Light bounce no longer fills shadows: the spilled light of a sector is measured from the light that lit it, and a floor, wall or ceiling point that this light cannot see (a wall, ledge, pillar or solid thing in the way) gets none of it. Before, the whole sector was washed evenly, so the shadow of a wall on the floor was filled in with colour.

## 0.10.68

- Flying projectiles and the flash of the player's gun now cast shadows (rays to every lit point, like the flashlight and the gyros). A menu option "Projectile And Gun Flash Shadows" and the BGLIGHT words `missile noshadow` / `muzzle noshadow` turn them off. If the muzzle point (28 units ahead of the eye) is inside a wall, the flash comes from the eye instead, so it still lights the room.
- Explosions (the light a projectile makes when it hits something) are 75% smaller: radius 420 -> 105, halo 80 -> 20. They do not cast shadows.

## 0.10.69

Voxel props: scenery made of voxels (a car, a barrel, a statue) that is placed in UDB like any thing, is lit by the level's lights, can be shot, and swaps to another model when it dies (a car that blows up and becomes a burning wreck).

**Size.** One voxel is one pixel of the art: 1 / 2.5 = 0.4 map units, the same size as the pixels of the player characters (the engine's BGSCALE for sprites). A model 100 voxels long is 40 units long, as long as a 100-pixel sprite is wide. Models are at most 256 voxels per axis in MagicaVoxel; bigger objects are put together from several models in one `.vox` scene (MagicaVoxel's scene graph is read, including translation and the 90-degree rotations).

**Making a model.** Build it in Blockbench, export it as OBJ/GLB, voxelize it in MagicaVoxel (import with a voxel size that gives the pixel size above), then save the `.vox`. Conventions:
- The lowest voxel of the model stands on the floor. The thing's x/y is MagicaVoxel's world origin.
- Forward (the way the thing faces at angle 0, east) is +x of the model; left is +y; up is +z.
- Colours are matched to the game palette (slots 0-239). Palette slots 241-255 in MagicaVoxel mean glowing: those voxels are drawn at full brightness and are not darkened by the level light (headlights, flames).
- Only the surface is used: voxels with all six neighbours filled are dropped when the model loads, so hollow and solid models cost the same.

**BGVOXEL lump.** A text lump (put as many as you like) with two kinds of line:

    prop  KEY EDNUM sprite SPRT [radius R] [height H] [health N] [mass M] [offset X Y Z]
    model KEY FRAME LUMP [light RADIUS STRENGTH R G B] [yaw DEGREES]

`prop` makes a thing type with editor number EDNUM (give it a sprite name of four letters, used only for the editor icon: `SPRT` + `A0`). It is solid and shootable. `model` gives it its looks, by frame: `A` idle, `B` the explosion, `C` and `D` the burning wreck (`D` is optional and alternates with `C`, so flames can change). `light` makes that frame a flickering point light of that radius, strength and colour. A shot-down prop plays `A` -> `B` (with the explosion sound and a radius damage like a barrel) -> `C`/`D`, and the wreck stays solid.

The car is the example: it is in `begotten.wad` (the lumps `BGVOXEL`, `VXCAR`, `VXCARB`, `VXCARC`, `VXCARD` and the sprite `VCARA0`), and its source files are in `game/voxel_props` (`data/BGVOXEL.txt`, `data/voxels/*.vox`, `sprites/props/VCARA0.png`) for the content tree. Thing 21101 is in the UDB configuration under "Begotten: Voxel props". `pack_wad.py` packs `.vox` files and `BGVOXEL.txt` like any other lump. A pwad can add its own `BGVOXEL` lump and models, but must use editor numbers that are not taken.

**Converter.** `tools/obj_to_vox.py model.glb VXNAME.vox` turns a Blockbench export (OBJ with its MTL and texture, glTF or GLB) straight into a `.vox`, so MagicaVoxel is optional. It samples the surface at 0.4 units per voxel, takes colours from the texture, reduces them to the palette, and puts materials with "emissive", "glow" or "light" in the name (or `--emissive WORD`, or a glTF emissive factor) into the glowing slots. `--length N` scales the model to N map units long, `--scale N` sets map units per model unit; Blockbench's Y up and -Z front are the default (`--up`, `--front` change them). It writes the model with the floor at z = 0 and the footprint centred on the thing, and prints the size in map units. Needs `pip install trimesh numpy pillow scipy`.

**How it is drawn.** A model is one sprite for the sorter: it is placed among walls and monsters by its distance and clipped by the walls in front of it. Its visible cells are drawn nearest-first into a small depth buffer and then copied to the screen. Each block of 6 x 6 x 6 cells is lit once per frame per face direction with the engine's lighting (distance, sector light, every dynamic light, shadows), so lit sides and dark sides read correctly and a flashlight sweeps across a car. A model casts a cylinder shadow for its radius and height.

**Cost.** Roughly 10 ms per car filling the screen on the slow test machine (the car prototype is 114,000 visible cells, much bigger than a typical prop). Cost grows with the number of visible cells and with the screen area the model covers, so keep props lean. `BGVOX_STATS=1` in the environment prints the timing once per second.

**Known limits.** Collision is the cylinder (radius, height), not the model's shape. A model is only drawn while its origin is on screen. Frame lights are plain point lights (no shadows).

## 0.10.70

Status bar faces can be bigger pictures. A face lump larger than the 35 x 32 slot (for example 192 x 176, from a photograph) is shrunk to the slot at the resolution of the screen instead of being drawn as 35 x 32 big pixels. The engine averages every source pixel that falls on a screen pixel (exact area average, on the colours), then matches the result to the palette with error diffusion (Floyd-Steinberg, ordinary colours 0-239 only), once per face and screen size, and keeps it.

- Names are unchanged: the stock set is `STFST00`... and a class set is its three-letter `face` prefix from BGCLASS (`SLDST00`, `WCHOUCH2`, ...). A set can mix sizes; a face smaller or equal to 35 x 32 is drawn as before, and a class that lacks a lump still falls back to the stock face.
- Any size works. The most the screen can show is the slot's size on the screen (about 190 x 175 at 1080p, 250 x 230 at 1440p, 380 x 350 at 4K), so a source of 192 x 176 is enough for 1080p and a little soft at 4K. The slot keeps its 35:32 shape: a picture of another shape is stretched to it, so keep photos at that ratio (192 x 176 is 12:11, close enough).
- `tools/pack_wad.py` dithers face lumps bigger than 35 x 32 when it packs them (the engine's averaging only brings the colours back from a dithered source, not from one snapped to the nearest colour). Other graphics are packed as before.
- Software renderer only; in OpenGL mode a big face would be drawn full size, so keep the old 35 x 32 faces if you use it.

## 0.10.71

- **Health colour behind the status face.** Where the face picture is transparent (the photo faces; the stock faces keep their own background), the slot is filled with a colour that follows the displayed player's health: dark green at full health, through amber, to bright red at none, changing a little with every point of health. Gold while invulnerable. Menu: "Health Colour Behind Face" (config `begotten_face_health_bg`, on by default). A class can set its own three colours (full health, half health, no health) with a BGCLASS line `facebg R G B  R G B  R G B`. The colour is matched to the palette (the glow ramps included, since the status bar is not lit), and it counts hue as well as brightness, so a dark green falls on the most greenish dark colour there is. The palette decides how it looks: PLAYPAL1 has no green at all, so "green" is its dark teal; add a green ramp to the palette and the colour finds it by itself.
- **Witch faces** (`WCHST00` ... `WCHDEAD0`) are now photographs at 192 x 176 (`game/face_witch/graphics` has the PNGs; `begotten.wad` has the lumps). Mapping: ST0/ST1/ST2 = idle left/front/right, TR = damage right, TL = damage left, OUCH = shock, EVL = grin, KILL = damage front, GOD0 = grin, DEAD0 = dead. Pain levels 0-2 use the healthy pictures, 3-4 the hurt ones. Every photo is scaled so that the distance between the eyes and the mouth is 68 pixels (measured in 3D with face landmarks, so a turned or lowered head gives the same number), and placed so that the middle of the eyes and mouth sits at the same spot of the 192 x 176 box (x 96, y 93) in every face: the features keep their size and place from face to face, as in the stock faces. Whatever of the hair falls outside the box is cropped; the whole face (forehead to chin, cheek to cheek) always stays in. `game/face_witch/tools` has the scripts and measurements (`measure.py` needs mediapipe 0.10.14 on Python 3.12; `layout.py` turns the measurements into the PNGs; change D and CY at the top of its caller to change the framing).
- Developer aid: `BEGOTTEN_HEALTH=n` starts the player with that health.

## 0.11.0 - 512 colours

The software renderer now draws with a palette of **512 colours** instead of 256. Still indexed colour (no true colour), but every pixel of every asset can use any of the 512 entries.

- **The palette.** Entries 0-255 are the colours of PLAYPAL exactly as before, so every old lump, texture and sprite looks as it did. Entries 256-511 are generated in-between colours: entry 256 + i is the middle of colour i and its partner (the next shade of the same ramp; the darkest shade pairs with black; the grey row and the glow row pair with their neighbour). 255 and 511 stay the reserved spare. A wad can paint the in-between colours itself with a lump named like the palette plus an X (`PLAYPALX`, `PLAYPAL1X`...), 14 x 768 bytes per palette, laid out like the palette. Palette switching (BGPAL, PLAYPAL1-3) works as before; PLAYPAL1-3 get their in-between colours as gradients between their own colours until you paint them.
- **Remap tables.** The colormap (light levels), translations, colour ranges and translucency tables are made once, relative to the PLAYPAL lump, and mean "the same colour, darker / blended" whichever palette a map uses (as in classic Doom). They are widened to 512 colours: more distinct shades per fade row, finer translucency. Translucency tables are generated in memory (exact nearest colour, a few milliseconds); legacy TRANMAP lumps still work.
- **Wide pictures and flats.** A picture or flat that uses an in-between colour is stored as a *wide* lump: patches start with `BGW1`, then an ordinary patch header and column offsets, and each post is `[top][length][length x uint16 index]`; flats are `BGW1` + 16-bit indices (square 64..1024). Classic lumps are read as before, so old wads and PNG-free content do not change.
- **`tools/pack_wad.py`** quantizes PNGs against all 512 colours and writes a wide lump only when a picture needs one (art that fits the classic 256 stays byte-identical, so repacking an existing tree changes nothing). It unpacks wide lumps back to PNG, honours `palette/PLAYPALX.lmp`, and dithers faces with the ordinary colours of all 512. A flat that needs in-between colours must be square.
- **UDB.** UDB shows the classic 256-colour palette; to preview wide art there, load the PNG folder as a resource.
- **Limits.** OpenGL mode is not supported with 512 colours (software renderer only). The automap colour picker still offers 256 swatches. Not tested here: the Windows build, real hardware, and how it looks on your screen.
- Developer aids: `BG_EXTRAS=hue` makes the in-between colours the vertical hue-row midpoint instead of the next shade.

## 0.11.1

- **Fix (Windows): lights, halos and translucency drew black.** The nearest-colour search of the 512-colour palette kept its best distance in a `long`; on Windows a `long` is 32 bits, so the start value overflowed and almost no colour was ever found (the search returned black). Everything that goes through it was affected: coloured and dynamic light pools on walls, halos and coronas, fog, translucency tables. It now uses 64-bit integers, and a selftest compares the search with a plain scan of the palette.

## 0.11.2

- **Witch faces redone** from the new photos (`Hi-def croppeds`: head and body). Same process as before, rerun: `game/face_witch/tools/measure.py` measures the landmarks (mediapipe 0.10.14, Python 3.12), `layout.py` scales every photo so the eyes-to-mouth distance is **57.8 px (15% smaller than the 68 px of the first set)** and puts the middle of eyes and mouth at (96, 93) of the 192 x 176 box, centred on the cheeks; the neck and jacket now show below the chin. Run `layout.py <photos> landmarks.json <out> [D] [CY]` to change D or CY. Photo to lump: `Idle_Left/Idle/Idle_Right` (healthy for pain 0-2, hurt for 3-4) = `WCHST*0/1/2`; `Damage_Right_*` = `WCHTR*0`; `Damage_Left_Hurt` = `WCHTL*0` (**no healthy "damage left" photo in the set, so the hurt one is used for all five levels**); `Shock_Healthy / Shock_Front_Hurt` = `WCHOUCH*`; `Grin_Healthy / Grin_Front_Hurt` = `WCHEVL*`; `Damage_Hurt / Damage_Front_Hurt` = `WCHKILL*` (healthy / hurt); `God_Mode` = `WCHGOD0`; `Dead` = `WCHDEAD0`. The lumps are wide (512-colour) pictures, dithered with all 512 colours.
- **Grit face per weapon.** The status face's rapid-fire "grit" now depends on the weapon: knife and chainsaw grit only while their blows land on an enemy (for 14 tics after each hit); pistol primary as in Doom (after 2 seconds of holding), pistol secondary never; shotgun slot (both modes) from the second shot of a hold; chaingun slot (both modes) after half the stock time (1 second); all spell weapons and the alien weapon as in Doom. Engine: `bg_FaceGritRule` in `dsda/begotten.c`, used by `ST_updateFaceWidget`.

## 0.11.3

- **Witch faces smaller and lower again:** the head is another 15% smaller (eyes to mouth 49.1 px) and the middle of eyes and mouth sits lower in the box (y 106 instead of 93), so more of the top of the head and hair shows and less of the clothes. `layout.py <photos> landmarks.json <out> 49.1 106` regenerates them.

## 0.11.4

- **Fix: crash when sprites were drawn (seen while shooting fireballs).** The column drawers can read up to one patch height past the last column of a patch (the texture wraps at the patch height, from a post that does not start at row 0). With 8-bit pixels the bytes found there were always a valid colour index; with 16-bit pixels they were the patch's column table, i.e. a colour index of tens of thousands, and the colormap lookup left its table. Every patch and composite texture now has a zeroed slack of one column height after its pixels.

## 0.11.5

No change to the game itself; the self tests were brought up to date and the 512-colour code was audited.

- **Audit.** A memory-checked (AddressSanitizer) build was played through all 32 maps of `begotten.wad` as the witch, with every weapon fired in primary and secondary mode, plus maps 1 and 10 as the other two classes: no memory error other than one read of a lump name one byte past its end at start-up (in the original lump hash code, harmless). The 32-bit `long` of Windows was checked for every place the new colour code uses `long`: the only one that could overflow was the one fixed in 0.11.1.
- **Self tests fixed.** The split-screen test still set the old saved player count (`bg_split_request` is what the menus set now), so three of its checks failed; the damage-multiplier tests assumed an old balance table (revenant firearm 80 / magic 130, a cacodemon entry) and a class-less player. They all pass now (`BEGOTTEN_SELFTEST=1`, `2`, `3`, and `8` / `9` with `BEGOTTEN_LOCALPLAYERS=2`). `BEGOTTEN_SELFTEST=4` (parry, thunderclap, blade, air control) needs a bigger test room than `MAP01` and fails the same way on 0.10.71, so it is not a result of the colour work.
