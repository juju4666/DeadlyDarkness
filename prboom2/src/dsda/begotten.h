//
// Begotten: weapon slots with primary / secondary fire, seven ammo types.
// Licensed under the GNU GPL v2 or later, like the rest of the engine.
//
// Every one of the 8 slots holds one weapon with two fire modes.  The primary
// mode is what the slot key selects; the secondary mode is fired with the
// alt-fire button.  Every mode is an ordinary weapon_t entry with its own
// weaponinfo (ammo, ammo per shot, states); while a secondary mode is firing,
// player->readyweapon is that mode and player->primaryweapon remembers the
// slot's primary weapon, which is restored when the attack finishes.
//

#ifndef __DSDA_BEGOTTEN__
#define __DSDA_BEGOTTEN__

#include "doomdef.h"
#include "d_player.h"
#include "p_mobj.h"

#define BG_SLOTS 8

// true in every Doom-family game (false for Heretic and Hexen)
extern dboolean bg_weapons;
int bg_AllocStates(int n);
int bg_StateTop(void);
int bg_PiercePercent(void);
int bg_PistolRoll(int random);
int bg_MusicFadeTics(void);
void bg_TranslateMusicLine(short* special, int* args);

extern weapontype_t bg_slot_primary[BG_SLOTS + 1]; // 1-based; wp_nochange = empty
extern weapontype_t bg_slot_alt[BG_SLOTS + 1];     // wp_nochange = no secondary

// Runs once after DEHACKED: weapon modes, states, new things, BGWEAPON lumps.
void dsda_InitBegotten(void);

// Slot (1..8) a weapon mode belongs to, 0 = none.
int bg_WeaponSlot(weapontype_t weapon);
// The slot's primary weapon for any mode of the slot (the weapon itself if none).
weapontype_t bg_PrimaryOf(weapontype_t weapon);
// The weapon the player has "selected": primaryweapon while a secondary mode fires.
weapontype_t bg_SelectedWeapon(const player_t* player);

// Alt-fire handling inside P_FireWeapon: swaps readyweapon to the slot's secondary
// mode (or back to the primary) according to the alt-fire state of the current
// ticcmd. Returns false if the shot must not happen (alt requested but unusable).
dboolean bg_BeginFire(player_t* player);
// Leaves secondary mode: readyweapon = primary. Safe to call at any time.
void bg_RestorePrimary(player_t* player);
// True if the secondary mode of the player's slot could be fired right now.
dboolean bg_AltUsable(const player_t* player, weapontype_t primary);

// Best weapon to switch to when out of ammo / on "toggle weapon".
weapontype_t bg_SwitchWeapon(player_t* player);
// Slot-based next/previous weapon (direction = +1 / -1).
weapontype_t bg_NextWeapon(const player_t* player, int direction);
// The primary weapon of slot n if the player owns it, else wp_nochange.
weapontype_t bg_SlotSelectable(const player_t* player, int slot);

// Weapon pickups: maps the stock Doom weapon a pickup gives to the slot weapon.
weapontype_t bg_PickupWeapon(weapontype_t stock);

// Message for picking up a slot weapon ("Picked up: Rifle / Scattergun").
const char* bg_PickupMessage(weapontype_t primary);

// New ammo pickups. Returns true (and fills the outputs) if the thing type is one.
dboolean bg_AmmoPickup(int mobjtype, int* ammo, int* clips, const char** message);

// Screen flashes. kind: 0 = blue (health), 1 = yellow (shield); big = a 100-point pickup.
void bg_PickupFlash(player_t* player, int kind, dboolean big);
int bg_StabPhase(const player_t* player);
const state_t* bg_StabPhaseState(int phase);
void bg_FlashTick(player_t* player);
// the tint palette (0 = none) of the pickup flash the player sees
int bg_FlashPalette(const player_t* player);
#define BG_PAL_BLUE    14  // 14..17: four strengths each
#define BG_PAL_YELLOW  18
#define BG_PAL_PURPLE  22
#define BG_PAL_COUNT   26
#define BG_ADRENALINE_TICS (120 * 35)
#define BG_IMMORTAL_TICS   (60 * 35)

