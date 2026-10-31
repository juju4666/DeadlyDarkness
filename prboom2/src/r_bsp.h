/* Emacs style mode select   -*- C -*-
 *-----------------------------------------------------------------------------
 *
 *
 *  PrBoom: a Doom port merged with LxDoom and LSDLDoom
 *  based on BOOM, a modified and improved DOOM engine
 *  Copyright (C) 1999 by
 *  id Software, Chi Hoang, Lee Killough, Jim Flynn, Rand Phares, Ty Halderman
 *  Copyright (C) 1999-2000 by
 *  Jess Haas, Nicolas Kalkhof, Colin Phipps, Florian Schulze
 *  Copyright 2005, 2006 by
 *  Florian Schulze, Colin Phipps, Neil Stevens, Andrey Budko
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 *  02111-1307, USA.
 *
 * DESCRIPTION:
 *      Refresh module, BSP traversal and handling.
 *
 *-----------------------------------------------------------------------------*/

#ifndef __R_BSP__
#define __R_BSP__

extern seg_t    *curline;
extern side_t   *sidedef;
extern line_t   *linedef;
extern sector_t *frontsector;
extern sector_t *backsector;

/* old code -- killough:
 * extern drawseg_t drawsegs[MAXDRAWSEGS];
 * new code -- killough: */
extern drawseg_t *drawsegs;
extern unsigned maxdrawsegs;

// e6y: resolution limitation is removed
extern byte *solidcol;

extern drawseg_t *ds_p;
extern drawseg_t *ds_base;      // the first drawseg of the view being drawn (a view seen through a portal has its own)

// Begotten: portals, see dsda/portal.h
extern dboolean r_portalseg;                 // the line being added is a portal: R_StoreWallRange makes a window of it
extern int *r_pwtop, *r_pwbot;               // where the wall loop writes the rows of the window (indexed by column)
extern int r_pwin_current;                   // the window it is filling, or -1
extern const line_t *r_exitline;             // seen through a portal: the line the view comes out of (NULL: the main view)
extern dboolean r_passclip;                  // seen through a portal: sprites must stay inside the window
extern int *r_passceil, *r_passfloor;
int R_NewPortalWindow(int x1, int x2, line_t* line);
void R_RenderPortals(void);                  // after the planes of the main view: the views through the windows
dboolean R_BehindExit(fixed_t x, fixed_t y); // seen through a portal: is this point behind the line the view comes out of?

void R_ClearClipSegs(void);
void R_ClearDrawSegs(void);
void R_RenderBSPNode(int bspnum);
void R_ForceRenderPolyObjs(void);

/* killough 4/13/98: fake floors/ceilings for deep water / fake ceilings: */
sector_t *R_FakeFlat(sector_t *, sector_t *, int *, int *, dboolean);

#endif
