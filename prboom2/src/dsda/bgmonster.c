//
// Begotten: BGMONSTER lump and the monster wind-up sound. See bgmonster.h.
//

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _MSC_VER
#include <strings.h>
#endif
#include "doomstat.h"
#include "info.h"
#include "lprintf.h"
#include "p_mobj.h"
#include "s_sound.h"
#include "sounds.h"
#include "w_wad.h"
#include "z_zone.h"
#include "dsda/bgmonster.h"
#include "dsda/bgnet.h"
#include "dsda/sfx.h"

#define MAX_WINDUPS 128

typedef struct { int doomednum; int sfx; } windup_t;
static windup_t windups[MAX_WINDUPS];
static int num_windups;

// Placeholders made of Doom's own sounds, until real ones are put in the WAD.
static const char* bg_monster_default_text =
  "// doomednum  sound lump\n"
  "3004 DSSGCOCK\n"  // zombieman
  "9    DSSGCOCK\n"  // shotgun guy
  "65   DSSGCOCK\n"  // chaingunner
  "84   DSSGCOCK\n"  // SS
  "3001 DSBGACT\n"   // imp
  "3002 DSSGTATK\n"  // demon
  "58   DSSGTATK\n"  // spectre
  "3006 DSSKLATK\n"  // lost soul
  "3005 DSCACSIT\n"  // cacodemon
  "3003 DSDMACT\n"   // baron
  "69   DSDMACT\n"   // hell knight
  "64   DSDMACT\n"   // arch-vile
  "66   DSDMACT\n"   // revenant
  "67   DSDMACT\n"   // mancubus
  "71   DSDMACT\n"   // pain elemental
  "16   DSDMACT\n"   // cyberdemon
  "7    DSDMACT\n"   // spider mastermind
  "21001 DSSGCOCK\n" // rocket trooper (new number)
  "21016 DSDMACT\n"  // articulator heart
  "68   DSSGCOCK\n"; // shock trooper (the arachnotron slot)

int bg_SfxFromLump(const char* lump)
{
  int id = S_GetSoundID(lump);
  int index;
  sfxinfo_t* sfx;
  char* name;
  size_t i;

  if (id)
    return id;
  if (W_CheckNumForName(lump) < 0)
    return 0;

  name = strdup(lump);
  for (i = 0; name[i]; ++i)
    name[i] = tolower((unsigned char) name[i]);

  sfx = dsda_NewSFX(&index);
  sfx->name = name;
  sfx->priority = 32;
  sfx->pitch = -1;
  sfx->lumpnum = -1;
  sfx->link = NULL;
  return index;
}

static void bg_ParseMonsterText(const char* text, int lumpnum)
{
  const char* p = text;
  int lineno = 0;

  while (*p)
  {
    char line[256], word[64];
    int n = 0, doomednum, i;

    while (*p && *p != '\n' && n < (int) sizeof(line) - 1)
      line[n++] = *p++;
    line[n] = '\0';
    if (*p == '\n')
      ++p;
    ++lineno;

    {
      char* c = strstr(line, "//");
      if (c)
        *c = '\0';
    }
    if (sscanf(line, "%d %63s", &doomednum, word) != 2)
    {
      for (i = 0; line[i] && isspace((unsigned char) line[i]); ++i) ;
      if (line[i])
        lprintf(LO_WARN, "BGMONSTER (lump %d, line %d): cannot understand \"%s\"\n", lumpnum, lineno, line);
      continue;
    }

    {
      int sfx = (!strcmp(word, "-") || !strcasecmp(word, "none")) ? -1 : bg_SfxFromLump(word);
      int slot = -1;

      if (!sfx)
      {
        lprintf(LO_WARN, "BGMONSTER (lump %d, line %d): there is no sound \"%s\"\n", lumpnum, lineno, word);
        continue;
      }
      for (i = 0; i < num_windups; ++i)
        if (windups[i].doomednum == doomednum)
          slot = i;
      if (slot < 0)
      {
        if (num_windups >= MAX_WINDUPS)
          continue;
        slot = num_windups++;
      }
      windups[slot].doomednum = doomednum;
      windups[slot].sfx = sfx < 0 ? 0 : sfx;
    }
  }
}

void dsda_InitBgMonsters(void)
{
  int i;

  num_windups = 0;
  bg_ParseMonsterText(bg_monster_default_text, -1);

  for (i = 0; i < numlumps; ++i)
  {
    char* text;
    int size;

    if (strncasecmp(lumpinfo[i].name, "BGMONSTE", 8))
      continue;
    size = W_LumpLength(i);
    text = Z_Malloc(size + 1);
    memcpy(text, W_LumpByNum(i), size);
    text[size] = '\0';
    bg_ParseMonsterText(text, i);
    Z_Free(text);
  }
}

int bg_MonsterWindupSfx(int doomednum)
{
  int i;

  for (i = 0; i < num_windups; ++i)
    if (windups[i].doomednum == doomednum)
      return windups[i].sfx;
  return 0;
}

void bg_MonsterWindup(mobj_t* actor)
{
  mobj_t* target = actor->target;
  int sfx;

  if (!target || !target->player || target->health <= 0 || actor->info->doomednum <= 0)
    return;

  sfx = bg_MonsterWindupSfx(actor->info->doomednum);
  if (!sfx)
    return;

  // only the player being aimed at hears it: in a network game, only the machine that owns them
  if (bgnet_InGame() && !bgnet_IsMineSlot((int) (target->player - players)))
    return;

  if (getenv("BEGOTTEN_WINDUPLOG"))
    lprintf(LO_INFO, "WINDUP: type %d plays sfx %d\n", actor->info->doomednum, sfx);
  S_StartMobjSound(actor, sfx);
}

void dsda_BgMonsterSelfTest(void)
{
  int fails = 0;
  int sg = bg_MonsterWindupSfx(9);
  int none = bg_MonsterWindupSfx(2018);

#define T(c, m) do { if (c) lprintf(LO_INFO, "ok:   monster: %s\n", m); else { ++fails; lprintf(LO_INFO, "FAIL: monster: %s\n", m); } } while (0)
  T(sg > 0 && sg < num_sfx, "the shotgun guy has a wind-up sound");
  T(S_sfx[sg].name && !strcasecmp(S_sfx[sg].name, "dssgcock"), "it is the shotgun cock placeholder");
  T(none == 0, "an unlisted thing has no wind-up sound");
  bg_ParseMonsterText("9 -\n99999 DSTINK\n", -2);
  T(bg_MonsterWindupSfx(9) == 0, "\"-\" removes an entry");
  T(bg_MonsterWindupSfx(99999) > 0, "a later line adds an entry");
  bg_ParseMonsterText("9 DSSGCOCK\n", -2);
  T(bg_MonsterWindupSfx(9) == sg, "an entry can be restored");
#undef T
  lprintf(LO_INFO, "BEGOTTEN SELFTEST (monsters): %s (%d failures)\n", fails ? "FAILED" : "PASSED", fails);
}
