//
// Begotten: lights for the software renderer. See bglight.c.
// Licensed under the GNU GPL v2 or later, like the rest of the engine.
//

#ifndef __BGLIGHT__
#define __BGLIGHT__

#include <stddef.h>
#include "r_defs.h"
#include "r_draw.h"

// Called once at start-up (after the wads are loaded): defines the placeable light things and reads BGLIGHT lumps.
void bg_LightsInit(void);

// Called every frame after the view is set up: collects the lights near the view.
void bg_LightsBeginFrame(void);

// true while at least one light may touch something on screen this frame
dboolean bg_LightsActive(void);

// Walls. BeginSeg tells whether any light reaches this wall; LitColumn then draws one column of it in runs of
// different brightness (the same job as colfunc(dcvars)). `scale` is the wall's scale at that column.
dboolean bg_LightBeginSeg(const seg_t* seg, const sector_t* front);
void bg_LitColumn(R_DrawColumn_f colfunc, draw_column_vars_t* dcvars, fixed_t scale);

// Masked (see-through) wall columns go through R_DrawMaskedColumn, which wants a plain draw function.
R_DrawColumn_f bg_LitMaskedFunc(R_DrawColumn_f real);
void bg_SetMaskedScale(fixed_t scale);

// Floors and ceilings. SetPlane before the spans of a visplane (height in map units, fixed); LitSpan draws the span
// in pieces and returns true, or returns false when no light reaches it (the caller then draws it as usual).
void bg_LightSetPlane(fixed_t height, int lightlevel);
dboolean bg_LitSpan(draw_span_vars_t* dsvars);

// Sprites: the colormap a thing at this spot should use.
const lighttable_t* bg_LightSprite(const lighttable_t* colormap, fixed_t x, fixed_t y, fixed_t z, const sector_t* sector);

const lighttable_t* bg_LightVoxel(const lighttable_t* colormap, double x, double y, double z, const double* n, const sector_t* sector);

double bg_LightProbe(double x, double y, double z, const double* n);   // self test
int bg_LightProbeTint(double x, double y, double z);
int bg_LightTestMap(int tint, int mq, int row, int idx);
int bg_LightCount(void);
int bg_LightTypeFor(int doomednum);   // the mobj type of a light thing, -1 if none

// Stationary lights are baked into the level (with line-of-sight shadows) the first frame after a level starts.
void bg_LightsNewLevel(void);   // call when a level has been set up
void bg_LightsBake(void);       // (re)bake now: the stationary light things that exist
dboolean bg_LightLos(double lx, double ly, double lz, double tx, double ty, double tz);   // self test: can light go from A to B?
int bg_LightBakedCount(void);
double bg_LightBakedAt(double x, double y, dboolean ceiling);
int bg_LightWallLayers(int segnum);
int bg_LightWarnSelfTest(int light_x, int door_special, int tag_on_sector);   // warnings raised for a light near a door
// glows round lights (drawn like sprites: r_things.c asks for them, sorts them in and clips them)
int bg_HaloPrepare(void);   // finds this frame's glows; returns how many
dboolean bg_HaloGet(int i, int* x1, int* x2, fixed_t* scale, fixed_t* gx, fixed_t* gy, fixed_t* gz, fixed_t* gzt);
void bg_DrawHalo(int i, const int* floorclip, const int* ceilingclip);
// weather: rain or snow through sky ceilings, thunder and lightning (BGLIGHT `weather`)
void bg_WeatherNewLevel(const char* name);
void bg_WeatherTick(void);                 // once per game tic
int bg_WeatherFlashLevel(void);            // 0..4: how strong a lightning flash is on the screen now
int bg_WeatherKind(void);                  // 0 none, 1 rain, 2 snow
int bg_WeatherCount(void);                 // specks alive (self test)
int bg_WeatherPrepare(void);
dboolean bg_WeatherGet(int i, int* x1, int* x2, fixed_t* scale, fixed_t* gx, fixed_t* gy, fixed_t* gz, fixed_t* gzt);
void bg_DrawWeather(int i, const int* floorclip, const int* ceilingclip);
int bg_SunSelfTest(char* msg, size_t msglen);   // sun rays on made-up geometry; returns failures
int bg_FogSelfTest(char* msg, size_t msglen);   // the fog curve on made-up settings; returns failures
int bg_LightLosSelfTest(char* msg, size_t msglen);   // shadow rays on made-up geometry; returns failures

void bg_FadeColormap(byte* cm, const byte* pal);   // darkest colormap rows fade to black (call on a private copy)

dboolean bg_IsLightEdNum(int ednum);   // is this editor number one of the placeable lights?

#endif
