//
// Begotten: grind rails (the swordsman's way along guard rails). Licensed under the GNU GPL v2 or later.
//
// A rail is a linedef with the special BG_RAIL_SPECIAL (300, "Grind Rail"). Args: arg0 = height above the floor in
// map units (0 = 24), arg1 = grinding speed in units a tic (0 = 26). Only the swordsman uses them: he hops on by
// jumping next to one, rides the chain of rails joined end to end at about a motorcycle's top speed, steers the
// speed a little with forward and back, never stops, and leaves by jumping (faster = higher and further) or by
// running off the end. Every bit of state is in the player's thing (special1 = speed, special2 = line, direction and
// jump-key bits, bg_cd = re-grab delay), so saved games, demos and network games need nothing more. Integer math only.
//
#ifndef __BGRAIL__
#define __BGRAIL__

#include "d_player.h"

#define BG_RAIL_SPECIAL 300

// in place of P_MovePlayer: true = the player is on a rail this tic
dboolean bgrail_Control(player_t* player);
dboolean bgrail_Grinding(const player_t* player);

#endif
