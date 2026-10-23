//
// Begotten: a palette per map (or group of maps).
// Licensed under the GNU GPL v2 or later, like the rest of the engine.
//
// A wad can carry up to ten palettes: PLAYPAL and PLAYPAL1 .. PLAYPAL9 (14 palettes each, as usual). The picture
// lumps are drawn with palette indices, so switching the palette recolours everything - walls, monsters, weapons,
// the HUD - at once. A BGPAL lump says which map uses which palette:
//
//   # map <first>[-<last>] <palette>     palette 0 = PLAYPAL, 1 = PLAYPAL1 ... 9 = PLAYPAL9
//   default <palette>                    title, menus and anything that is not a map (normally 0)
//   map 1-8 0
//   map 9-16 1
//
// Map numbers are the ones in MAPxx (Doom 2 format). A palette lump that is missing falls back to PLAYPAL. Several
// BGPAL lumps are read in load order; the last line that matches a map wins. Without a BGPAL lump nothing changes
// (the palette key of the menu keeps working as before).
//
#ifndef __DSDA_BGPALETTE__
#define __DSDA_BGPALETTE__

void dsda_InitBgPalettes(void);   // after the WADs are loaded
void bg_UpdatePalette(void);      // every frame, before drawing: switches when the screen / map calls for another palette
int bg_PaletteForMap(int map);    // the palette number a map uses (0 .. 9)

#endif
