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
 *  Refresh of things, i.e. objects represented by sprites.
 *
 *-----------------------------------------------------------------------------*/

#include "doomstat.h"
#include "w_wad.h"
#include "r_main.h"
#include "dsda/bglight.h"
#include "dsda/bgvoxel.h"
#include "r_bsp.h"
#include "r_segs.h"
#include "r_draw.h"
#include "r_things.h"
#include "dsda/bgpickup.h"
#include "dsda/bgscale.h"
#include "r_fps.h"
#include "v_video.h"
#include "p_pspr.h"
#include "lprintf.h"
#include "e6y.h"//e6y

#include "dsda/bgclass.h"
#include "dsda/begotten.h"
#include "dsda/bgcoop.h"
#include "dsda/bgnet.h"
#include "dsda/configuration.h"
#include "dsda/render_stats.h"
#include "dsda/settings.h"

#define BASEYCENTER 100

static int *clipbot = NULL; // killough 2/8/98: // dropoff overflow
static int *cliptop = NULL; // change to MAX_*  // dropoff overflow

//
// Sprite rotation 0 is facing the viewer,
//  rotation 1 is one angle turn CLOCKWISE around the axis.
// This is not the same as the angle,
//  which increases counter clockwise (protractor).
// There was a lot of stuff grabbed wrong, so I changed it...
//

fixed_t pspriteiscale;
// proff 11/06/98: Added for high-res
fixed_t pspritexscale;
fixed_t pspriteyscale;
fixed_t pspriteiyscale;

static const lighttable_t **spritelights;        // killough 1/25/98 made static

//e6y: added for GL
float pspriteyscale_f;
float pspritexscale_f;

typedef struct drawseg_xrange_item_s
{
  short x1, x2;
  drawseg_t *user;
} drawseg_xrange_item_t;

typedef struct drawsegs_xrange_s
{
  drawseg_xrange_item_t *items;
  int count;
} drawsegs_xrange_t;

#define DS_RANGES_COUNT 3
static drawsegs_xrange_t drawsegs_xranges[DS_RANGES_COUNT];

static drawseg_xrange_item_t *drawsegs_xrange;
static unsigned int drawsegs_xrange_size = 0;
static int drawsegs_xrange_count = 0;

// constant arrays
//  used for psprite clipping and initializing clipping

// e6y: resolution limitation is removed
int *negonearray;        // killough 2/8/98: // dropoff overflow
int *screenheightarray;  // change to MAX_* // dropoff overflow

//
// INITIALIZATION FUNCTIONS
//

// variables used to look up and range check thing_t sprites patches

spritedef_t *sprites;

#define MAX_SPRITE_FRAMES 30          /* Macroized -- killough 1/25/98 */

static spriteframe_t sprtemp[MAX_SPRITE_FRAMES];
static int maxframe;

void R_InitSpritesRes(void)
{
  if (xtoviewangle) Z_Free(xtoviewangle);
  if (linearskyangle) Z_Free (linearskyangle);
  if (negonearray) Z_Free(negonearray);
  if (screenheightarray) Z_Free(screenheightarray);

  xtoviewangle = Z_Calloc(1, (SCREENWIDTH + 1) * sizeof(*xtoviewangle));
  linearskyangle = Z_Calloc(1, (SCREENWIDTH + 1) * sizeof(*linearskyangle));
  negonearray = Z_Calloc(1, SCREENWIDTH * sizeof(*negonearray));
  screenheightarray = Z_Calloc(1, SCREENWIDTH * sizeof(*screenheightarray));

  if (clipbot) Z_Free(clipbot);

  clipbot = Z_Calloc(1, 2 * SCREENWIDTH * sizeof(*clipbot));
  cliptop = clipbot + SCREENWIDTH;
}

void R_UpdateVisSpriteTranMap(vissprite_t *vis, mobj_t *thing)
{
  if (thing && thing->tranmap)
    vis->tranmap = thing->tranmap;
  else if (vis->mobjflags & g_mf_translucent)
    vis->tranmap = main_tranmap;
  else
    vis->tranmap = NULL;
}

//
// R_InstallSpriteLump
// Local function for R_InitSprites.
//

static void R_InstallSpriteLump(int lump, unsigned frame,
                                char rot, dboolean flipped)
{
  unsigned int rotation;

  if (rot >= '0' && rot <= '9')
  {
    rotation = rot - '0';
  }
  else if (rot >= 'A')
  {
    rotation = rot - 'A' + 10;
  }
  else
  {
    rotation = 17;
  }

  if (frame >= MAX_SPRITE_FRAMES || rotation > 16)
    I_Error("R_InstallSpriteLump: Bad frame characters in lump %i", lump);

  if ((int) frame > maxframe)
    maxframe = frame;

  if (rotation == 0)
    {    // the lump should be used for all rotations
      int r;
      for (r = 14; r >= 0; r -= 2)
        if (sprtemp[frame].lump[r] == -1)
          {
            sprtemp[frame].lump[r] = lump - firstspritelump;
            if (flipped)
            {
              sprtemp[frame].flip |= (1 << r);
            }
            sprtemp[frame].rotate = false; //jff 4/24/98 if any subbed, rotless
          }
      return;
    }

  // the lump is only used for one rotation

  if (rotation <= 8)
  {
    rotation = (rotation - 1) * 2;
  }
  else
  {
    rotation = (rotation - 9) * 2 + 1;
  }

  if (sprtemp[frame].lump[rotation] == -1)
    {
      sprtemp[frame].lump[rotation] = lump - firstspritelump;
      if (flipped)
      {
        sprtemp[frame].flip |= (1 << rotation);
      }
      sprtemp[frame].rotate = true; //jff 4/24/98 only change if rot used
    }
}

//
// R_InitSpriteDefs
// Pass a null terminated list of sprite names
// (4 chars exactly) to be used.
//
// Builds the sprite rotation matrixes to account
// for horizontally flipped sprites.
//
// Will report an error if the lumps are inconsistent.
// Only called at startup.
//
// Sprite lump names are 4 characters for the actor,
//  a letter for the frame, and a number for the rotation.
//
// A sprite that is flippable will have an additional
//  letter/number appended.
//
// The rotation character can be 0 to signify no rotations.
//
// 1/25/98, 1/31/98 killough : Rewritten for performance
//
// Empirically verified to have excellent hash
// properties across standard Doom sprites:

#define R_SpriteNameHash(s) ((unsigned)((s)[0]-((s)[1]*3-(s)[3]*2-(s)[2])*2))

