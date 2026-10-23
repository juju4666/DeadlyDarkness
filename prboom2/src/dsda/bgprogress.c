//
// Begotten: campaign progress and unlockable classes.
// Licensed under the GNU GPL v2 or later, like the rest of the engine.
//

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _MSC_VER
#include <strings.h>
#endif
#include "doomtype.h"

#include "doomstat.h"
#include "d_player.h"
#include "g_game.h"
#include "lprintf.h"
#include "umapinfo.h"
#include "w_wad.h"
#include "z_zone.h"

#include "dsda/data_organizer.h"
#include "dsda/episode.h"
#include "dsda/bgprogress.h"

#define NAMELEN 9

static char completed[BG_PROGRESS_MAX_MAPS][NAMELEN];
static int num_completed;
static char profile_path[1024];
static char override_path[1024];

static char required[BG_PROGRESS_MAX_MAPS][NAMELEN];
static int num_required;
static dboolean required_valid;

static void bg_Upper(char* dst, const char* src)
{
  int i;

  for (i = 0; i < NAMELEN - 1 && src[i]; ++i)
    dst[i] = (char) toupper((unsigned char) src[i]);
  dst[i] = '\0';
}

static const char* bg_ProfilePath(void)
{
  if (override_path[0])
    return override_path;
  if (!profile_path[0])
  {
    const char* root = dsda_DataRoot();

    snprintf(profile_path, sizeof profile_path, "%s%sbegotten_profile.txt", root ? root : "",
             (root && *root && root[strlen(root) - 1] != '/' && root[strlen(root) - 1] != '\\') ? "/" : "");
  }
  return profile_path;
}

//
// the profile file
//

static dboolean bg_ListHas(char list[][NAMELEN], int n, const char* name)
{
  int i;

  for (i = 0; i < n; ++i)
    if (!strcmp(list[i], name))
      return true;
  return false;
}

static void bg_LoadProfile(void)
{
  FILE* f = fopen(bg_ProfilePath(), "r");
  char line[128];

  num_completed = 0;
  if (!f)
    return;
  while (fgets(line, sizeof line, f))
  {
    char word[32], name[32];

    if (sscanf(line, "%31s %31s", word, name) == 2 && !strcasecmp(word, "completed") &&
        num_completed < BG_PROGRESS_MAX_MAPS)
    {
      char up[NAMELEN];

      bg_Upper(up, name);
      if (!bg_ListHas(completed, num_completed, up))
        strcpy(completed[num_completed++], up);
    }
  }
  fclose(f);
}

static void bg_SaveProfile(void)
{
  FILE* f = fopen(bg_ProfilePath(), "w");
  int i;

  if (!f)
  {
    lprintf(LO_WARN, "Begotten: cannot write %s\n", bg_ProfilePath());
    return;
  }
  fprintf(f, "# Begotten profile: maps finished so far (single player and co-op, any difficulty)\n");
  for (i = 0; i < num_completed; ++i)
    fprintf(f, "completed %s\n", completed[i]);
  fclose(f);
}

//
// the required maps
//

static const struct MapEntry* bg_FindEntry(const char* lump)
{
  unsigned int i;

  for (i = 0; i < Maps.mapcount; ++i)
    if (Maps.maps[i].lumpname && !strcasecmp(Maps.maps[i].lumpname, lump))
      return &Maps.maps[i];
  return NULL;
}

// the map reached by a normal exit from `lump`; "" if none
static void bg_NextMap(const char* lump, char* out)
{
  const struct MapEntry* e = bg_FindEntry(lump);
  int num;

  out[0] = '\0';
  if (e && e->nextmap[0])
  {
    bg_Upper(out, e->nextmap);
    return;
  }
  // no UMAPINFO entry: Doom II order, map 30 ends the game
  if (!strncasecmp(lump, "MAP", 3) && isdigit((unsigned char) lump[3]) && (num = atoi(lump + 3)) < 30)
    snprintf(out, NAMELEN, "MAP%02d", num + 1);
}

static dboolean bg_IsEpisodeStart(const char* lump)
{
  size_t i;

  for (i = 0; i < num_episodes; ++i)
    if (episodes[i].map_lump && !strcasecmp(episodes[i].map_lump, lump))
      return true;
  return false;
}

