//
// Begotten: damage kinds and per-monster vulnerabilities.
// Licensed under the GNU GPL v2 or later, like the rest of the engine.
//
// Every weapon (and every projectile type) deals a "damage kind"; every
// kind belongs to a category.  The DMGKINDS lump says how much damage each
// monster takes per kind or per category.
//

#ifndef __DSDA_DAMAGE_KIND__
#define __DSDA_DAMAGE_KIND__

#include "p_mobj.h"

typedef enum {
  DK_NONE = -1,
  DK_BLADE,
  DK_BULLET,
  DK_SHELL,
  DK_BELT,
  DK_FIRE,
  DK_TEMPEST,
  DK_SPIRIT,
  DK_HELLFIRE,
  DK_NUMKINDS
} damage_kind_t;

typedef enum {
  DC_MELEE,
  DC_FIREARM,
  DC_MAGIC,
  DC_SPECIAL,
  DC_NUMCATEGORIES
} damage_category_t;

// Reads every DMGKINDS lump (later lumps override earlier ones).
// Must run after the mobj table is final (after DEHACKED).
void dsda_InitDamageKinds(void);

// The kind of damage an attack deals (DK_NONE if it has no kind).
damage_kind_t dsda_DamageKind(mobj_t* inflictor, mobj_t* source);

// The category a kind belongs to, and the kind a weapon mode deals (DK_NONE if none).
damage_category_t dsda_KindCategory(damage_kind_t kind);
damage_kind_t dsda_WeaponKind(int weapon);

// Applies the target's vulnerability to the damage of an attack.
int dsda_ScaleDamage(mobj_t* target, mobj_t* inflictor, mobj_t* source, int damage);

// Developer self test (BEGOTTEN_SELFTEST=1 in the environment). Exits the game.
void dsda_DamageKindSelfTest(void);
// Same, but inside a loaded level with real monsters and real damage.
void dsda_DamageKindLiveTest(void);

#endif