static void R_InitSpriteDefs(const char * const * namelist)
{
  size_t numentries = lastspritelump-firstspritelump+1;
  struct { int index, next; } *hash;
  int i;

  if (!numentries || !*namelist)
    return;

  sprites = Z_Calloc(num_sprites, sizeof(*sprites));

  // Create hash table based on just the first four letters of each sprite
  // killough 1/31/98

  hash = Z_Malloc(sizeof(*hash)*numentries); // allocate hash table

  for (i=0; (size_t)i<numentries; i++)             // initialize hash table as empty
    hash[i].index = -1;

  for (i=0; (size_t)i<numentries; i++)             // Prepend each sprite to hash chain
    {                                      // prepend so that later ones win
      int j = R_SpriteNameHash(lumpinfo[i+firstspritelump].name) % numentries;
      hash[i].next = hash[j].index;
      hash[j].index = i;
    }

  // scan all the lump names for each of the names,
  //  noting the highest frame letter.

  for (i=0 ; i<num_sprites ; i++)
    {
      int k;
      int rot;
      const char *spritename;
      int j;

      spritename = namelist[i];
      if (!spritename)
        continue;

      j = hash[R_SpriteNameHash(spritename) % numentries].index;
      if (j >= 0)
        {
          memset(sprtemp, -1, sizeof(sprtemp));
          for (k = 0; k < MAX_SPRITE_FRAMES; k++)
          {
            sprtemp[k].flip = 0;
          }

          maxframe = -1;
          do
            {
              register lumpinfo_t *lump = lumpinfo + j + firstspritelump;

              // Fast portable comparison -- killough
              // (using int pointer cast is nonportable):

              if (!((lump->name[0] ^ spritename[0]) |
                    (lump->name[1] ^ spritename[1]) |
                    (lump->name[2] ^ spritename[2]) |
                    (lump->name[3] ^ spritename[3])))
                {
                  R_InstallSpriteLump(j+firstspritelump,
                                      lump->name[4] - 'A',
                                      lump->name[5],
                                      false);
                  if (lump->name[6])
                    R_InstallSpriteLump(j+firstspritelump,
                                        lump->name[6] - 'A',
                                        lump->name[7],
                                        true);
                }
            }
          while ((j = hash[j].next) >= 0);

          // check the frames that were found for completeness
          if ((sprites[i].numframes = ++maxframe))  // killough 1/31/98
            {
              int frame;
              for (frame = 0; frame < maxframe; frame++)
              {
                switch (sprtemp[frame].rotate)
                  {
                  case -1:
                    // no rotations were found for that frame at all
                    //I_Error ("R_InitSprites: No patches found "
                    //         "for %.8s frame %c", namelist[i], frame+'A');
                    break;

                  case 0:
                    // only the first rotation is needed
                    for (rot = 1; rot < 16; rot++)
                    {
                      sprtemp[frame].lump[rot] = sprtemp[frame].lump[0];
                    }
                    // If the frame is flipped, they all should be
                    if (sprtemp[frame].flip & 1)
                    {
                      sprtemp[frame].flip = 0xFFFF;
                    }
                    break;

                  case 1:
                    // must have all 8 frames
                    for (rot = 0; rot < 8; rot++)
                    {
                      if (sprtemp[frame].lump[rot * 2 + 1] == -1)
                      {
                        sprtemp[frame].lump[rot * 2 + 1] = sprtemp[frame].lump[rot * 2];
                        if (sprtemp[frame].flip & (1 << (rot * 2)))
                        {
                          sprtemp[frame].flip |= 1 << (rot * 2 + 1);
                        }
                      }
                      if (sprtemp[frame].lump[rot * 2] == -1)
                      {
                        sprtemp[frame].lump[rot * 2] = sprtemp[frame].lump[rot * 2 + 1];
                        if (sprtemp[frame].flip & (1 << (rot * 2 + 1)))
                        {
                          sprtemp[frame].flip |= 1 << (rot * 2);
                        }
                      }

                    }
                    for (rot = 0; rot < 16; rot++)
                    {
                      if (sprtemp[frame].lump[rot] == -1)
                        I_Error ("R_InitSprites: Sprite %.8s frame %c "
                                 "is missing rotations",
                                 namelist[i], frame+'A');
                    }
                    break;
                  }
              }

              for (frame = 0; frame < maxframe; frame++)
              {
                if (sprtemp[frame].rotate == -1)
                {
                  memset(&sprtemp[frame].lump, 0, sizeof(sprtemp[0].lump));
                  sprtemp[frame].flip = 0;
                  sprtemp[frame].rotate = 0;
                }
              }

              // allocate space for the frames present and copy sprtemp to it
              sprites[i].spriteframes =
                Z_Malloc (maxframe * sizeof(spriteframe_t));
              memcpy (sprites[i].spriteframes, sprtemp,
                      maxframe*sizeof(spriteframe_t));
            }
        }
    }
  Z_Free(hash);             // free hash table
}

//
// GAME FUNCTIONS
//

static vissprite_t *vissprites, **vissprite_ptrs;  // killough
static int num_vissprite, num_vissprite_alloc, num_vissprite_ptrs;

//
// R_InitSprites
// Called at program start.
//

void R_InitSprites(const char * const *namelist)
{
  int i;
  for (i=0; i<SCREENWIDTH; i++)    // killough 2/8/98
    negonearray[i] = -1;
  R_InitSpriteDefs(namelist);
  bg_FixPickupFrames();
}

//
// R_ClearSprites
// Called at frame start.
//

void R_ClearSprites (void)
{
  num_vissprite = 0;            // killough
}

//
// R_NewVisSprite
//

static vissprite_t *R_NewVisSprite(void)
{
  if (num_vissprite >= num_vissprite_alloc)             // killough
    {
      size_t num_vissprite_alloc_prev = num_vissprite_alloc;

      num_vissprite_alloc = num_vissprite_alloc ? num_vissprite_alloc*2 : 128;
      vissprites = Z_Realloc(vissprites,num_vissprite_alloc*sizeof(*vissprites));

      //e6y: set all fields to zero
      memset(vissprites + num_vissprite_alloc_prev, 0,
        (num_vissprite_alloc - num_vissprite_alloc_prev)*sizeof(*vissprites));
    }
 {
   vissprite_t* v = vissprites + num_vissprite++;

   v->bg_halo = 0; v->bg_weather = 0; v->bg_voxel = 0;
   return v;
 }
}

//
// R_DrawMaskedColumn
// Used for sprites and masked mid textures.
// Masked means: partly transparent, i.e. stored
//  in posts/runs of opaque pixels.
//

int   *mfloorclip;   // dropoff overflow
int   *mceilingclip; // dropoff overflow
fixed_t spryscale;
int64_t sprtopscreen; // R_WiggleFix
int colheight; // Scaled software fuzz

void R_DrawMaskedColumn(
  const rpatch_t *patch,
  R_DrawColumn_f colfunc,
  draw_column_vars_t *dcvars,
  const rcolumn_t *column,
  const rcolumn_t *prevcolumn,
  const rcolumn_t *nextcolumn
)
{
  int     i;
  int64_t     topscreen; // R_WiggleFix
  int64_t     bottomscreen; // R_WiggleFix
  fixed_t basetexturemid = dcvars->texturemid;
  
  colheight = 0;

  dcvars->texheight = patch->height; // killough 11/98
  for (i=0; i<column->numPosts; i++) {
      const rpost_t *post = &column->posts[i];

      // calculate unclipped screen coordinates for post
      topscreen = sprtopscreen + spryscale*post->topdelta;
      bottomscreen = topscreen + spryscale*post->length;

      dcvars->yl = (int)((topscreen+FRACUNIT-1)>>FRACBITS);
      dcvars->yh = (int)((bottomscreen-1)>>FRACBITS);

      if (dcvars->yh >= mfloorclip[dcvars->x])
        dcvars->yh = mfloorclip[dcvars->x]-1;

      if (dcvars->yl <= mceilingclip[dcvars->x])
        dcvars->yl = mceilingclip[dcvars->x]+1;

      if (dcvars->yh >= dcvars->baseclip && dcvars->baseclip != -1)
        dcvars->yh = dcvars->baseclip;

      // killough 3/2/98, 3/27/98: Failsafe against overflow/crash:
      if (dcvars->yl >= 0 && dcvars->yl <= dcvars->yh && dcvars->yh < viewheight)
        {
          dcvars->source = column->pixels + post->topdelta;
          dcvars->prevsource = prevcolumn->pixels + post->topdelta;
          dcvars->nextsource = nextcolumn->pixels + post->topdelta;

          dcvars->texturemid = basetexturemid - (post->topdelta<<FRACBITS);
          dcvars->pspritepostheight = dcvars->isplayersprite ? post->length : 0;

          dcvars->edgeslope = post->slope;
          // Drawn by either R_DrawColumn
          //  or (SHADOW) R_DrawFuzzColumn.
          dcvars->drawingmasked = 1; // POPE
          colfunc (dcvars);
          dcvars->drawingmasked = 0; // POPE

          colheight += dcvars->yh - dcvars->yl + 1;
        }
    }
  dcvars->texturemid = basetexturemid;
}

