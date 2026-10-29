//
// Begotten: voxel props (scenery made of voxels, e.g. a car that explodes into a burning wreck).
// Licensed under the GNU GPL v2 or later, like the rest of the engine.
//
// A voxel prop is an ordinary thing (placeable, shootable, with a state chain) whose sprite is a voxel model: a grid of
// coloured cells read from a MagicaVoxel .vox lump. The models go through the sprite code's sorting and clipping (a
// vissprite per model) and are lit by the same lights as everything else: each block of cells is lit per face
// direction. One voxel is one art pixel: 1 / BGSCALE-sprites map units, the same size as the pixels of the player's
// sprites. See LIGHTING.md and BEGOTTEN_ENGINE.md (0.10.69).
//
// The BGVOXEL lump defines the props:
//   prop    KEY EDNUM sprite SPRT [radius R] [height H] [health N] [mass M] [wreck] [offset X Y Z]
//   model   KEY FRAME LUMP [light RADIUS STRENGTH R G B] [yaw DEGREES]
// FRAME is a letter: A idle, B explosion, C and D the burning wreck (D is optional). See BEGOTTEN_ENGINE.md.
//

#ifndef __BGVOXEL__
#define __BGVOXEL__

#include "r_defs.h"
#include "p_mobj.h"

// start-up, after the roster: reads BGVOXEL lumps and builds the thing types
void bgvox_Init(void);
// a level has been set up: the palette may have changed, so the models are made again when next drawn
void bgvox_NewLevel(void);
// every frame, after the view is set up
void bgvox_BeginFrame(void);

// Is this thing drawn as a voxel model? If so, makes this frame's instance of it and returns its number + 1, with its
// screen extent; 0 when it is a normal sprite (or off the screen).
int bgvox_Project(const mobj_t* thing, fixed_t fx, fixed_t fy, fixed_t fz, int* x1, int* x2, fixed_t* scale,
                  fixed_t* gz, fixed_t* gzt);
// the colormap the sprite code worked out for the instance (distance and sector light), and the sector it stands in
void bgvox_SetBase(int inst, const lighttable_t* colormap, const sector_t* sector);
// draw it (the sprite code has worked out the clipping)
void bgvox_Draw(int inst, const int* floorclip, const int* ceilingclip);

// the light a model frame gives (the burning wreck): returns true and fills radius, strength and colour (0..1)
dboolean bgvox_FrameLight(const mobj_t* thing, double* radius, double* strength, double* col);

int bgvox_Count(void);                 // number of voxel props defined
int bgvox_CellsDrawn(void);            // cells drawn last frame (statistics)
int bgvox_SelfTest(char* msg, size_t msglen);   // loader and projection on a made-up model; returns failures

#endif