// Co-op pickups (0.10.14). True while more than one player is in a non-deathmatch game.
dboolean bg_CoopItems(void);
// Whether this pickup is one shared pool at the current difficulty (the first to take it removes it for everybody).
dboolean bg_ItemShared(const mobj_t* item);
dboolean bg_TakenBy(const mobj_t* item, const player_t* player);
void bg_MarkTaken(mobj_t* item, const player_t* player);
dboolean bg_AllTaken(const mobj_t* item);
dboolean bg_IsKeyItem(const mobj_t* item);
// A player's keys are wiped on death (Nightmare): the keys they held lie there for them to take again.
void bg_ForgetKeys(int playernum);

// Names for the HUD and messages.
const char* bg_AmmoName(int ammo);
const char* bg_WeaponName(weapontype_t weapon);
// HUD order of the ammo types.
extern const int bg_ammo_order[7];

// The mobj type of new ammo pickup i (0-5), -1 if unknown.
int bg_PickupType(int i);

// A player missile has just damaged a thing (the tornado sticks to it).
void bg_MissileHit(mobj_t* missile, mobj_t* target);

// Stun: freezes a living, painable, non-boss monster for `tics` (refreshes a running stun,
// never starts one during the short immunity that follows a stun). The chance to take hold
// is the monster's painchance. Returns true if the stun took hold.
#define BG_STUN_IMMUNE_TICS 25
dboolean bg_StunMobj(mobj_t* mobj, int tics);
int bg_StunDamage(const mobj_t* target, const mobj_t* source, int damage); // players hit stunned monsters harder
void bg_FireDeflected(player_t* player);
dboolean bg_ParryMissile(mobj_t* missile, mobj_t* victim);
dboolean bg_CoopShield(const mobj_t* a, const mobj_t* b); // true: a and b are players who must not hurt each other
dboolean bg_ParryDamage(mobj_t* target, mobj_t* inflictor, mobj_t* source, int damage);

// Word lookups used by the BGCLASS parser: weapon mode (or number) / ammo type; -1 if unknown.
int bg_WeaponFromWord(const char* word);
int bg_AmmoFromWord(const char* word);

// Looks up one of the new mobj types by name ("tornado", "hellfire"); -1 if unknown.
int bg_NamedMobjType(const char* name);

// Developer self tests (BEGOTTEN_SELFTEST=3 at start; =4 inside a level).
void dsda_BegottenSelfTest(void);
void dsda_BegottenLiveTest(void);
void dsda_BegottenArena(void); // BEGOTTEN_SELFTEST=5: real-time arena with monsters
void dsda_BegottenShowcase(void); // BEGOTTEN_SELFTEST=6: only a row of pickups in front of the player
void dsda_BegottenSaveTest(void);
void dsda_BegottenCoopTest(void);
void dsda_BegottenSpectateTest(void);

// Checkpoint line (special 174). bg_Checkpoint runs inside the line special and only records that a player
// crossed one; bg_CheckpointUpdate runs between tics (from dsda_UpdateAutoSaves) and does the work.
int bg_Checkpoint(mobj_t* mo, struct line_s* line);
void bg_CheckpointUpdate(void);
int bg_CheckpointCount(void);
void bg_CheckpointNewLevel(void);   // env BEGOTTEN_SELFTEST=8: save, play, load, compare (developer aid)
void dsda_BegottenDevUse(void);      // env BEGOTTEN_USE=tic: player 1 presses use then (developer aid)
void dsda_BegottenDevTeleport(void); // env BEGOTTEN_TELEPORT="x y degrees": player 1 starts there (developer aid)

void bg_ViewShift(const mobj_t* thing, fixed_t vx, fixed_t vy, fixed_t* x, fixed_t* y);

int bg_ClapBlast(player_t* player);
int bg_WeaponPickupClips(weapontype_t weapon, int clips);

void bg_WeaponKick(player_t* player);

void bg_PerfFrame(void); // called once per drawn frame: logs the frame rate every ten seconds

void bg_PlaceRedirected(mobj_t* mo);
void bg_GuideMissile(mobj_t* mo);
void dsda_BegottenLightDemo(void);

#endif