static void R_SetSpritelights(int lightlevel)
{
  int lightnum = (lightlevel >> LIGHTSEGSHIFT) + (extralight * LIGHTBRIGHT);
  spritelights = scalelight[BETWEEN(0, LIGHTLEVELS - 1, lightnum)];
}


//
// R_DrawVisSprite
//  mfloorclip and mceilingclip should also be set.
//
// CPhipps - new wad lump handling, *'s to const*'s
static void R_DrawVisSprite(vissprite_t *vis)
{
  int      texturecolumn;
  fixed_t  frac;
  const rpatch_t *patch = R_PatchByNum(vis->patch+firstspritelump);
  R_DrawColumn_f colfunc;
  draw_column_vars_t dcvars;
  fixed_t  artscale;

  R_SetDefaultDrawColumnVars(&dcvars);

  dcvars.colormap = vis->colormap;

  if (vis->mobjflags & MF_PLAYERSPRITE)
    dcvars.isplayersprite = true;

  // hexen_note: colfunc: No idea how to merge this right now...
  // if (vis->mobjflags & (MF_SHADOW | MF_ALTSHADOW))
  // {
  //     if (vis->mobjflags & MF_TRANSLATION)
  //     {
  //         colfunc = R_DrawTranslatedTLColumn;
  //         dc_translation = translationtables - 256
  //             + (vis->pclass - 1) * ((g_maxplayers - 1) * 256) +
  //             ((vis->mobjflags & MF_TRANSLATION) >> (MF_TRANSSHIFT - 8));
  //     }
  //     else if (vis->mobjflags & MF_SHADOW)
  //     {                       // Draw using shadow column function
  //         colfunc = tlcolfunc;
  //     }
  //     else
  //     {
  //         colfunc = R_DrawAltTLColumn;
  //     }
  // }
  // else if (vis->mobjflags & MF_TRANSLATION)
  // {
  //     // Draw using translated column function
  //     colfunc = R_DrawTranslatedColumn;
  //     dc_translation = translationtables - 256
  //         + (vis->pclass - 1) * ((g_maxplayers - 1) * 256) +
  //         ((vis->mobjflags & MF_TRANSLATION) >> (MF_TRANSSHIFT - 8));
  // }

  // killough 4/11/98: rearrange and handle translucent sprites
  // mixed with translucent/non-translucenct 2s normals

  if (!dcvars.colormap)   // NULL colormap = shadow draw
  {
    R_ResetFuzzCol(colheight); // Reset fuzz column for new sprite
    colfunc = R_GetDrawColumnFunc(RDC_PIPELINE_FUZZ, RDRAW_FILTER_POINT);    // killough 3/14/98
  }
  else if (vis->color)
  {
    colfunc = R_GetDrawColumnFunc(RDC_PIPELINE_TRANSLATED, RDRAW_FILTER_POINT);
    dcvars.translation = colrngs[vis->color];
  }
  else if (vis->mobjflags & MF_TRANSLATION)
  {
    colfunc = R_GetDrawColumnFunc(RDC_PIPELINE_TRANSLATED, RDRAW_FILTER_POINT);
    dcvars.translation = translationtables - 256 +
      ((vis->mobjflags & MF_TRANSLATION) >> (MF_TRANSSHIFT-8) );
  }
  else if (vis->tranmap) // phares
  {
    colfunc = R_GetDrawColumnFunc(RDC_PIPELINE_TRANSLUCENT, RDRAW_FILTER_POINT);
    tranmap = vis->tranmap;
  }
  else
  {
    colfunc = R_GetDrawColumnFunc(RDC_PIPELINE_STANDARD, RDRAW_FILTER_POINT); // killough 3/14/98, 4/11/98
  }

// proff 11/06/98: Changed for high-res
  // Begotten: the picture has `artscale` pixels per map unit (BGSCALE); vis->scale and vis->texturemid are in map units,
  // spryscale / iscale / texturemid below are in picture pixels. artscale = 1 changes nothing.
  artscale = (vis->mobjflags & MF_PLAYERSPRITE) ? bg_scale_weapons : bg_scale_sprites;
  dcvars.iscale = FixedMul(FixedDiv (FRACUNIT, vis->scale), artscale);
  dcvars.texturemid = FixedMul(vis->texturemid, artscale);
  frac = vis->startfrac;
  spryscale = FixedDiv(vis->scale, artscale);
  sprtopscreen = centeryfrac - FixedMul(dcvars.texturemid,spryscale);

  // check to see if weapon is a vissprite
  if(vis->mobjflags & MF_PLAYERSPRITE)
  {
    // [FG] fix garbage lines at the top of weapon sprites
    dcvars.iscale = FixedMul(pspriteiyscale, artscale);
    dcvars.texturemid += FixedMul(((centery - viewheight/2)<<FRACBITS), dcvars.iscale);
    sprtopscreen += (viewheight/2 - centery)<<FRACBITS;
  }

  if (vis->floorclip && !(vis->mobjflags & MF_PLAYERSPRITE))
  {
    fixed_t sprbotscreen = sprtopscreen + FixedMul(LittleShort(patch->height) << FRACBITS, spryscale);
    dcvars.baseclip = (sprbotscreen - FixedMul(vis->floorclip, vis->scale)) >> FRACBITS;
  }

  for (dcvars.x=vis->x1 ; dcvars.x<=vis->x2 ; dcvars.x++, frac += vis->xiscale)
    {
      texturecolumn = frac>>FRACBITS;

      if (!dcvars.colormap) R_CheckFuzzCol(dcvars.x, colheight);

      R_DrawMaskedColumn(
        patch,
        colfunc,
        &dcvars,
        R_GetPatchColumnClamped(patch, texturecolumn),
        R_GetPatchColumnClamped(patch, texturecolumn-1),
        R_GetPatchColumnClamped(patch, texturecolumn+1)
      );
    }
}

int r_near_clip_plane = MINZ;

void R_SetClipPlanes(void)
{
  // thing is behind view plane?
  if ((V_IsOpenGLMode()) && (HaveMouseLook() || (gl_render_fov > FOV90)))
  {
    r_near_clip_plane = -(FRACUNIT * 80);
  }
  else
  {
    r_near_clip_plane = MINZ;
  }
}

//
// R_ProjectSprite
// Generates a vissprite for a thing if it might be visible.
//

dboolean LevelUseFullBright = true;

