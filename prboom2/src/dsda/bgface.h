//
// Begotten: status bar faces made from larger pictures.
// Licensed under the GNU GPL v2 or later, like the rest of the engine.
//
// A face lump bigger than the 35 x 32 slot of the status bar (for example 192 x 176, from a photograph) is shrunk to the
// size the slot has on the screen: averaged over every source pixel it covers, then matched to the palette with
// error diffusion, so that the face has the detail of the screen's own resolution instead of 35 x 32 big pixels.
// The result is made once per face and screen size and kept.
//

#ifndef __BGFACE__
#define __BGFACE__

#include "doomtype.h"

#define BG_FACE_W 35
#define BG_FACE_H 32

// Draws lump `lump` into the box x, y, w, h (status bar coordinates, 320 x 200) with the status bar's own placement.
// Returns false if it cannot (OpenGL mode, bad lump); the caller then draws the patch as usual.
dboolean bg_DrawFitPatch(int lump, int x, int y, int w, int h, int flags);
// forget the shrunk copies (resolution or palette change)
void bg_FitPatchFlush(void);
// the palette index (an ordinary colour, 0..239) nearest to a colour
int bg_PaletteNearest(int r, int g, int b);
// the palette index nearest to a colour for a flat fill (all colours, hue counts more than for pictures)
int bg_PaletteNearestFlat(int r, int g, int b);
// fills a box (status bar coordinates, stretched like the status bar) with a palette index
void bg_FillSlot(int x, int y, int w, int h, int flags, int colour);
// self-test of the shrinking on a made-up picture; returns the number of failures
int bg_FitSelfTest(char* msg, size_t msglen);

#endif