static void bg_AddRequired(const char* name)
{
  char up[NAMELEN];

  bg_Upper(up, name);
  if (num_required < BG_PROGRESS_MAX_MAPS && !bg_ListHas(required, num_required, up))
    strcpy(required[num_required++], up);
}

static dboolean bg_ReadProgressLump(void)
{
  int lump = W_CheckNumForName("BGPROGRESS");
  const char* data;
  char* copy;
  char* tok;
  int len, found = 0;

  if (lump < 0)
    return false;
  len = W_LumpLength(lump);
  copy = malloc(len + 1);
  data = W_LumpByNum(lump);
  memcpy(copy, data, len);
  copy[len] = '\0';

  // one map lump name per word; # and // start comments; the word "maps" is ignored
  {
    char* line = copy;

    while (line && *line)
    {
      char* eol = strpbrk(line, "\r\n");
      char* hash;

      if (eol)
        *eol = '\0';
      if ((hash = strchr(line, '#')))
        *hash = '\0';
      if ((hash = strstr(line, "//")))
        *hash = '\0';
      for (tok = strtok(line, " \t,="); tok; tok = strtok(NULL, " \t,="))
      {
        if (!strcasecmp(tok, "maps") || !strcasecmp(tok, "require") || !strcasecmp(tok, "required"))
          continue;
        bg_AddRequired(tok);
        ++found;
      }
      line = eol ? eol + 1 : NULL;
      while (line && (*line == '\r' || *line == '\n'))
        ++line;
    }
  }
  free(copy);
  return found > 0;
}

static void bg_ComputeRequired(void)
{
  size_t e;

  num_required = 0;
  required_valid = true;

  if (bg_ReadProgressLump())
    return;

  // derived: follow the normal exits from the first map of every episode
  for (e = 0; e < (num_episodes ? num_episodes : 1); ++e)
  {
    char cur[NAMELEN];
    int guard;

    if (num_episodes && episodes[e].map_lump)
      bg_Upper(cur, episodes[e].map_lump);
    else
      strcpy(cur, "MAP01");

    for (guard = 0; guard < BG_PROGRESS_MAX_MAPS; ++guard)
    {
      char next[NAMELEN];

      if (!W_LumpNameExists(cur))
        break;
      bg_AddRequired(cur);
      bg_NextMap(cur, next);
      if (!next[0] || !strcmp(next, cur) || bg_IsEpisodeStart(next) || bg_ListHas(required, num_required, next))
        break;
      strcpy(cur, next);
    }
  }
}

static void bg_EnsureRequired(void)
{
  if (!required_valid)
    bg_ComputeRequired();
}

//
// public interface
//

void dsda_InitBgProgress(void)
{
  required_valid = false;
  bg_LoadProfile();
}

void bg_ProgressSetPath(const char* path)
{
  snprintf(override_path, sizeof override_path, "%s", path ? path : "");
  required_valid = false;
}

void bg_ProgressReset(void)
{
  num_completed = 0;
  required_valid = false;
}

dboolean bg_MapIsRequired(const char* lump)
{
  char up[NAMELEN];

  bg_EnsureRequired();
  bg_Upper(up, lump);
  return bg_ListHas(required, num_required, up);
}

dboolean bg_MapCompleted(const char* lump)
{
  char up[NAMELEN];

  bg_Upper(up, lump);
  return bg_ListHas(completed, num_completed, up);
}

void bg_RecordMapCompleted(const char* lump)
{
  char up[NAMELEN];
  player_t* p = &players[consoleplayer];

  if (!lump || demoplayback || timingdemo)
    return;
  // warping and cheating do not count
  if (p->cheats & (CF_GODMODE | CF_NOCLIP | CF_NOTARGET))
    return;

  bg_Upper(up, lump);
  if (bg_ListHas(completed, num_completed, up) || num_completed >= BG_PROGRESS_MAX_MAPS)
    return;
  strcpy(completed[num_completed++], up);
  bg_SaveProfile();
}

void bg_CampaignCounts(int* done, int* total)
{
  int i, d = 0;

  bg_EnsureRequired();
  for (i = 0; i < num_required; ++i)
    if (bg_ListHas(completed, num_completed, required[i]))
      ++d;
  if (done) *done = d;
  if (total) *total = num_required;
}