static void R_ProjectSprite (mobj_t* thing, int lightlevel)
{
  fixed_t   gzt, gzb;               // killough 3/27/98
  fixed_t   tx;
  fixed_t   xscale;
  int       x1;
  int       x2;
  spritedef_t   *sprdef;
  spriteframe_t *sprframe;
  int       lump;
  dboolean   flip;
  int       bgsprite, bgframe; // Begotten: the sprite and frame actually drawn
  vissprite_t *vis;
  fixed_t   iscale;
  int heightsec;      // killough 3/27/98

  // transform the origin point
  //e6y
  fixed_t tr_x, tr_y;
  fixed_t fx, fy, fz;
  fixed_t gxt, gyt;
  fixed_t tz, tz2;
  int width;

  if (thing->flags2 & MF2_DONTDRAW)
  {
    return;
  }

  if (V_IsOpenGLMode())
  {
    gld_ProjectSprite(thing, lightlevel);
    return;
  }

  if (R_ViewInterpolation())
  {
    fx = thing->PrevX + FixedMul (tic_vars.frac, thing->x - thing->PrevX);
    fy = thing->PrevY + FixedMul (tic_vars.frac, thing->y - thing->PrevY);
    fz = thing->PrevZ + FixedMul (tic_vars.frac, thing->z - thing->PrevZ);
  }
  else
  {
    fx = thing->x;
    fy = thing->y;
    fz = thing->z;
  }

  if (bg_weapons) bg_ViewShift(thing, viewx, viewy, &fx, &fy);
  tr_x = fx - viewx;
  tr_y = fy - viewy;

  gxt = FixedMul(tr_x,viewcos);
  gyt = -FixedMul(tr_y,viewsin);

  tz = gxt-gyt;

  // thing is behind view plane?
  if (tz < r_near_clip_plane)
    return;

  xscale = FixedDiv(projection, tz);

  gxt = -FixedMul(tr_x,viewsin);
  gyt = FixedMul(tr_y,viewcos);
  tx = -(gyt+gxt);

  // too far off the side?
  if (D_abs(tx) > ((int64_t) tz << 2))
    return;

  // Begotten: a voxel prop is drawn as a model, not as a picture. It goes through the same sorting and clipping.
  if (bg_weapons && !thing->player && thing->subsector)
  {
    int vx1, vx2, vi;
    fixed_t vscale, vgz, vgzt;

    vi = bgvox_Project(thing, fx, fy, fz, &vx1, &vx2, &vscale, &vgz, &vgzt);
    if (vi)
    {
      vissprite_t* vv = R_NewVisSprite();
      const lighttable_t* vcm;

      R_SetSpritelights(lightlevel);
      if (fixedcolormap)
        vcm = fixedcolormap;
      else
      {
        int vindex = (int)(((int64_t)xscale * 160 / wide_centerx) >> LIGHTSCALESHIFT);

        if (vindex >= MAXLIGHTSCALE)
          vindex = MAXLIGHTSCALE - 1;
        vcm = spritelights[vindex];
      }
      vv->bg_voxel = vi;
      vv->x1 = (short) vx1;
      vv->x2 = (short) vx2;
      vv->gx = fx;
      vv->gy = fy;
      vv->gz = vgz;
      vv->gzt = vgzt;
      vv->scale = vscale;
      vv->heightsec = thing->subsector->sector->heightsec;
      vv->mobjflags = thing->flags;
      vv->floorclip = 0;
      vv->texturemid = 0;
      vv->colormap = vcm;
      bgvox_SetBase(vi, vcm, thing->subsector->sector);
      return;
    }
  }

    // decide which patch to use for sprite relative to player
#ifdef RANGECHECK
  if ((unsigned) thing->sprite >= (unsigned)num_sprites)
    I_Error ("R_ProjectSprite: Invalid sprite number %i", thing->sprite);
#endif

  // Begotten: a player's body is drawn from the class' pose art (jump, shoot, parry, per weapon)
  // when it has some, else from the class' replacement sprite, else the stock one
  bgsprite = thing->sprite;
  bgframe = thing->frame;
  if (bg_weapons && thing->player)
  {
    if (!bg_PlayerPose(thing, &bgsprite, &bgframe))
      bgsprite = bg_ClassSprite(thing->player->bgclass, thing->sprite, thing->frame);
  }
  sprdef = &sprites[bgsprite];

#ifdef RANGECHECK
  if ((bgframe&FF_FRAMEMASK) >= sprdef->numframes)
    I_Error ("R_ProjectSprite: Invalid sprite frame %i : %i", thing->sprite,
             thing->frame);
#endif

  if (!sprdef->spriteframes)
    I_Error ("R_ProjectSprite: Missing spriteframes %i : %i", thing->sprite,
             thing->frame);

  sprframe = &sprdef->spriteframes[bgframe & FF_FRAMEMASK];

  if (sprframe->rotate)
    {
      // choose a different rotation based on player view
      angle_t rot;
      angle_t ang = R_PointToAngle2(viewx, viewy, fx, fy);
      if (sprframe->lump[0] == sprframe->lump[1])
      {
        rot = (ang - thing->angle + (angle_t)(ANG45/2)*9) >> 28;
      }
      else
      {
        rot = (ang - thing->angle + (angle_t)(ANG45 / 2) * 9 -
          (angle_t)(ANG180 / 16)) >> 28;
      }
      lump = sprframe->lump[rot];
      flip = (dboolean)(sprframe->flip & (1 << rot));
    }
  else
    {
      // use single rotation for all views
      lump = sprframe->lump[0];
      flip = (dboolean)(sprframe->flip & 1);
    }

  {
    const rpatch_t* patch = R_PatchByNum(lump+firstspritelump);
    thing->patch_width = patch->width;

    /* calculate edges of the shape
     * cph 2003/08/1 - fraggle points out that this offset must be flipped
     * if the sprite is flipped; e.g. FreeDoom imp is messed up by this. */
    // Begotten: sizes and offsets are in picture pixels; bg_scale_sprites pixels make one map unit
    if (flip) {
      tx -= BG_UNITS(patch->width - patch->leftoffset, bg_scale_sprites);
    } else {
      tx -= BG_UNITS(patch->leftoffset, bg_scale_sprites);
    }
    x1 = (centerxfrac + FixedMul(tx,xscale)) >> FRACBITS;

    tx += BG_UNITS(patch->width, bg_scale_sprites);
    x2 = ((centerxfrac + FixedMul (tx,xscale) - FRACUNIT/2) >> FRACBITS);

    gzt = fz + BG_UNITS(patch->topoffset, bg_scale_sprites);
    gzb = gzt - BG_UNITS(patch->height, bg_scale_sprites);
    width = patch->width;
  }

  // off the side?
  if (x1 > viewwidth || x2 < 0)
    return;

  // [RH] Reject sprites that are off the top or bottom of the screen
  tz2 = FixedMul(tr_x, viewtancos) + FixedMul(tr_y, viewtansin);
  if (FixedMul(globaluclip, tz2) > viewz - gzb ||
    FixedMul(globaldclip, tz2) < viewz - gzt)
  {
    return;
  }

    // killough 3/27/98: exclude things totally separated
    // from the viewer, by either water or fake ceilings
    // killough 4/11/98: improve sprite clipping for underwater/fake ceilings

  heightsec = thing->subsector->sector->heightsec;

  if (heightsec != -1)   // only clip things which are in special sectors
    {
      int phs = viewplayer->mo->subsector->sector->heightsec;
      if (phs != -1 && viewz < sectors[phs].floorheight ?
          fz >= sectors[heightsec].floorheight :
          gzt < sectors[heightsec].floorheight)
        return;
      if (phs != -1 && viewz > sectors[phs].ceilingheight ?
          gzt < sectors[heightsec].ceilingheight &&
          viewz >= sectors[heightsec].ceilingheight :
          fz >= sectors[heightsec].ceilingheight)
        return;
    }

  //e6y FIXME!!!
  if (thing == players[displayplayer].mo && walkcamera.type != 2)
//  if (thing->player && thing->player == &players[displayplayer] && walkcamera.type != 2)
    return;

  // store information in a vissprite
  vis = R_NewVisSprite ();

  vis->gx = fx;
  vis->gy = fy;
  vis->gz = fz;

  //vis->isplayersprite = false; // e6y

  // killough 3/27/98: save sector for special clipping later
  vis->heightsec = heightsec;

  vis->mobjflags = thing->flags;
  // Begotten co-op: a pickup the viewer has already taken is drawn translucent
  if (bg_weapons && (thing->flags & MF_SPECIAL) && viewplayer && (bg_TakenBy(thing, viewplayer) || bg_RucksackHidden(thing, viewplayer)))
    vis->mobjflags |= g_mf_translucent;
// proff 11/06/98: Changed for high-res
  vis->scale = FixedDiv(projectiony, tz);
  vis->gzt = gzt;                          // killough 3/27/98

  if (heretic)
  {
    if (thing->flags2 & MF2_FEETARECLIPPED
        && vis->gz <= thing->subsector->sector->floorheight)
    {
      vis->floorclip = 10 << FRACBITS;
    }
    else
      vis->floorclip = 0;
  }
  else if (hexen)
  {
    if (thing->flags & MF_TRANSLATION)
    {
      if (thing->player)
      {
        vis->pclass = thing->player->pclass;
      }
      else
      {
        vis->pclass = thing->special1.i;
      }
      if (vis->pclass > 3)
      {
        vis->pclass = 1;
      }
    }
    // foot clipping
    vis->floorclip = thing->floorclip;
  }
  else
  {
    vis->floorclip = 0;
  }

  vis->texturemid = vis->gzt - viewz - vis->floorclip;
  vis->x1 = x1 < 0 ? 0 : x1;
  vis->x2 = x2 >= viewwidth ? viewwidth-1 : x2;
  iscale = FixedMul(FixedDiv (FRACUNIT, xscale), bg_scale_sprites);
  vis->color = thing->color;

  if (flip)
    {
      vis->startfrac = (width<<FRACBITS)-1;
      vis->xiscale = -iscale;
    }
  else
    {
      vis->startfrac = 0;
      vis->xiscale = iscale;
    }

  if (vis->x1 > x1)
    vis->startfrac += vis->xiscale*(vis->x1-x1);
  vis->patch = lump;

  R_SetSpritelights(lightlevel);

  // get light level
  if (thing->flags & g_mf_shadow)
      vis->colormap = NULL;             // shadow draw
  else if (fixedcolormap)
    vis->colormap = fixedcolormap;      // fixed map
  else if (LevelUseFullBright && bgframe & FF_FULLBRIGHT)
    vis->colormap = fullcolormap;     // full bright  // killough 3/20/98
  else
    {      // diminished light
      int index = (int)(((int64_t)xscale * 160 / wide_centerx) >> LIGHTSCALESHIFT);
      if (index >= MAXLIGHTSCALE)
        index = MAXLIGHTSCALE - 1;
      vis->colormap = spritelights[index];
    }

  if (bg_weapons && vis->colormap && thing->subsector)
    vis->colormap = bg_LightSprite(vis->colormap, fx, fy, fz + (thing->height >> 1), thing->subsector->sector);

  R_UpdateVisSpriteTranMap(vis, thing);
}

