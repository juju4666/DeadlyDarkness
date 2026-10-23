//
// Begotten: art at more than one pixel per map unit. See bgscale.h.
// Licensed under the GNU GPL v2 or later, like the rest of the engine.
//

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "doomstat.h"
#include "lprintf.h"
#include "w_wad.h"
#include "z_zone.h"

#include "dsda/bgscale.h"

fixed_t bg_scale_sprites = FRACUNIT;
fixed_t bg_scale_textures = FRACUNIT;
fixed_t bg_scale_weapons = FRACUNIT;

static void bg_ParseScaleText(char* text)
{
  char* line = text;

  while (line && *line)
  {
    char* next = strchr(line, '\n');
    char* hash;
    char* end;
    double v;
    fixed_t* target = NULL;

    if (next)
      *next++ = '\0';
    hash = strchr(line, '#');
    if (hash)
      *hash = '\0';
    while (*line && isspace((unsigned char) *line))
      ++line;
    if (!strncasecmp(line, "sprites", 7)) target = &bg_scale_sprites, line += 7;
    else if (!strncasecmp(line, "textures", 8)) target = &bg_scale_textures, line += 8;
    else if (!strncasecmp(line, "weapons", 7)) target = &bg_scale_weapons, line += 7;
    if (target)
    {
      v = strtod(line, &end);
      if (end != line && v >= 0.25 && v <= 16.0)
        *target = (fixed_t) (v * FRACUNIT + 0.5);
      else
        lprintf(LO_WARN, "BGSCALE: cannot use \"%s\" (a number from 0.25 to 16 is expected)\n", line);
    }
    line = next;
  }
}

void dsda_InitBgScale(void)
{
  int i;

  bg_scale_sprites = bg_scale_textures = bg_scale_weapons = FRACUNIT;
  for (i = 0; i < numlumps; ++i)
  {
    char* text;
    int size;

    if (strncasecmp(lumpinfo[i].name, "BGSCALE", 8))
      continue;
    size = W_LumpLength(i);
    text = Z_Malloc(size + 1);
    memcpy(text, W_LumpByNum(i), size);
    text[size] = '\0';
    bg_ParseScaleText(text);
    Z_Free(text);
  }
  if (bg_scale_sprites != FRACUNIT || bg_scale_textures != FRACUNIT || bg_scale_weapons != FRACUNIT)
    lprintf(LO_INFO, "BGSCALE: sprites %.2f, textures %.2f, weapons %.2f pixels per unit\n",
            bg_scale_sprites / 65536.0, bg_scale_textures / 65536.0, bg_scale_weapons / 65536.0);
}
