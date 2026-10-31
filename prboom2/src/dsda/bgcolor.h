//
// Begotten: the 512-colour palette.
// Licensed under the GNU GPL v2 or later, like the rest of the engine.
//
// The screen is drawn with pixel_t palette indices and the palette has BG_NUMCOLORS (512) entries. Indices 0..255 are
// the colours of a classic 256-colour PLAYPAL, so every old lump keeps its look; indices 256..511 are "in-between"
// colours: entry 256 + i is the middle of colour i and a partner colour bg_ExtraPair(i) (the next hue row of the
// palette, the next shade of a grey ramp...). Art made for the wide palette can use any of the 512 entries in any
// pixel. A wad can replace the generated in-between colours by painting them itself: a lump named like the palette
// plus an X (PLAYPALX for PLAYPAL, PLAYPAL1X for PLAYPAL1 ...) holds the 256 extra colours of every sub-palette, laid
// out like the palette itself (14 * 256 * 3 bytes).
//
// This file also keeps the tools that go with a wide palette: the exact nearest-colour search (a colour cube of
// candidate lists, so a 512 x 512 translucency table takes milliseconds) and the widening of the old 256-entry remap
// tables (colormaps, translations, colour ranges) to the wide palette.
//
#ifndef __DSDA_BGCOLOR__
#define __DSDA_BGCOLOR__

#include "doomtype.h"

// the partner of colour i (0..255) for the in-between colour 256 + i
int bg_ExtraPair(int i);

// BG_NUMCOLORS * 3 bytes: sub-palette `pal` of a PLAYPAL of `count` 256-colour palettes, widened. `extra` (may be NULL)
// is the content of a PLAYPALX lump (see above). The result stays valid until bg_PaletteChanged().
const byte* bg_WidePalette(const byte* playpal, int count, int pal, const byte* extra, int extra_len);

// the palette the remap tables are made relative to: the first palette of the PLAYPAL lump, widened. A colormap or
// translation table is a table of indices that mean "the same colour, darker" in whatever palette the map is shown
// with, so it does not change when the palette does.
const byte* bg_ReferencePalette(void);

// call when the PLAYPAL in use changes or is freed: drops every table derived from it
void bg_PaletteChanged(void);
// counts the calls above, so a cache can tell that the palette is not the one it was made for
unsigned bg_PaletteGeneration(void);

// ---- exact nearest colour ----
// A search structure for a palette of BG_NUMCOLORS colours with the given weights of the squared differences of red, green
// and blue; the colours 255 and 511 (the spare of every theme) are never returned. Finding takes a few comparisons.
typedef struct bg_nn_s bg_nn_t;
bg_nn_t* bg_NNBuild(const byte* pal, int wr, int wg, int wb);
int bg_NNFind(const bg_nn_t* nn, int r, int g, int b);
void bg_NNFree(bg_nn_t* nn);

// the same in the reference palette (plain distance)
int bg_ColorNearest(int r, int g, int b);

// A remap table of the old 256 colours (colormap row, player translation, colour range) widened to the whole palette:
// out[i] = t[i] for i < 256, and an in-between colour goes to the palette colour nearest to the middle of where its
// two partners go.
void bg_WidenRemap(const byte* t, pixel_t* out);

// the rows of a colormap lump (rows of 256 entries) widened to rows of BG_NUMCOLORS entries
void bg_WidenColormap(const byte* lump, int rows, pixel_t* out);

int bg_SelfTest(char* msg, size_t msglen);

#endif