//
// R_AddSprites
// During BSP traversal, this adds sprites by sector.
//
// killough 9/18/98: add lightlevel as parameter, fixing underwater lighting
void R_AddSprites(subsector_t* subsec, int lightlevel)
{
  sector_t* sec=subsec->sector;
  mobj_t *thing;

  if (compatibility_level <= boom_202_compatibility)
    lightlevel = sec->lightlevel;

  // Handle all things in sector.

  if (dsda_ShowAliveMonsters() && V_IsOpenGLMode())
  {
    if (dsda_ShowAliveMonsters() == 1)
    {
      for (thing = sec->thinglist; thing; thing = thing->snext)
      {
        if (!ALIVE(thing))
          R_ProjectSprite(thing, lightlevel);
      }
    }
  }
  else
  {
    for (thing = sec->thinglist; thing; thing = thing->snext)
    {
      R_ProjectSprite(thing, lightlevel);
    }
  }
}

//
// R_AddAllAliveMonstersSprites
// Add all alive monsters.
//
void R_AddAllAliveMonstersSprites(void)
{
  int i;
  sector_t* sec;
  mobj_t *thing;

  for (i = 0; i < numsectors; i++)
  {
    sec = &sectors[i];
    for (thing = sec->thinglist; thing; thing = thing->snext)
    {
      if (ALIVE(thing))
      {
        thing->flags |= MF_NO_DEPTH_TEST;
        R_ProjectSprite(thing, 255);
        thing->flags &= ~MF_NO_DEPTH_TEST;
      }
    }
  }
}

// [crispy] apply bobbing (or centering) to the player's weapon sprite
static void R_ApplyWeaponBob (fixed_t *sx, dboolean bobx, fixed_t *sy, dboolean boby)
{
	const angle_t angle = (128 * leveltime) & FINEMASK;
	fixed_t bob = viewplayer->bob * dsda_WeaponBob() / 4;

	if (sx)
	{
		*sx = FRACUNIT;

		if (bobx)
		{
			 *sx += FixedMul(bob, finecosine[angle]);
		}
	}

	if (sy)
	{
		*sy = 32 * FRACUNIT; // [crispy] WEAPONTOP

		if (boby)
		{
			*sy += FixedMul(bob, finesine[angle & (FINEANGLES / 2 - 1)]);
		}
	}
}

//
// R_DrawPSprite
//

// heretic
static int PSpriteSY[NUMCLASSES][NUMWEAPONS] = {
  {
    0,                          // staff
    5 * FRACUNIT,               // goldwand
    15 * FRACUNIT,              // crossbow
    15 * FRACUNIT,              // blaster
    15 * FRACUNIT,              // skullrod
    15 * FRACUNIT,              // phoenix rod
    15 * FRACUNIT,              // mace
    15 * FRACUNIT,              // gauntlets
    15 * FRACUNIT               // beak
  },
  {0, -12 * FRACUNIT, -10 * FRACUNIT, 10 * FRACUNIT}, // Fighter
  {-8 * FRACUNIT, 10 * FRACUNIT, 10 * FRACUNIT, 0}, // Cleric
  {9 * FRACUNIT, 20 * FRACUNIT, 20 * FRACUNIT, 20 * FRACUNIT}, // Mage
  {10 * FRACUNIT, 10 * FRACUNIT, 10 * FRACUNIT, 10 * FRACUNIT} // Pig
};

