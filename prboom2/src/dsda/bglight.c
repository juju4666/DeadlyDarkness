//
// Begotten: lights for the software renderer.
//
// Doom darkens every wall column, floor row and sprite by picking one of 32 brightness tables (colormaps) from the
// sector's light level and the distance. A light here is one more input to that choice: it makes the table
// brighter by a fraction that depends on how close the surface is to the light, so the lit area is a glow made of
// bands of the 32 brightness steps, exactly like the rest of the Doom lighting.
//
//   - point lights (a ball of light), spot lights (a cone) and tube lights (a strip: the distance is measured to a
//     line segment, which is what a long kitchen lamp does) are all one function, see bg_Contribution
//   - a light only reaches the sectors it can get into: a flood from its own sector through open two-sided lines,
//     so it does not shine through walls
//   - walls are drawn in vertical runs of equal brightness and floors in horizontal ones, found by sampling the
//     light along the column or span and refining where the brightness step changes
//   - coloured lights: a light with a tint makes the brightness it adds that colour, using lazily built tinted
//     colormaps (a few steps of how much tint x each brightness row), nearest-colour matched to the palette
//   - things placed in the map (doomed numbers from the BGLIGHT lump) and a lantern carried by every player
//
// Nothing here changes the game (it only changes how a frame is drawn), so it is safe in lockstep network games.
//
// Licensed under the GNU GPL v2 or later, like the rest of the engine.
//

#pragma GCC diagnostic ignored "-Wbad-function-cast"
#include <math.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#ifndef _MSC_VER
#include <strings.h>
#endif
#include <ctype.h>
#ifdef _MSC_VER
#define strtok_r strtok_s
#endif
#include <stdio.h>

#include "doomstat.h"
#include "doomtype.h"
#include "info.h"
#include "lprintf.h"
#include "p_mobj.h"
#include "p_tick.h"
#include "p_setup.h"
#include "r_main.h"
#include "r_state.h"
#include "r_fps.h"
#include "r_things.h"
#include "r_patch.h"
#include "tables.h"
#include "v_video.h"
#include "w_wad.h"
#include "z_zone.h"

#include "dsda/configuration.h"
#include "dsda/time.h"
#include "e6y.h"
#include "dsda/mobjinfo.h"
#include "dsda/begotten.h"
#include "dsda/map_format.h"
#include "dsda/aim.h"
#include "dsda/bglight.h"
#include "dsda/bgmonster.h"
#include "s_sound.h"
#include "dsda/mapinfo.h"
#include "hexen/po_man.h"

typedef struct { double yaw, pitch; unsigned long long us; dboolean valid; } bg_aim_t;
static bg_aim_t fl_aim[MAX_MAXPLAYERS];
#define BG_FLASHLIGHT_LAG 0.075            // seconds the beam takes to catch up (time constant)
#define BG_FLASHLIGHT_PITCH_SIGN (-1.0)   // dsda pitch is positive when looking down; the light pitch is positive up
typedef unsigned long long bg_key_t;   // what a run is drawn with: light row, tint, fog (see bg_LitKey)
#define BG_MAXLIGHTS 12
#define BG_MAXDEFS 56
#define BG_SAMPLE_STEP 16       // pixels between brightness samples along a column or span

enum { BGL_POINT, BGL_SPOT, BGL_TUBE };

typedef struct
{
  int doomednum;
  char name[24];
  int type;
  double radius, strength;
  double height;
  dboolean from_ceiling;
  double tube_length;
  double spot_inner, spot_outer;   // half angles, degrees (0 = no cone)
  double pitch;                    // degrees, -90 is straight down
  double spin;                     // degrees per second the cone / tube turns round (BGLIGHT `spin`; movable lights only)
  int flicker;                     // 0 steady, 1 flicker, 2 slow pulse
  double flick_rate;               // flicker: changes per second
  double flick_depth;              // how far it dims: 0.3 = down to 70%
  double flick_period;             // pulse: seconds per cycle
  int tint;                        // 0 = white
  int mobjtype;
  dboolean movable;                // live every frame (lifts, doors...) instead of baked into the level
  double halo;                     // size of the glow drawn round the light, in map units (0 = none)
  double corona;                   // radius in screen pixels of the corona (a bright star that keeps its size at any distance), 0 = none
  double cnear, cfar;              // the corona is invisible nearer than cnear map units and full beyond cfar
  dboolean shadow;                 // movable light that casts shadows (a ray to every lit point; BGLIGHT `shadow`)
  double glow_radius, glow_strength; // a small point light at the light itself (BGLIGHT `glow`), e.g. the lamp of a gyro beacon
  double side, drop;               // flashlight only: held this far to the right of and below the eye (map units), so that shadows show
} bg_lightdef_t;

static void bg_CheckTintCache(void);
static void bg_FogPrepare(void);
static void bg_FogBuildLut(int zone);
static int bg_BaseRow(const lighttable_t* cm);
struct bg_light_s;
static int bg_StaticIndex(const mobj_t* mo);
static int bg_PrepStatics(void);   // marks the baked lights near the view; returns how many
static void bg_WorldLosEnsure(void);
static void bg_CollectOccluders(void);
static dboolean thing_shadows_on = true;   // BGLIGHT `thingshadows off` turns the things' shadows off for the whole wad
static dboolean bg_ClearPath(double x0, double y0, double z0, double x1, double y1, double z1);

typedef struct
{
  double a[3], b[3];               // the light is the segment a-b (a point when they are equal)
  double radius, r2, strength;
  dboolean omni;
  double dir[3], cos_in, cos_out;
  double col[3];                   // colour, largest channel 1 (white: 1 1 1)
  int sector;
  int* stamp;                      // stamp[s] == frame_id: this light can reach sector s
  double flick;
  double halo;                     // glow size, see bg_lightdef_t
  double corona, cnear, cfar;
  dboolean sun;                    // a directional light (BGLIGHT `sun`): no position, `sdir` points toward the sun
  double sdir[3];
  dboolean shadow;                 // live light that is blocked by walls, steps and doors
} bg_light_t;

static const char bg_default_lights[] =
  "# color NAME R G B   (0-255; only the proportions matter)\n"
  "color warm 255 150 70\n"
  "color candle 255 205 140\n"
  "color cool 190 215 255\n"
  "color green 120 255 150\n"
  "color red 255 70 60\n"
  "color blue 90 120 255\n"
  "color violet 190 110 255\n"
  "color amber 255 170 50\n"
  "# light DOOMED NAME radius R strength S height H [color NAME] [flags]   (20101-20108 are STATIONARY: baked\n"
  "# into the level with real shadows; add the flag `movable` for a light that must change every frame)\n"
  "light 20101 torch  halo 36 corona 16 radius 320 strength 0.9 height 48 color warm flicker\n"
  "light 20102 lamp   halo 44 corona 20 radius 480 strength 1.0 height 32\n"
  "light 20103 spot   radius 640 strength 1.0 height 24 ceiling spot 25 50 pitch -90\n"
  "light 20104 strip  radius 384 strength 1.0 height 8 ceiling tube 128 spot 40 85 pitch -90 color cool\n"
  "light 20105 candle halo 20 corona 9 radius 200 strength 0.8 height 20 color candle flicker\n"
  "light 20106 green  halo 40 corona 16 radius 400 strength 0.9 height 40 color green pulse\n"
  "light 20107 red    halo 40 corona 16 radius 400 strength 0.9 height 40 color red pulse\n"
  "light 20108 blue   halo 40 corona 16 radius 400 strength 0.9 height 40 color blue\n"
  "# the same lights again as MOVABLE (20201-20208): live every frame, for lights on lifts and doors\n"
  "light 20201 torch_m  halo 36 corona 16 radius 320 strength 0.9 height 48 color warm flicker movable\n"
  "light 20202 lamp_m   halo 44 corona 20 radius 480 strength 1.0 height 32 movable\n"
  "light 20203 spot_m   radius 640 strength 1.0 height 24 ceiling spot 25 50 pitch -90 movable\n"
  "light 20204 strip_m  radius 384 strength 1.0 height 8 ceiling tube 128 spot 40 85 pitch -90 color cool movable\n"
  "light 20205 candle_m halo 20 corona 9 radius 200 strength 0.8 height 20 color candle flicker movable\n"
  "light 20206 green_m  halo 40 corona 16 radius 400 strength 0.9 height 40 color green pulse movable\n"
  "light 20207 red_m    halo 40 corona 16 radius 400 strength 0.9 height 40 color red pulse movable\n"
  "light 20208 blue_m   halo 40 corona 16 radius 400 strength 0.9 height 40 color blue movable\n"
  "# gyro lights (20301-20303): a beam that turns round, like an emergency beacon. Always movable. `spin` is degrees per second\n"
  "# (negative turns the other way; a placed gyro's first argument sets it, in tens of degrees per second); `glow RADIUS STRENGTH`\n"
  "# adds a small round light at the lamp itself; the thing's angle is where the beam points at the start, so two things 180 degrees apart\n"
  "# make the two-beam kind. Thing_Deactivate / Thing_Activate (or the editor's Dormant flag) switch it off and on.\n"
  "light 20301 gyro_red   halo 28 corona 12 radius 480 strength 1.0 height 24 ceiling spot 12 30 pitch -20 color red   spin 480 glow 320 0.6\n"
  "light 20302 gyro_amber halo 28 corona 12 radius 480 strength 1.0 height 24 ceiling spot 12 30 pitch -20 color amber spin 480 glow 320 0.6\n"
  "light 20303 gyro_blue  halo 28 corona 12 radius 480 strength 1.0 height 24 ceiling spot 12 30 pitch -20 color blue  spin 480 glow 320 0.6\n"
  "lantern radius 150 strength 0.7 height 52 color candle\n"
  "# lights that follow the action: projectiles (colour from their own sprite unless a colour is given), the\n"
  "# explosion of a projectile, and the flash of a player's gun\n"
  "missile radius 230 strength 0.85 halo 28 corona 9 color auto\n"
  "explosion radius 420 strength 1.0 halo 80 color auto\n"
  "muzzle radius 340 strength 0.9 height 0 color warm\n"
  "# power-ups on the floor glow (slow pulse) in the colour of their sprite, until they are picked up\n"
  "powerup radius 220 strength 0.8 height 24 halo 26 color auto\n"
  "# the flash of a bullet puff or a chainsaw spark: small, warm and quick\n"
  "puff radius 120 strength 0.75 height 0 color warm\n"
  "# the flashlight the light amplification pickup gives: a cone along the player's view (spot INNER OUTER = half angles)\n"
  "flashlight radius 760 strength 1.0 height 41 spot 13 32 color cool\n";

static bg_lightdef_t defs[BG_MAXDEFS];
static int num_defs;
static int first_type = -1;
static bg_lightdef_t lantern = { 0, "lantern", BGL_POINT, 150, 0.7, 52, false, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1 };
static bg_lightdef_t dyn_missile = { 0, "missile", BGL_POINT, 230, 0.85, 0, false, 0, 0, 0, 0, 0, 0, 0, 0, -1, -1 };
static bg_lightdef_t dyn_explosion = { 0, "explosion", BGL_POINT, 420, 1.0, 0, false, 0, 0, 0, 0, 0, 0, 0, 0, -1, -1 };
static bg_lightdef_t dyn_powerup = { 0, "powerup", BGL_POINT, 220, 0.8, 24, false, 0, 0, 0, 0, 2, 0, 0.3, 2.0, -1, -1 };
static bg_lightdef_t dyn_puff = { 0, "puff", BGL_POINT, 120, 0.75, 0, false, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1 };
static bg_lightdef_t dyn_flashlight = { 0, "flashlight", BGL_SPOT, 760, 1.0, 41, false, 0, 13, 32, 0, 0, 0, 0, 0, 0, -1 };
static bg_lightdef_t dyn_muzzle = { 0, "muzzle", BGL_POINT, 340, 0.9, 0, false, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1 };

// fog: a per-map haze that fades what is far away into a colour (BGLIGHT command `fog`). Lights scatter in it: the
// haze takes on the light (and colour) that falls on the surface behind it.
#define BG_MAXFOGS 16
#define BG_FOG_STEPS 12
#define BG_FOG_LIGHT_STEPS 7              // how much light the fog gets, in steps
typedef struct { char map[9]; int tag; double col[3], density, scatter, max, dark; } bg_fogdef_t;   // tag -1: the level's own
static bg_fogdef_t fogdefs[BG_MAXFOGS];
static int num_fogdefs;
static dboolean fog_active;                    // this level has fog
static dboolean fog_on;                        // fog is on in this frame (level has it, option on, software)
// fog zones: the level's own fog and one per sector tag that has its own settings (zone 0 = no fog)
#define BG_FOGZONES 16
#define BG_FOG_LUT 2048
#define BG_FOG_LUT_STEP 4.0                // map units between table entries
typedef struct { double col[3], density, scatter, max, dark; double lut[BG_FOG_LUT + 1]; } bg_fogzone_t;
static bg_fogzone_t fogz[BG_FOGZONES];
static int num_fogz = 1;                       // zones in use, counting the empty zone 0
static unsigned char* sector_zone;             // the zone of each sector
static int sector_zone_n;
static int view_zone;                          // the zone the viewer is in this frame
static dboolean fog_multi;                     // sectors differ in their fog (else every sector has the same zone)
static int sector_zone_default;                // the zone every sector has when !fog_multi

#define BG_FOG_SLOTS 4096
#define BG_FOG_HASH 8192
static byte (*fog_pool)[256];
static bg_key_t* fog_hkey;
static short* fog_hidx;
static int fog_count;

static void bg_FogCacheClear(void)
{
  if (fog_hkey)
    memset(fog_hkey, 0, sizeof(bg_key_t) * BG_FOG_HASH);
  fog_count = 0;
}

// sun: a directional light for one level (BGLIGHT command `sun`), baked like the stationary lights
typedef struct { char map[9]; double angle, elev, strength; int tint; } bg_sundef_t;
static bg_sundef_t sundefs[BG_MAXFOGS];
static int num_sundefs;
static dboolean sun_active;
static double sun_angle, sun_elev, sun_strength;
static int sun_tint;

// weather: rain or snow on one level (BGLIGHT command `weather`), with optional thunder
typedef struct { char map[9]; int kind; double density, wind, thunder; } bg_weatherdef_t;   // kind 1 rain, 2 snow
static bg_weatherdef_t weatherdefs[BG_MAXFOGS];
static int num_weatherdefs;
static int weather_kind;                    // 0 none
static double weather_density, weather_wind, weather_thunder;

#define BG_MAXTINTS 16
static struct { char name[16]; double c[3]; } tints[BG_MAXTINTS];   // c: 0..1, the largest channel is 1
static int num_tints = 1;                                          // 0 is white
static dboolean tints_ready;

static int bg_FindTint(const char* name)
{
  int i;

  if (!tints_ready)
  {
    tints[0].c[0] = tints[0].c[1] = tints[0].c[2] = 1.0;
    tints_ready = true;
  }

  for (i = 1; i < num_tints; ++i)
    if (!strcasecmp(tints[i].name, name))
      return i;
  return 0;
}

static bg_light_t lights[BG_MAXLIGHTS];
static int num_lights;
static int* stamps[BG_MAXLIGHTS];
static int stamp_sectors;
static int frame_id;
static dboolean frame_active;
static int* flood_stack;

#define BG_FLAT_STEP 8.0        // map units between floor / ceiling samples
#define BG_WALL_STEP 8.0        // ... and between wall samples, along and up
#define BG_MAXSTATIC 128
#define BG_LOS_CELL 64.0
#define BG_MAXWALLBAKED 24      // baked lights blended on one wall column

typedef struct { int w, h; double x0, y0, step; byte* v; } bg_grid_t;   // step: map units between samples (0: BG_FLAT_STEP)
typedef struct { int light, w, h; double z0; byte* v; byte* colmax; } bg_wlayer_t;   // v[i * h + j]: along i, up j
typedef struct { int n; bg_wlayer_t* l; } bg_seglay_t;

typedef struct
{
  bg_light_t g;
  bg_lightdef_t def;
  const mobj_t* mo;
  fixed_t mx, my;
  int type;
  unsigned seed;
  int seen;                     // frame the thing was last seen: a shot-out lamp stops lighting
  double flick;
  double reach;
  dboolean sun;                 // the level's sun (see bg_BakeSun): no thing, always seen
  bg_grid_t floor, ceil;
} bg_slight_t;

static bg_slight_t* sl;
static int num_sl;
static int sv[BG_MAXSTATIC], num_sv;
static bg_seglay_t* seglay;
static int seglay_n;
static int level_serial, bake_serial = -1;
static int* line_seen;
static int line_seen_n, ray_id;
static int bake_warnings;
static dboolean bg_warn_any_format;   // self test only


//
// Definitions
//

