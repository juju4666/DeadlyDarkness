//
// Begotten: animated pickups and the nine keys.
// Licensed under the GNU GPL v2 or later, like the rest of the engine.
//
// Every weapon pickup and every key is drawn with an 8 frame loop (frames A .. H of its sprite, no rotations):
//   weapon pickups: SHOT (slot 3) MGUN (4) LAUN (5) PLAS (6) BFUG (7) SGN2 (8)
//   keys:           BKEY blue, YKEY yellow, RKEY red, GKEY green, CKEY cyan, MKEY magenta, KKEY black, AKEY grey, WKEY white
// A sprite that has fewer than 8 frames loops over the frames it has.
//
// Keys: nine colours, one key each (the skull keys are gone).  Their slots in player->cards[] are:
//   0 blue, 1 yellow, 2 red (the stock card slots), 6 green, 7 cyan, 8 magenta, 9 black, 10 grey, 11 white (slots 3-5 stay unused).
// Editor numbers: blue 5, yellow 6, red 13 (stock), green 22001, cyan 22002, magenta 22003, black 22004, grey 22005, white 22006.
// Locks in Hexen / UDMF format maps (the "lock" argument of Door_LockedRaise and friends):
//   1 red, 2 blue, 3 yellow (stock numbers), 7 green, 8 cyan, 9 magenta, 10 black, 11 grey, 12 white;
//   100 = any key, 101 = all nine, 129 / 130 / 131 = red / blue / yellow.  Doom format maps only know red, blue and yellow.
//
#ifndef __DSDA_BGPICKUP__
#define __DSDA_BGPICKUP__

#include "doomtype.h"

#define BG_KEYS 9

void bg_InitPickups(void);                  // after the mobj types exist (dsda_InitBegotten)
void bg_FixPickupFrames(void);              // after the sprites are loaded: loops over the frames that exist
int bg_KeyForSprite(int sprite);            // the cards[] slot of a key's sprite, or -1
int bg_KeySlot(int colour);                 // colour 0 .. 8 (blue yellow red green cyan magenta black grey white) -> cards[] slot
const char* bg_KeyName(int colour);         // "blue", "green" ...
const char* bg_KeyMessage(int slot);        // "Picked up a green key."
int bg_KeyColourOfSlot(int slot);           // cards[] slot -> colour 0 .. 8, or -1 (unused slots)
int bg_LockSlot(int lock);                  // zdoom lock number -> cards[] slot of the nine keys, or -1

int bg_KeyType(int colour);                // the mobj type of a key colour, or -1
int bg_KeyLockNumber(int colour);           // its Hexen / UDMF lock number
int bg_PickupSelfTest(void);                // number of failures

#endif
