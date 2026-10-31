//
// Copyright(C) 2023 by Ryan Krafnick
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//	DSDA TRANMAP
//

#include <string.h>

#include "lprintf.h"
#include "v_video.h"
#include "w_wad.h"
#include "z_zone.h"

#include "dsda/bgcolor.h"

#include "tranmap.h"

// A translucency table: entry [dest * BG_NUMCOLORS + source] is the palette colour nearest to the blend of the two. It
// is worked out from the reference palette (the whole wide palette, with an exact nearest-colour search) when first
// asked for, like the original game's table it does not change with the palette in use.

static const int default_tranmap_alpha = 66;
static const size_t tranmap_length = (size_t) BG_NUMCOLORS * BG_NUMCOLORS * sizeof(pixel_t);
static pixel_t* tranmap_data[100];

#define MAX_LUMP_TRANMAPS 32
static struct { int lump; pixel_t* data; } lump_tranmaps[MAX_LUMP_TRANMAPS];
static int num_lump_tranmaps;

static void dsda_FillTranMap(pixel_t* buffer, unsigned int alpha)
{
  const byte* pal = bg_ReferencePalette();
  int i, j;
  const int w1 = (int) alpha, w2 = 100 - (int) alpha;

  if (!pal)
  {
    for (i = 0; i < BG_NUMCOLORS; i++)
      for (j = 0; j < BG_NUMCOLORS; j++)
        buffer[i * BG_NUMCOLORS + j] = (pixel_t) j;
    return;
  }
  for (i = 0; i < BG_NUMCOLORS; i++)
  {
    const byte* d = pal + 3 * i;

    for (j = 0; j < BG_NUMCOLORS; j++)
    {
      const byte* s = pal + 3 * j;

      buffer[i * BG_NUMCOLORS + j] = (pixel_t) bg_ColorNearest((d[0] * w2 + s[0] * w1 + 50) / 100,
                                                               (d[1] * w2 + s[1] * w1 + 50) / 100,
                                                               (d[2] * w2 + s[2] * w1 + 50) / 100);
    }
  }
}

const pixel_t* dsda_TranMap(unsigned int alpha) {
  if (alpha > 99)
    return NULL;

  if (!tranmap_data[alpha]) {
    tranmap_data[alpha] = Z_Malloc(tranmap_length);
    dsda_FillTranMap(tranmap_data[alpha], alpha);
  }

  return tranmap_data[alpha];
}

// A legacy TRANMAP lump (256 x 256 indices): kept for the first 256 colours, the rest worked out like the others.
static void dsda_FillFromLump(pixel_t* buffer, const byte* src)
{
  int i, j;

  dsda_FillTranMap(buffer, default_tranmap_alpha);
  for (i = 0; i < 256; i++)
    for (j = 0; j < 256; j++)
      buffer[i * BG_NUMCOLORS + j] = src[i * 256 + j];
}

const pixel_t* dsda_TranMapFromLump(int lump)
{
  int i;

  for (i = 0; i < num_lump_tranmaps; i++)
    if (lump_tranmaps[i].lump == lump)
      return lump_tranmaps[i].data;

  if (W_LumpLength(lump) < 256 * 256)
    return dsda_TranMap(default_tranmap_alpha);

  if (num_lump_tranmaps == MAX_LUMP_TRANMAPS)
    return dsda_TranMap(default_tranmap_alpha);   // far more than any wad has
  lump_tranmaps[num_lump_tranmaps].lump = lump;
  lump_tranmaps[num_lump_tranmaps].data = Z_Malloc(tranmap_length);
  dsda_FillFromLump(lump_tranmaps[num_lump_tranmaps].data, W_LumpByNum(lump));
  return lump_tranmaps[num_lump_tranmaps++].data;
}

const pixel_t* dsda_DefaultTranMap(void) {
  int lump;

  lump = W_CheckNumForName("TRANMAP");

  if (lump != LUMP_NOT_FOUND && W_LumpLength(lump) >= 256 * 256)
    return dsda_TranMapFromLump(lump);

  return dsda_TranMap(default_tranmap_alpha);
}
