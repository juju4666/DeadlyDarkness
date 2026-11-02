//
// Begotten: drivable vehicles (a motorcycle and a mecha). Licensed under the GNU GPL v2 or later.
//
// A vehicle is a voxel prop (see bgvoxel.h) whose BGVOXEL line says `vehicle moto` or `vehicle mecha`. The player gets
// in with Use next to it and out with Use again. While he is in, his own thing is the "rider": it has no body of its
// own (not solid, not shootable), is carried to the vehicle's position at the end of every tic, and its movement keys
// drive the vehicle instead. The vehicle is an ordinary thing, so walls, steps, monsters and portals are handled by the
// normal movement code; this module only decides its momentum each tic. Everything is kept in the two things
// (the `tracer` pointers, the vehicle's momentum, angle and `special1/2` counters), so a saved game, a demo and a
// network game need nothing more, and the arithmetic is all integer so every machine gets the same result.
//
#ifndef __BGVEHICLE__
#define __BGVEHICLE__

#include "d_player.h"
#include "p_mobj.h"

// the vehicle this player is in (NULL: on foot)
mobj_t* bgveh_VehicleOf(const mobj_t* rider);
dboolean bgveh_Mounted(const player_t* player);

// the Use key: get in (a vehicle is within reach) or out; true = the key was used up
dboolean bgveh_Use(player_t* player);

// instead of P_MovePlayer and the weapons while driving; true = the player is in a vehicle
dboolean bgveh_Control(player_t* player);

// after the things have moved: carry the riders, throw them out of vehicles that were destroyed
void bgveh_PostTick(void);

// the view of a rider (replaces the walking height and bob); true = done
dboolean bgveh_Camera(player_t* player);

// damage meant for a rider goes to his vehicle
mobj_t* bgveh_Redirect(mobj_t* target);

// is `vehicle` the one `shooter` is riding? (shots and rockets pass through their own vehicle)
dboolean bgveh_OwnVehicle(const mobj_t* shooter, const mobj_t* vehicle);

// the vehicle is not drawn in its rider's own view
dboolean bgveh_HideFromView(const mobj_t* thing, const player_t* viewer);

// the tank: the driver of one, and whether it is in the "tracks" style (the two tracks replace the movement keys)
dboolean bgveh_IsTankDriver(const player_t* player);
dboolean bgveh_WantsTracks(const player_t* player);

// the tank hull's state actions (see bgvoxel.c)
void bgveh_A_TankThink(mobj_t* hull);
void bgveh_A_TankWreck(mobj_t* wreck);

// developer aid (BEGOTTEN_VEHTEST): plays player 1's controls from a script
void bgveh_TestCmd(player_t* player);

// text for the status bar ("MOTORCYCLE 120/150  SPEED 84"), false when on foot
dboolean bgveh_StatusText(const player_t* player, char* buf, size_t len);

#endif