static void bg_ParseLightText(const char* text_in)
{
  char* text = strdup(text_in);
  char* line = text;

  while (line && *line)
  {
    char* next = strchr(line, '\n');
    char* hash;
    char* tok;
    char* save = NULL;

    if (next)
      *next++ = '\0';
    hash = strchr(line, '#');
    if (hash)
      *hash = '\0';
    hash = strstr(line, "//");
    if (hash)
      *hash = '\0';

    tok = strtok_r(line, " \t\r", &save);
    if (tok && !strcasecmp(tok, "color"))
    {
      char* nm = strtok_r(NULL, " \t\r", &save);
      char* cr = strtok_r(NULL, " \t\r", &save);
      char* cg = strtok_r(NULL, " \t\r", &save);
      char* cb = strtok_r(NULL, " \t\r", &save);

      if (nm && cr && cg && cb)
      {
        double r = atof(cr), g = atof(cg), b = atof(cb), mx = r > g ? r : g;
        int t = bg_FindTint(nm);

        if (b > mx) mx = b;
        if (mx > 0)
        {
          if (!t && num_tints < BG_MAXTINTS)
            t = num_tints++;
          if (t)
          {
            snprintf(tints[t].name, sizeof tints[t].name, "%s", nm);
            tints[t].c[0] = r / mx; tints[t].c[1] = g / mx; tints[t].c[2] = b / mx;
          }
        }
      }
    }
    else if (tok && !strcasecmp(tok, "light"))
    {
      bg_lightdef_t d;
      char* w;

      memset(&d, 0, sizeof d);
      d.radius = 256;
      d.strength = 1;
      d.height = 32;
      d.mobjtype = -1;
      w = strtok_r(NULL, " \t\r", &save);
      d.doomednum = w ? atoi(w) : 0;
      w = strtok_r(NULL, " \t\r", &save);
      snprintf(d.name, sizeof d.name, "%s", w ? w : "light");
      while ((w = strtok_r(NULL, " \t\r", &save)))
      {
        char* v;

        if (!strcasecmp(w, "ceiling")) d.from_ceiling = true;
        else if (!strcasecmp(w, "movable")) d.movable = true;
        else if (!strcasecmp(w, "shadow")) d.shadow = true;
        else if (!strcasecmp(w, "flicker")) d.flicker = 1;
        else if (!strcasecmp(w, "pulse")) d.flicker = 2;
        else if (!strcasecmp(w, "rate") && (v = strtok_r(NULL, " \t\r", &save))) d.flick_rate = atof(v);
        else if (!strcasecmp(w, "depth") && (v = strtok_r(NULL, " \t\r", &save))) d.flick_depth = atof(v);
        else if (!strcasecmp(w, "period") && (v = strtok_r(NULL, " \t\r", &save))) d.flick_period = atof(v);
        else if (!strcasecmp(w, "radius") && (v = strtok_r(NULL, " \t\r", &save))) d.radius = atof(v);
        else if (!strcasecmp(w, "strength") && (v = strtok_r(NULL, " \t\r", &save))) d.strength = atof(v);
        else if (!strcasecmp(w, "height") && (v = strtok_r(NULL, " \t\r", &save))) d.height = atof(v);
        else if (!strcasecmp(w, "halo") && (v = strtok_r(NULL, " \t\r", &save))) d.halo = atof(v);
        else if (!strcasecmp(w, "corona") && (v = strtok_r(NULL, " \t\r", &save))) d.corona = atof(v);
        else if (!strcasecmp(w, "coronafade"))
        {
          char* v1 = strtok_r(NULL, " \t\r", &save);
          char* v2 = strtok_r(NULL, " \t\r", &save);

          if (v1 && v2)
            d.cnear = atof(v1), d.cfar = atof(v2);
        }
        else if (!strcasecmp(w, "color") && (v = strtok_r(NULL, " \t\r", &save))) d.tint = bg_FindTint(v);
        else if (!strcasecmp(w, "pitch") && (v = strtok_r(NULL, " \t\r", &save))) d.pitch = atof(v);
        else if (!strcasecmp(w, "glow"))
        {
          char* v1 = strtok_r(NULL, " \t\r", &save);
          char* v2 = strtok_r(NULL, " \t\r", &save);

          if (v1 && v2)
            d.glow_radius = atof(v1), d.glow_strength = atof(v2);
        }
        else if (!strcasecmp(w, "spin") && (v = strtok_r(NULL, " \t\r", &save)))
        {
          d.spin = atof(v);
          if (d.spin != 0.0)
            d.movable = true;     // a turning light has to be worked out every frame
        }
        else if (!strcasecmp(w, "tube") && (v = strtok_r(NULL, " \t\r", &save)))
        {
          d.tube_length = atof(v);
          d.type = BGL_TUBE;
        }
        else if (!strcasecmp(w, "spot"))
        {
          char* v1 = strtok_r(NULL, " \t\r", &save);
          char* v2 = strtok_r(NULL, " \t\r", &save);

          if (v1 && v2)
          {
            d.spot_inner = atof(v1);
            d.spot_outer = atof(v2);
            if (d.type == BGL_POINT)
              d.type = BGL_SPOT;
          }
        }
      }
      if (d.doomednum > 0 && d.radius > 0)
      {
        int i;

        for (i = 0; i < num_defs; ++i)   // a later definition replaces an earlier one with the same number
          if (defs[i].doomednum == d.doomednum)
            break;
        if (i < BG_MAXDEFS)
        {
          d.mobjtype = i < num_defs ? defs[i].mobjtype : -1;
          defs[i] = d;
          if (i == num_defs)
            ++num_defs;
        }
      }
    }
    else if (tok && !strcasecmp(tok, "fog"))
    {
      bg_fogdef_t f;
      char* w;
      int i;

      memset(&f, 0, sizeof f);
      f.col[0] = 40; f.col[1] = 46; f.col[2] = 58;
      f.density = 0.0012; f.scatter = 0.8; f.max = 0.97; f.dark = 0.1;
      f.tag = -1;
      while ((w = strtok_r(NULL, " \t\r", &save)))
      {
        char* v;

        if (!strcasecmp(w, "map") && (v = strtok_r(NULL, " \t\r", &save))) snprintf(f.map, sizeof f.map, "%s", v);
        else if (!strcasecmp(w, "color"))
        {
          char* g; char* b;

          if ((v = strtok_r(NULL, " \t\r", &save)) && (g = strtok_r(NULL, " \t\r", &save)) && (b = strtok_r(NULL, " \t\r", &save)))
            f.col[0] = atof(v), f.col[1] = atof(g), f.col[2] = atof(b);
        }
        else if (!strcasecmp(w, "sector") && (v = strtok_r(NULL, " \t\r", &save))) f.tag = atoi(v);
        else if (!strcasecmp(w, "density") && (v = strtok_r(NULL, " \t\r", &save))) f.density = atof(v);
        else if (!strcasecmp(w, "scatter") && (v = strtok_r(NULL, " \t\r", &save))) f.scatter = atof(v);
        else if (!strcasecmp(w, "max") && (v = strtok_r(NULL, " \t\r", &save))) f.max = atof(v);
        else if (!strcasecmp(w, "dark") && (v = strtok_r(NULL, " \t\r", &save))) f.dark = atof(v);
      }
      for (i = 0; i < 3; ++i)
        f.col[i] = f.col[i] < 0 ? 0 : f.col[i] > 255 ? 255 : f.col[i];
      if (f.max < 0) f.max = 0;
      if (f.max > 1) f.max = 1;
      if (f.dark < 0) f.dark = 0;
      if (f.dark > 1) f.dark = 1;
      for (i = 0; i < num_fogdefs; ++i)
        if (!strcasecmp(fogdefs[i].map, f.map) && fogdefs[i].tag == f.tag)
          break;
      if (i < BG_MAXFOGS)
      {
        fogdefs[i] = f;
        if (i == num_fogdefs)
          ++num_fogdefs;
      }
    }
    else if (tok && !strcasecmp(tok, "sun"))
    {
      bg_sundef_t u;
      char* w;
      int i;

      memset(&u, 0, sizeof u);
      u.angle = 45; u.elev = 40; u.strength = 0.9;
      while ((w = strtok_r(NULL, " \t\r", &save)))
      {
        char* v;

        if (!strcasecmp(w, "map") && (v = strtok_r(NULL, " \t\r", &save))) snprintf(u.map, sizeof u.map, "%s", v);
        else if (!strcasecmp(w, "angle") && (v = strtok_r(NULL, " \t\r", &save))) u.angle = atof(v);
        else if (!strcasecmp(w, "elevation") && (v = strtok_r(NULL, " \t\r", &save))) u.elev = atof(v);
        else if (!strcasecmp(w, "strength") && (v = strtok_r(NULL, " \t\r", &save))) u.strength = atof(v);
        else if (!strcasecmp(w, "color") && (v = strtok_r(NULL, " \t\r", &save))) u.tint = bg_FindTint(v);
      }
      if (u.elev < 3) u.elev = 3;
      if (u.elev > 89) u.elev = 89;
      if (u.strength < 0) u.strength = 0;
      for (i = 0; i < num_sundefs; ++i)
        if (!strcasecmp(sundefs[i].map, u.map))
          break;
      if (i < BG_MAXFOGS)
      {
        sundefs[i] = u;
        if (i == num_sundefs)
          ++num_sundefs;
      }
    }
    else if (tok && !strcasecmp(tok, "weather"))
    {
      bg_weatherdef_t u;
      char* w;
      int i;

      memset(&u, 0, sizeof u);
      u.kind = 1; u.density = 1.0; u.wind = 0; u.thunder = 0;
      while ((w = strtok_r(NULL, " \t\r", &save)))
      {
        char* v;

        if (!strcasecmp(w, "map") && (v = strtok_r(NULL, " \t\r", &save))) snprintf(u.map, sizeof u.map, "%s", v);
        else if (!strcasecmp(w, "rain")) u.kind = 1;
        else if (!strcasecmp(w, "snow")) u.kind = 2;
        else if (!strcasecmp(w, "none")) u.kind = 0;
        else if (!strcasecmp(w, "density") && (v = strtok_r(NULL, " \t\r", &save))) u.density = atof(v);
        else if (!strcasecmp(w, "wind") && (v = strtok_r(NULL, " \t\r", &save))) u.wind = atof(v);
        else if (!strcasecmp(w, "thunder") && (v = strtok_r(NULL, " \t\r", &save))) u.thunder = atof(v);
      }
      if (u.density < 0.1) u.density = 0.1;
      if (u.density > 3.0) u.density = 3.0;
      if (u.wind < -16) u.wind = -16;
      if (u.wind > 16) u.wind = 16;
      if (u.thunder < 0) u.thunder = 0;
      for (i = 0; i < num_weatherdefs; ++i)
        if (!strcasecmp(weatherdefs[i].map, u.map))
          break;
      if (i < BG_MAXFOGS)
      {
        weatherdefs[i] = u;
        if (i == num_weatherdefs)
          ++num_weatherdefs;
      }
    }
    else if (tok && !strcasecmp(tok, "thingshadows"))
    {
      char* v = strtok_r(NULL, " \t\r", &save);

      thing_shadows_on = !(v && !strcasecmp(v, "off"));
    }
    else if (tok && (!strcasecmp(tok, "lantern") || !strcasecmp(tok, "missile") || !strcasecmp(tok, "explosion") ||
                     !strcasecmp(tok, "muzzle") || !strcasecmp(tok, "powerup") ||
                     !strcasecmp(tok, "puff") || !strcasecmp(tok, "flashlight")))
    {
      bg_lightdef_t* t = !strcasecmp(tok, "lantern") ? &lantern : !strcasecmp(tok, "missile") ? &dyn_missile :
                         !strcasecmp(tok, "explosion") ? &dyn_explosion : !strcasecmp(tok, "powerup") ? &dyn_powerup :
                         !strcasecmp(tok, "puff") ? &dyn_puff :
                         !strcasecmp(tok, "flashlight") ? &dyn_flashlight : &dyn_muzzle;
      char* w;

      while ((w = strtok_r(NULL, " \t\r", &save)))
      {
        char* v;

        if (!strcasecmp(w, "radius") && (v = strtok_r(NULL, " \t\r", &save))) t->radius = atof(v);
        else if (!strcasecmp(w, "strength") && (v = strtok_r(NULL, " \t\r", &save))) t->strength = atof(v);
        else if (!strcasecmp(w, "height") && (v = strtok_r(NULL, " \t\r", &save))) t->height = atof(v);
        else if (!strcasecmp(w, "halo") && (v = strtok_r(NULL, " \t\r", &save))) t->halo = atof(v);
        else if (!strcasecmp(w, "corona") && (v = strtok_r(NULL, " \t\r", &save))) t->corona = atof(v);
        else if (!strcasecmp(w, "coronafade"))
        {
          char* v1 = strtok_r(NULL, " \t\r", &save);
          char* v2 = strtok_r(NULL, " \t\r", &save);

          if (v1 && v2)
            t->cnear = atof(v1), t->cfar = atof(v2);
        }
        else if (!strcasecmp(w, "shadow")) t->shadow = true;
        else if (!strcasecmp(w, "noshadow")) t->shadow = false;
        else if (!strcasecmp(w, "offset") && t == &dyn_flashlight)
        {
          char* v2;

          if ((v = strtok_r(NULL, " \t\r", &save)) && (v2 = strtok_r(NULL, " \t\r", &save)))
            t->side = atof(v), t->drop = atof(v2);
        }
        else if (!strcasecmp(w, "color") && (v = strtok_r(NULL, " \t\r", &save)))
          t->tint = !strcasecmp(v, "auto") ? -1 : bg_FindTint(v);
        else if (!strcasecmp(w, "spot") && t == &dyn_flashlight)
        {
          char* v2;

          if ((v = strtok_r(NULL, " \t\r", &save)) && (v2 = strtok_r(NULL, " \t\r", &save)))
            t->spot_inner = atof(v), t->spot_outer = atof(v2);
        }
      }
    }
    line = next;
  }
  free(text);
}

void bg_LightsInit(void)
{
  int i;

  num_defs = 0;
  num_fogdefs = 0;
  num_sundefs = 0;
  num_weatherdefs = 0;
  thing_shadows_on = true;
  dyn_flashlight.shadow = true;
  dyn_flashlight.side = 10;
  dyn_flashlight.drop = 8;   // the player's flashlight casts shadows (BGLIGHT `flashlight noshadow`, or the menu option, turns it off)
  bg_ParseLightText(bg_default_lights);
  for (i = 0; i < numlumps; ++i)
  {
    const char* data;
    int size;
    char* text;

    if (strncasecmp(lumpinfo[i].name, "BGLIGHT", 8))
      continue;
    size = W_LumpLength(i);
    data = W_LumpByNum(i);
    text = Z_Malloc(size + 1);
    memcpy(text, data, size);
    text[size] = '\0';
    bg_ParseLightText(text);
    Z_Free(text);
  }

  // one invisible, non-solid thing type per definition
  for (i = 0; i < num_defs; ++i)
  {
    mobjinfo_t info = mobjinfo[MT_PUFF];

    info.doomednum = defs[i].doomednum;
    info.spawnstate = S_TNT1;
    info.seestate = info.painstate = info.meleestate = info.missilestate = info.deathstate = info.xdeathstate = S_NULL;
    info.raisestate = S_NULL;
    info.flags = MF_NOBLOCKMAP | MF_NOGRAVITY;
    info.radius = 8 * FRACUNIT;
    info.height = 8 * FRACUNIT;
    info.speed = 0;
    info.damage = 0;
    info.seesound = info.attacksound = info.painsound = info.deathsound = info.activesound = 0;
    defs[i].mobjtype = dsda_AppendMobjInfo(&info);
    if (first_type < 0)
      first_type = defs[i].mobjtype;
  }
  lprintf(LO_INFO, "bg_LightsInit: %d light things\n", num_defs);
}

static const bg_lightdef_t* bg_DefForType(int type)
{
  int i;

  for (i = 0; i < num_defs; ++i)
    if (defs[i].mobjtype == type)
      return &defs[i];
  return NULL;
}

//
// Lights in this frame
//

static double bg_Fix(fixed_t f)
{
  return (double) f / 65536.0;
}

// brightness factor of a light now. flicker: random level, a new one `rate` times a second, dimming by up to `depth`.
// pulse: a smooth swing between full and 1 - depth, once every `period` seconds.
static double bg_FlickerFor(const bg_lightdef_t* d, unsigned seed)
{
  if (d->flicker == 1)
  {
    double rate = d->flick_rate > 0 ? d->flick_rate : 11.7, depth = d->flick_depth > 0 ? d->flick_depth : 0.26;
    unsigned step = (unsigned) ((double) leveltime * rate / 35.0);
    unsigned h = (step * 2654435761u) ^ (seed * 40503u);

    h ^= h >> 13;
    h *= 0x5bd1e995u;
    if (depth > 1) depth = 1;
    return 1.0 - depth * (1.0 - (double) ((h >> 8) & 255) / 255.0);
  }
  if (d->flicker == 2)
  {
    double period = d->flick_period > 0 ? d->flick_period : 1.5, depth = d->flick_depth > 0 ? d->flick_depth : 0.4;

    if (depth > 1) depth = 1;
    return 1.0 - depth * 0.5 * (1.0 - sin((double) leveltime / 35.0 * 2.0 * M_PI / period + (double) seed));
  }
  return 1.0;
}

static void bg_MobjPos(const mobj_t* mo, double* x, double* y, double* z)
{
  if (R_ViewInterpolation())
  {
    *x = bg_Fix(mo->PrevX + FixedMul(tic_vars.frac, mo->x - mo->PrevX));
    *y = bg_Fix(mo->PrevY + FixedMul(tic_vars.frac, mo->y - mo->PrevY));
    *z = bg_Fix(mo->PrevZ + FixedMul(tic_vars.frac, mo->z - mo->PrevZ));
  }
  else
  {
    *x = bg_Fix(mo->x);
    *y = bg_Fix(mo->y);
    *z = bg_Fix(mo->z);
  }
}

static double bg_Dist2D(double px, double py, double ax, double ay, double bx, double by)
{
  double dx = bx - ax, dy = by - ay;
  double l2 = dx * dx + dy * dy;
  double t = l2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / l2 : 0;

  if (t < 0) t = 0;
  if (t > 1) t = 1;
  return hypot(px - (ax + dx * t), py - (ay + dy * t));
}

// Which sectors can this light reach? A flood from its own sector through open two-sided lines that pass close
// enough to it. (A closed door has no opening, so it stops the light.)
static void bg_FloodSectors(bg_light_t* L)
{
  int sp = 0;
  int* st = L->stamp;

  if (L->sector < 0 || L->sector >= numsectors)
    return;
  flood_stack[sp++] = L->sector;
  st[L->sector] = frame_id;
  while (sp)
  {
    int s = flood_stack[--sp];
    const sector_t* sec = &sectors[s];
    int i;

    for (i = 0; i < sec->linecount; ++i)
    {
      const line_t* ln = sec->lines[i];
      const sector_t* other;
      int o;
      fixed_t top, bottom;

      if (!(ln->flags & ML_TWOSIDED) || !ln->frontsector || !ln->backsector)
        continue;
      other = ln->frontsector == sec ? ln->backsector : ln->frontsector;
      o = (int) (other - sectors);
      if (st[o] == frame_id)
        continue;
      top = ln->frontsector->ceilingheight < ln->backsector->ceilingheight ?
            ln->frontsector->ceilingheight : ln->backsector->ceilingheight;
      bottom = ln->frontsector->floorheight > ln->backsector->floorheight ?
               ln->frontsector->floorheight : ln->backsector->floorheight;
      if (top <= bottom)
        continue;
      if (bg_Dist2D(0.5 * (L->a[0] + L->b[0]), 0.5 * (L->a[1] + L->b[1]), bg_Fix(ln->v1->x), bg_Fix(ln->v1->y),
                    bg_Fix(ln->v2->x), bg_Fix(ln->v2->y)) > L->radius + 0.5 * hypot(L->b[0] - L->a[0], L->b[1] - L->a[1]))
        continue;
      st[o] = frame_id;
      flood_stack[sp++] = o;
    }
  }
}

// fills in a light (geometry, colour, flicker of this moment) from its definition
static void bg_InitLight(bg_light_t* L, const bg_lightdef_t* d, const mobj_t* mo, double x, double y, double z, double yaw,
                         unsigned seed, const double* colour)
{
  {
    int* keep = L->stamp;

    memset(L, 0, sizeof *L);
    L->stamp = keep;
  }
  L->radius = d->radius;
  L->halo = d->halo;
  L->shadow = d->shadow;
  L->corona = d->corona;
  L->cnear = d->cnear > 0 ? d->cnear : 40.0;
  L->cfar = d->cfar > L->cnear ? d->cfar : L->cnear + 100.0;
  L->r2 = d->radius * d->radius;
  L->strength = d->strength;
  if (colour)
  {
    L->col[0] = colour[0]; L->col[1] = colour[1]; L->col[2] = colour[2];
  }
  else
  {
    int t = d->tint > 0 ? d->tint : 0;

    L->col[0] = tints[t].c[0]; L->col[1] = tints[t].c[1]; L->col[2] = tints[t].c[2];
  }
  L->flick = bg_FlickerFor(d, seed);
  L->a[0] = L->b[0] = x;
  L->a[1] = L->b[1] = y;
  L->a[2] = L->b[2] = z;
  if (d->type == BGL_TUBE)
  {
    double hx = cos(yaw) * d->tube_length * 0.5, hy = sin(yaw) * d->tube_length * 0.5;

    L->a[0] = x - hx; L->a[1] = y - hy;
    L->b[0] = x + hx; L->b[1] = y + hy;
  }
  L->omni = d->spot_outer <= 0;
  if (!L->omni)
  {
    double pitch = d->pitch * M_PI / 180.0;

    L->dir[0] = cos(pitch) * cos(yaw);
    L->dir[1] = cos(pitch) * sin(yaw);
    L->dir[2] = sin(pitch);
    L->cos_in = cos(d->spot_inner * M_PI / 180.0);
    L->cos_out = cos(d->spot_outer * M_PI / 180.0);
  }
  L->sector = mo && mo->subsector ? (int) (mo->subsector->sector - sectors) : -1;
}

static void bg_AddLight(const bg_lightdef_t* d, const mobj_t* mo, double x, double y, double z, double yaw, unsigned seed,
                        const double* colour)
{
  bg_light_t* L;
  double view_x = bg_Fix(viewx), view_y = bg_Fix(viewy);
  double reach = d->radius + 0.5 * d->tube_length;

  if (hypot(x - view_x, y - view_y) > reach + 1800.0)
    return;
  if (num_lights >= BG_MAXLIGHTS)
  {
    // keep the nearest ones: replace the farthest if this one is nearer
    int i, farthest = 0;
    double fd = -1, nd = hypot(x - view_x, y - view_y);

    for (i = 0; i < num_lights; ++i)
    {
      double dd = hypot(0.5 * (lights[i].a[0] + lights[i].b[0]) - view_x, 0.5 * (lights[i].a[1] + lights[i].b[1]) - view_y);

      if (dd > fd)
      {
        fd = dd;
        farthest = i;
      }
    }
    if (nd >= fd)
      return;
    L = &lights[farthest];
  }
  else
    L = &lights[num_lights++];
  bg_InitLight(L, d, mo, x, y, z, yaw, seed, colour);
}

// The colour of a projectile, taken from its sprite: the bright, saturated pixels of the first frame, mixed. Cached
// per thing type. White when the sprite has no colour to speak of.
static double (*mcol)[3];
static unsigned char* mcol_done;
static int mcol_types;

static const double* bg_MobjColour(const mobj_t* mo)
{
  static const double white[3] = { 1, 1, 1 };
  const unsigned char* pal = V_GetPlaypal();
  const state_t* st;
  const spriteframe_t* sf;
  int type = mo->type;

  if (!pal || type < 0 || type >= num_mobj_types)
    return white;
  if (mcol_types != num_mobj_types)
  {
    free(mcol);
    free(mcol_done);
    mcol = calloc(num_mobj_types, sizeof *mcol);
    mcol_done = calloc(num_mobj_types, 1);
    mcol_types = num_mobj_types;
  }
  if (mcol_done[type])
    return mcol[type];
  mcol_done[type] = 1;
  mcol[type][0] = mcol[type][1] = mcol[type][2] = 1.0;
  st = &states[mobjinfo[type].spawnstate];
  if (st->sprite >= 0 && st->sprite < num_sprites && sprites[st->sprite].spriteframes &&
      (st->frame & 0x7fff) < sprites[st->sprite].numframes)
  {
    int lump;
    const rpatch_t* patch;
    double sum[3] = { 0, 0, 0 }, wt = 0;
    int x;

    sf = &sprites[st->sprite].spriteframes[st->frame & 0x7fff];
    lump = sf->lump[0] >= 0 ? sf->lump[0] : sf->lump[1];
    patch = R_PatchByNum(lump + firstspritelump);
    if (patch)
      for (x = 0; x < patch->width; ++x)
      {
        const rcolumn_t* col = &patch->columns[x];
        int pn;

        for (pn = 0; pn < col->numPosts; ++pn)
        {
          const rpost_t* po = &col->posts[pn];
          int k;

          for (k = 0; k < po->length; ++k)
          {
            const unsigned char* c = pal + 3 * col->pixels[po->topdelta + k];
            int mx = c[0] > c[1] ? (c[0] > c[2] ? c[0] : c[2]) : (c[1] > c[2] ? c[1] : c[2]);
            int mn = c[0] < c[1] ? (c[0] < c[2] ? c[0] : c[2]) : (c[1] < c[2] ? c[1] : c[2]);
            double sat = mx ? (double) (mx - mn) / mx : 0, w = sat * sat * (double) mx;

            sum[0] += w * c[0]; sum[1] += w * c[1]; sum[2] += w * c[2];
            wt += w;
          }
        }
      }
    if (wt > 0)
    {
      double m = sum[0] > sum[1] ? (sum[0] > sum[2] ? sum[0] : sum[2]) : (sum[1] > sum[2] ? sum[1] : sum[2]);

      if (m > 0)
        mcol[type][0] = sum[0] / m, mcol[type][1] = sum[1] / m, mcol[type][2] = sum[2] / m;
    }
  }
  return mcol[type];
}

// the floor power-ups: soul sphere, invulnerability, berserk, invisibility, radiation suit, map, light amp, mega sphere
static dboolean bg_IsPowerup(int type)
{
  return type == MT_MISC12 || type == MT_INV || type == MT_MISC13 || type == MT_INS || type == MT_MISC14 ||
         type == MT_MISC15 || type == MT_MISC16 || type == MT_MEGA;
}