static void R_DrawPSprite (pspdef_t *psp)
{
  pspdef_t *psp_orig = psp;
  int           x1, x2;
  int           gx1;
  spritedef_t   *sprdef;
  spriteframe_t *sprframe;
  int           lump;
  dboolean       flip;
  vissprite_t   *vis;
  vissprite_t   avis;
  int           width;
  fixed_t       topoffset;
  fixed_t       psp_sx = psp->sx;
  fixed_t       psp_sy = psp->sy;

  // decide which patch to use

#ifdef RANGECHECK
  if ( (unsigned)psp->state->sprite >= (unsigned)num_sprites)
    I_Error ("R_ProjectSprite: Invalid sprite number %i", psp->state->sprite);
#endif

  sprdef = &sprites[bg_weapons && viewplayer ? bg_ClassSprite(viewplayer->bgclass, psp->state->sprite, psp->state->frame) : psp->state->sprite];

  if (bg_weapons && (psp->state->frame & FF_FRAMEMASK) >= sprdef->numframes && sprdef->numframes > 0)
  {
    // a class' art that stops short of the last blade frames (charge, stab): hold the last frame it has
    static pspdef_t clamp_psp;
    static state_t clamp_state;

    clamp_state = *psp->state;
    clamp_state.frame = (clamp_state.frame & ~FF_FRAMEMASK) | (sprdef->numframes - 1);
    clamp_psp = *psp;
    clamp_psp.state = &clamp_state;
    psp = &clamp_psp;
  }
#ifdef RANGECHECK
  if ( (psp->state->frame & FF_FRAMEMASK)  >= sprdef->numframes)
    I_Error ("R_ProjectSprite: Invalid sprite frame %i : %li",
             psp->state->sprite, psp->state->frame);
#endif

  sprframe = &sprdef->spriteframes[psp->state->frame & FF_FRAMEMASK];

  lump = sprframe->lump[0];
  flip = (dboolean)(sprframe->flip & 1);

  {
    int weapon_attack_alignment = dsda_IntConfig(dsda_config_weapon_attack_alignment);

    // [crispy] don't align swiping weapons
    const dboolean swiping_weapon = hexen && (viewplayer->pclass == PCLASS_FIGHTER ||
                                             (viewplayer->pclass == PCLASS_CLERIC &&
                                             viewplayer->readyweapon == wp_first));

    if (!dsda_WeaponBob() && !(swiping_weapon && viewplayer->attackdown))
    {
      static fixed_t last_sy = 32 * FRACUNIT;

      psp_sx = FRACUNIT;

      if (psp->state->action != A_Lower && psp->state->action != A_Raise)
      {
        last_sy = psp->sy;
        psp_sy = 32 * FRACUNIT;
      }
      else if (psp->state->action == A_Lower)
      {
        // We want to move smoothly from where we were
        psp_sy -= (last_sy - 32 * FRACUNIT);
      }
    }
    else if (weapon_attack_alignment && viewplayer->attackdown && !psp->state->misc1)
    { // [crispy] center the weapon sprite horizontally and vertically
      R_ApplyWeaponBob(&psp_sx, weapon_attack_alignment == CENTERWEAPON_BOB, NULL, false);

      // [crispy] don't center vertically during lowering and raising states
      if (weapon_attack_alignment >= CENTERWEAPON_HORVER &&
          psp->state->action != A_Lower && psp->state->action != A_Raise && !swiping_weapon)
      {
          R_ApplyWeaponBob(NULL, false, &psp_sy, weapon_attack_alignment == CENTERWEAPON_BOB);
      }
    }
    else if (psp->state->action == A_WeaponReady && psp->state->tics > 1 && movement_smooth)
    {
      // Interpolate bobbing for animated weapons (Chainsaw)
      R_ApplyWeaponBob(&psp_sx, true, &psp_sy, true);
    }
    else if (psp->state->action == A_WeaponReady && dsda_WeaponBob() < 4)
    {
      // Always apply Weaponbob when using bobbing increments
      R_ApplyWeaponBob(&psp_sx, true, &psp_sy, true);
    }
  }

  {
    const rpatch_t* patch = R_PatchByNum(lump+firstspritelump);
    // calculate edges of the shape
    fixed_t       tx;
    tx = psp_sx-160*FRACUNIT;

    tx -= BG_UNITS(patch->leftoffset, bg_scale_weapons);
    x1 = (centerxfrac + FixedMul (tx,pspritexscale))>>FRACBITS;

    // [AR] opengl weapon alignment
    gx1 = x1;

    tx += BG_UNITS(patch->width, bg_scale_weapons);
    x2 = ((centerxfrac + FixedMul (tx, pspritexscale) ) >>FRACBITS) - 1;

    width = patch->width;
    topoffset = BG_UNITS(patch->topoffset, bg_scale_weapons);
  }

  if (psp_orig == &viewplayer->psprites[ps_weapon])
    bgnet_NoteWeaponDraw(psp_orig, psp_sx, psp_sy, lump);

  // off the side
  if (x2 < 0 || x1 > viewwidth)
    return;

  // store information in a vissprite
  vis = &avis;
  vis->mobjflags = MF_PLAYERSPRITE;
  vis->pclass = 0;
  vis->floorclip = 0;
   // killough 12/98: fix psprite positioning problem
  vis->texturemid = (BASEYCENTER<<FRACBITS) /* +  FRACUNIT/2 */ -
                    (psp_sy-topoffset);

  if (R_FullView() && raven)
  {
    vis->texturemid -= PSpriteSY[viewplayer->pclass][players[consoleplayer].readyweapon];
  }

  // Move the weapon down for 1280x1024.
  vis->texturemid -= psprite_offset;

  vis->x1 = x1 < 0 ? 0 : x1;
  vis->x2 = x2 >= viewwidth ? viewwidth-1 : x2;

  // [AR] opengl weapon alignment
  vis->gx1 = gx1;

// proff 11/06/98: Added for high-res
  vis->scale = pspriteyscale;
  vis->color = 0;

  if (flip)
    {
      vis->xiscale = -FixedMul(pspriteiscale, bg_scale_weapons);
      vis->startfrac = (width<<FRACBITS)-1;
    }
  else
    {
      vis->xiscale = FixedMul(pspriteiscale, bg_scale_weapons);
      vis->startfrac = 0;
    }

  if (vis->x1 > x1)
    vis->startfrac += vis->xiscale*(vis->x1-x1);

  vis->patch = lump;

  if (viewplayer->powers[pw_invisibility] > 4*32
      || viewplayer->powers[pw_invisibility] & 8)
  {
    if (heretic)
    {
      vis->mobjflags |= MF_SHADOW;
      vis->colormap = spritelights[MAXLIGHTSCALE-1];
    }
    else
    {
      vis->colormap = NULL;                    // shadow draw
    }
  }
  else if (viewplayer->powers[pw_invulnerability] && viewplayer->pclass == PCLASS_CLERIC)
  {
    vis->colormap = spritelights[MAXLIGHTSCALE - 1];
    if (viewplayer->powers[pw_invulnerability] > 4 * 32)
    {
      if (viewplayer->mo->flags2 & MF2_DONTDRAW)
      {                   // don't draw the psprite
        vis->mobjflags |= MF_SHADOW;
      }
      else if (viewplayer->mo->flags & MF_SHADOW)
      {
        vis->mobjflags |= MF_ALTSHADOW;
      }
    }
    else if (viewplayer->powers[pw_invulnerability] & 8)
    {
      vis->mobjflags |= MF_SHADOW;
    }
  }
  else if (fixedcolormap)
    vis->colormap = fixedcolormap;           // fixed color
  else if (psp->state->frame & FF_FULLBRIGHT)
    vis->colormap = fullcolormap;            // full bright // killough 3/20/98
  else
    // e6y: original code is restored
    vis->colormap = spritelights[MAXLIGHTSCALE-1];  // local light

  // Begotten: the muzzle flash layer is drawn see-through (when the translucency table is on)
  if (bg_weapons && viewplayer && psp_orig == &viewplayer->psprites[ps_flash])
    vis->mobjflags |= g_mf_translucent;

  R_UpdateVisSpriteTranMap(vis, NULL);

  //e6y: interpolation for weapon bobbing
  if (movement_smooth)
  {
    typedef struct interpolate_s
    {
      int x1;
      int x1_prev;
      int gx1;
      int gx1_prev;
      int texturemid;
      int texturemid_prev;
      int lump;
      int gt;
    } psp_interpolate_t;

    // one per local player AND per psprite layer (weapon, flash): split-screen views and the two layers must not
    // interpolate against each other.  "A new tic has begun" is tracked here by gametic, not by the display's
    // global realframe flag, which one drawn frame could consume for another.
    static psp_interpolate_t psp_inters[MAX_MAXPLAYERS][NUMPSPRITES];
    int layer = (int) (psp_orig - viewplayer->psprites);
    psp_interpolate_t* pi_;

    if (layer < 0 || layer >= NUMPSPRITES)
      layer = 0;
    pi_ = &psp_inters[(viewplayer >= players && viewplayer < players + MAX_MAXPLAYERS) ? (int) (viewplayer - players) : 0][layer];

    if (pi_->gt != gametic)
    {
      pi_->gt = gametic;
      pi_->x1 = pi_->x1_prev;
      pi_->gx1 = pi_->gx1_prev;
      pi_->texturemid = pi_->texturemid_prev;
    }

    pi_->x1_prev = vis->x1;
    pi_->gx1_prev = vis->gx1;
    pi_->texturemid_prev = vis->texturemid;

    // Do not interpolate on the first tic of the level
    if (leveltime > 1)
    {
      if (lump == pi_->lump)
      {
        int deltax = vis->x2 - vis->x1;
        vis->x1 = pi_->x1 + FixedMul (tic_vars.frac, (vis->x1 - pi_->x1));
        vis->gx1 = pi_->gx1 + FixedMul(tic_vars.frac, (vis->gx1 - pi_->gx1));
        vis->x2 = vis->x1 + deltax;
        vis->texturemid = pi_->texturemid + FixedMul (tic_vars.frac, (vis->texturemid - pi_->texturemid));
      }
      else
      {
        pi_->x1 = vis->x1;
        pi_->gx1 = vis->gx1;
        pi_->texturemid = vis->texturemid;
        pi_->lump=lump;
      }
    }
  }

  if (dsda_HideWeapon())
    return;

  // proff 11/99: don't use software stuff in OpenGL
  if (V_IsSoftwareMode())
  {
    R_DrawVisSprite(vis);
  }
  else
  {
    int lightlevel;
    sector_t tmpsec;
    int floorlightlevel, ceilinglightlevel;

    if ((vis->colormap==fixedcolormap) || (vis->colormap==fullcolormap))
      lightlevel=255;
    else
    {
//      lightlevel = (viewplayer->mo->subsector->sector->lightlevel) + (extralight << LIGHTSEGSHIFT);
      R_FakeFlat( viewplayer->mo->subsector->sector, &tmpsec,
                  &floorlightlevel, &ceilinglightlevel, false);
      lightlevel = ((floorlightlevel+ceilinglightlevel) >> 1) + (extralight << LIGHTSEGSHIFT);

      if (lightlevel < 0)
        lightlevel = 0;
      else if (lightlevel >= 255)
        lightlevel = 255;
    }
    gld_DrawWeapon(lump,vis,lightlevel);
  }
}

