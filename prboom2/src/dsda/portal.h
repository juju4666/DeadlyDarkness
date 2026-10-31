/*
 * Begotten: line portals.
 *
 * Two lines of a map can be tied together: whoever looks at the front of the first sees, through it, what is in front
 * of the second, and whoever (or whatever) walks through it comes out of the second.  The two places can be anywhere
 * in the map, so rooms need not fit together in space (the door that leads to room A, then, after a switch, to room B).
 *
 * In the map editor (ZDoom's specials, so UDB understands them):
 *   Line_SetPortal(targetline, thisline, type, planeanchor)   -- 156
 *        puts the portal on the lines with id `thisline` (0 = the line carrying the special) towards the first line
 *        with id `targetline`.  Both lines should carry one for each other, then it works both ways.  `type` and
 *        `planeanchor` are ignored (every portal is "interactive").
 *   Line_SetPortalTarget(thisline, targetline)                -- 107
 *        changes the destination of the portal lines `thisline` while the game runs (a switch, a script).
 * A portal line is better one-sided and of the same length as its target.
 * The player's side of a line is its front: what the player faces must be the front sidedef.
 */
#ifndef __BG_PORTAL__
#define __BG_PORTAL__

#include "doomtype.h"
#include "m_fixed.h"
#include "tables.h"
#include "r_defs.h"

typedef struct bg_portal_s
{
  line_t* src;           // the line one looks at ...
  line_t* dst;           // ... and sees the surroundings of this one
  int64_t c, s;          // the turn (cos and sin, 2.30 fixed point)
  angle_t dangle;        // the turn as an angle
  fixed_t dz;            // how much higher or lower the far side is
  fixed_t ox1, oy1, ox2, oy2;   // where the line was at the last tic (it may be on a polyobject)
} bg_portal_t;

void P_PortalSweep(const bg_portal_t* p);     // p_map.c: things that a portal line has moved over come out of the other end

extern int bg_numportals;           // portal lines in this map (0: all of the portal code is skipped)
extern line_t** bg_portal_lines;    // ... and the lines themselves

void bg_PortalsReset(void);          // new map: no portals
dboolean bg_PortalLink(line_t* src, line_t* dst);    // dst NULL removes the portal of src
void bg_PortalSpawn(line_t* l);      // a Line_SetPortal at map start
dboolean bg_PortalExecute(line_t* l, int special, const int* args);   // the same specials, run while playing
void bg_PortalsUpdate(void);         // every tic: the lines of a polyobject may have moved
void bg_PortalsFinish(void);         // after all lines have been looked at

void bg_PortalMapPoint(const bg_portal_t* p, fixed_t x, fixed_t y, fixed_t* ox, fixed_t* oy);
void bg_PortalMapVector(const bg_portal_t* p, fixed_t x, fixed_t y, fixed_t* ox, fixed_t* oy);

// the line, on this side (front), and a move from (x0,y0) to (x1,y1) that crosses it from the front: where along
// the move (0..FRACUNIT), or -1
fixed_t bg_PortalCross(const line_t* l, fixed_t x0, fixed_t y0, fixed_t x1, fixed_t y1);

// can this line be walked through?
static inline dboolean bg_IsPortal(const line_t* l) { return l->portal != NULL; }

extern const bg_portal_t* bg_last_crossing;      // the portal the last P_TryMove went through (NULL if none)

void bg_PortalSelfTest(int* failures);
void bg_PortalLiveTest(void);                 // BEGOTTEN_SELFTEST=P (the portal test map)

#endif
