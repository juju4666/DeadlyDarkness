//
// Begotten: the "wind-up" sound a monster makes the moment it commits to an attack.
// Entries come from the BGMONSTER lump (built-in defaults first, then every BGMONSTER lump):
//     <doomednum> <sound lump name>        e.g.   9  DSWNDSGT
// Only the player being targeted hears it (on this machine), never monster-vs-monster fights.
//

#ifndef __DSDA_BGMONSTER__
#define __DSDA_BGMONSTER__

#include "doomtype.h"
#include "p_mobj.h"

void dsda_InitBgMonsters(void);
void bg_MonsterWindup(mobj_t* actor);   // call when a monster switches into its attack state
int bg_MonsterWindupSfx(int doomednum); // sfx id or 0 (for the selftest)

int bg_SfxFromLump(const char* lump);  // sfx id for a sound name / lump (registers a new one); 0 = none
void dsda_BgMonsterSelfTest(void);

#endif