//
// R_DrawPlayerSprites
//

void R_DrawPlayerSprites(void)
{
  int i;
  pspdef_t *psp;

  if (walkcamera.type != 0)
    return;

  // get light level
  R_SetSpritelights(viewplayer->mo->subsector->sector->lightlevel);

  // clip to screen bounds
  mfloorclip = screenheightarray;
  mceilingclip = negonearray;

  // add all active psprites
  for (i=0, psp=viewplayer->psprites; i<NUMPSPRITES; i++,psp++)
    if (psp->state)
      R_DrawPSprite (psp);
}

//
// R_SortVisSprites
//
// Rewritten by Lee Killough to avoid using unnecessary
// linked lists, and to use faster sorting algorithm.
//

#ifdef DJGPP

// killough 9/22/98: inlined memcpy of pointer arrays
// CPhipps - added memory as modified
#define bcopyp(d, s, n) asm(" cld; rep; movsl;" :: "D"(d), "S"(s), "c"(n) : "%cc", "%esi", "%edi", "%ecx", "memory")

#else

#define bcopyp(d, s, n) memcpy(d, s, (n) * sizeof(void *))

#endif

// killough 9/2/98: merge sort

static void msort(vissprite_t **s, vissprite_t **t, int n)
{
  if (n >= 16)
    {
      int n1 = n/2, n2 = n - n1;
      vissprite_t **s1 = s, **s2 = s + n1, **d = t;

      msort(s1, t, n1);
      msort(s2, t, n2);

      while ((*s1)->scale > (*s2)->scale ?
             (*d++ = *s1++, --n1) : (*d++ = *s2++, --n2));

      if (n2)
        bcopyp(d, s2, n2);
      else
        bcopyp(d, s1, n1);

      bcopyp(s, t, n);
    }
  else
    {
      int i;
      for (i = 1; i < n; i++)
        {
          vissprite_t *temp = s[i];
          if (s[i-1]->scale < temp->scale)
            {
              int j = i;
              while ((s[j] = s[j-1])->scale < temp->scale && --j);
              s[j] = temp;
            }
        }
    }
}

void R_SortVisSprites (void)
{
  if (num_vissprite)
    {
      int i = num_vissprite;

      // If we need to allocate more pointers for the vissprites,
      // allocate as many as were allocated for sprites -- killough
      // killough 9/22/98: allocate twice as many

      if (num_vissprite_ptrs < num_vissprite*2)
        {
          Z_Free(vissprite_ptrs);  // better than realloc -- no preserving needed
          vissprite_ptrs = Z_Malloc((num_vissprite_ptrs = num_vissprite_alloc*2)
                                  * sizeof *vissprite_ptrs);
        }

      while (--i>=0)
        vissprite_ptrs[num_vissprite-i-1] = vissprites+i;

      // killough 9/22/98: replace qsort with merge sort, since the keys
      // are roughly in order to begin with, due to BSP rendering.

      msort(vissprite_ptrs, vissprite_ptrs + num_vissprite, num_vissprite);
    }
}

//
// R_DrawSprite
//

static void R_DrawSprite (vissprite_t* spr)
{
  drawseg_t *ds;
  int     x;
  int     r1;
  int     r2;
  fixed_t scale;
  fixed_t lowscale;

  for (x = spr->x1 ; x<=spr->x2 ; x++)
    clipbot[x] = -2;
  for (x = spr->x1 ; x<=spr->x2 ; x++)
    cliptop[x] = -2;

  // Scan drawsegs from end to start for obscuring segs.
  // The first drawseg that has a greater scale is the clip seg.

  // Modified by Lee Killough:
  // (pointer check was originally nonportable
  // and buggy, by going past LEFT end of array):

  // e6y: optimization
  if (drawsegs_xrange_size)
  {
    const drawseg_xrange_item_t *last = &drawsegs_xrange[drawsegs_xrange_count - 1];
    drawseg_xrange_item_t *curr = &drawsegs_xrange[-1];
    while (++curr <= last)
    {
      // determine if the drawseg obscures the sprite
      if (curr->x1 > spr->x2 || curr->x2 < spr->x1)
        continue;      // does not cover sprite

      ds = curr->user;

      if (ds->scale1 > ds->scale2)
      {
        lowscale = ds->scale2;
        scale = ds->scale1;
      }
      else
      {
        lowscale = ds->scale1;
        scale = ds->scale2;
      }

      if (scale < spr->scale || (lowscale < spr->scale &&
        !R_PointOnSegSide (spr->gx, spr->gy, ds->curline)))
      {
        if (ds->maskedtexturecol)       // masked mid texture?
        {
          r1 = ds->x1 < spr->x1 ? spr->x1 : ds->x1;
          r2 = ds->x2 > spr->x2 ? spr->x2 : ds->x2;
          R_RenderMaskedSegRange(ds, r1, r2);
        }
        continue;               // seg is behind sprite
      }

      r1 = ds->x1 < spr->x1 ? spr->x1 : ds->x1;
      r2 = ds->x2 > spr->x2 ? spr->x2 : ds->x2;

      // clip this piece of the sprite
      // killough 3/27/98: optimized and made much shorter

      if (ds->silhouette&SIL_BOTTOM && spr->gz < ds->bsilheight) //bottom sil
        for (x=r1 ; x<=r2 ; x++)
          if (clipbot[x] == -2)
            clipbot[x] = ds->sprbottomclip[x];

      if (ds->silhouette&SIL_TOP && spr->gzt > ds->tsilheight)   // top sil
        for (x=r1 ; x<=r2 ; x++)
          if (cliptop[x] == -2)
            cliptop[x] = ds->sprtopclip[x];
    }
  }

  // killough 3/27/98:
  // Clip the sprite against deep water and/or fake ceilings.
  // killough 4/9/98: optimize by adding mh
  // killough 4/11/98: improve sprite clipping for underwater/fake ceilings
  // killough 11/98: fix disappearing sprites

  if (spr->heightsec != -1)  // only things in specially marked sectors
    {
      fixed_t h,mh;
      int phs = viewplayer->mo->subsector->sector->heightsec;
      if ((mh = sectors[spr->heightsec].floorheight) > spr->gz &&
          (h = centeryfrac - FixedMul(mh-=viewz, spr->scale)) >= 0 &&
          (h >>= FRACBITS) < viewheight) {
        if (mh <= 0 || (phs != -1 && viewz > sectors[phs].floorheight))
          {                          // clip bottom
            for (x=spr->x1 ; x<=spr->x2 ; x++)
              if (clipbot[x] == -2 || h < clipbot[x])
                clipbot[x] = h;
          }
        else                        // clip top
    if (phs != -1 && viewz <= sectors[phs].floorheight) // killough 11/98
      for (x=spr->x1 ; x<=spr->x2 ; x++)
        if (cliptop[x] == -2 || h > cliptop[x])
    cliptop[x] = h;
      }

      if ((mh = sectors[spr->heightsec].ceilingheight) < spr->gzt &&
          (h = centeryfrac - FixedMul(mh-viewz, spr->scale)) >= 0 &&
          (h >>= FRACBITS) < viewheight) {
        if (phs != -1 && viewz >= sectors[phs].ceilingheight)
          {                         // clip bottom
            for (x=spr->x1 ; x<=spr->x2 ; x++)
              if (clipbot[x] == -2 || h < clipbot[x])
                clipbot[x] = h;
          }
        else                       // clip top
          for (x=spr->x1 ; x<=spr->x2 ; x++)
            if (cliptop[x] == -2 || h > cliptop[x])
              cliptop[x] = h;
      }
    }
  // killough 3/27/98: end special clipping for deep water / fake ceilings

  // all clipping has been performed, so draw the sprite
  // check for unclipped columns

  for (x = spr->x1 ; x<=spr->x2 ; x++)
    if (clipbot[x] == -2)
      clipbot[x] = viewheight;

  for (x = spr->x1 ; x<=spr->x2 ; x++)
    if (cliptop[x] == -2)
      cliptop[x] = -1;

  mfloorclip = clipbot;
  mceilingclip = cliptop;
  if (spr->bg_voxel)
    bgvox_Draw(spr->bg_voxel, mfloorclip, mceilingclip);          // Begotten: a voxel model
  else if (spr->bg_halo)
    bg_DrawHalo(spr->bg_halo - 1, mfloorclip, mceilingclip);     // Begotten: the glow round a light
  else if (spr->bg_weather)
    bg_DrawWeather(spr->bg_weather - 1, mfloorclip, mceilingclip);  // Begotten: rain or snow
  else
    R_DrawVisSprite (spr);
}