// projectiles, their explosions and the flashes of guns
static void bg_AddDynamicLights(void)
{
  thinker_t* th;
  int i;

  for (th = thinkercap.next; th != &thinkercap; th = th->next)
  {
    const mobj_t* mo;
    const bg_lightdef_t* d;
    double x, y, z, fade = 1.0;
    bg_lightdef_t tmp;

    if (th->function != P_MobjThinker)
      continue;
    mo = (const mobj_t*) th;
    if ((first_type >= 0 && mo->type >= first_type && mo->type < first_type + num_defs) || mo->type < 0 ||
        mo->type >= num_mobj_types || !mo->state)
      continue;
    if (mo->flags & MF_MISSILE)
      d = &dyn_missile;
    else if ((mobjinfo[mo->type].flags & MF_MISSILE) && mobjinfo[mo->type].deathstate > 0)
    {
      int idx = (int) (mo->state - states) - mobjinfo[mo->type].deathstate;

      if (idx < 0 || idx > 8)
        continue;
      fade = 1.0 - 0.11 * idx;
      d = &dyn_explosion;
    }
    else if ((mo->flags & MF_SPECIAL) && bg_IsPowerup(mo->type))
      d = &dyn_powerup;
    else if (mo->type == MT_PUFF)
    {
      int idx = (int) (mo->state - states) - S_PUFF1;

      if (idx < 0 || idx > 3)
        continue;
      fade = 1.0 - 0.27 * idx;         // four frames: bright, then dying out
      d = &dyn_puff;
    }
    else
      continue;
    tmp = *d;
    tmp.strength *= fade;
    bg_MobjPos(mo, &x, &y, &z);
    if (d == &dyn_puff)
    {
      // a puff sits in the wall it hit: bring the light 8 units out towards the viewer so the wall itself is lit
      double ox = bg_Fix(viewx) - x, oy = bg_Fix(viewy) - y, ol = hypot(ox, oy);

      if (ol > 8.0)
        x += 8.0 * ox / ol, y += 8.0 * oy / ol;
    }
    bg_AddLight(&tmp, mo, x, y, d == &dyn_powerup ? z + d->height : z + bg_Fix(mo->height) * 0.5 + d->height, 0,
                d == &dyn_powerup ? (unsigned) (mo->x >> 16) : 0,
                d->tint < 0 ? bg_MobjColour(mo) : NULL);
  }

  for (i = 0; i < MAX_MAXPLAYERS; ++i)
  {
    const mobj_t* pm;
    double x, y, z, a;

    if (!playeringame[i] || !(pm = players[i].mo) || pm->health <= 0 || !players[i].psprites[ps_flash].state)
      continue;
    bg_MobjPos(pm, &x, &y, &z);
    a = (double) pm->angle * (2.0 * M_PI / 4294967296.0);
    bg_AddLight(&dyn_muzzle, pm, x + 28.0 * cos(a), y + 28.0 * sin(a), z + 40.0 + dyn_muzzle.height, 0, 0, NULL);
  }

  // the flashlight: the light amplification pickup, a cone along the view; it blinks for its last seconds
  for (i = 0; i < MAX_MAXPLAYERS; ++i)
  {
    const mobj_t* pm;
    double x, y, z, a;
    bg_lightdef_t tmp;
    int left;

    if (!playeringame[i] || !(pm = players[i].mo) || pm->health <= 0)
      continue;
    left = players[i].powers[pw_infrared];
    if (left <= 0 || (left <= 4 * 32 && !(left & 8)))
      continue;
    bg_MobjPos(pm, &x, &y, &z);
    {
      // the cone follows the view with a little lag (like a camera on a spring arm): the aim eases toward where the
      // view points, which also hides the 35 Hz steps of the player's angle. Turned off by `begotten_flashlight_smooth`.
      bg_aim_t* A = &fl_aim[i];
      double ty = (double) (i == displayplayer ? viewangle : pm->angle) * (2.0 * M_PI / 4294967296.0);
      double tp = (double) (int) (i == displayplayer ? viewpitch : dsda_PlayerPitch(&players[i])) * (2.0 * M_PI / 4294967296.0);
      unsigned long long now = dsda_RealTimeUS();
      double dt = A->valid ? (double) ((long long) now - (long long) A->us) / 1000000.0 : 0;

      if (A->valid && dt <= 0 && dsda_IntConfig(dsda_config_begotten_flashlight_smooth))
      {
        /* same millisecond as the last frame: keep the aim as it is */
      }
      else if (!dsda_IntConfig(dsda_config_begotten_flashlight_smooth) || !A->valid || dt > 0.25 || dt < 0)
      {
        A->yaw = ty; A->pitch = tp;                          // off, first frame, or a pause / level change: snap
      }
      else
      {
        double dy = ty - A->yaw, k = 1.0 - exp(-dt / BG_FLASHLIGHT_LAG);

        while (dy > M_PI) dy -= 2.0 * M_PI;
        while (dy < -M_PI) dy += 2.0 * M_PI;
        if (fabs(dy) > 1.6)
          A->yaw = ty;                                       // a teleport or a spin-around: do not swing the beam
        else
          A->yaw += dy * k;
        A->pitch += (tp - A->pitch) * k;
      }
      if (!(A->valid && dt <= 0))
        A->us = now;
      A->valid = true;
      a = A->yaw;
      tmp = dyn_flashlight;
      tmp.shadow = tmp.shadow && dsda_IntConfig(dsda_config_begotten_flashlight_shadows) && !getenv("BGLIGHT_NOSHADOW");   // (the env switch is for tests)
      tmp.pitch = BG_FLASHLIGHT_PITCH_SIGN * A->pitch * (180.0 / M_PI);
    }
    {
      double ox = x + 6.0 * cos(a), oy = y + 6.0 * sin(a), oz = z + tmp.height;

      // with shadows the beam comes from a hand's width to the right of the eye and a little below it: a light at the
      // eye shows exactly what the eye sees, so its shadows would all be hidden behind the things that cast them.
      // (Not when that spot is on the other side of a wall, or the player is looking from a corner.)
      if (tmp.shadow && (tmp.side != 0.0 || tmp.drop != 0.0))
      {
        double sx = ox + tmp.side * sin(a), sy = oy - tmp.side * cos(a), sz = oz - tmp.drop;

        if (bg_ClearPath(ox, oy, oz, sx, sy, sz))
        {
          ox = sx; oy = sy; oz = sz;
        }
      }
      bg_AddLight(&tmp, pm, ox, oy, oz, a, (unsigned) i, NULL);
    }
  }
  for (i = 0; i < MAX_MAXPLAYERS; ++i)
    if (!playeringame[i] || !players[i].mo || players[i].mo->health <= 0 || players[i].powers[pw_infrared] <= 0)
      fl_aim[i].valid = false;                               // it starts straight when picked up again
}

void bg_LightsBeginFrame(void)
{
  thinker_t* th;
  int i;

  frame_active = false;
  fog_on = false;
  num_lights = 0;
  if (!bg_weapons || !V_IsSoftwareMode() || fixedcolormap || !playeringame[0] || numsectors <= 0)
    return;

  if (stamp_sectors != numsectors)
  {
    for (i = 0; i < BG_MAXLIGHTS; ++i)
    {
      free(stamps[i]);
      stamps[i] = calloc(numsectors, sizeof(int));
      lights[i].stamp = stamps[i];
    }
    free(flood_stack);
    flood_stack = malloc(sizeof(int) * (numsectors + 8));
    stamp_sectors = numsectors;
    frame_id = 0;
  }
  ++frame_id;
  for (i = 0; i < BG_MAXLIGHTS; ++i)
    lights[i].stamp = stamps[i];
  if (bake_serial != level_serial)
    bg_LightsBake();

  if (first_type >= 0)
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
      const mobj_t* mo;
      const bg_lightdef_t* d;
      double x, y, z;

      if (th->function != P_MobjThinker)
        continue;
      mo = (const mobj_t*) th;
      if (mo->type < first_type || !(d = bg_DefForType(mo->type)))
        continue;
      if (mo->flags2 & MF2_DORMANT)
        continue;                  // switched off (Thing_Deactivate or the editor's Dormant flag): baked or not, it gives no light
      if ((i = bg_StaticIndex(mo)) >= 0)
      {
        sl[i].seen = frame_id;     // baked: it lights through its grids, nothing to do here
        continue;
      }
      bg_MobjPos(mo, &x, &y, &z);
      if (d->from_ceiling && mo->subsector)
        z = bg_Fix(mo->subsector->sector->ceilingheight) - d->height;
      else
        z += d->height;
      {
        // placed in the map editor: args 0-4 tune this one light (0 = the definition's own value):
        // radius in units of 8, strength in percent, colour as red, green, blue (0-255; 0 0 0 = the definition's colour)
        bg_lightdef_t tmp = *d;
        double col[3];
        const double* cp = NULL;

        // a turning light uses its first argument for the speed instead (tens of degrees a second)
        if (mo->special_args[0] > 0 && tmp.spin != 0.0)
          tmp.spin = (tmp.spin < 0 ? -10.0 : 10.0) * mo->special_args[0];
        else if (mo->special_args[0] > 0)
          tmp.radius = 8.0 * mo->special_args[0];
        if (mo->special_args[1] > 0) tmp.strength = 0.01 * mo->special_args[1];
        if (mo->special_args[2] | mo->special_args[3] | mo->special_args[4])
        {
          double r = mo->special_args[2] & 255, g = mo->special_args[3] & 255, b = mo->special_args[4] & 255;
          double mx = r > g ? (r > b ? r : b) : (g > b ? g : b);

          col[0] = r / mx; col[1] = g / mx; col[2] = b / mx;
          cp = col;
        }
        double yaw = (double) mo->angle * (2.0 * M_PI / 4294967296.0);

        if (tmp.spin != 0.0)       // turning: one full circle every 360 / spin seconds, smooth between tics
          yaw += tmp.spin * (M_PI / 180.0) * ((double) leveltime + (double) tic_vars.frac / 65536.0) / 35.0;
        bg_AddLight(&tmp, mo, x, y, z, yaw,
                    (unsigned) (mo->x >> 16) * 31u + (unsigned) (mo->y >> 16), cp);
        if (tmp.glow_radius > 0 && tmp.glow_strength > 0)
        {
          // the small round light at the lamp itself (the beam alone leaves its own surroundings dark)
          bg_lightdef_t g = tmp;

          g.type = BGL_POINT;
          g.spot_inner = g.spot_outer = 0;
          g.spin = 0;
          g.pitch = 0;
          g.radius = tmp.glow_radius;
          g.strength = tmp.strength * tmp.glow_strength;
          g.halo = 0;
          g.corona = 0;
          g.shadow = false;
          g.glow_radius = 0;
          bg_AddLight(&g, mo, x, y, z, yaw, (unsigned) (mo->x >> 16) * 31u + (unsigned) (mo->y >> 16), cp);
        }
      }
    }

  bg_AddDynamicLights();

  if (dsda_IntConfig(dsda_config_begotten_lantern) && !getenv("BGLIGHT_NOLANTERN"))   // (the env switch is for tests and screenshots)
    for (i = 0; i < MAX_MAXPLAYERS; ++i)
    {
      double x, y, z;

      if (!playeringame[i] || !players[i].mo || players[i].mo->health <= 0)
        continue;
      bg_MobjPos(players[i].mo, &x, &y, &z);
      bg_AddLight(&lantern, players[i].mo, x, y, z + lantern.height, 0, (unsigned) i, NULL);
    }

  for (i = 0; i < num_lights; ++i)
    bg_FloodSectors(&lights[i]);
  i = bg_PrepStatics();     // (always: it also readies the baked lights when live ones are in the frame too)
  fog_on = fog_active && dsda_IntConfig(dsda_config_begotten_fog) && sector_zone_n == numsectors;
  view_zone = 0;
  if (fog_on)
  {
    const subsector_t* vss = R_PointInSubsector(viewx, viewy);

    if (vss)
      view_zone = sector_zone[vss->sector - sectors];
  }
  {
    int j;

    for (j = 0; j < num_lights; ++j)
      if (lights[j].shadow)
      {
        bg_WorldLosEnsure();
        bg_CollectOccluders();
        break;
      }
  }
  frame_active = num_lights > 0 || i > 0 || fog_on;
  if (frame_active)
    bg_CheckTintCache();
  if (fog_count > BG_FOG_SLOTS * 3 / 4)
    bg_FogCacheClear();
  if (fog_on)
    bg_FogPrepare();
}

dboolean bg_LightsActive(void)
{
  return frame_active;
}

//
// How much light does a surface point get?
//

// 0..1 for one light. `n` is the surface normal (NULL for things, which are lit from every side).
static double bg_Contribution(const bg_light_t* L, double px, double py, double pz, const double* n)
{
  double cx = L->a[0], cy = L->a[1], cz = L->a[2];
  double dx, dy, dz, d2, d, i;

  if (L->sun)
  {
    double c = n ? n[0] * L->sdir[0] + n[1] * L->sdir[1] + n[2] * L->sdir[2] : 0.8;   // things: lit from every side

    if (c <= 0)
      return 0;
    i = L->strength * L->flick * (0.35 + 0.65 * c);
    return i > 0.98 ? 0.98 : i;
  }
  if (L->a[0] != L->b[0] || L->a[1] != L->b[1])
  {
    double ex = L->b[0] - L->a[0], ey = L->b[1] - L->a[1];
    double t = ((px - L->a[0]) * ex + (py - L->a[1]) * ey) / (ex * ex + ey * ey);

    if (t < 0) t = 0;
    if (t > 1) t = 1;
    cx = L->a[0] + ex * t;
    cy = L->a[1] + ey * t;
  }
  dx = px - cx;
  dy = py - cy;
  dz = pz - cz;
  d2 = dx * dx + dy * dy + dz * dz;
  if (d2 >= L->r2)
    return 0;
  d = sqrt(d2);
  i = 1.0 - d2 / L->r2;
  i *= i;
  if (d > 0.001)
  {
    if (n)
    {
      double c = -(n[0] * dx + n[1] * dy + n[2] * dz) / d;   // surface normal against the way to the light

      if (c <= 0)
        return 0;
      i *= 0.35 + 0.65 * c;
    }
    if (!L->omni)
    {
      double ca = (L->dir[0] * dx + L->dir[1] * dy + L->dir[2] * dz) / d;

      if (ca <= L->cos_out)
        return 0;
      if (ca < L->cos_in)
      {
        double s = (ca - L->cos_out) / (L->cos_in - L->cos_out);

        i *= s * s * (3.0 - 2.0 * s);
      }
    }
  }
  i *= L->strength * L->flick;
  return i > 0.98 ? 0.98 : i;
}

typedef struct { double dark, sum, sc[3]; } bg_acc_t;

static void bg_AccInit(bg_acc_t* a)
{
  a->dark = 1.0;
  a->sum = 0;
  a->sc[0] = a->sc[1] = a->sc[2] = 0;
}

static void bg_AccAdd(bg_acc_t* a, double c, const double* col)
{
  if (c <= 0)
    return;
  a->sum += c;
  a->sc[0] += c * col[0]; a->sc[1] += c * col[1]; a->sc[2] += c * col[2];
  a->dark *= 1.0 - c;
}

// *colour gets the added lights' colours mixed by how much each contributes (largest channel 1; white when none)
static double bg_AccDone(const bg_acc_t* a, double* colour)
{
  if (colour)
  {
    if (a->sum > 0)
    {
      double mx = a->sc[0] > a->sc[1] ? a->sc[0] : a->sc[1];

      if (a->sc[2] > mx) mx = a->sc[2];
      colour[0] = a->sc[0] / mx; colour[1] = a->sc[1] / mx; colour[2] = a->sc[2] / mx;
    }
    else
      colour[0] = colour[1] = colour[2] = 1.0;
  }
  return 1.0 - a->dark;
}

// the frame's live lights in `set` (indices) at a point in sector `sector` (-1: don't check the sector)
static dboolean bg_ShadowLit(const bg_light_t* L, double px, double py, double pz, const double* n);

static void bg_AccLive(bg_acc_t* a, const int* set, int count, double px, double py, double pz, const double* n, int sector)
{
  int k;

  for (k = 0; k < count; ++k)
  {
    const bg_light_t* L = &lights[set[k]];
    double c;

    if (sector >= 0 && L->stamp[sector] != frame_id)
      continue;
    c = bg_Contribution(L, px, py, pz, n);
    if (c > 0 && L->shadow && !bg_ShadowLit(L, px, py, pz, n))
      continue;
    bg_AccAdd(a, c, L->col);
  }
}

static double bg_Total(const int* set, int count, double px, double py, double pz, const double* n, int sector, double* colour)
{
  bg_acc_t a;

  bg_AccInit(&a);
  bg_AccLive(&a, set, count, px, py, pz, n, sector);
  return bg_AccDone(&a, colour);
}


//
// Tinted colormaps
//
// A coloured light adds its colour to what the surface already has. For a pixel of palette colour S in a row of
// the normal colormap (darkened colour D), a light of strength I and colour C (largest channel 1) gives
//     D(lit row) + S * I * (1 - brightness of the row) * (C - 1)
// i.e. the white light's result with the colour taken out of the part the light added. The tint maps are built for
// 12 steps of "how much was added" and every row, lazily, and thrown away when the palette or colormap changes.
//

#define BG_TINT_STEPS 12

#define BG_COLOUR_LEVELS 5                  // levels per channel: 125 colours
static byte* tint_cache[BG_COLOUR_LEVELS * BG_COLOUR_LEVELS * BG_COLOUR_LEVELS + 1][BG_TINT_STEPS + 1][NUMCOLORMAPS];
// colours -> palette index. The cells get narrower toward black (a cell edge is at 255 * (l / N)^2) because dark
// colours sit close together, and 8-unit cells washed their hues out to grey. Index 255 is never handed out: it is the
// spare dark green of every theme (the automap background), not a colour of the art.
#define BG_CUBE 40
static byte* nearest_cube;
static unsigned char cube_q[256];           // 0..255 -> cell
static byte bg_Nearest(int r, int g, int b);
#define BG_CUBE_AT(r, g, b) nearest_cube[((int) cube_q[(r)] * BG_CUBE + (int) cube_q[(g)]) * BG_CUBE + (int) cube_q[(b)]]
static byte bg_Nearest(int r, int g, int b)
{
  return BG_CUBE_AT(r < 0 ? 0 : r > 255 ? 255 : r, g < 0 ? 0 : g > 255 ? 255 : g, b < 0 ? 0 : b > 255 ? 255 : b);
}
static const byte* cache_pal;
static const lighttable_t* cache_cmap;
static unsigned cache_hash;

static unsigned bg_PalHash(const byte* pal)
{
  unsigned h = 2166136261u;
  int i;

  for (i = 0; i < 768; ++i)
    h = (h ^ pal[i]) * 16777619u;
  return h;
}

static void bg_DropTintCache(void)
{
  int t, m, r;

  for (t = 0; t <= BG_COLOUR_LEVELS * BG_COLOUR_LEVELS * BG_COLOUR_LEVELS; ++t)
    for (m = 0; m <= BG_TINT_STEPS; ++m)
      for (r = 0; r < NUMCOLORMAPS; ++r)
      {
        free(tint_cache[t][m][r]);
        tint_cache[t][m][r] = NULL;
      }
}

static void bg_CheckTintCache(void)
{
  const byte* pal = V_GetPlaypal();
  unsigned h;

  if (!pal)
    return;
  h = bg_PalHash(pal);
  if (pal == cache_pal && h == cache_hash && fullcolormap == cache_cmap && nearest_cube)
    return;
  bg_DropTintCache();
  bg_FogCacheClear();
  cache_pal = pal;
  cache_hash = h;
  cache_cmap = fullcolormap;
  if (!nearest_cube)
    nearest_cube = malloc(BG_CUBE * BG_CUBE * BG_CUBE);
  {
    int r, g, b, v;
    int centre[BG_CUBE];

    for (v = 0; v < 256; ++v)
    {
      int q = (int) (sqrt((double) v / 255.0) * BG_CUBE);

      cube_q[v] = (unsigned char) (q >= BG_CUBE ? BG_CUBE - 1 : q);
    }
    for (v = 0; v < BG_CUBE; ++v)
      centre[v] = (int) (255.0 * ((v + 0.5) / BG_CUBE) * ((v + 0.5) / BG_CUBE) + 0.5);
    for (r = 0; r < BG_CUBE; ++r)
      for (g = 0; g < BG_CUBE; ++g)
        for (b = 0; b < BG_CUBE; ++b)
        {
          int cr = centre[r], cg = centre[g], cb = centre[b], i, best = 0;
          long bd = 1L << 30;

          for (i = 0; i < 255; ++i)
          {
            long dr = pal[i * 3] - cr, dg = pal[i * 3 + 1] - cg, db = pal[i * 3 + 2] - cb;
            long d = dr * dr * 3 + dg * dg * 4 + db * db * 2;   // the eye cares most about green

            if (d < bd)
            {
              bd = d;
              best = i;
            }
          }
          nearest_cube[(r * BG_CUBE + g) * BG_CUBE + b] = (byte) best;
        }
  }
}

// The darkest colormap rows are not black (a ramp's last shade is a faint colour), so a sector at light 0 still shows
// its walls. The last rows fade to true black: row 31 is black, and the fade starts at BG_BLACK_ROW.
#define BG_BLACK_ROW 22
static double bg_RowFade(int row)
{
  static int start = -1;

  if (start < 0)
  {
    const char* e = getenv("BGLIGHT_BLACKROW");

    start = e ? atoi(e) : BG_BLACK_ROW;
    if (start < 1) start = 1;
    if (start > 31) start = 31;
  }
  if (row <= start)
    return 1.0;
  return row >= 31 ? 0.0 : (double) (31 - row) / (double) (31 - start);
}

// Fades the darkest rows of a colormap towards black, in place. Called on a private copy when the colormaps are
// loaded, so every drawing path (ours and the plain one for walls and flats no light reaches) sees the same black.
void bg_FadeColormap(byte* cm, const byte* pal)
{
  int row, i, j;

  if (!cm || !pal)
    return;
  for (row = 0; row < 32; ++row)
  {
    double f = bg_RowFade(row);

    if (f >= 1.0)
      continue;
    for (i = 0; i < 255; ++i)
    {
      const byte* d = pal + 3 * (int) cm[row * 256 + i];
      int r = (int) (d[0] * f + 0.5), g = (int) (d[1] * f + 0.5), b = (int) (d[2] * f + 0.5), best = 0;
      long bd = 1L << 30;

      // too dark to tell apart: true black (otherwise the darkest bluish palette colour wins for every dark hue and
      // far walls turn into flat dark-blue panels)
      if (r < 14 && g < 14 && b < 14)
      {
        cm[row * 256 + i] = 0;
        continue;
      }
      for (j = 0; j < 255; ++j)
      {
        long dr = pal[j * 3] - r, dg = pal[j * 3 + 1] - g, db = pal[j * 3 + 2] - b;
        long dd = dr * dr + dg * dg + db * db;   // plain distance: keeps the hue of what is faded

        if (dd < bd)
        {
          bd = dd;
          best = j;
        }
      }
      cm[row * 256 + i] = (byte) best;
    }
  }
}

static const byte* bg_TintTable(int tint, int mq, int row)   // tint = colour code + 1
{
  byte** slot = &tint_cache[tint][mq][row];

  if (!*slot)
  {
    const byte* pal = cache_pal;
    byte* t = malloc(256);
    double m = (double) mq / BG_TINT_STEPS;
    int i, c;
    double tc[3] = { (double) ((tint - 1) / (BG_COLOUR_LEVELS * BG_COLOUR_LEVELS)) / (BG_COLOUR_LEVELS - 1),
                     (double) (((tint - 1) / BG_COLOUR_LEVELS) % BG_COLOUR_LEVELS) / (BG_COLOUR_LEVELS - 1),
                     (double) ((tint - 1) % BG_COLOUR_LEVELS) / (BG_COLOUR_LEVELS - 1) };

    for (i = 0; i < 256; ++i)
    {
      const byte* d = pal + 3 * (int) cache_cmap[row * 256 + i];
      const byte* sc = pal + 3 * i;
      int out[3];

      for (c = 0; c < 3; ++c)
      {
        double v = d[c] + sc[c] * m * (tc[c] - 1.0);

        out[c] = v < 0 ? 0 : v > 255 ? 255 : (int) (v + 0.5);
      }
      t[i] = BG_CUBE_AT(out[0], out[1], out[2]);
    }
    *slot = t;
  }
  return *slot;
}

