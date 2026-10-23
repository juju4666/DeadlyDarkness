//
// Begotten: art at more than one pixel per map unit.
// Licensed under the GNU GPL v2 or later, like the rest of the engine.
//
// Doom draws one picture pixel per map unit (a 56 unit tall player is a 56 pixel tall sprite). A BGSCALE lump lets the
// art have more pixels than that, so pictures can be drawn at a higher resolution while the map, the physics and the
// collisions stay exactly as they are:
//
//   sprites 2      sprites (things, pickups, monsters, the players) have 2 pixels per map unit
//   textures 2     wall textures have 2 pixels per map unit
//   weapons 2      first-person weapons have 2 pixels per 320x200 screen pixel
//
// Numbers may be fractions (2.5). The HUD, menus, intermission and the sky are not affected. Flats need no setting:
// a flat lump of 128x128 (or 256x256, 512x512) bytes is drawn over the same 64x64 map units as a 64x64 one.
// A line that is missing means 1 (unchanged).
//
#ifndef __DSDA_BGSCALE__
#define __DSDA_BGSCALE__

#include "m_fixed.h"

extern fixed_t bg_scale_sprites;   // picture pixels per map unit, 16.16
extern fixed_t bg_scale_textures;
extern fixed_t bg_scale_weapons;

// picture pixels -> map units (or 320x200 units): FixedDiv(px << FRACBITS, scale)
#define BG_UNITS(px, scale) FixedDiv((px) * FRACUNIT, (scale))
// map units (fixed) -> picture pixels (fixed)
#define BG_PIXELS(units, scale) FixedMul((units), (scale))
// the same, whole picture pixels, rounded
#define BG_PIXELS_INT(units, scale) ((FixedMul((units), (scale)) + FRACUNIT / 2) >> FRACBITS)
// a (fixed) offset in map units to a whole number of picture pixels (rounded down, as the offsets always were)
#define BG_OFFSET_PX(units, scale) (FixedMul((units), (scale)) >> FRACBITS)

void dsda_InitBgScale(void);   // reads the BGSCALE lumps (before the textures are set up)

#endif
