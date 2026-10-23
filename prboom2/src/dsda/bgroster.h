//
// Begotten: the monster roster (Modern, Medieval, Alien factions and the three bosses).
// Licensed under the GNU GPL v2 or later, like the rest of the engine.
//
// The new monsters and their projectiles are defined in C, on top of copies of Doom's own
// state chains (like the weapon states in begotten.c).  Every number a designer wants to
// tune is a named value that the BGMOBJ lump can override:
//
//     <name|doomednum>  <field>  <value>  [<field> <value> ...]
//
// e.g.   werewolf health 160 speed 12 leapmax 640
//        3004 health 30 foedamage 130        (an existing Doom monster, by its editor number)
//
// See BGMOBJ.txt (data/lumps/bgmobj.lmp) for the list of names and fields.
//

#ifndef __DSDA_BGROSTER__
#define __DSDA_BGROSTER__

#include "doomtype.h"
#include "m_fixed.h"
#include "p_mobj.h"
#include "dsda/begotten.h"

// mobj->bg_flags
#define BG_BOOST    1 // revived by an Aegis: double damage and double movement (health is doubled when revived)
#define BG_KAMIKAZE 2 // a Hatred whose current charge ends in an explosion

// Defines the roster's mobj types and states, after the weapon states (called by dsda_InitBegotten).
void dsda_InitBgRoster(void);

// The mobj type of a roster thing by its name ("werewolf", "aegis" ...) for UMAPINFO bossaction and
// DMGKINDS; -1 if there is no such thing.
int bg_RosterTypeByName(const char* name);

// Hitscan monsters (zombieman, shotgunner, chaingunner, SS ...): a BGMOBJ can scale their damage and
// their spread (percent, 100 = stock).
int bg_FoeDamage(const mobj_t* attacker, int damage);
int bg_FoeSpreadPct(const mobj_t* attacker);

// A draining shot has just been dealt `damage` to `target`: heals `source`.
void bg_DrainHeal(mobj_t* target, mobj_t* inflictor, mobj_t* source, int damage);

// A flying Hatred has hit something (or a wall).  Returns true if it exploded instead of the usual
// contact damage; the caller then leaves the Hatred alone.
dboolean bg_SkullImpact(mobj_t* skull);

// p_enemy.c (static helpers made available)
dboolean bg_CheckMeleeRange(mobj_t* actor);
dboolean bg_CheckRange(mobj_t* actor, fixed_t range);

// Revolver secondary: a hitscan shot that goes through monsters (see the BGWEAPON "pierce" line).
// Returns the number of things hit.
int bg_PierceAttack(mobj_t* source, angle_t angle, fixed_t range, fixed_t slope, int damage, int maxhits);

void dsda_BgRosterSelfTest(void);

#endif