// a "key" says which brightness a run is drawn with: the final row, and for a tinted light also which tint at how
// many steps. Runs are split wherever the key changes.
//
// Stationary lights: baked
//
// A light that never moves is worked out once, when the level starts, into small grids: one over the floor and one
// over the ceiling around it, and one for every wall segment it reaches. Each grid sample holds how much of the light
// arrives there after a real line-of-sight test against the map (walls, pillars, steps, closed doors), so solid things
// block the light and cast shadows. In the frame only a lookup is left, times the light's flicker.
//


// the fog of this level: the entries that name it, else those that name no map; one zone for the level's own fog
// (no `sector`) and one for each sector tag. Sectors without their own fog take the level's.
static void bg_FogZonesBuild(const char* name)
{
  int pass, i, s, def_zone = 0, tagzone[BG_FOGZONES], tags[BG_FOGZONES];
  int n = 1;

  fog_active = false;
  bg_FogCacheClear();
  memset(tagzone, 0, sizeof tagzone);
  for (pass = 0; pass < 2; ++pass)          // map-less entries first, then those that name the map (they win)
    for (i = 0; i < num_fogdefs; ++i)
    {
      const bg_fogdef_t* f = &fogdefs[i];
      int z = -1, k;

      if (pass == 0 ? f->map[0] != 0 : (!name || strcasecmp(f->map, name)))
        continue;
      if (f->tag < 0)
        z = def_zone;
      else
        for (k = 1; k < n; ++k)
          if (tags[k] == f->tag)
            z = k;
      if (z <= 0)
      {
        if (n >= BG_FOGZONES)
          continue;
        z = n++;
        if (f->tag < 0)
          def_zone = z;
        else
          tags[z] = f->tag;
      }
      fogz[z].col[0] = f->col[0]; fogz[z].col[1] = f->col[1]; fogz[z].col[2] = f->col[2];
      fogz[z].density = f->density;
      fogz[z].scatter = f->scatter;
      fogz[z].max = f->max;
      fogz[z].dark = f->dark;
    }
  num_fogz = n;
  free(sector_zone);
  sector_zone = calloc(numsectors > 0 ? numsectors : 1, 1);
  sector_zone_n = numsectors;
  for (s = 0; s < numsectors; ++s)
  {
    int z = def_zone, k;

    for (k = 1; k < n; ++k)
      if (k != def_zone && sectors[s].tag != 0 && tags[k] == sectors[s].tag)
        z = k;
    sector_zone[s] = (unsigned char) z;
  }
  for (i = 1; i < n; ++i)
    bg_FogBuildLut(i);
  fog_active = n > 1;
  fog_multi = false;
  sector_zone_default = sector_zone && numsectors > 0 ? sector_zone[0] : 0;
  for (s = 1; s < numsectors; ++s)
    if (sector_zone[s] != sector_zone[0])
      fog_multi = true;
  if (fog_active)
  {
    lprintf(LO_INFO, "BGLIGHT: fog on this level (%d zone%s)", n - 1, n > 2 ? "s" : "");
    for (i = 1; i < n; ++i)
      lprintf(LO_INFO, "%s %s colour %.0f %.0f %.0f density %g scatter %g", i == 1 ? ":" : ";",
              i == def_zone ? "level" : "sector tag", fogz[i].col[0], fogz[i].col[1], fogz[i].col[2], fogz[i].density,
              fogz[i].scatter);
    lprintf(LO_INFO, "\n");
  }
}

void bg_WeatherNewLevel(const char* name);
void bg_LightsNewLevel(void)
{
  const char* name = dsda_MapLumpName(gameepisode, gamemap);
  int i;

  ++level_serial;
  {
    int sbest = -1;

    for (i = 0; i < num_sundefs; ++i)
      if (name && !strcasecmp(sundefs[i].map, name))
        sbest = i;
    if (sbest < 0)
      for (i = 0; i < num_sundefs; ++i)
        if (!sundefs[i].map[0])
          sbest = i;
    sun_active = sbest >= 0;
    if (sun_active)
    {
      sun_angle = sundefs[sbest].angle;
      sun_elev = sundefs[sbest].elev;
      sun_strength = sundefs[sbest].strength;
      sun_tint = sundefs[sbest].tint;
      lprintf(LO_INFO, "BGLIGHT: sun on this level (from %g degrees, %g up, strength %g)\n", sun_angle, sun_elev, sun_strength);
    }
  }
  bg_FogZonesBuild(name);
  bg_WeatherNewLevel(name);
}

// where on the light (a point, or the nearest point of a strip) the light for point (px, py) comes from
static void bg_Emitter(const bg_light_t* L, double px, double py, double* cx, double* cy)
{
  *cx = L->a[0];
  *cy = L->a[1];
  if (L->a[0] != L->b[0] || L->a[1] != L->b[1])
  {
    double ex = L->b[0] - L->a[0], ey = L->b[1] - L->a[1];
    double t = ((px - L->a[0]) * ex + (py - L->a[1]) * ey) / (ex * ex + ey * ey);

    if (t < 0) t = 0;
    if (t > 1) t = 1;
    *cx = L->a[0] + ex * t;
    *cy = L->a[1] + ey * t;
  }
}

// Sun rays (bg_SunReaches) see the sky as open: a sky ceiling does not stop a ray, and a one-sided line is only as
// tall as its sector. They also note the last two-sided line a ray crossed, which says what sector it ended up in.
static dboolean bg_sky_open;
static double bg_cross_t;
static const sector_t* bg_cross_sector;

static double bg_CeilFor(const sector_t* sec)
{
  return bg_sky_open && sec->ceilingpic == skyflatnum ? 1e9 : bg_Fix(sec->ceilingheight);
}

// does this line stop a ray from (lx, ly, lz) to (tx, ty, tz)? One-sided lines do; a two-sided line does where the
// ray is outside its opening (a step, a lowered ceiling, a closed door).
static dboolean bg_LineBlocks(const line_t* ln, double lx, double ly, double lz, double tx, double ty, double tz)
{
  double ax = bg_Fix(ln->v1->x), ay = bg_Fix(ln->v1->y);
  double sx = bg_Fix(ln->v2->x) - ax, sy = bg_Fix(ln->v2->y) - ay;
  double rx = tx - lx, ry = ty - ly;
  double den = rx * sy - ry * sx, qx, qy, t, u, z, bottom, top, c1, c2;

  if (fabs(den) < 1e-9)
    return false;
  qx = ax - lx;
  qy = ay - ly;
  t = (qx * sy - qy * sx) / den;
  u = (qx * ry - qy * rx) / den;
  if (t <= 1e-4 || t >= 1.0 - 1e-4 || u < -1e-6 || u > 1.0 + 1e-6)
    return false;
  z = lz + t * (tz - lz);
  if (!ln->backsector || !ln->frontsector)
  {
    if (bg_sky_open)
    {
      const sector_t* only = ln->frontsector ? ln->frontsector : ln->backsector;

      return !only || z < bg_Fix(only->ceilingheight);
    }
    return true;
  }
  bottom = bg_Fix(ln->frontsector->floorheight > ln->backsector->floorheight ? ln->frontsector->floorheight : ln->backsector->floorheight);
  c1 = bg_CeilFor(ln->frontsector);
  c2 = bg_CeilFor(ln->backsector);
  top = c1 < c2 ? c1 : c2;
  if (z <= bottom || z >= top)
    return true;
  if (bg_sky_open && t > bg_cross_t)
  {
    bg_cross_t = t;
    bg_cross_sector = sx * ry - sy * rx < 0 ? ln->frontsector : ln->backsector;   // the front sector is on the right
  }
  return false;
}

// the lines near a light, in a grid of cells, so that a ray only looks at the lines along its way
typedef struct { double x0, y0; int w, h; int* start; int* items; } bg_los_t;

static void bg_LosBuild(bg_los_t* g, double cx, double cy, double reach)
{
  int pass, i, c, nc;
  int* cursor = NULL;

  g->x0 = floor((cx - reach) / BG_LOS_CELL) * BG_LOS_CELL;
  g->y0 = floor((cy - reach) / BG_LOS_CELL) * BG_LOS_CELL;
  g->w = g->h = (int) (2.0 * reach / BG_LOS_CELL) + 3;
  nc = g->w * g->h;
  g->start = calloc(nc + 1, sizeof(int));
  g->items = NULL;
  for (pass = 0; pass < 2; ++pass)
  {
    if (pass == 1)
    {
      for (c = 0; c < nc; ++c)
        g->start[c + 1] += g->start[c];
      g->items = malloc(sizeof(int) * (g->start[nc] + 1));
      cursor = malloc(sizeof(int) * nc);
      memcpy(cursor, g->start, sizeof(int) * nc);
    }
    for (i = 0; i < numlines; ++i)
    {
      const line_t* ln = &lines[i];
      double x1 = bg_Fix(ln->v1->x), y1 = bg_Fix(ln->v1->y), x2 = bg_Fix(ln->v2->x), y2 = bg_Fix(ln->v2->y);
      int cx0 = (int) floor(((x1 < x2 ? x1 : x2) - g->x0) / BG_LOS_CELL), cx1 = (int) floor(((x1 > x2 ? x1 : x2) - g->x0) / BG_LOS_CELL);
      int cy0 = (int) floor(((y1 < y2 ? y1 : y2) - g->y0) / BG_LOS_CELL), cy1 = (int) floor(((y1 > y2 ? y1 : y2) - g->y0) / BG_LOS_CELL);
      int ix, iy;

      if (cx1 < 0 || cy1 < 0 || cx0 >= g->w || cy0 >= g->h)
        continue;
      if (cx0 < 0) cx0 = 0;
      if (cy0 < 0) cy0 = 0;
      if (cx1 >= g->w) cx1 = g->w - 1;
      if (cy1 >= g->h) cy1 = g->h - 1;
      for (iy = cy0; iy <= cy1; ++iy)
        for (ix = cx0; ix <= cx1; ++ix)
        {
          if (pass == 0)
            ++g->start[iy * g->w + ix + 1];
          else
            g->items[cursor[iy * g->w + ix]++] = i;
        }
    }
  }
  free(cursor);
}

static void bg_LosFree(bg_los_t* g)
{
  free(g->start);
  free(g->items);
  g->start = g->items = NULL;
}

// can a ray from the light to the target get there? (g == NULL: look at every line of the map)
static dboolean bg_Los(const bg_los_t* g, double lx, double ly, double lz, double tx, double ty, double tz)
{
  double dx = tx - lx, dy = ty - ly;
  int ix, iy, ex, ey, sx, sy, guard, i;
  double tdx, tdy, nx, ny;

  if (line_seen_n != numlines)
  {
    free(line_seen);
    line_seen = calloc(numlines + 1, sizeof(int));
    line_seen_n = numlines;
    ray_id = 0;
  }
  if (!g)
  {
    for (i = 0; i < numlines; ++i)
      if (bg_LineBlocks(&lines[i], lx, ly, lz, tx, ty, tz))
        return false;
    return true;
  }
  ++ray_id;
  ix = (int) floor((lx - g->x0) / BG_LOS_CELL);
  iy = (int) floor((ly - g->y0) / BG_LOS_CELL);
  ex = (int) floor((tx - g->x0) / BG_LOS_CELL);
  ey = (int) floor((ty - g->y0) / BG_LOS_CELL);
  sx = dx > 0 ? 1 : -1;
  sy = dy > 0 ? 1 : -1;
  tdx = dx != 0 ? BG_LOS_CELL / fabs(dx) : 1e30;
  tdy = dy != 0 ? BG_LOS_CELL / fabs(dy) : 1e30;
  nx = dx > 0 ? ((ix + 1) * BG_LOS_CELL + g->x0 - lx) / dx : dx < 0 ? (ix * BG_LOS_CELL + g->x0 - lx) / dx : 1e30;
  ny = dy > 0 ? ((iy + 1) * BG_LOS_CELL + g->y0 - ly) / dy : dy < 0 ? (iy * BG_LOS_CELL + g->y0 - ly) / dy : 1e30;
  for (guard = g->w + g->h + 8; guard > 0; --guard)
  {
    if (ix >= 0 && iy >= 0 && ix < g->w && iy < g->h)
    {
      int c = iy * g->w + ix;

      for (i = g->start[c]; i < g->start[c + 1]; ++i)
      {
        int l = g->items[i];

        if (line_seen[l] == ray_id)
          continue;
        line_seen[l] = ray_id;
        if (bg_LineBlocks(&lines[l], lx, ly, lz, tx, ty, tz))
          return false;
      }
    }
    if (ix == ex && iy == ey)
      break;
    if (nx < ny)
    {
      ix += sx;
      nx += tdx;
    }
    else
    {
      iy += sy;
      ny += tdy;
    }
  }
  return true;
}

// Shadows for live lights (the flashlight): one grid over the whole level, built once, that rays are traced through.
// The rays read the sectors' current heights, so a door that opens or a lift that moves changes the shadows.
static bg_los_t world_los;
static int world_serial = -1;

static void bg_WorldLosEnsure(void)
{
  double x0 = 1e30, y0 = 1e30, x1 = -1e30, y1 = -1e30, reach;
  int i;

  if (world_serial == level_serial && world_los.start)
    return;
  bg_LosFree(&world_los);
  for (i = 0; i < numvertexes; ++i)
  {
    double x = bg_Fix(vertexes[i].x), y = bg_Fix(vertexes[i].y);

    if (x < x0) x0 = x;
    if (x > x1) x1 = x;
    if (y < y0) y0 = y;
    if (y > y1) y1 = y;
  }
  if (x1 < x0)
    x0 = y0 = x1 = y1 = 0;
  reach = 0.5 * (x1 - x0 > y1 - y0 ? x1 - x0 : y1 - y0) + 2.0 * BG_LOS_CELL;
  bg_LosBuild(&world_los, 0.5 * (x0 + x1), 0.5 * (y0 + y1), reach);
  world_serial = level_serial;
}

// Things that stop a beam: solid monsters, players, columns, barrels, seen as upright cylinders (radius, height). Gathered
// once a frame, only those near a light that casts shadows.
#define BG_MAXOCC 48
typedef struct { double x, y, r, zb, zt; } bg_occ_t;
static bg_occ_t occ[BG_MAXOCC];
static int num_occ;

static void bg_CollectOccluders(void)
{
  thinker_t* th;

  num_occ = 0;
  if (!thing_shadows_on || !dsda_IntConfig(dsda_config_begotten_thing_shadows) || getenv("BGLIGHT_NOTHINGSHADOW"))   // (the env switch is for tests)
    return;
  for (th = thinkercap.next; th != &thinkercap && num_occ < BG_MAXOCC; th = th->next)
  {
    const mobj_t* mo;
    double x, y, r;
    int j, in_reach = 0;

    if (th->function != P_MobjThinker)
      continue;
    mo = (const mobj_t*) th;
    if (!(mo->flags & MF_SOLID) || (mo->flags & (MF_MISSILE | MF_CORPSE | MF_NOCLIP)) || mo->type < 0 ||
        mo->type >= num_mobj_types || !mo->state || mo->height <= 0 || mo->radius <= 0)
      continue;
    if (first_type >= 0 && mo->type >= first_type && mo->type < first_type + num_defs)
      continue;
    x = bg_Fix(mo->x);
    y = bg_Fix(mo->y);
    r = bg_Fix(mo->radius);
    for (j = 0; j < num_lights && !in_reach; ++j)
      if (lights[j].shadow && hypot(x - lights[j].a[0], y - lights[j].a[1]) < lights[j].radius + r)
        in_reach = 1;
    if (!in_reach)
      continue;
    occ[num_occ].x = x;
    occ[num_occ].y = y;
    occ[num_occ].r = r;
    occ[num_occ].zb = bg_Fix(mo->z);
    occ[num_occ].zt = bg_Fix(mo->z) + bg_Fix(mo->height);
    ++num_occ;
  }
}

// does a thing stand between the light and the target? (a thing the light is inside, or the target is on, never does)
static dboolean bg_ThingBlocks(double lx, double ly, double lz, double tx, double ty, double tz)
{
  double rx = tx - lx, ry = ty - ly, a = rx * rx + ry * ry;
  int i;

  if (a < 1e-9)
    return false;
  for (i = 0; i < num_occ; ++i)
  {
    const bg_occ_t* o = &occ[i];
    double dx = lx - o->x, dy = ly - o->y, ex = tx - o->x, ey = ty - o->y;
    double b, c, disc, sq, t1, t2, z1, z2;

    if (dx * dx + dy * dy < o->r * o->r || ex * ex + ey * ey < (o->r + 2.0) * (o->r + 2.0))
      continue;
    b = 2.0 * (dx * rx + dy * ry);
    c = dx * dx + dy * dy - o->r * o->r;
    disc = b * b - 4.0 * a * c;
    if (disc < 0)
      continue;
    sq = sqrt(disc);
    t1 = (-b - sq) / (2.0 * a);
    t2 = (-b + sq) / (2.0 * a);
    if (t2 <= 0 || t1 >= 1.0)
      continue;
    if (t1 < 0) t1 = 0;
    if (t2 > 1.0) t2 = 1.0;
    z1 = lz + (tz - lz) * t1;
    z2 = lz + (tz - lz) * t2;
    if ((z1 > z2 ? z1 : z2) >= o->zb && (z1 < z2 ? z1 : z2) <= o->zt)
      return true;
  }
  return false;
}

static dboolean bg_ClearPath(double x0, double y0, double z0, double x1, double y1, double z1)
{
  bg_WorldLosEnsure();
  return world_los.start ? bg_Los(&world_los, x0, y0, z0, x1, y1, z1) : false;
}

// can the light reach this point? `n` (the surface normal, NULL for things) moves the end of the ray one unit off the
// surface, as in the bake, so that a wall does not shade itself.
static dboolean bg_ShadowLit(const bg_light_t* L, double px, double py, double pz, const double* n)
{
  double tx = px, ty = py, tz = pz;

  if (!world_los.start)
    return true;
  if (n)
  {
    tx += n[0]; ty += n[1]; tz += n[2];
  }
  if (!bg_Los(&world_los, L->a[0], L->a[1], L->a[2], tx, ty, tz))
    return false;
  return num_occ == 0 || !bg_ThingBlocks(L->a[0], L->a[1], L->a[2], tx, ty, tz);
}

static void bg_BakeFlats(bg_slight_t* S, const bg_los_t* los)
{
  const bg_light_t* L = &S->g;
  double mx = 0.5 * (L->a[0] + L->b[0]), my = 0.5 * (L->a[1] + L->b[1]);
  double x0 = floor((mx - S->reach) / BG_FLAT_STEP) * BG_FLAT_STEP, y0 = floor((my - S->reach) / BG_FLAT_STEP) * BG_FLAT_STEP;
  int w = (int) (2.0 * S->reach / BG_FLAT_STEP) + 3, h = w;
  int ceiling, i, j;

  for (ceiling = 0; ceiling < 2; ++ceiling)
  {
    bg_grid_t* G = ceiling ? &S->ceil : &S->floor;
    double n[3] = { 0, 0, ceiling ? -1.0 : 1.0 };

    G->w = w;
    G->h = h;
    G->x0 = x0;
    G->y0 = y0;
    G->v = calloc(w * h, 1);
    for (j = 0; j < h; ++j)
      for (i = 0; i < w; ++i)
      {
        double x = x0 + i * BG_FLAT_STEP, y = y0 + j * BG_FLAT_STEP, z, c, ex, ey;
        const subsector_t* ss;

        if (fabs(x) > 32000.0 || fabs(y) > 32000.0)
          continue;
        ss = R_PointInSubsector((fixed_t) (x * 65536.0), (fixed_t) (y * 65536.0));
        if (!ss)
          continue;
        z = bg_Fix(ceiling ? ss->sector->ceilingheight : ss->sector->floorheight);
        c = bg_Contribution(L, x, y, z, n);
        if (c <= 0)
          continue;
        bg_Emitter(L, x, y, &ex, &ey);
        if (!bg_Los(los, ex, ey, L->a[2], x, y, z + n[2]))
          continue;
        G->v[j * w + i] = (byte) (c * 255.0 + 0.5);
      }
  }
}

static void bg_BakeWalls(bg_slight_t* S, int index, const bg_los_t* los)
{
  const bg_light_t* L = &S->g;
  double mx = 0.5 * (L->a[0] + L->b[0]), my = 0.5 * (L->a[1] + L->b[1]);
  int s;

  for (s = 0; s < numsegs; ++s)
  {
    const seg_t* sg = &segs[s];
    double ax, ay, bx, by, len, dx, dy, n[3], midx, midy, ex, ey, zlo, zhi;
    int w, h, i, j, any = 0;
    byte* v;
    byte* cm;
    bg_wlayer_t* lay;

    if (!sg->frontsector)
      continue;
    ax = bg_Fix(sg->v1->x); ay = bg_Fix(sg->v1->y);
    bx = bg_Fix(sg->v2->x); by = bg_Fix(sg->v2->y);
    len = hypot(bx - ax, by - ay);
    if (len < 0.001 || bg_Dist2D(mx, my, ax, ay, bx, by) >= S->reach)
      continue;
    dx = (bx - ax) / len;
    dy = (by - ay) / len;
    n[0] = dy; n[1] = -dx; n[2] = 0;                    // the visible side of a seg is on its right
    midx = 0.5 * (ax + bx); midy = 0.5 * (ay + by);
    bg_Emitter(L, midx, midy, &ex, &ey);
    if (n[0] * (ex - midx) + n[1] * (ey - midy) <= 0 && n[0] * (ex - ax) + n[1] * (ey - ay) <= 0 &&
        n[0] * (ex - bx) + n[1] * (ey - by) <= 0)
      continue;                                          // the light is behind this wall
    zlo = bg_Fix(sg->frontsector->floorheight);
    zhi = bg_Fix(sg->frontsector->ceilingheight);
    if (sg->backsector)
    {
      double bf = bg_Fix(sg->backsector->floorheight), bc = bg_Fix(sg->backsector->ceilingheight);

      if (bf < zlo) zlo = bf;
      if (bc > zhi) zhi = bc;
    }
    if (zhi - zlo > 2048.0)
      zhi = zlo + 2048.0;
    w = (int) ceil(len / BG_WALL_STEP) + 1;
    h = (int) ceil((zhi - zlo) / BG_WALL_STEP) + 1;
    if (h < 2) h = 2;
    if (w * h > 400000)
      continue;
    v = calloc(w * h, 1);
    cm = calloc(w, 1);
    for (i = 0; i < w; ++i)
    {
      double u = i * BG_WALL_STEP, px, py;

      if (u > len) u = len;
      px = ax + dx * u;
      py = ay + dy * u;
      for (j = 0; j < h; ++j)
      {
        double z = zlo + j * BG_WALL_STEP, c;

        if (z > zhi) z = zhi;
        c = bg_Contribution(L, px, py, z, n);
        if (c <= 0)
          continue;
        bg_Emitter(L, px, py, &ex, &ey);
        if (!bg_Los(los, ex, ey, L->a[2], px + n[0], py + n[1], z))
          continue;
        v[i * h + j] = (byte) (c * 255.0 + 0.5);
        if (v[i * h + j] > cm[i]) cm[i] = v[i * h + j];
        any = 1;
      }
    }
    if (!any)
    {
      free(v);
      free(cm);
      continue;
    }
    seglay[s].l = realloc(seglay[s].l, sizeof(bg_wlayer_t) * (seglay[s].n + 1));
    lay = &seglay[s].l[seglay[s].n++];
    lay->light = index;
    lay->w = w;
    lay->h = h;
    lay->z0 = zlo;
    lay->v = v;
    lay->colmax = cm;
  }
}

