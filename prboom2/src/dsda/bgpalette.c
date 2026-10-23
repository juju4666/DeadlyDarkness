//
// Begotten: a palette per map (or group of maps). See bgpalette.h.
// Licensed under the GNU GPL v2 or later, like the rest of the engine.
//

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "doomstat.h"
#include "lprintf.h"
#include "w_wad.h"
#include "z_zone.h"
#include "v_video.h"

#include "dsda/palette.h"
#include "dsda/bgpalette.h"

#define BG_PAL_MAX 128

static struct { int first, last, pal; } ranges[BG_PAL_MAX];
static int num_ranges;
static int default_pal;
static dboolean have_table;
static int applied = -1;

static void bg_ParsePaletteText(char* text)
{
  char* line = text;

  while (line && *line)
  {
    char* next = strchr(line, '\n');
    int a, b, p;
    char* hash;

    if (next)
      *next++ = '\0';
    hash = strchr(line, '#');
    if (hash)
      *hash = '\0';
    while (*line && isspace((unsigned char) *line))
      ++line;
    if (!strncasecmp(line, "default", 7) && sscanf(line + 7, "%d", &p) == 1 && p >= 0 && p <= 9)
    {
      default_pal = p;
      have_table = true;
    }
    else if (!strncasecmp(line, "map", 3))
    {
      int n = sscanf(line + 3, "%d-%d %d", &a, &b, &p);

      if (n != 3)
      {
        n = sscanf(line + 3, "%d %d", &a, &p);
        b = a;
        n = n == 2 ? 3 : 0;
      }
      if (n == 3 && a >= 0 && b >= a && p >= 0 && p <= 9 && num_ranges < BG_PAL_MAX)
      {
        ranges[num_ranges].first = a;
        ranges[num_ranges].last = b;
        ranges[num_ranges].pal = p;
        ++num_ranges;
        have_table = true;
      }
      else
        lprintf(LO_WARN, "BGPAL: cannot read the line \"%s\"\n", line);
    }
    line = next;
  }
}

void dsda_InitBgPalettes(void)
{
  int i;

  num_ranges = 0;
  default_pal = 0;
  have_table = false;
  applied = -1;
  for (i = 0; i < numlumps; ++i)
  {
    char* text;
    int size;

    if (strncasecmp(lumpinfo[i].name, "BGPAL", 8))
      continue;
    size = W_LumpLength(i);
    text = Z_Malloc(size + 1);
    memcpy(text, W_LumpByNum(i), size);
    text[size] = '\0';
    bg_ParsePaletteText(text);
    Z_Free(text);
  }
  if (have_table)
    lprintf(LO_INFO, "BGPAL: %d palette range(s), default palette %d\n", num_ranges, default_pal);
}

int bg_PaletteForMap(int map)
{
  int i, pal = default_pal;

  for (i = 0; i < num_ranges; ++i)
    if (map >= ranges[i].first && map <= ranges[i].last)
      pal = ranges[i].pal;
  return pal;
}

void bg_UpdatePalette(void)
{
  int want, idx;

  if (!have_table)
    return;
  want = (gamestate == GS_LEVEL || gamestate == GS_INTERMISSION || gamestate == GS_FINALE) ? bg_PaletteForMap(gamemap) : default_pal;
  if (want == applied)
    return;
  applied = want;
  idx = playpal_default + want;
  if (idx != playpal_default && W_CheckNumForName(dsda_PlayPalData(idx)->lump_name) == LUMP_NOT_FOUND)
    idx = playpal_default;
  if (idx != playpal_index)
    V_SetPlayPal(idx);
}