dboolean bg_CampaignComplete(void)
{
  int done, total;

  bg_CampaignCounts(&done, &total);
  return total > 0 && done >= total;
}

//
// self test
//

static int progress_failures;

#define PCHECK(cond, what) do { \
    if (cond) lprintf(LO_INFO, "ok:   %s\n", what); \
    else { lprintf(LO_INFO, "FAIL: %s\n", what); ++progress_failures; } } while (0)

void dsda_BgProgressSelfTest(void)
{
  char path[256];
  int done, total, i;
  FILE* f;
  player_t* p = &players[consoleplayer];
  int cheats0 = p->cheats;

  progress_failures = 0;
  snprintf(path, sizeof path, "/tmp/begotten_profile_test_%d.txt", (int) (size_t) &progress_failures % 100000);
  remove(path);
  bg_ProgressSetPath(path);
  bg_ProgressReset();
  p->cheats &= ~(CF_GODMODE | CF_NOCLIP | CF_NOTARGET);

  bg_CampaignCounts(&done, &total);
  PCHECK(total > 7, "progress: required maps are derived from the episodes");
  PCHECK(bg_MapIsRequired("MAP01") && bg_MapIsRequired("map07") && bg_MapIsRequired("MAP09"),
         "progress: normal-exit chain of the episodes is required");
  PCHECK(!bg_MapIsRequired("MAP08"), "progress: the secret map is not required");
  PCHECK(!bg_MapIsRequired("MAP31") && !bg_MapIsRequired("MAP32"), "progress: Doom II secret maps are not required");
  PCHECK(!bg_CampaignComplete() && done == 0, "progress: nothing finished at the start");

  bg_RecordMapCompleted("MAP01");
  bg_RecordMapCompleted("map01");
  bg_CampaignCounts(&done, NULL);
  PCHECK(done == 1, "progress: finishing a map counts once");

  p->cheats |= CF_GODMODE;
  bg_RecordMapCompleted("MAP02");
  p->cheats &= ~CF_GODMODE;
  bg_CampaignCounts(&done, NULL);
  PCHECK(done == 1 && !bg_MapCompleted("MAP02"), "progress: a map finished with cheats on does not count");

  // it is stored: forget everything in memory and load the file again
  bg_ProgressReset();
  bg_ProgressSetPath(path);
  dsda_InitBgProgress();
  PCHECK(bg_MapCompleted("MAP01"), "progress: the profile file survives a restart");

  // finishing the required maps (the secret map does not matter) completes the campaign
  bg_RecordMapCompleted("MAP08");
  PCHECK(!bg_CampaignComplete(), "progress: the secret map alone does nothing");
  bg_CampaignCounts(&done, &total);
  for (i = 0; i < 40; ++i)
  {
    char name[16];

    snprintf(name, sizeof name, "MAP%02d", i);
    if (bg_MapIsRequired(name))
      bg_RecordMapCompleted(name);
  }
  PCHECK(bg_CampaignComplete(), "progress: every required map finished completes the campaign");

  f = fopen(path, "r");
  PCHECK(f != NULL, "progress: the profile file exists");
  if (f) fclose(f);
  remove(path);
  bg_ProgressSetPath(NULL);
  bg_ProgressReset();
  p->cheats = cheats0;

  lprintf(LO_INFO, "BEGOTTEN SELFTEST (progress): %s (%d failures)\n",
          progress_failures ? "FAILED" : "PASSED", progress_failures);
  exit(progress_failures ? 1 : 0);
}

//
// map list for menus
//

int bg_ListMapCount(void)
{
  return Maps.mapcount < BG_PROGRESS_MAX_MAPS ? (int) Maps.mapcount : BG_PROGRESS_MAX_MAPS;
}

const char* bg_ListMapName(int index)
{
  if (index < 0 || index >= bg_ListMapCount() || !Maps.maps[index].lumpname)
    return "";
  return Maps.maps[index].lumpname;
}

const char* bg_ListMapTitle(int index)
{
  if (index < 0 || index >= bg_ListMapCount())
    return "";
  if (Maps.maps[index].levelname)
    return Maps.maps[index].levelname;
  return Maps.maps[index].label ? Maps.maps[index].label : "";
}

dboolean bg_ListMapCompleted(int index)
{
  return bg_ListMapName(index)[0] && bg_MapCompleted(bg_ListMapName(index));
}