// ---- the sun
// Does sunlight reach a point? The ray goes toward the sun (s: unit vector, up) until it is above every ceiling. A
// sky ceiling is open; any other ceiling, and any wall in the way, stops it. A window lets it in only through its
// opening, so sills and lintels cast shadows. `origin` is the sector the point is in.
static dboolean bg_SunReaches(const bg_los_t* g, const sector_t* origin, double x, double y, double z, double maxz, const double* s)
{
  double t = (maxz + 2.0 - z) / s[2], tx, ty, tz;
  const sector_t* f;
  dboolean ok;

  if (t < 1.0)
    t = 1.0;
  if (t > 40000.0)
    t = 40000.0;
  if (g)       // no need to follow the ray out of the map
  {
    double hix = g->x0 + g->w * BG_LOS_CELL - 1.0, lox = g->x0 + 1.0, hiy = g->y0 + g->h * BG_LOS_CELL - 1.0, loy = g->y0 + 1.0;

    if (s[0] > 1e-9 && x + s[0] * t > hix) t = (hix - x) / s[0];
    else if (s[0] < -1e-9 && x + s[0] * t < lox) t = (lox - x) / s[0];
    if (s[1] > 1e-9 && y + s[1] * t > hiy) t = (hiy - y) / s[1];
    else if (s[1] < -1e-9 && y + s[1] * t < loy) t = (loy - y) / s[1];
    if (t < 1.0)
      t = 1.0;
  }
  tx = x + s[0] * t;
  ty = y + s[1] * t;
  tz = z + s[2] * t;
  bg_sky_open = true;
  bg_cross_t = 0;
  bg_cross_sector = origin;
  ok = bg_Los(g, x, y, z, tx, ty, tz);
  bg_sky_open = false;
  if (!ok)
    return false;
  f = bg_cross_t > 0 ? bg_cross_sector : origin;
  return f && f->ceilingpic == skyflatnum;
}

static void bg_BakeSun(void)
{
  bg_slight_t* S;
  bg_los_t los;
  double minx = 1e30, miny = 1e30, maxx = -1e30, maxy = -1e30, maxz = 0, step = BG_FLAT_STEP, cx, cy, reach;
  double el = sun_elev * M_PI / 180.0, an = sun_angle * M_PI / 180.0, s3[3], n[3] = { 0, 0, 1 };
  int i, j, w, h, sn;

  if (!sun_active || num_sl >= BG_MAXSTATIC)
    return;
  for (i = 0; i < numlines; ++i)
  {
    double x1 = bg_Fix(lines[i].v1->x), y1 = bg_Fix(lines[i].v1->y);

    if (x1 < minx) minx = x1;
    if (x1 > maxx) maxx = x1;
    if (y1 < miny) miny = y1;
    if (y1 > maxy) maxy = y1;
  }
  for (i = 0; i < numsectors; ++i)
    if (bg_Fix(sectors[i].ceilingheight) > maxz)
      maxz = bg_Fix(sectors[i].ceilingheight);
  if (minx > maxx)
    return;
  s3[0] = cos(el) * cos(an);
  s3[1] = cos(el) * sin(an);
  s3[2] = sin(el);
  S = &sl[num_sl];
  memset(S, 0, sizeof *S);
  S->sun = true;
  S->type = -1;
  S->seen = 0;
  S->flick = 1.0;
  S->g.sun = true;
  S->g.omni = true;
  S->g.flick = 1.0;
  S->g.strength = sun_strength;
  S->g.sector = -1;
  S->g.sdir[0] = s3[0]; S->g.sdir[1] = s3[1]; S->g.sdir[2] = s3[2];
  S->g.col[0] = tints[sun_tint].c[0]; S->g.col[1] = tints[sun_tint].c[1]; S->g.col[2] = tints[sun_tint].c[2];
  cx = 0.5 * (minx + maxx);
  cy = 0.5 * (miny + maxy);
  reach = 0.5 * (maxx - minx > maxy - miny ? maxx - minx : maxy - miny) + 96.0;
  bg_LosBuild(&los, cx, cy, reach);

  // floors: one grid over the whole level (coarser when the level is large)
  for (;;)
  {
    w = (int) ((maxx - minx + 32.0) / step) + 3;
    h = (int) ((maxy - miny + 32.0) / step) + 3;
    if ((double) w * h <= 2500000.0)
      break;
    step *= 2.0;
  }
  S->floor.w = w;
  S->floor.h = h;
  S->floor.step = step;
  S->floor.x0 = floor((minx - 16.0) / step) * step;
  S->floor.y0 = floor((miny - 16.0) / step) * step;
  S->floor.v = calloc((size_t) w * h, 1);
  for (j = 0; j < h; ++j)
    for (i = 0; i < w; ++i)
    {
      double x = S->floor.x0 + i * step, y = S->floor.y0 + j * step, c;
      const subsector_t* ss;

      if (fabs(x) > 32000.0 || fabs(y) > 32000.0)
        continue;
      ss = R_PointInSubsector((fixed_t) (x * 65536.0), (fixed_t) (y * 65536.0));
      if (!ss)
        continue;
      c = bg_Contribution(&S->g, x, y, bg_Fix(ss->sector->floorheight), n);
      if (c <= 0 || !bg_SunReaches(&los, ss->sector, x, y, bg_Fix(ss->sector->floorheight) + 1.0, maxz, s3))
        continue;
      S->floor.v[(size_t) j * w + i] = (byte) (c * 255.0 + 0.5);
    }

  // walls facing the sun
  for (sn = 0; sn < numsegs; ++sn)
  {
    const seg_t* sg = &segs[sn];
    double ax, ay, bx, by, len, dx, dy, nn[3], zlo, zhi;
    int sw, sh, a, b, any = 0;
    byte* v;
    byte* cm;
    bg_wlayer_t* lay;

    if (!sg->frontsector)
      continue;
    ax = bg_Fix(sg->v1->x); ay = bg_Fix(sg->v1->y);
    bx = bg_Fix(sg->v2->x); by = bg_Fix(sg->v2->y);
    len = hypot(bx - ax, by - ay);
    if (len < 0.001)
      continue;
    dx = (bx - ax) / len;
    dy = (by - ay) / len;
    nn[0] = dy; nn[1] = -dx; nn[2] = 0;
    if (nn[0] * s3[0] + nn[1] * s3[1] <= 0)
      continue;                                          // facing away from the sun
    zlo = bg_Fix(sg->frontsector->floorheight);
    zhi = bg_Fix(sg->frontsector->ceilingheight);
    if (zhi - zlo > 2048.0)
      zhi = zlo + 2048.0;
    sw = (int) ceil(len / BG_WALL_STEP) + 1;
    sh = (int) ceil((zhi - zlo) / BG_WALL_STEP) + 1;
    if (sh < 2) sh = 2;
    if (sw * sh > 400000)
      continue;
    v = calloc(sw * sh, 1);
    cm = calloc(sw, 1);
    for (a = 0; a < sw; ++a)
    {
      double u = a * BG_WALL_STEP, px, py;

      if (u > len) u = len;
      px = ax + dx * u;
      py = ay + dy * u;
      for (b = 0; b < sh; ++b)
      {
        double z = zlo + b * BG_WALL_STEP, c;

        if (z > zhi) z = zhi;
        c = bg_Contribution(&S->g, px, py, z, nn);
        if (c <= 0)
          continue;
        if (z > zhi - 0.5) z = zhi - 0.5;
        if (z < zlo + 0.5) z = zlo + 0.5;
        if (!bg_SunReaches(&los, sg->frontsector, px + nn[0], py + nn[1], z, maxz, s3))
          continue;
        v[a * sh + b] = (byte) (c * 255.0 + 0.5);
        if (v[a * sh + b] > cm[a]) cm[a] = v[a * sh + b];
        any = 1;
      }
    }
    if (!any)
    {
      free(v);
      free(cm);
      continue;
    }
    seglay[sn].l = realloc(seglay[sn].l, sizeof(bg_wlayer_t) * (seglay[sn].n + 1));
    lay = &seglay[sn].l[seglay[sn].n++];
    lay->light = num_sl;
    lay->w = sw;
    lay->h = sh;
    lay->z0 = zlo;
    lay->v = v;
    lay->colmax = cm;
  }
  bg_LosFree(&los);
  ++num_sl;
}

// ---- warnings: a stationary light must not need to change when something moves
static dboolean bg_MoverSpecial(int sp)
{
  return (sp >= 10 && sp <= 14) || (sp >= 20 && sp <= 30) || (sp >= 35 && sp <= 37) || (sp >= 40 && sp <= 47) ||
         (sp >= 60 && sp <= 69) || sp == 155 || sp == 202 || sp == 203 || sp == 235 || sp == 236 || (sp >= 245 && sp <= 247);
}

static int bg_BakeWarnings(void)
{
  byte* moving;
  int i, k, s;

  if (!bg_warn_any_format && !map_format.hexen && !map_format.zdoom)
    return 0;                     // the door and lift actions below are the Hexen / UDMF ones
  bake_warnings = 0;
  moving = calloc(numsectors, 1);

  for (i = 0; i < numlines; ++i)
  {
    const line_t* ln = &lines[i];

    if (!bg_MoverSpecial(ln->special))
      continue;
    if (ln->special_args[0] > 0)
    {
      for (s = 0; s < numsectors; ++s)
        if (sectors[s].tag == ln->special_args[0])
          moving[s] = 1;
    }
    else if (ln->special <= 14 && ln->backsector)
      moving[ln->backsector - sectors] = 1;
  }
  for (k = 0; k < num_sl; ++k)
  {
    const bg_light_t* L = &sl[k].g;
    double mx = 0.5 * (L->a[0] + L->b[0]), my = 0.5 * (L->a[1] + L->b[1]);
    int found = -1, own = -1;

    if (sl[k].mo && sl[k].mo->subsector && moving[sl[k].mo->subsector->sector - sectors])
      own = (int) (sl[k].mo->subsector->sector - sectors);
    for (i = 0; i < numlines && found < 0 && own < 0; ++i)
    {
      const line_t* ln = &lines[i];

      if (((ln->frontsector && moving[ln->frontsector - sectors]) || (ln->backsector && moving[ln->backsector - sectors])) &&
          bg_Dist2D(mx, my, bg_Fix(ln->v1->x), bg_Fix(ln->v1->y), bg_Fix(ln->v2->x), bg_Fix(ln->v2->y)) < sl[k].reach * 0.9)
        found = (ln->frontsector && moving[ln->frontsector - sectors]) ? (int) (ln->frontsector - sectors) : (int) (ln->backsector - sectors);
    }
    if (own >= 0 || found >= 0)
    {
      ++bake_warnings;
      lprintf(LO_WARN, "BGLIGHT warning: stationary light %d at (%d, %d) is %s a moving sector (sector %d, a door or lift): "
              "the baked light will not follow it - use the movable twin (%d) if this light must change\n",
              sl[k].def.doomednum, (int) mx, (int) my, own >= 0 ? "inside" : "near", own >= 0 ? own : found,
              sl[k].def.doomednum + 100);
    }
  }
  free(moving);
  return bake_warnings;
}

static void bg_BakeFree(void)
{
  int k, s, j;

  for (k = 0; k < num_sl; ++k)
  {
    free(sl[k].floor.v);
    free(sl[k].ceil.v);
  }
  free(sl);
  sl = NULL;
  num_sl = 0;
  if (seglay)
  {
    for (s = 0; s < seglay_n; ++s)
    {
      for (j = 0; j < seglay[s].n; ++j)
      {
        free(seglay[s].l[j].v);
        free(seglay[s].l[j].colmax);
      }
      free(seglay[s].l);
    }
    free(seglay);
  }
  seglay = NULL;
  seglay_n = 0;
}

void bg_LightsBake(void)
{
  thinker_t* th;
  clock_t t0 = clock();
  int k;

  bg_BakeFree();
  bake_serial = level_serial;
  bake_warnings = 0;
  if (first_type < 0 || numsectors <= 0 || !lines)
    return;
  sl = calloc(BG_MAXSTATIC, sizeof *sl);
  seglay_n = numsegs;
  seglay = calloc(numsegs + 1, sizeof *seglay);
  for (th = thinkercap.next; th != &thinkercap && num_sl < BG_MAXSTATIC - 1; th = th->next)
  {
    const mobj_t* mo;
    const bg_lightdef_t* d;
    bg_lightdef_t tmp;
    bg_slight_t* S;
    double col[3], x, y, z;
    const double* cp = NULL;

    if (th->function != P_MobjThinker)
      continue;
    mo = (const mobj_t*) th;
    if (mo->type < first_type || !(d = bg_DefForType(mo->type)) || d->movable)
      continue;
    tmp = *d;
    if (mo->special_args[0] > 0) tmp.radius = 8.0 * mo->special_args[0];
    if (mo->special_args[1] > 0) tmp.strength = 0.01 * mo->special_args[1];
    if (mo->special_args[2] | mo->special_args[3] | mo->special_args[4])
    {
      double r = mo->special_args[2] & 255, g = mo->special_args[3] & 255, b = mo->special_args[4] & 255;
      double mxc = r > g ? (r > b ? r : b) : (g > b ? g : b);

      col[0] = r / mxc; col[1] = g / mxc; col[2] = b / mxc;
      cp = col;
    }
    x = bg_Fix(mo->x);
    y = bg_Fix(mo->y);
    z = bg_Fix(mo->z);
    if (tmp.from_ceiling && mo->subsector)
      z = bg_Fix(mo->subsector->sector->ceilingheight) - tmp.height;
    else
      z += tmp.height;
    S = &sl[num_sl];
    S->def = tmp;
    S->mo = mo;
    S->mx = mo->x;
    S->my = mo->y;
    S->type = mo->type;
    S->seed = (unsigned) (mo->x >> 16) * 31u + (unsigned) (mo->y >> 16);
    bg_InitLight(&S->g, &tmp, mo, x, y, z, (double) mo->angle * (2.0 * M_PI / 4294967296.0), S->seed, cp);
    S->g.flick = 1.0;     // baked at full strength; the flicker is applied in the frame
    S->g.stamp = NULL;
    S->reach = tmp.radius + 0.5 * tmp.tube_length;
    ++num_sl;
  }
  for (k = 0; k < num_sl; ++k)
  {
    bg_los_t los;
    double mx = 0.5 * (sl[k].g.a[0] + sl[k].g.b[0]), my = 0.5 * (sl[k].g.a[1] + sl[k].g.b[1]);

    bg_LosBuild(&los, mx, my, sl[k].reach + 16.0);
    bg_BakeFlats(&sl[k], &los);
    bg_BakeWalls(&sl[k], k, &los);
    bg_LosFree(&los);
  }
  bg_BakeWarnings();
  bg_BakeSun();
  lprintf(LO_INFO, "BGLIGHT: baked %d stationary lights in %.0f ms%s\n", num_sl, 1000.0 * (double) (clock() - t0) / CLOCKS_PER_SEC,
          bake_warnings ? " (see the warnings above)" : "");
}

static int bg_StaticIndex(const mobj_t* mo)
{
  int k;

  for (k = 0; k < num_sl; ++k)
    if (sl[k].mo == mo && sl[k].type == mo->type && sl[k].mx == mo->x && sl[k].my == mo->y)
      return k;
  return -1;
}

// each frame: which baked lights count (their thing still exists, they are near the view), with their flicker
static int bg_PrepStatics(void)
{
  int k;
  double vx = bg_Fix(viewx), vy = bg_Fix(viewy);

  num_sv = 0;
  for (k = 0; k < num_sl; ++k)
  {
    bg_slight_t* S = &sl[k];
    double mx = 0.5 * (S->g.a[0] + S->g.b[0]), my = 0.5 * (S->g.a[1] + S->g.b[1]);

    S->flick = 0;
    S->g.flick = 0;
    if (S->sun)
    {
      S->seen = frame_id;
      S->flick = S->g.flick = 1.0;
      if (num_sv < BG_MAXSTATIC)
        sv[num_sv++] = k;
      continue;
    }
    if (S->seen != frame_id)
      continue;
    S->flick = bg_FlickerFor(&S->def, S->seed);
    S->g.flick = S->flick;
    if (S->flick <= 0 || hypot(mx - vx, my - vy) > S->reach + 1800.0 || num_sv >= BG_MAXSTATIC)
      continue;
    sv[num_sv++] = k;
  }
  return num_sv;
}

// bilinear lookup in a flat's grid: 0..1 (before flicker)
static double bg_GridAt(const bg_grid_t* G, double x, double y)
{
  double st = G->step > 0 ? G->step : BG_FLAT_STEP;
  double fx = (x - G->x0) / st, fy = (y - G->y0) / st;
  int ix = (int) floor(fx), iy = (int) floor(fy);
  const byte* p;
  double ux, uy, a, b;

  if (!G->v || ix < 0 || iy < 0 || ix >= G->w - 1 || iy >= G->h - 1)
    return 0;
  ux = fx - ix;
  uy = fy - iy;
  p = G->v + iy * G->w + ix;
  a = p[0] + (p[1] - p[0]) * ux;
  b = p[G->w] + (p[G->w + 1] - p[G->w]) * ux;
  return (a + (b - a) * uy) * (1.0 / 255.0);
}

// is there baked floor light near this point at all? (used to keep things in shadow dark)
static dboolean bg_GridAny(const bg_grid_t* G, double x, double y)
{
  double st = G->step > 0 ? G->step : BG_FLAT_STEP;
  int ix = (int) floor((x - G->x0) / st), iy = (int) floor((y - G->y0) / st);

  if (!G->v || ix < 0 || iy < 0 || ix >= G->w - 1 || iy >= G->h - 1)
    return false;
  return G->v[iy * G->w + ix] || G->v[iy * G->w + ix + 1] || G->v[(iy + 1) * G->w + ix] || G->v[(iy + 1) * G->w + ix + 1];
}

static double bg_WallAt(const bg_wlayer_t* Y, int iu, double fu, double z)
{
  double jf = (z - Y->z0) / BG_WALL_STEP, fj, a, b;
  int j;
  const byte* p;

  if (jf < 0) jf = 0;
  if (jf > Y->h - 1) jf = Y->h - 1;
  j = (int) jf;
  if (j > Y->h - 2) j = Y->h - 2;
  fj = jf - j;
  p = Y->v + iu * Y->h + j;
  a = p[0] + (p[1] - p[0]) * fj;
  b = p[Y->h] + (p[Y->h + 1] - p[Y->h]) * fj;
  return (a + (b - a) * fu) * (1.0 / 255.0);
}

// test helpers
dboolean bg_LightLos(double lx, double ly, double lz, double tx, double ty, double tz)
{
  return bg_Los(NULL, lx, ly, lz, tx, ty, tz);
}

int bg_LightBakedCount(void)
{
  return num_sl;
}

// the baked floor (ceiling) light at a point, from the lights whose things still exist
double bg_LightBakedAt(double x, double y, dboolean ceiling)
{
  bg_acc_t a;
  int k;

  bg_AccInit(&a);
  for (k = 0; k < num_sl; ++k)
  {
    double v = bg_GridAt(ceiling ? &sl[k].ceil : &sl[k].floor, x, y);

    if (sl[k].seen == frame_id)
      bg_AccAdd(&a, v, sl[k].g.col);
  }
  return bg_AccDone(&a, NULL);
}

// self test of the sun rays on made-up geometry: a window in a wall between a room and the open air
int bg_SunSelfTest(char* msg, size_t msglen)
{
  static vertex_t vtx[4];
  static line_t ln[1];
  static sector_t sec[2];
  line_t* keep_lines = lines;
  int keep_n = numlines, fails = 0;
  double up[3], low[3], over[3];
  double e20 = 20.0 * M_PI / 180.0;

  msg[0] = 0;
  memset(vtx, 0, sizeof vtx);
  memset(ln, 0, sizeof ln);
  memset(sec, 0, sizeof sec);
  vtx[0].x = 0; vtx[0].y = 300 * FRACUNIT;
  vtx[1].x = 600 * FRACUNIT; vtx[1].y = 300 * FRACUNIT;
  sec[0].floorheight = 0; sec[0].ceilingheight = 128 * FRACUNIT; sec[0].ceilingpic = (short) (skyflatnum + 1);   // the room, south of the wall
  sec[1].floorheight = 0; sec[1].ceilingheight = 512 * FRACUNIT; sec[1].ceilingpic = (short) skyflatnum;         // outside, north
  ln[0].v1 = &vtx[0];
  ln[0].v2 = &vtx[1];
  ln[0].frontsector = &sec[0];     // the front sector is to the right of v1 -> v2: south
  ln[0].backsector = &sec[1];
  lines = ln;
  numlines = 1;
  low[0] = 0; low[1] = cos(e20); low[2] = sin(e20);      // a sun low in the north
  up[0] = 0; up[1] = 0.01; up[2] = 1.0;                   // almost overhead
  over[0] = 0; over[1] = -cos(e20); over[2] = sin(e20);  // a sun in the south
#define BG_SUNCHECK(cond, text) do { if (!(cond)) { ++fails; if (!msg[0]) snprintf(msg, msglen, "%s", text); } } while (0)
  BG_SUNCHECK(bg_SunReaches(NULL, &sec[0], 256, 100, 40, 512, low), "sun comes in through a window opening");
  BG_SUNCHECK(!bg_SunReaches(NULL, &sec[0], 256, 100, 40, 512, up), "a solid ceiling shades a room from an overhead sun");
  BG_SUNCHECK(bg_SunReaches(NULL, &sec[1], 256, 400, 1, 512, up), "open air is in the sun");
  sec[1].floorheight = 100 * FRACUNIT;                     // a sill 100 high
  BG_SUNCHECK(!bg_SunReaches(NULL, &sec[0], 256, 100, 10, 512, low), "the sill shades the floor under the window");
  sec[1].floorheight = 0;
  sec[0].ceilingheight = 100 * FRACUNIT;                   // a lintel at 100
  BG_SUNCHECK(!bg_SunReaches(NULL, &sec[0], 256, 100, 90, 512, low), "the lintel shades what is above it");
  BG_SUNCHECK(!bg_SunReaches(NULL, &sec[0], 256, 100, 40, 512, over), "a sun behind the wall does not shine through it");
#undef BG_SUNCHECK
  lines = keep_lines;
  numlines = keep_n;
  return fails;
}

