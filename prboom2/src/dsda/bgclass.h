//
// Begotten: player classes (data driven).
// Licensed under the GNU GPL v2 or later, like the rest of the engine.
//
// A class is a set of numbers read from the BGCLASS lump: starting health and
// inventory, movement speed, and per-kind / per-category multipliers for the
// damage the player deals, the fire rate and the spread of his weapons.  Any
// number of classes (up to BG_MAX_CLASSES) can be defined; the swordsman is
// just one more entry.
//

#ifndef __DSDA_BGCLASS__
#define __DSDA_BGCLASS__

#include "d_player.h"
#include "dsda/damage_kind.h"

#define BG_MAX_CLASSES 8
#define BG_MAX_SPRITE_MAPS 48

// Per-class sound slots ("sound <slot> <lump>" in BGCLASS)
enum { BG_SND_PAIN, BG_SND_DEATH, BG_SND_XDEATH, BG_SND_OOF, BG_SND_NOWAY, BG_SND_JUMP, BG_NUM_CLASS_SOUNDS };

// Reads the built-in defaults and every BGCLASS lump. Runs after dsda_InitBegotten.
void dsda_InitBgClasses(void);

int bg_ClassCount(void);
dboolean bg_ClassAvailable(int cls);    // false while a class is still locked
const char* bg_ClassName(int cls);       // "Soldier"
const char* bg_ClassWord(int cls);       // "soldier" (the lump identifier)
int bg_ClassFromWord(const char* word);  // -1 if unknown; also accepts a number

// The class each player will have the next time he is reborn (set by the menu / -class).
extern int bg_player_class[MAX_MAXPLAYERS];

// Gives a freshly reborn player his class' health, armor, weapons and ammo.
void bg_ClassReborn(player_t* player);

// Class modifiers. All of them are 100% (no change) for an unknown class or a non-player.
int bg_ClassDamagePct(const player_t* player, damage_kind_t kind);
int bg_ClassSpreadPct(const player_t* player);  // for the weapon in hand
int bg_ClassScaleMove(const player_t* player, int move);
int bg_ClassParryWindow(const player_t* player); // tics, 0 = no parry
int bg_ClassChargeTics(const player_t* player);   // held attack: tics to a full charge, 0 = none
int bg_ClassChargeDamage(const player_t* player, int held);
fixed_t bg_ClassLunge(const player_t* player, int held);
int bg_ClassRiposte(const player_t* player);      // damage of the riposte, 0 = none
int bg_ClassAirControl(const player_t* player);   // percent of ground acceleration while airborne
dboolean bg_ClassMeleeOnly(const player_t* player);
struct mobj_s;
int bg_FoeSpread(const struct mobj_s* attacker, int offset); // scales a monster's hitscan spread
int bg_ClassBladeDamage(const player_t* player, int stock, int roll);
int bg_ClassScaleTics(const player_t* player, int tics);
fixed_t bg_ClassSwapSpeed(const player_t* player, fixed_t base, int weapon); // weapon raise / lower speed  // weapon attack state length

// Thing spawn filter for class-flagged map things (Hexen thing flags: class 1/2/3).
// Things with none of the three flags set are for everybody.
dboolean bg_ClassAllowsThing(unsigned int options);
dboolean bg_ClassAllowsPlayerStart(int player, unsigned int options);
void bg_StashStart(const mapthing_t* mthing, int player);
void bg_ClearStashedStarts(void);
void bg_FlushStarts(void);

// Health limits. `fallback` is the engine's normal value, used when the class sets none.
typedef enum {
  BG_HP_NORMAL,  // stimpak / medikit / berserk
  BG_HP_BONUS,   // health bonus (potion)
  BG_HP_SOUL,    // soulsphere
  BG_HP_MEGA     // megasphere sets health to this
} bg_health_cap_t;
int bg_ClassHealthCap(const player_t* player, bg_health_cap_t which, int fallback);

// The sound to play for `sfx` made by `origin` (a mobj): the class' own pain / death / gibbed / oof /
// no-way sound when the origin is a player whose class has one, else `sfx` unchanged.
int bg_ClassSfx(const void* origin, int sfx);

// Jumping. "jump <height>" in BGCLASS is how high the jump rises in map units (0 = no jump).
fixed_t bg_JumpSpeedFor(int height);          // the upward speed that rises that high
fixed_t bg_ClassJumpMomz(const player_t* player);  // 0 = cannot jump, -1 = no class (use the stock jump)
int bg_ClassJumpHeight(int cls);
int bg_ClassJumpSfx(const player_t* player);       // the class' jump sound (BGCLASS "sound jump"), 0 = none

// Body poses: what other players see of a player. See BGCLASS "pose".
// bg_PoseTick runs once per tic for every player; bg_PlayerPose is called by the renderer and
// replaces the sprite / frame of a player's body when the class has art for the current pose.
struct mobj_s;
void bg_PoseTick(player_t* player);
dboolean bg_PlayerPose(const struct mobj_s* thing, int* sprite, int* frame);
void dsda_BgPoseSelfTest(void);

// Class art. The sprite a thing / weapon of this class is drawn with (the original sprite
// number if the class has no replacement for it, or no replacement frame).
int bg_ClassSprite(int cls, int sprite, int frame);
// Prefix (3 letters) of the class' status bar face lumps, NULL for the stock faces.
const char* bg_ClassFacePrefix(int cls);
// The colours behind the class' status bar face (BGCLASS `facebg`): r g b at full health, at half health and at none.
// False if the class does not set them (the engine's own colours are used).
dboolean bg_ClassFaceBg(int cls, int* rgb);
// Lump number of the class' standing player sprite (front view), -1 if there is none.
int bg_ClassPortraitLump(int cls);

void dsda_BgClassSelfTest(void);

#endif