// Begotten: the glows round lights go through the same sorting and clipping as sprites
static void bg_ProjectHalos(void)
{
  int i, n;

  if (!bg_weapons || V_IsOpenGLMode())
    return;
  n = bg_WeatherPrepare();
  for (i = 0; i < n; ++i)
  {
    vissprite_t* vis;
    int x1, x2;
    fixed_t scale, gx, gy, gz, gzt;

    if (!bg_WeatherGet(i, &x1, &x2, &scale, &gx, &gy, &gz, &gzt))
      continue;
    vis = R_NewVisSprite();
    vis->bg_halo = 0;
    vis->bg_weather = i + 1;
    vis->x1 = (short) x1;
    vis->x2 = (short) x2;
    vis->scale = scale;
    vis->gx = gx;
    vis->gy = gy;
    vis->gz = gz;
    vis->gzt = gzt;
    vis->heightsec = -1;
    vis->mobjflags = 0;
    vis->floorclip = 0;
  }
  n = bg_HaloPrepare();
  for (i = 0; i < n; ++i)
  {
    vissprite_t* vis;
    int x1, x2;
    fixed_t scale, gx, gy, gz, gzt;

    if (!bg_HaloGet(i, &x1, &x2, &scale, &gx, &gy, &gz, &gzt))
      continue;
    vis = R_NewVisSprite();
    vis->bg_halo = i + 1;
    vis->bg_weather = 0;
    vis->x1 = (short) x1;
    vis->x2 = (short) x2;
    vis->scale = scale;
    vis->gx = gx;
    vis->gy = gy;
    vis->gz = gz;
    vis->gzt = gzt;
    vis->heightsec = -1;
    vis->mobjflags = 0;
    vis->floorclip = 0;
  }
}

//
// R_DrawMasked
//

void R_DrawMasked(void)
{
  int i;
  drawseg_t *ds;
  int cx = SCREENWIDTH / 2;

  bg_ProjectHalos();
  R_SortVisSprites();

  // e6y
  // Reducing of cache misses in the following R_DrawSprite()
  // Makes sense for scenes with huge amount of drawsegs.
  // ~12% of speed improvement on epic.wad map05
  for(i = 0; i < DS_RANGES_COUNT; i++)
    drawsegs_xranges[i].count = 0;

  if (num_vissprite > 0)
  {
    if (drawsegs_xrange_size < maxdrawsegs)
    {
      drawsegs_xrange_size = 2 * maxdrawsegs;
      for(i = 0; i < DS_RANGES_COUNT; i++)
      {
        drawsegs_xranges[i].items = Z_Realloc(
          drawsegs_xranges[i].items,
          drawsegs_xrange_size * sizeof(drawsegs_xranges[i].items[0]));
      }
    }
    for (ds = ds_p; ds-- > drawsegs;)
    {
      if (ds->silhouette || ds->maskedtexturecol)
      {
        drawsegs_xranges[0].items[drawsegs_xranges[0].count].x1 = ds->x1;
        drawsegs_xranges[0].items[drawsegs_xranges[0].count].x2 = ds->x2;
        drawsegs_xranges[0].items[drawsegs_xranges[0].count].user = ds;

        // e6y: ~13% of speed improvement on sunder.wad map10
        if (ds->x1 < cx)
        {
          drawsegs_xranges[1].items[drawsegs_xranges[1].count] =
            drawsegs_xranges[0].items[drawsegs_xranges[0].count];
          drawsegs_xranges[1].count++;
        }
        if (ds->x2 >= cx)
        {
          drawsegs_xranges[2].items[drawsegs_xranges[2].count] =
            drawsegs_xranges[0].items[drawsegs_xranges[0].count];
          drawsegs_xranges[2].count++;
        }

        drawsegs_xranges[0].count++;
      }
    }
  }

  // draw all vissprites back to front

  dsda_RecordVisSprites(num_vissprite);

  for (i = num_vissprite ;--i>=0; )
  {
    vissprite_t* spr = vissprite_ptrs[i];

    if (spr->x2 < cx)
    {
      drawsegs_xrange = drawsegs_xranges[1].items;
      drawsegs_xrange_count = drawsegs_xranges[1].count;
    }
    else if (spr->x1 >= cx)
    {
      drawsegs_xrange = drawsegs_xranges[2].items;
      drawsegs_xrange_count = drawsegs_xranges[2].count;
    }
    else
    {
      drawsegs_xrange = drawsegs_xranges[0].items;
      drawsegs_xrange_count = drawsegs_xranges[0].count;
    }

    R_DrawSprite(vissprite_ptrs[i]);
  }

  // render any remaining masked mid textures

  // Modified by Lee Killough:
  // (pointer check was originally nonportable
  // and buggy, by going past LEFT end of array):

  //    for (ds=ds_p-1 ; ds >= drawsegs ; ds--)    old buggy code

  for (ds=ds_p ; ds-- > drawsegs ; )  // new -- killough
    if (ds->maskedtexturecol)
      R_RenderMaskedSegRange(ds, ds->x1, ds->x2);

  // draw the psprites on top of everything
  R_DrawPlayerSprites ();
}