// self test of the shadow rays on made-up geometry: a pillar, a step, a closed door and a low ceiling.
// Returns the number of failures; `msg` names the first.
int bg_LightLosSelfTest(char* msg, size_t msglen)
{
  static vertex_t vtx[16];
  static line_t ln[8];
  static sector_t sec[4];
  line_t* keep_lines = lines;
  int keep_n = numlines, fails = 0;
  int i;
  static const int px[5] = { 224, 288, 288, 224, 224 }, py[5] = { 224, 224, 288, 288, 224 };

  msg[0] = 0;
  memset(vtx, 0, sizeof vtx);
  memset(ln, 0, sizeof ln);
  memset(sec, 0, sizeof sec);
  for (i = 0; i < 5; ++i)
  {
    vtx[i].x = px[i] * FRACUNIT;
    vtx[i].y = py[i] * FRACUNIT;
  }
  sec[0].floorheight = 0; sec[0].ceilingheight = 128 * FRACUNIT;            // the room
  sec[1].floorheight = 24 * FRACUNIT; sec[1].ceilingheight = 128 * FRACUNIT; // a step up
  sec[2].floorheight = 0; sec[2].ceilingheight = 0;                          // a closed door
  sec[3].floorheight = 0; sec[3].ceilingheight = 60 * FRACUNIT;              // a low ceiling
  lines = ln;
  numlines = 4;
#define BG_LOSCHECK(cond, text) do { if (!(cond)) { ++fails; if (!msg[0]) snprintf(msg, msglen, "%s", text); } } while (0)
  for (i = 0; i < 4; ++i)
  {
    ln[i].v1 = &vtx[i];
    ln[i].v2 = &vtx[i + 1];
    ln[i].frontsector = &sec[0];
    ln[i].backsector = NULL;
  }
  BG_LOSCHECK(!bg_Los(NULL, 256, 100, 40, 256, 400, 40), "a pillar blocks the light behind it");
  BG_LOSCHECK(bg_Los(NULL, 100, 100, 40, 100, 400, 40), "light passes beside the pillar");
  // a two-sided line along y = 300 from x = 0 to 600
  vtx[8].x = 0; vtx[8].y = 300 * FRACUNIT; vtx[9].x = 600 * FRACUNIT; vtx[9].y = 300 * FRACUNIT;
  lines = &ln[4];
  numlines = 1;
  ln[4].v1 = &vtx[8];
  ln[4].v2 = &vtx[9];
  ln[4].frontsector = &sec[0];
  ln[4].backsector = &sec[1];
  BG_LOSCHECK(!bg_Los(NULL, 256, 100, 32, 256, 400, 10), "a low ray hits a 24 unit step");
  BG_LOSCHECK(bg_Los(NULL, 256, 100, 32, 256, 400, 60), "a high ray clears the step");
  ln[4].backsector = &sec[2];
  BG_LOSCHECK(!bg_Los(NULL, 256, 100, 40, 256, 400, 40), "a closed door blocks");
  ln[4].backsector = &sec[3];
  BG_LOSCHECK(!bg_Los(NULL, 256, 100, 40, 256, 400, 110), "a ray over a lowered ceiling is blocked");
  BG_LOSCHECK(bg_Los(NULL, 256, 100, 40, 256, 400, 50), "a ray under a lowered ceiling passes");
#undef BG_LOSCHECK
  lines = keep_lines;
  numlines = keep_n;
  return fails;
}

// self test of the door / lift warning on made-up geometry. Returns the number of warnings raised.
int bg_LightWarnSelfTest(int light_x, int door_special, int tag_on_sector)
{
  static vertex_t vtx[4];
  static line_t ln[2];
  static sector_t sec[2];
  static subsector_t ss;
  static mobj_t mo;
  bg_slight_t* keep_sl = sl;
  int keep_nsl = num_sl, keep_ns = numsectors, keep_nl = numlines, n;
  line_t* keep_lines = lines;
  sector_t* keep_sectors = sectors;
  bg_slight_t one;

  memset(vtx, 0, sizeof vtx); memset(ln, 0, sizeof ln); memset(sec, 0, sizeof sec); memset(&one, 0, sizeof one);
  memset(&ss, 0, sizeof ss); memset(&mo, 0, sizeof mo);
  vtx[0].x = 300 * FRACUNIT; vtx[0].y = 0; vtx[1].x = 300 * FRACUNIT; vtx[1].y = 200 * FRACUNIT;
  sec[0].tag = 0; sec[1].tag = tag_on_sector;
  ln[0].v1 = &vtx[0]; ln[0].v2 = &vtx[1];
  ln[0].frontsector = &sec[0]; ln[0].backsector = &sec[1];
  ln[0].special = door_special; ln[0].special_args[0] = tag_on_sector;
  ss.sector = &sec[0];
  mo.subsector = &ss;
  one.mo = &mo;
  one.def.doomednum = 20101;
  one.g.a[0] = one.g.b[0] = light_x;
  one.g.a[1] = one.g.b[1] = 100;
  one.reach = 320;
  sl = &one; num_sl = 1; sectors = sec; numsectors = 2; lines = ln; numlines = 1;
  bg_warn_any_format = true;
  n = bg_BakeWarnings();
  bg_warn_any_format = false;
  sl = keep_sl; num_sl = keep_nsl; sectors = keep_sectors; numsectors = keep_ns; lines = keep_lines; numlines = keep_nl;
  return n;
}

int bg_LightWallLayers(int segnum)
{
  return seglay && segnum >= 0 && segnum < seglay_n ? seglay[segnum].n : 0;
}

static int bg_ColourField(const double* col)
{
  int q[3], c;

  for (c = 0; c < 3; ++c)
    q[c] = (int) (col[c] * (BG_COLOUR_LEVELS - 1) + 0.5);
  if (q[0] == BG_COLOUR_LEVELS - 1 && q[1] == BG_COLOUR_LEVELS - 1 && q[2] == BG_COLOUR_LEVELS - 1)
    return 0;   // white
  return 1 + (q[0] * BG_COLOUR_LEVELS + q[1]) * BG_COLOUR_LEVELS + q[2];
}

// the exact (not yet rounded) brightness of a point: row and tint steps as fractions, so that the rounding can be
// ordered-dithered per pixel
typedef struct
{
  double rf, mf;
  int field;
  double ff;                 // fog fraction 0..1 (0: none)
  double fl;                 // how much light the fog gets here, 0..1 (its colour is `field`, as for the surface)
  int fz;                    // the fog zone
  double am;                 // how much of the haze's own colour is there: 1 in a bright sector, `dark` in a black one
} bg_level_t;

static void bg_LitLevel(bg_level_t* l, int base_row, double total, const double* col)
{
  l->rf = (double) base_row * (1.0 - total);
  if (l->rf < 0)
    l->rf = 0;
  l->field = bg_ColourField(col);
  l->mf = total * (double) base_row / (double) NUMCOLORMAPS * BG_TINT_STEPS;
  l->ff = 0;
  l->fl = 0;
  l->fz = 0;
  l->am = 1.0;
}

static double* inv_cos;                    // 1 / cos(angle of screen column x): ray distance = depth * inv_cos[x]
static int inv_cos_w;

static void bg_FogPrepare(void)           // at the start of a frame with fog
{
  int x;

  if (inv_cos_w != viewwidth || !inv_cos)
  {
    free(inv_cos);
    inv_cos = malloc(sizeof(double) * (viewwidth + 1));
    inv_cos_w = viewwidth;
  }
  for (x = 0; x < viewwidth; ++x)
    inv_cos[x] = 1.0 / cos((double) (int) xtoviewangle[x] * (2.0 * M_PI / 4294967296.0));
}

static void bg_FogBuildLut(int zone)       // at the start of a level, for each fog zone
{
  int i;
  bg_fogzone_t* z = &fogz[zone];

  for (i = 0; i <= BG_FOG_LUT; ++i)
  {
    double f = 1.0 - exp(-z->density * (double) i * BG_FOG_LUT_STEP);

    z->lut[i] = f > z->max ? z->max : f < 0 ? 0 : f;
  }
}

static double bg_FogFraction(int zone, double dist)
{
  int i = (int) (dist * (1.0 / BG_FOG_LUT_STEP) + 0.5);

  return fogz[zone].lut[i < 0 ? 0 : i > BG_FOG_LUT ? BG_FOG_LUT : i];
}

// which fog a surface in `zone` is seen through: the thicker of its own and the viewer's
static int bg_PickZone(int zone)
{
  if (!view_zone)
    return zone;
  if (!zone)
    return view_zone;
  return fogz[view_zone].density > fogz[zone].density ? view_zone : zone;
}

// self test of the fog curve with made-up settings (restores the level's own); returns the number of failures
int bg_FogSelfTest(char* msg, size_t msglen)
{
  bg_fogzone_t keep = fogz[BG_FOGZONES - 1], k1 = fogz[1], k2 = fogz[2];
  double prev = -1, f0, f1, f2, fm, fz2;
  int fails = 0, i, kv = view_zone;

  fogz[BG_FOGZONES - 1].density = 0.002;
  fogz[BG_FOGZONES - 1].max = 0.9;
  bg_FogBuildLut(BG_FOGZONES - 1);
  f0 = bg_FogFraction(BG_FOGZONES - 1, 0);
  f1 = bg_FogFraction(BG_FOGZONES - 1, 400);
  f2 = bg_FogFraction(BG_FOGZONES - 1, 1500);
  fm = bg_FogFraction(BG_FOGZONES - 1, 1e9);
  for (i = 0; i < 4000; i += 37)
  {
    double f = bg_FogFraction(BG_FOGZONES - 1, i);

    if (f < prev - 1e-9)
      ++fails;
    prev = f;
  }
  snprintf(msg, msglen, "none at 0 (%.3f), 1-exp(-0.8) at 400 (%.3f), thicker at 1500 (%.3f), capped at 0.9 (%.3f), never thinner with distance",
           f0, f1, f2, fm);
  if (f0 > 0.001 || fabs(f1 - (1.0 - exp(-0.8))) > 0.01 || f2 <= f1 || fabs(fm - 0.9) > 1e-6)
    ++fails;
  // zones: a surface is seen through the thicker of its own fog and the viewer's
  fogz[1].density = 0.001;
  fogz[2].density = 0.004;
  view_zone = 0;
  fz2 = (bg_PickZone(0) == 0 && bg_PickZone(1) == 1) ? 0 : 1;
  view_zone = 1;
  fz2 += (bg_PickZone(0) == 1 && bg_PickZone(2) == 2) ? 0 : 1;
  view_zone = 2;
  fz2 += (bg_PickZone(1) == 2 && bg_PickZone(0) == 2) ? 0 : 1;
  if (fz2 > 0)
  {
    ++fails;
    snprintf(msg, msglen, "a surface should be seen through the thicker of its own fog and the viewer's");
  }
  view_zone = kv;
  fogz[BG_FOGZONES - 1] = keep;
  fogz[1] = k1;
  fogz[2] = k2;
  return fails;
}

// the fog at a point `dist` away: how much of it, and its colour, which is the haze's own colour plus the light that
// reaches the point (so a lamp makes the haze round it glow, in the lamp's colour)
static void bg_FogLevel(bg_level_t* l, double dist, double total, int zone, int lightlevel)
{
  if (!fog_on)
    return;
  zone = bg_PickZone(zone);
  if (!zone)
    return;
  l->ff = bg_FogFraction(zone, dist);
  l->fz = zone;
  l->fl = fogz[zone].scatter * total > 1.0 ? 1.0 : fogz[zone].scatter * total;
  {
    // the haze is lit light, not a glow of its own: in a dark sector it goes dark too (down to `dark` of its colour)
    double amb = (double) (lightlevel < 0 ? 0 : lightlevel > 255 ? 255 : lightlevel) / 255.0;

    l->am = fogz[zone].dark + (1.0 - fogz[zone].dark) * amb;
  }
}

// thr in [0,1): 0.5 rounds to nearest, an ordered-dither threshold rounds up for that share of the pixels.
// key: bits 0-5 colormap row, 6-10 tint steps, 11-17 tint colour, 18-21 fog steps, 22-24 fog light, 25-31 its colour,
// 32-35 fog zone, 36-39 how much of the haze colour (ambient light)
static bg_key_t bg_LitKey(const bg_level_t* l, double thr)
{
  int row = (int) (l->rf + thr);
  bg_key_t key = (bg_key_t) row;

  if (l->field > 0)
  {
    int mq = (int) (l->mf + thr);

    if (mq > BG_TINT_STEPS)
      mq = BG_TINT_STEPS;
    if (mq > 0)
      key = (bg_key_t) row | ((bg_key_t) mq << 6) | ((bg_key_t) l->field << 11);
  }
  if (l->ff > 0)
  {
    int fs = (int) (l->ff * BG_FOG_STEPS + thr);

    if (fs > BG_FOG_STEPS)
      fs = BG_FOG_STEPS;
    if (fs > 0)
    {
      int lv = (int) (l->fl * BG_FOG_LIGHT_STEPS + thr);

      lv = lv > BG_FOG_LIGHT_STEPS ? BG_FOG_LIGHT_STEPS : lv;
      key |= (bg_key_t) fs << 18 | (bg_key_t) l->fz << 32 | (bg_key_t) (int) (l->am * 15.0 + 0.5) << 36;
      if (lv > 0)
        key |= (bg_key_t) lv << 22 | (bg_key_t) l->field << 25;
    }
  }
  return key;
}

// 8x8 Bayer threshold for a screen pixel, 1/128 .. 127/128
static double bg_Bayer(int x, int y)
{
  int v = 0, i;

  x &= 7;
  y &= 7;
  for (i = 2; i >= 0; --i)
    v = (v << 2) | ((((x >> i) ^ (y >> i)) & 1) << 1) | ((y >> i) & 1);
  return ((double) v + 0.5) / 64.0;
}

// the fogged colormaps made so far, by key; made when first needed, emptied between frames when it fills up (a pointer
// handed out stays good for the whole frame)
static const lighttable_t* bg_FogTable(bg_key_t key, const lighttable_t* base)
{
  unsigned h;
  int i;
  byte* t;
  double f, fc[3];
  const byte* pal = cache_pal;

  if (!pal || !nearest_cube)
    return base;
  if (!fog_hkey)
  {
    fog_hkey = calloc(BG_FOG_HASH, sizeof *fog_hkey);
    fog_hidx = calloc(BG_FOG_HASH, sizeof *fog_hidx);
    fog_pool = malloc((size_t) BG_FOG_SLOTS * 256);
    fog_count = 0;
  }
  h = (unsigned) ((key * 0x9E3779B97F4A7C15ULL) >> 51) & (BG_FOG_HASH - 1);
  while (fog_hkey[h])
  {
    if (fog_hkey[h] == key)
      return fog_pool[fog_hidx[h]];
    h = (h + 1) & (BG_FOG_HASH - 1);
  }
  if (fog_count >= BG_FOG_SLOTS)
    return base;                    // full: no fog for the rest of this frame
  f = (double) ((key >> 18) & 15) / (double) BG_FOG_STEPS;
  {
    const double* zc = fogz[(key >> 32) & 15].col;
    double lv = (double) ((key >> 22) & 7) / (double) BG_FOG_LIGHT_STEPS;
    double amb = (double) ((key >> 36) & 15) / 15.0;
    int field = (int) ((key >> 25) & 127);
    double tc[3] = { 1, 1, 1 };

    if (field > 0)
    {
      tc[0] = (double) ((field - 1) / (BG_COLOUR_LEVELS * BG_COLOUR_LEVELS)) / (BG_COLOUR_LEVELS - 1);
      tc[1] = (double) (((field - 1) / BG_COLOUR_LEVELS) % BG_COLOUR_LEVELS) / (BG_COLOUR_LEVELS - 1);
      tc[2] = (double) ((field - 1) % BG_COLOUR_LEVELS) / (BG_COLOUR_LEVELS - 1);
    }
    for (i = 0; i < 3; ++i)
    {
      fc[i] = zc[i] * amb + lv * 255.0 * tc[i];
      if (fc[i] > 255.0)
        fc[i] = 255.0;
    }
  }
  t = fog_pool[fog_count];
  for (i = 0; i < 256; ++i)
  {
    const byte* c = pal + 3 * (int) base[i];
    int r = (int) (c[0] + (fc[0] - c[0]) * f + 0.5), g = (int) (c[1] + (fc[1] - c[1]) * f + 0.5),
        b = (int) (c[2] + (fc[2] - c[2]) * f + 0.5);

    t[i] = BG_CUBE_AT(r < 0 ? 0 : r > 255 ? 255 : r, g < 0 ? 0 : g > 255 ? 255 : g, b < 0 ? 0 : b > 255 ? 255 : b);
  }
  fog_hkey[h] = key;
  fog_hidx[h] = (short) fog_count++;
  return t;
}

static const lighttable_t* bg_KeyColormap(bg_key_t key)
{
  int row = (int) (key & 63), mq = (int) ((key >> 6) & 31), tint = (int) ((key >> 11) & 127);
  const lighttable_t* base;

  if (tint == 0 || !nearest_cube)
    base = fullcolormap + row * 256;
  else
    base = bg_TintTable(tint, mq, row);
  return (key >> 18) ? bg_FogTable(key, base) : base;
}

//
// Walls
//

static struct
{
  const seg_t* seg;
  int sector;
  double n[3];
  double ax, ay, dx, dy, len;       // the seg: from (ax, ay) along (dx, dy), len long
  const bg_seglay_t* lay;           // its baked lights
  int count;
  int set[BG_MAXLIGHTS];
} wall;

dboolean bg_LightBeginSeg(const seg_t* seg, const sector_t* front)
{
  int i;
  double ax, ay, bx, by, len;

  wall.count = 0;
  if (!frame_active || !seg || !front)
    return false;
  wall.seg = seg;
  wall.sector = (int) (front - sectors);
  ax = bg_Fix(seg->v1->x); ay = bg_Fix(seg->v1->y);
  bx = bg_Fix(seg->v2->x); by = bg_Fix(seg->v2->y);
  len = hypot(bx - ax, by - ay);
  if (len < 0.001)
    return false;
  // the visible side of a seg is on its right
  wall.n[0] = (by - ay) / len;
  wall.n[1] = -(bx - ax) / len;
  wall.n[2] = 0;
  wall.ax = ax; wall.ay = ay;
  wall.dx = (bx - ax) / len; wall.dy = (by - ay) / len;
  wall.len = len;
  wall.lay = NULL;
  if (seglay && num_sv > 0)
  {
    int sn = (int) (seg - segs);

    if (sn >= 0 && sn < seglay_n && seglay[sn].n > 0)
      wall.lay = &seglay[sn];
  }
  for (i = 0; i < num_lights; ++i)
  {
    const bg_light_t* L = &lights[i];
    double m = bg_Dist2D(0.5 * (L->a[0] + L->b[0]), 0.5 * (L->a[1] + L->b[1]), ax, ay, bx, by);

    if (L->stamp[wall.sector] == frame_id && m < L->radius + 0.5 * hypot(L->b[0] - L->a[0], L->b[1] - L->a[1]))
      wall.set[wall.count++] = i;
  }
  return wall.count > 0 || wall.lay != NULL || fog_on;
}

// the world point a screen column looks at, `scale` being the wall's scale there
static double bg_ColumnPoint(int x, fixed_t scale, double* px, double* py)
{
  double theta = (double) (int) xtoviewangle[x] * (2.0 * M_PI / 4294967296.0);
  double ang = (double) viewangle * (2.0 * M_PI / 4294967296.0) + theta;
  double depth = bg_Fix(projectiony) / bg_Fix(scale);   // scale is the vertical scale
  double r = depth / cos(theta);
  double vx = bg_Fix(viewx), vy = bg_Fix(viewy), cx = cos(ang), cy = sin(ang);

  // `scale` is a rounded fixed-point number, so a point worked out from it jitters from column to column, and at a
  // shallow angle to the wall by a lot. A shadow edge then flickers (its columns come out lit or dark at random). The
  // exact point is where the view ray meets the wall's own line.
  if (wall.len > 0.001)
  {
    double den = cx * wall.dy - cy * wall.dx;

    if (fabs(den) > 1e-6)
    {
      double t = ((wall.ax - vx) * wall.dy - (wall.ay - vy) * wall.dx) / den;

      if (t > 0 && fabs(t - r) < 0.25 * r + 4.0)
        r = t;
    }
  }
  *px = vx + r * cx;
  *py = vy + r * cy;
  return r;
}

// Split [lo, hi] into pieces drawn with the same brightness key. `level_at` gives the exact brightness at a
// position; it is sampled every `step` pixels (BG_SAMPLE_STEP) and interpolated in between. Each pixel is then rounded,
// either to the nearest step or (dither option) with an ordered-dither threshold, so that the steps blend into each
// other. `vertical` says the positions are rows of screen column `fixed`, otherwise columns of screen row `fixed`.
// With `refine` (a light that casts shadows is in play) a piece where the brightness jumps is sampled again in
// halves, down to BG_REFINE_MIN pixels, so that a shadow's edge stays sharp without sampling everything finely.
typedef struct
{
  void (*level_at)(void*, int, bg_level_t*);
  void (*draw)(void*, int, int, bg_key_t);
  void* ctx;
  dboolean vertical, dither, refine;
  int fixed, lo, start;
  bg_key_t cur;
} bg_runs_t;

#define BG_REFINE_MIN 2
#define BG_REFINE_JUMP 1.2     // brightness rows between two samples that make it worth sampling in between

static void bg_RunPiece(bg_runs_t* r, int a, const bg_level_t* la, int b, const bg_level_t* lb)
{
  int p;

  if (r->refine && b - a > BG_REFINE_MIN && fabs(la->rf - lb->rf) > BG_REFINE_JUMP)
  {
    bg_level_t lm;
    int m = a + (b - a) / 2;

    r->level_at(r->ctx, m, &lm);
    bg_RunPiece(r, a, la, m, &lm);
    bg_RunPiece(r, m, &lm, b, lb);
    return;
  }
  for (p = a; p <= b; ++p)
  {
    bg_level_t l;
    double t = b > a ? (double) (p - a) / (double) (b - a) : 0.0;
    double thr = 0.5;
    bg_key_t key;

    if (p == a && a != r->lo)
      continue;   // already done as the end of the last piece
    l.rf = la->rf + (lb->rf - la->rf) * t;
    l.mf = la->mf + (lb->mf - la->mf) * t;
    l.field = t < 0.5 ? la->field : lb->field;
    l.ff = la->ff + (lb->ff - la->ff) * t;
    l.fl = la->fl + (lb->fl - la->fl) * t;
    l.am = la->am + (lb->am - la->am) * t;
    l.fz = t < 0.5 ? la->fz : lb->fz;
    if (r->dither)
      thr = r->vertical ? bg_Bayer(r->fixed, p) : bg_Bayer(p, r->fixed);
    key = bg_LitKey(&l, thr);
    if (r->cur == ~(bg_key_t) 0)
      r->cur = key;
    else if (key != r->cur)
    {
      r->draw(r->ctx, r->start, p - 1, r->cur);
      r->start = p;
      r->cur = key;
    }
  }
}

static void bg_Runs(int lo, int hi, void (*level_at)(void*, int, bg_level_t*), void (*draw)(void*, int, int, bg_key_t),
                    void* ctx, dboolean vertical, int fixed, int step, dboolean refine)
{
  bg_runs_t r;
  bg_level_t la, lb;
  int a = lo;

  r.level_at = level_at;
  r.draw = draw;
  r.ctx = ctx;
  r.vertical = vertical;
  r.dither = dsda_IntConfig(dsda_config_begotten_light_dither) != 0;
  r.refine = refine;
  r.fixed = fixed;
  r.lo = lo;
  r.start = lo;
  r.cur = ~(bg_key_t) 0;
  level_at(ctx, lo, &la);
  while (a <= hi)
  {
    int b = a + step;

    if (b > hi)
      b = hi;
    if (b > a)
      level_at(ctx, b, &lb);
    else
      lb = la;
    bg_RunPiece(&r, a, &la, b, &lb);
    if (b >= hi)
      break;
    a = b;
    la = lb;
  }
  draw(ctx, r.start, hi, r.cur);
}

// does one of these lights cast shadows? (then bg_Runs samples more finely where the brightness jumps)
static dboolean bg_AnyShadow(const int* set, int count)
{
  int k;

  for (k = 0; k < count; ++k)
    if (lights[set[k]].shadow)
      return true;
  return false;
}

// the colormap row (0 = full bright .. 31 = dark) a colormap pointer is, or -1 when it is not one of ours
static int bg_BaseRow(const lighttable_t* cm)
{
  long d;

  if (!cm || !fullcolormap)
    return -1;
  d = (long) (cm - fullcolormap);
  if (d < 0 || d >= 32 * 256 || (d & 255))
    return -1;
  return (int) (d >> 8);
}

typedef struct
{
  R_DrawColumn_f f;
  draw_column_vars_t* d;
  int base_row;
  int light;                        // the sector's light level (for the haze)
  double px, py, scale, dist;
  int zone;
  int count;
  int set[BG_MAXLIGHTS];
  int nb;                           // baked lights reaching this column
  struct { const bg_wlayer_t* L; const bg_slight_t* S; int iu; double fu; } b[BG_MAXWALLBAKED];
} colctx_t;

static void bg_ColLevel(void* c, int y, bg_level_t* out)
{
  colctx_t* k = (colctx_t*) c;
  double pz = bg_Fix(viewz) + ((double) centery - (double) y) / k->scale;
  double col[3], total;
  bg_acc_t a;
  int i;

  bg_AccInit(&a);
  if (k->count)
    bg_AccLive(&a, k->set, k->count, k->px, k->py, pz, wall.n, -1);
  for (i = 0; i < k->nb; ++i)
  {
    double v = bg_WallAt(k->b[i].L, k->b[i].iu, k->b[i].fu, pz) * k->b[i].S->flick;

    bg_AccAdd(&a, v > 0.98 ? 0.98 : v, k->b[i].S->g.col);
  }
  total = bg_AccDone(&a, col);
  bg_LitLevel(out, k->base_row, total, col);
  bg_FogLevel(out, k->dist, total, k->zone, k->light);
}

static void bg_ColDraw(void* c, int y1, int y2, bg_key_t row)
{
  colctx_t* k = (colctx_t*) c;

  if (y2 < y1)
    return;
  k->d->yl = y1;
  k->d->yh = y2;
  k->d->colormap = bg_KeyColormap(row);
  k->f(k->d);
}

void bg_LitColumn(R_DrawColumn_f colfunc, draw_column_vars_t* d, fixed_t scale)
{
  colctx_t k;
  int i, yl = d->yl, yh = d->yh;
  const lighttable_t* cm = d->colormap;

  k.base_row = bg_BaseRow(cm);
  k.count = 0;
  k.nb = 0;
  k.light = sectors[wall.sector].lightlevel;
  if (k.base_row < 0 || (wall.count == 0 && !wall.lay && !fog_on) || scale <= 0 || yh < yl)
  {
    colfunc(d);
    return;
  }
  if (fog_on && wall.count == 0 && !wall.lay && !dsda_IntConfig(dsda_config_begotten_light_dither))
  {
    // only fog on this wall: the whole column gets one colormap
    static const double white[3] = { 1, 1, 1 };
    bg_level_t lv;

    bg_LitLevel(&lv, k.base_row, 0.0, white);
    bg_FogLevel(&lv, bg_Fix(projectiony) / bg_Fix(scale) * inv_cos[d->x], 0.0, sector_zone[wall.sector], sectors[wall.sector].lightlevel);
    d->colormap = bg_KeyColormap(bg_LitKey(&lv, 0.5));
    colfunc(d);
    d->colormap = cm;
    return;
  }
  k.dist = bg_ColumnPoint(d->x, scale, &k.px, &k.py);
  k.zone = fog_on ? sector_zone[wall.sector] : 0;
  for (i = 0; i < wall.count; ++i)
  {
    const bg_light_t* L = &lights[wall.set[i]];
    double h = bg_Dist2D(k.px, k.py, L->a[0], L->a[1], L->b[0], L->b[1]);

    if (h < L->radius)
      k.set[k.count++] = wall.set[i];
  }
  if (wall.lay)
  {
    double u = (k.px - wall.ax) * wall.dx + (k.py - wall.ay) * wall.dy;

    if (u < 0) u = 0;
    if (u > wall.len) u = wall.len;
    for (i = 0; i < wall.lay->n && k.nb < BG_MAXWALLBAKED; ++i)
    {
      const bg_wlayer_t* Y = &wall.lay->l[i];
      const bg_slight_t* S = &sl[Y->light];
      double uf = u / BG_WALL_STEP;
      int iu = (int) uf;

      if (S->flick <= 0)
        continue;
      if (iu > Y->w - 2) iu = Y->w - 2;
      if (!Y->colmax[iu] && !Y->colmax[iu + 1])
        continue;
      k.b[k.nb].L = Y;
      k.b[k.nb].S = S;
      k.b[k.nb].iu = iu;
      k.b[k.nb].fu = uf - iu > 1.0 ? 1.0 : uf - iu;
      ++k.nb;
    }
  }
  if (!k.count && !k.nb && !fog_on)
  {
    colfunc(d);
    return;
  }
  k.f = colfunc;
  k.d = d;
  k.scale = bg_Fix(scale);
  k.f = colfunc;
  k.d = d;
  bg_Runs(yl, yh, bg_ColLevel, bg_ColDraw, &k, true, d->x, BG_SAMPLE_STEP, bg_AnyShadow(k.set, k.count));
  d->yl = yl;
  d->yh = yh;
  d->colormap = cm;
}

// masked columns
static R_DrawColumn_f masked_real;
static fixed_t masked_scale;

static void bg_MaskedLit(draw_column_vars_t* d)
{
  bg_LitColumn(masked_real, d, masked_scale);
}

R_DrawColumn_f bg_LitMaskedFunc(R_DrawColumn_f real)
{
  masked_real = real;
  return bg_MaskedLit;
}

void bg_SetMaskedScale(fixed_t scale)
{
  masked_scale = scale;
}

//
// Floors and ceilings
//

static double plane_z, plane_n[3];
static fixed_t plane_height;
static int plane_light;

void bg_LightSetPlane(fixed_t height, int lightlevel)
{
  plane_light = lightlevel;
  plane_height = height;
  plane_z = bg_Fix(height);
  plane_n[0] = plane_n[1] = 0;
  plane_n[2] = height < viewz ? 1.0 : -1.0;     // a floor faces up, a ceiling down
}

typedef struct
{
  draw_span_vars_t* ds;
  int base_row;
  int light;                        // the plane's light level (for the haze)
  double depth;
  int count;
  int set[BG_MAXLIGHTS];
  int nst;                          // baked lights reaching this span
  const bg_slight_t* st[BG_MAXWALLBAKED];
  dboolean ceiling;
} spanctx_t;

static void bg_SpanLevel(void* c, int x, bg_level_t* out)
{
  spanctx_t* k = (spanctx_t*) c;
  double r, px = 0, py = 0;
  double col[3], total;
  bg_acc_t a;
  int i;

  if (k->count || k->nst || fog_multi)
  {
    double theta = (double) (int) xtoviewangle[x] * (2.0 * M_PI / 4294967296.0);
    double ang = (double) viewangle * (2.0 * M_PI / 4294967296.0) + theta;

    r = k->depth / cos(theta);
    px = bg_Fix(viewx) + r * cos(ang);
    py = bg_Fix(viewy) + r * sin(ang);
  }
  else
    r = k->depth * inv_cos[x];     // fog only
  bg_AccInit(&a);
  if (k->count)
  {
    subsector_t* ss = R_PointInSubsector((fixed_t) (px * 65536.0), (fixed_t) (py * 65536.0));

    bg_AccLive(&a, k->set, k->count, px, py, plane_z, plane_n, ss ? (int) (ss->sector - sectors) : -1);
  }
  for (i = 0; i < k->nst; ++i)
  {
    const bg_slight_t* S = k->st[i];
    double v = bg_GridAt(k->ceiling ? &S->ceil : &S->floor, px, py) * S->flick;

    bg_AccAdd(&a, v > 0.98 ? 0.98 : v, S->g.col);
  }
  total = bg_AccDone(&a, col);
  bg_LitLevel(out, k->base_row, total, col);
  {
    int zone = 0;

    if (fog_on)
    {
      if (fog_multi)
      {
        const subsector_t* zs = R_PointInSubsector((fixed_t) (px * 65536.0), (fixed_t) (py * 65536.0));

        zone = zs ? sector_zone[zs->sector - sectors] : 0;
      }
      else
        zone = num_fogz > 1 ? sector_zone_default : 0;
    }
    bg_FogLevel(out, r, total, zone, k->light);
  }
}

static void bg_SpanDraw(void* c, int x1, int x2, bg_key_t row)
{
  spanctx_t* k = (spanctx_t*) c;
  draw_span_vars_t s = *k->ds;

  if (x2 < x1)
    return;
  s.x1 = x1;
  s.x2 = x2;
  s.xfrac += (x1 - k->ds->x1) * k->ds->xstep;
  s.yfrac += (x1 - k->ds->x1) * k->ds->ystep;
  s.colormap = bg_KeyColormap(row);
  R_DrawSpan(&s);
}

dboolean bg_LitSpan(draw_span_vars_t* ds)
{
  spanctx_t k;
  int i;
  double d1, d2, dz;
  double ax, ay, bx, by, t1, t2, depth;

  if (!frame_active || fixedcolormap || ds->x2 < ds->x1)
    return false;
  k.base_row = bg_BaseRow(ds->colormap);
  k.light = plane_light;
  if (k.base_row < 0 || ds->z <= 0)
    return false;
  depth = bg_Fix(ds->z);
  k.depth = depth;
  k.ds = ds;
  k.count = 0;

  // the two ends of the span in the world, to see which lights can reach it at all
  t1 = (double) (int) xtoviewangle[ds->x1] * (2.0 * M_PI / 4294967296.0);
  t2 = (double) (int) xtoviewangle[ds->x2] * (2.0 * M_PI / 4294967296.0);
  d1 = (double) viewangle * (2.0 * M_PI / 4294967296.0);
  d2 = depth / cos(t1);
  ax = bg_Fix(viewx) + d2 * cos(d1 + t1);
  ay = bg_Fix(viewy) + d2 * sin(d1 + t1);
  d2 = depth / cos(t2);
  bx = bg_Fix(viewx) + d2 * cos(d1 + t2);
  by = bg_Fix(viewy) + d2 * sin(d1 + t2);
  for (i = 0; i < num_lights; ++i)
  {
    const bg_light_t* L = &lights[i];
    double h = bg_Dist2D(0.5 * (L->a[0] + L->b[0]), 0.5 * (L->a[1] + L->b[1]), ax, ay, bx, by);
    double reach = L->radius + 0.5 * hypot(L->b[0] - L->a[0], L->b[1] - L->a[1]);

    dz = plane_z - L->a[2];
    if (fabs(dz) < L->radius && h < sqrt(reach * reach - dz * dz + 1.0))
      k.set[k.count++] = i;
  }
  k.nst = 0;
  k.ceiling = plane_n[2] < 0;
  for (i = 0; i < num_sv && k.nst < BG_MAXWALLBAKED; ++i)
  {
    const bg_slight_t* S = &sl[sv[i]];
    double h;

    if (S->sun)
    {
      if (!k.ceiling)
        k.st[k.nst++] = S;
      continue;
    }
    h = bg_Dist2D(0.5 * (S->g.a[0] + S->g.b[0]), 0.5 * (S->g.a[1] + S->g.b[1]), ax, ay, bx, by);
    dz = plane_z - S->g.a[2];
    if (fabs(dz) < S->g.radius && h < sqrt(S->reach * S->reach - dz * dz + 1.0))
      k.st[k.nst++] = S;
  }
  if (!k.count && !k.nst && !fog_on)
    return false;
  bg_Runs(ds->x1, ds->x2, bg_SpanLevel, bg_SpanDraw, &k, false, ds->y, BG_SAMPLE_STEP, bg_AnyShadow(k.set, k.count));
  return true;
}

//
// Sprites
//

const lighttable_t* bg_LightSprite(const lighttable_t* colormap, fixed_t x, fixed_t y, fixed_t z, const sector_t* sector)
{
  int set[BG_MAXLIGHTS];
  int i, base_row, s;
  double total, col[3], sprite_dist = 0;

  if (!frame_active || !colormap || !sector)
    return colormap;
  base_row = bg_BaseRow(colormap);
  if (base_row < 0)
    return colormap;
  s = (int) (sector - sectors);
  for (i = 0; i < num_lights; ++i)
    set[i] = i;
  {
    bg_acc_t a;
    double fx = bg_Fix(x), fy = bg_Fix(y);

    sprite_dist = hypot(fx - bg_Fix(viewx), fy - bg_Fix(viewy));

    bg_AccInit(&a);
    bg_AccLive(&a, set, num_lights, fx, fy, bg_Fix(z), NULL, s);
    // baked lights: lit like any thing, but only where their floor grid says the light gets there (shadows)
    for (i = 0; i < num_sv; ++i)
    {
      const bg_slight_t* S = &sl[sv[i]];

      if (S->sun)
        bg_AccAdd(&a, bg_GridAt(&S->floor, fx, fy), S->g.col);
      else if (bg_GridAny(&S->floor, fx, fy))
        bg_AccAdd(&a, bg_Contribution(&S->g, fx, fy, bg_Fix(z), NULL), S->g.col);
    }
    total = bg_AccDone(&a, col);
  }
  {
    bg_level_t lv;

    bg_LitLevel(&lv, base_row, total, col);
    bg_FogLevel(&lv, sprite_dist, total, fog_on ? sector_zone[s] : 0, sector->lightlevel);
    return bg_KeyColormap(bg_LitKey(&lv, 0.5));
  }
}


//
// Glows round lights
//
// A light with a `halo` size gets a soft glow on the screen where it is, as if the air round it was lit. The glows
// are made as vissprites in r_things.c, so that nearer walls and things hide them and farther ones sit under them. They
// are drawn by adding the light's colour to what is already on the screen ("screen" blend), through the palette.
//

#define BG_MAXHALOS 48
typedef struct
{
  double x, y, z, tz;
  int cx, cy, r;
  double a, col[3];
  dboolean corona;                   // a corona (fixed screen size) rather than a glow
} bg_halo_t;

static bg_halo_t halos[BG_MAXHALOS];
static int num_halos;

static void bg_HaloAdd(const double* pa, const double* pb, double size, double strength, const double* col, dboolean corona,
                       double cnear, double cfar)
{
  double x = 0.5 * (pa[0] + pb[0]), y = 0.5 * (pa[1] + pb[1]), z = 0.5 * (pa[2] + pb[2]);
  double dx = x - bg_Fix(viewx), dy = y - bg_Fix(viewy), cosv = bg_Fix(viewcos), sinv = bg_Fix(viewsin);
  double tz = dx * cosv + dy * sinv, tx = -(dy * cosv - dx * sinv);
  double sx, sy, fade;
  bg_halo_t* H;

  if (num_halos >= BG_MAXHALOS || size <= 0 || strength <= 0 || tz < 8.0)
    return;
  sx = bg_Fix(projection) / tz;
  sy = bg_Fix(projectiony) / tz;
  H = &halos[num_halos];
  H->corona = corona;
  if (corona)
  {
    // the same size on the screen at any distance (size is in pixels at the standard 320 wide view); gone when close
    H->r = (int) (size * (double) viewwidth / 320.0 + 0.5);
    fade = (tz - cnear) / (cfar - cnear);
    fade = fade < 0 ? 0 : fade > 1.0 ? 1.0 : fade;
    if (fade <= 0)
      return;
    fade = fade * fade * (3.0 - 2.0 * fade);
  }
  else
  {
    H->r = (int) (size * sx);
    if (H->r > 110) H->r = 110;
    if (H->r > viewwidth / 3) H->r = viewwidth / 3;
    fade = tz / (size * 1.5);                    // close up the glow fades out instead of filling the screen
    fade = fade > 1.0 ? 1.0 : fade;
  }
  if (H->r < 3)
    return;
  H->cx = (int) (centerx + tx * sx);
  H->cy = (int) (centery - (z - bg_Fix(viewz)) * sy);
  if (H->cx + H->r < 0 || H->cx - H->r >= viewwidth || H->cy + H->r < 0 || H->cy - H->r >= viewheight)
    return;
  H->a = strength * fade * (corona ? 0.9 : 0.55);
  H->x = x; H->y = y; H->z = z; H->tz = tz;
  H->col[0] = col[0]; H->col[1] = col[1]; H->col[2] = col[2];
  ++num_halos;
}

int bg_HaloPrepare(void)
{
  int i;

  dboolean halo_on = dsda_IntConfig(dsda_config_begotten_halo) != 0, corona_on = dsda_IntConfig(dsda_config_begotten_corona) != 0;

  num_halos = 0;
  if (!frame_active || !cache_pal || !nearest_cube || (!halo_on && !corona_on))
    return 0;
  for (i = 0; i < num_lights; ++i)
  {
    const bg_light_t* L = &lights[i];

    if (L->sun)
      continue;
    if (L->halo > 0 && halo_on)
      bg_HaloAdd(L->a, L->b, L->halo, L->strength * L->flick, L->col, false, 0, 0);
    if (L->corona > 0 && corona_on)
      bg_HaloAdd(L->a, L->b, L->corona, L->strength * L->flick, L->col, true, L->cnear, L->cfar);
  }
  for (i = 0; i < num_sv; ++i)
  {
    const bg_slight_t* S = &sl[sv[i]];

    if (S->sun)
      continue;
    if (S->g.halo > 0 && halo_on)
      bg_HaloAdd(S->g.a, S->g.b, S->g.halo, S->g.strength * S->flick, S->g.col, false, 0, 0);
    if (S->g.corona > 0 && corona_on)
      bg_HaloAdd(S->g.a, S->g.b, S->g.corona, S->g.strength * S->flick, S->g.col, true, S->g.cnear, S->g.cfar);
  }
  return num_halos;
}

dboolean bg_HaloGet(int i, int* x1, int* x2, fixed_t* scale, fixed_t* gx, fixed_t* gy, fixed_t* gz, fixed_t* gzt)
{
  const bg_halo_t* H;

  if (i < 0 || i >= num_halos)
    return false;
  H = &halos[i];
  *x1 = H->cx - H->r < 0 ? 0 : H->cx - H->r;
  *x2 = H->cx + H->r >= viewwidth ? viewwidth - 1 : H->cx + H->r;
  *scale = (fixed_t) (bg_Fix(projectiony) / H->tz * 65536.0 * 0.98);   // a hair behind a thing at the same distance
  *gx = (fixed_t) (H->x * 65536.0);
  *gy = (fixed_t) (H->y * 65536.0);
  *gz = (fixed_t) ((H->z - 4.0) * 65536.0);
  *gzt = (fixed_t) ((H->z + 4.0) * 65536.0);
  return true;
}

void bg_DrawHalo(int i, const int* floorclip, const int* ceilingclip)
{
  const bg_halo_t* H;
  const byte* pal = cache_pal;
  int x, y, r2;
  double gr, gg, gb;

  if (i < 0 || i >= num_halos || !pal || !nearest_cube)
    return;
  H = &halos[i];
  r2 = H->r * H->r;
  gr = H->col[0] * 255.0;
  gg = H->col[1] * 255.0;
  gb = H->col[2] * 255.0;
  for (x = H->cx - H->r; x <= H->cx + H->r; ++x)
  {
    int dx = x - H->cx, half, y0, y1;

    if (x < 0 || x >= viewwidth)
      continue;
    half = (int) sqrt((double) (r2 - dx * dx));
    y0 = H->cy - half;
    y1 = H->cy + half;
    if (y0 <= ceilingclip[x]) y0 = ceilingclip[x] + 1;
    if (y1 >= floorclip[x]) y1 = floorclip[x] - 1;
    if (y0 < 0) y0 = 0;
    if (y1 >= viewheight) y1 = viewheight - 1;
    for (y = y0; y <= y1; ++y)
    {
      int dy = y - H->cy;
      double t = 1.0 - (double) (dx * dx + dy * dy) / (double) r2, a;
      byte* p;
      const byte* c;
      int r, g, b;

      if (H->corona)
      {
        // a bright core, a soft body, and a faint cross of streaks through the middle
        double rr = (double) (dx * dx + dy * dy) / (double) r2, ax = fabs((double) dx) / (double) H->r, ay = fabs((double) dy) / (double) H->r;
        double core = exp(-rr * 14.0), body = t * t * 0.35;
        double streak = (1.0 - ax) * exp(-ay * ay * 900.0 / (double) r2 * 8.0) * 0.55 + (1.0 - ay) * exp(-ax * ax * 900.0 / (double) r2 * 8.0) * 0.55;

        a = H->a * (core + body + (streak > 0 ? streak * streak : 0));
        if (a > 1.0)
          a = 1.0;
      }
      else
        a = H->a * t * t * t;
      if (a < 0.02 || a < bg_Bayer(x, y) * 0.12)
        continue;                    // the faint edge is dithered away
      p = drawvars.topleft + y * drawvars.pitch + x;
      c = pal + 3 * (int) *p;
      r = (int) (c[0] + gr * a * (255 - c[0]) * (1.0 / 255.0) + 0.5);
      g = (int) (c[1] + gg * a * (255 - c[1]) * (1.0 / 255.0) + 0.5);
      b = (int) (c[2] + gb * a * (255 - c[2]) * (1.0 / 255.0) + 0.5);
      *p = bg_Nearest(r, g, b);
    }
  }
}

int bg_LightTypeFor(int doomednum)
{
  int i;

  for (i = 0; i < num_defs; ++i)
    if (defs[i].doomednum == doomednum)
      return defs[i].mobjtype;
  return -1;
}

// for the self test: the total light from this frame's lights on a point (no sector check)
double bg_LightProbe(double x, double y, double z, const double* n)
{
  int set[BG_MAXLIGHTS], i;

  for (i = 0; i < num_lights; ++i)
    set[i] = i;
  return bg_Total(set, num_lights, x, y, z, n, -1, NULL);
}

int bg_LightProbeTint(double x, double y, double z)   // the colour field a point gets (0 = white)
{
  int set[BG_MAXLIGHTS], i;
  double col[3];

  for (i = 0; i < num_lights; ++i)
    set[i] = i;
  bg_Total(set, num_lights, x, y, z, NULL, -1, col);
  return bg_ColourField(col);
}

// the palette index a tinted light turns palette index `idx` in colormap row `row` into, with `mq` of 12 tint steps
int bg_LightTestMap(int tint, int mq, int row, int idx)
{
  bg_CheckTintCache();
  if (!nearest_cube || tint <= 0 || tint > BG_COLOUR_LEVELS * BG_COLOUR_LEVELS * BG_COLOUR_LEVELS)
    return -1;
  return bg_TintTable(tint, mq, row)[idx];
}

int bg_LightCount(void)
{
  return num_lights;
}


//
// Weather
//
// Rain and snow are specks that fall only through sky-ceiling sectors (a roof stops them). They are cosmetic and use
// their own random numbers, so they never touch the game's. Each speck is drawn as a vissprite (see r_things.c), so
// walls and things hide the ones behind them. Thunder is a flash of the blue screen palette plus a sound a moment later.
//

#define BG_MAXWP 600
typedef struct { double x, y, z, vx, vy, vz, phase; } bg_wp_t;
typedef struct { int cx, y0, y1, w; double tz, z; } bg_wpv_t;
static bg_wp_t wps[BG_MAXWP];
static int num_wps;
static bg_wpv_t wpv[BG_MAXWP];
static int num_wpv;
static unsigned wx_rng = 12345u;
static double wx_accum;
static int wx_primed, wx_sound_at, wx_strike_in, wx_flash, wx_thunder_at;
static int wx_sfx_rain = -1, wx_sfx_thunder = -1;

static double bg_WxRand(void)
{
  wx_rng ^= wx_rng << 13; wx_rng ^= wx_rng >> 17; wx_rng ^= wx_rng << 5;
  return (wx_rng & 0xffffff) / 16777216.0;
}

void bg_WeatherNewLevel(const char* name)
{
  int i, best = -1;

  for (i = 0; i < num_weatherdefs; ++i)
    if (name && !strcasecmp(weatherdefs[i].map, name))
      best = i;
  if (best < 0)
    for (i = 0; i < num_weatherdefs; ++i)
      if (!weatherdefs[i].map[0])
        best = i;
  weather_kind = best >= 0 ? weatherdefs[best].kind : 0;
  num_wps = 0;
  num_wpv = 0;
  wx_primed = 0;
  wx_accum = 0;
  wx_flash = 0;
  wx_thunder_at = 0;
  wx_sound_at = 0;
  wx_strike_in = 0;
  if (weather_kind)
  {
    weather_density = weatherdefs[best].density;
    weather_wind = weatherdefs[best].wind;
    weather_thunder = weatherdefs[best].thunder;
    wx_strike_in = weather_thunder > 0 ? (int) (35 * weather_thunder * (0.4 + bg_WxRand())) : 0;
    if (weather_thunder > 0 && getenv("BEGOTTEN_WXFIRST"))
      wx_strike_in = atoi(getenv("BEGOTTEN_WXFIRST"));      // test aid: the first strike at this tic
    lprintf(LO_INFO, "BGLIGHT: %s on this level (density %g, wind %g, thunder every ~%g s)\n",
            weather_kind == 1 ? "rain" : "snow", weather_density, weather_wind, weather_thunder);
  }
}

int bg_WeatherKind(void) { return weather_kind; }
int bg_WeatherCount(void) { return num_wps; }

static const sector_t* bg_WxSector(double x, double y)
{
  const subsector_t* ss = R_PointInSubsector((fixed_t) (x * 65536.0), (fixed_t) (y * 65536.0));

  return ss ? ss->sector : NULL;
}

static void bg_WxSpawn(double vx0, double vy0, double vz0, dboolean anywhere)
{
  bg_wp_t* P;
  const sector_t* sec;
  double ang, rad, x = 0, y = 0, top, ceil, floorz;
  int tries;

  if (num_wps >= BG_MAXWP)
    return;
  sec = NULL;
  for (tries = 0; tries < 10; ++tries)       // look for open sky near the viewer
  {
    ang = bg_WxRand() * 2.0 * M_PI;
    rad = 40.0 + 700.0 * sqrt(bg_WxRand());
    x = vx0 + rad * cos(ang);
    y = vy0 + rad * sin(ang);
    sec = bg_WxSector(x, y);
    if (sec && sec->ceilingpic == skyflatnum)
      break;
    sec = NULL;
  }
  if (!sec)
    return;
  ceil = bg_Fix(sec->ceilingheight);
  floorz = bg_Fix(sec->floorheight);
  top = vz0 + 300.0 < ceil ? vz0 + 300.0 : ceil;
  if (top <= floorz + 8)
    return;
  P = &wps[num_wps++];
  P->x = x; P->y = y;
  P->z = anywhere ? floorz + (top - floorz) * bg_WxRand() : top - 1.0;
  P->phase = bg_WxRand() * 6.28;
  if (weather_kind == 1)
  {
    P->vx = weather_wind * 1.5; P->vy = 0; P->vz = -(22.0 + 6.0 * bg_WxRand());
  }
  else
  {
    P->vx = weather_wind * 0.5; P->vy = 0; P->vz = -(1.6 + 1.2 * bg_WxRand());
  }
}

static void bg_WxStep(double vx0, double vy0, double vz0)
{
  int i;

  for (i = 0; i < num_wps; )
  {
    bg_wp_t* P = &wps[i];
    const sector_t* sec;
    dboolean dead;

    P->x += P->vx; P->y += P->vy; P->z += P->vz;
    if (weather_kind == 2)
    {
      P->phase += 0.09;
      P->x += 0.7 * sin(P->phase);
      P->y += 0.7 * cos(P->phase * 0.8);
    }
    sec = bg_WxSector(P->x, P->y);
    dead = !sec || sec->ceilingpic != skyflatnum || P->z <= bg_Fix(sec->floorheight) || P->z > bg_Fix(sec->ceilingheight) ||
           fabs(P->x - vx0) > 760 || fabs(P->y - vy0) > 760;
    if (dead)
      wps[i] = wps[--num_wps];
    else
      ++i;
  }
}

// once per game tic (not while paused)
// developer aid (BEGOTTEN_DUMP=file): logs every change of a sector's floor, ceiling or light, and the position of tagged things,
// once a tic.  Used by tools/make_script_lab.py to check that each demo script of the script lab does something.
static void bg_DevDump(void)
{
  static FILE* f;
  static int tried, nprev;
  static struct { int fl, ce, li, tex, x1, x2, sc, rot, gr, dm; } *prev;
  static struct { unsigned fl; int sp, tx, xo, yo, sc; } *lprev;
  static struct { int x, y, z, hp, st, alive, seen, tid; mobj_t* ptr; } tprev[4096];
  static int ntp;
  int i;
  thinker_t* th;

  if (!tried)
  {
    tried = 1;
    if (getenv("BEGOTTEN_DUMP"))
      f = fopen(getenv("BEGOTTEN_DUMP"), "w");
  }
  if (!f || gamestate != GS_LEVEL)
    return;
  if (!prev || nprev != numsectors)
  {
    nprev = numsectors;
    prev = realloc(prev, sizeof(*prev) * numsectors);
    memset(prev, 0x7f, sizeof(*prev) * numsectors);
  }
  for (i = 0; i < numsectors; ++i)
  {
    int fl = sectors[i].floorheight >> 16, ce = sectors[i].ceilingheight >> 16, li = sectors[i].lightlevel;
    int tex = sectors[i].floorpic * 1000 + sectors[i].ceilingpic;
    int x1 = (sectors[i].floor_xoffs >> 8) + (sectors[i].floor_yoffs >> 8) * 7;
    int x2 = (sectors[i].ceiling_xoffs >> 8) + (sectors[i].ceiling_yoffs >> 8) * 7;
    int sc = (sectors[i].floor_xscale >> 8) + (sectors[i].floor_yscale >> 8) * 7 + (sectors[i].ceiling_xscale >> 8) * 13 + (sectors[i].ceiling_yscale >> 8) * 17;
    int rot = (int) (sectors[i].floor_rotation >> 20) + (int) (sectors[i].ceiling_rotation >> 20) * 5;
    int gr = sectors[i].gravity >> 8, dm = sectors[i].damage.amount;
    if (fl != prev[i].fl || ce != prev[i].ce || li != prev[i].li || tex != prev[i].tex || x1 != prev[i].x1 ||
        x2 != prev[i].x2 || sc != prev[i].sc || rot != prev[i].rot || gr != prev[i].gr || dm != prev[i].dm)
    {
      fprintf(f, "%d S %d tag %d floor %d ceil %d light %d tex %d pan %d %d scale %d rot %d grav %d dmg %d\n", leveltime, i,
              sectors[i].tag, fl, ce, li, tex, x1, x2, sc, rot, gr, dm);
      prev[i].fl = fl; prev[i].ce = ce; prev[i].li = li; prev[i].tex = tex;
      prev[i].x1 = x1; prev[i].x2 = x2; prev[i].sc = sc; prev[i].rot = rot; prev[i].gr = gr; prev[i].dm = dm;
    }
  }
  if (!lprev || nprev != numsectors || 1)
  {
    static int nl;
    if (nl != numlines)
    {
      nl = numlines;
      lprev = realloc(lprev, sizeof(*lprev) * numlines);
      memset(lprev, 0x7f, sizeof(*lprev) * numlines);
    }
  }
  for (i = 0; i < numlines; ++i)
  {
    const line_t* l = &lines[i];
    const side_t* sd;
    unsigned fl;
    int sp, tx, xo, yo, sc;
    if (l->id < 1 || l->id > 999 || l->sidenum[0] < 0)
      continue;
    sd = &sides[l->sidenum[0]];
    fl = (unsigned) l->flags; sp = l->special; tx = sd->midtexture;
    xo = (sd->textureoffset + sd->textureoffset_mid) >> 8; yo = (sd->rowoffset + sd->rowoffset_mid) >> 8;
    sc = (sd->scalex_mid >> 8) * 31 + (sd->scaley_mid >> 8);
    if (fl != lprev[i].fl || sp != lprev[i].sp || tx != lprev[i].tx || xo != lprev[i].xo || yo != lprev[i].yo || sc != lprev[i].sc)
    {
      fprintf(f, "%d L %d id %d flags %u special %d mid %d off %d %d scale %d\n", leveltime, i, l->id, fl, sp, tx, xo, yo, sc);
      lprev[i].fl = fl; lprev[i].sp = sp; lprev[i].tx = tx; lprev[i].xo = xo; lprev[i].yo = yo; lprev[i].sc = sc;
    }
  }
  {
    int k;
    for (k = 0; k < ntp; ++k)
      tprev[k].seen = 0;
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
      mobj_t* m;
      int x, y, z;
      if (th->function != P_MobjThinker)
        continue;
      m = (mobj_t*) th;
      if (m->tid < 1000)
        continue;
      x = m->x >> 16; y = m->y >> 16; z = m->z >> 16;
      for (k = 0; k < ntp && tprev[k].ptr != m; ++k)
        ;
      if (k == ntp)
      {
        if (ntp >= 4096)
          continue;
        ++ntp;
        tprev[k].ptr = m; tprev[k].tid = m->tid; tprev[k].x = tprev[k].y = tprev[k].z = tprev[k].hp = 0x7fffffff; tprev[k].alive = 0;
      }
      tprev[k].seen = 1;
      if (!tprev[k].alive || tprev[k].x != x || tprev[k].y != y || tprev[k].z != z || tprev[k].hp != m->health ||
          tprev[k].st != (int) (m->state - states))
      {
        fprintf(f, "%d T %d type %d at %d %d %d hp %d state %d\n", leveltime, m->tid, (int) m->type, x, y, z, m->health,
                (int) (m->state - states));
        tprev[k].x = x; tprev[k].y = y; tprev[k].z = z; tprev[k].hp = m->health; tprev[k].st = (int) (m->state - states);
        tprev[k].alive = 1;
      }
    }
    for (k = 0; k < ntp; ++k)
      if (!tprev[k].seen && tprev[k].alive)
      {
        fprintf(f, "%d T %d gone\n", leveltime, tprev[k].tid);
        tprev[k].alive = 0;
        tprev[k].ptr = NULL;
      }
  }
  if (playeringame[0] && players[0].mo && getenv("BEGOTTEN_GOD"))
    players[0].cheats |= CF_GODMODE;      // developer aid for the unattended checks
  if (playeringame[0] && players[0].mo)
  {
    static int ux, uy, uz, uh;
    int x = players[0].mo->x >> 16, y = players[0].mo->y >> 16, z = players[0].mo->z >> 16, h = players[0].health;
    if (x != ux || y != uy || z != uz || h != uh)
    {
      fprintf(f, "%d U x %d y %d z %d health %d\n", leveltime, x, y, z, h);
      ux = x; uy = y; uz = z; uh = h;
    }
  }
  for (i = 0; i < po_NumPolyobjs; ++i)
  {
    static struct { int x, y, a; } pp[256];
    int x = polyobjs[i].startSpot.x >> 16, y = polyobjs[i].startSpot.y >> 16, a = (int) (polyobjs[i].angle >> 24);
    if (i < 256 && (pp[i].x != x || pp[i].y != y || pp[i].a != a))
    {
      fprintf(f, "%d P %d tag %d at %d %d angle %d\n", leveltime, i, polyobjs[i].tag, x, y, a);
      pp[i].x = x; pp[i].y = y; pp[i].a = a;
    }
  }
  fflush(f);
}

void bg_WeatherTick(void)
{

  const player_t* pl;
  double vx0, vy0, vz0, n;
  const sector_t* here;
  int i;

  bg_DevDump();
  if (wx_flash > 0)
    --wx_flash;
  if (!weather_kind || gamestate != GS_LEVEL || !dsda_IntConfig(dsda_config_begotten_weather))
  {
    num_wps = 0;
    return;
  }
  pl = &players[displayplayer];
  if (!pl->mo)
    return;
  vx0 = bg_Fix(pl->mo->x); vy0 = bg_Fix(pl->mo->y); vz0 = bg_Fix(pl->mo->z + pl->viewheight);
  if (!wx_primed)
  {
    wx_primed = 1;
    for (i = 0; i < 70; ++i)
    {
      double k = (weather_kind == 1 ? 30.0 : 4.0) * weather_density;
      int j, c = (int) k;

      for (j = 0; j < c; ++j)
        bg_WxSpawn(vx0, vy0, vz0, i < 4 ? true : false);
      bg_WxStep(vx0, vy0, vz0);
    }
    for (i = 0; i < 160; ++i)
      bg_WxSpawn(vx0, vy0, vz0, true);
  }
  n = (weather_kind == 1 ? 30.0 : 4.0) * weather_density + wx_accum;
  for (i = 0; i < (int) n; ++i)
    bg_WxSpawn(vx0, vy0, vz0, false);
  wx_accum = n - (int) n;
  bg_WxStep(vx0, vy0, vz0);
  if (getenv("BEGOTTEN_WXLOG") && leveltime % 70 == 0)
    lprintf(LO_INFO, "BGWEATHER: %d specks, %d on screen\n", num_wps, num_wpv);

  here = bg_WxSector(vx0, vy0);
  if (here && here->ceilingpic == skyflatnum && weather_kind == 1 && leveltime >= wx_sound_at)
  {
    if (wx_sfx_rain < 0) wx_sfx_rain = bg_SfxFromLump("DSRAIN");
    if (wx_sfx_rain > 0)
      S_StartVoidSound(wx_sfx_rain);
    wx_sound_at = leveltime + 70;
  }
  if (weather_thunder > 0 && weather_kind == 1)
  {
    if (wx_strike_in > 0 && --wx_strike_in == 0)
    {
      wx_flash = 14;
      if (getenv("BEGOTTEN_WXLOG"))
        lprintf(LO_INFO, "BGWEATHER: lightning at tic %d\n", leveltime);
      wx_thunder_at = leveltime + 14 + (int) (bg_WxRand() * 70.0);
      wx_strike_in = (int) (35 * weather_thunder * (0.5 + bg_WxRand()));
    }
    if (wx_thunder_at && leveltime >= wx_thunder_at)
    {
      wx_thunder_at = 0;
      if (wx_sfx_thunder < 0) wx_sfx_thunder = bg_SfxFromLump("DSTHUNDR");
      if (wx_sfx_thunder > 0)
        S_StartVoidSound(wx_sfx_thunder);
    }
  }
}

// the screen palette for a lightning flash (0 = none): blue-white, strongest first, flickering as it dies
int bg_WeatherFlashLevel(void)
{
  static const int shape[15] = { 0, 4, 4, 2, 0, 3, 4, 3, 2, 2, 1, 1, 1, 1, 1 };

  if (wx_flash <= 0 || wx_flash > 14 || !dsda_IntConfig(dsda_config_begotten_weather))
    return 0;
  return shape[wx_flash];
}

int bg_WeatherPrepare(void)
{
  int i;
  double cosv = bg_Fix(viewcos), sinv = bg_Fix(viewsin), sx = bg_Fix(projection), sy = bg_Fix(projectiony);
  double k = (double) viewwidth / 320.0;

  num_wpv = 0;
  if (!weather_kind || !num_wps || !dsda_IntConfig(dsda_config_begotten_weather) || fixedcolormap)
    return 0;
  bg_CheckTintCache();
  if (!cache_pal || !nearest_cube)
    return 0;
  for (i = 0; i < num_wps; ++i)
  {
    const bg_wp_t* P = &wps[i];
    double dx = P->x - bg_Fix(viewx), dy = P->y - bg_Fix(viewy);
    double tz = dx * cosv + dy * sinv, tx = -(dy * cosv - dx * sinv), z = P->z;
    bg_wpv_t* V;
    int cx, cy;

    if (tz < 12.0)
      continue;
    cx = (int) (centerx + tx * sx / tz);
    cy = (int) (centery - (z - bg_Fix(viewz)) * sy / tz);
    if (cx < 0 || cx >= viewwidth || cy < -40 || cy >= viewheight + 40)
      continue;
    V = &wpv[num_wpv++];
    V->cx = cx; V->tz = tz; V->z = z;
    if (weather_kind == 1)
    {
      int len = (int) (34.0 * sy / tz);
      if (len < 2) len = 2;
      if (len > (int) (14 * k)) len = (int) (14 * k);
      V->y0 = cy - len; V->y1 = cy; V->w = 1;
    }
    else
    {
      int w = (int) (2.2 * k * 90.0 / tz + 0.5);
      if (w < 1) w = 1;
      if (w > (int) (3 * k + 0.5)) w = (int) (3 * k + 0.5);
      V->y0 = cy; V->y1 = cy + w - 1; V->w = w;
    }
  }
  return num_wpv;
}

dboolean bg_WeatherGet(int i, int* x1, int* x2, fixed_t* scale, fixed_t* gx, fixed_t* gy, fixed_t* gz, fixed_t* gzt)
{
  const bg_wpv_t* V;
  const bg_wp_t* P;

  if (i < 0 || i >= num_wpv)
    return false;
  V = &wpv[i];
  *x1 = V->cx;
  *x2 = V->cx + V->w - 1 >= viewwidth ? viewwidth - 1 : V->cx + V->w - 1;
  *scale = (fixed_t) (bg_Fix(projectiony) / V->tz * 65536.0);
  /* the sort only needs a spot in the world: use the view-space distance through a point straight ahead */
  *gx = (fixed_t) ((bg_Fix(viewx) + V->tz * bg_Fix(viewcos)) * 65536.0);
  *gy = (fixed_t) ((bg_Fix(viewy) + V->tz * bg_Fix(viewsin)) * 65536.0);
  *gz = (fixed_t) ((V->z - 2.0) * 65536.0);
  *gzt = (fixed_t) ((V->z + 2.0) * 65536.0);
  (void) P;
  return true;
}

void bg_DrawWeather(int i, const int* floorclip, const int* ceilingclip)
{
  const bg_wpv_t* V;
  int x, y;
  double a, tr, tg, tb;
  const byte* pal = cache_pal;

  if (i < 0 || i >= num_wpv || !pal || !nearest_cube)
    return;
  V = &wpv[i];
  if (weather_kind == 1) { tr = 185; tg = 205; tb = 235; a = 0.7; }
  else { tr = 255; tg = 255; tb = 255; a = 0.85; }
  {
    double d = V->tz > 700 ? 0.5 : 1.0 - V->tz / 1400.0;      // far specks fade
    a *= d;
  }
  for (x = V->cx; x < V->cx + V->w && x < viewwidth; ++x)
  {
    int y0 = V->y0, y1 = V->y1;

    if (x < 0)
      continue;
    if (y0 <= ceilingclip[x]) y0 = ceilingclip[x] + 1;
    if (y1 >= floorclip[x]) y1 = floorclip[x] - 1;
    if (y0 < 0) y0 = 0;
    if (y1 >= viewheight) y1 = viewheight - 1;
    for (y = y0; y <= y1; ++y)
    {
      double aa = a;
      byte* p = drawvars.topleft + y * drawvars.pitch + x;
      const byte* c = pal + 3 * (int) *p;
      int r, g, b;

      if (weather_kind == 1)
        aa = a * (0.45 + 0.55 * (double) (y - V->y0 + 1) / (double) (V->y1 - V->y0 + 1));   // a streak, faint at its tail
      r = (int) (c[0] + (tr - c[0]) * aa + 0.5);
      g = (int) (c[1] + (tg - c[1]) * aa + 0.5);
      b = (int) (c[2] + (tb - c[2]) * aa + 0.5);
      *p = bg_Nearest(r, g, b);
    }
  }
}

