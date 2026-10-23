//
// Begotten: player classes (data driven).
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
#include "d_items.h"
#include "doomdef.h"
#include "lprintf.h"
#include "p_inter.h"
#include "p_mobj.h"
#include "p_pspr.h"
#include "r_state.h"
#include "w_wad.h"
#include "dsda/sprite.h"
#include "z_zone.h"

#include "dsda/begotten.h"
#include "dsda/bgclass.h"
#include "dsda/bgroster.h"
#include "dsda/bgmonster.h"
#include "sounds.h"
#include "dsda/bgprogress.h"
#include "dsda/configuration.h"
#include "dsda/damage_kind.h"

#define PCT_UNSET (-1)

// body poses (BGCLASS "pose"): the sprite sets other players see
enum { POSE_STAND, POSE_RUN, POSE_SHOOT, POSE_JUMP, POSE_PARRY, POSE_CHARGE, POSE_CHARGERUN, POSE_CHARGEJUMP, POSE_STAB, POSE_STABAIR,
       NUM_POSES };
static const char* pose_words[NUM_POSES] = { "stand", "run", "shoot", "jump", "parry", "charge", "chargerun", "chargejump", "stab", "stabair" };
#define BG_MAX_POSES 96

typedef struct {
  int pose;
  int weapon;       // -1 = whatever weapon is in hand
  char sprite[5];
  int frames;       // frames in the sequence (jump: A rising, B peak, C falling)
  int tics;         // tics per frame
  int sprnum;       // resolved sprite number, -1 = none
} bg_pose_t;

typedef struct {
  char word[24];
  char name[32];
  int health;
  int armor;
  int speed;                              // percent
  dboolean weapon_given[NUMWEAPONS];
  int ammo[NUMAMMO];                      // starting ammo
  int maxammo[NUMAMMO];                   // percent of the normal maximum
  int damage_kind[DK_NUMKINDS];           // percent, PCT_UNSET = use the category
  int damage_category[DC_NUMCATEGORIES];  // percent
  int rate_category[DC_NUMCATEGORIES];    // fire rate, percent
  int swap_category[DC_NUMCATEGORIES];    // weapon raise / lower speed, percent
  int spread_category[DC_NUMCATEGORIES];  // spread, percent
  int maxhealth[4];                       // BG_HP_*, PCT_UNSET = engine default
  dboolean locked;                        // "unlock campaign": hidden until the whole campaign is finished
  dboolean meleeonly;                     // only the knife/blade: no guns, no ammo
  int blade_min, blade_max;               // blade damage range, 0 = stock punch
  int charge_tics, charge_min, charge_max; // held attack: tics to full charge, damage at a split-second hold / full charge (0 = no held attack)
  int lunge_min, lunge_max;               // stab propulsion in map units per tic at the weakest / full charge
  int riposte;                            // damage of the riposte on a stunned monster, 0 = none
  int aircontrol;                         // percent of ground acceleration available while airborne
  int foeaim;                             // percent of the normal hitscan spread monsters have against this class
  int parry;                              // parry window in tics, 0 = class cannot parry
  char face[4];                           // status bar face lump prefix, "" = stock faces
  int jump;                               // apex height of a jump in map units, 0 = the class cannot jump
  fixed_t jump_momz;                      // the upward speed that gives that apex (computed)
  int sound[BG_NUM_CLASS_SOUNDS];         // sfx ids replacing the stock player sounds, 0 = stock
  int num_poses;
  bg_pose_t pose[BG_MAX_POSES];
  int num_sprite_maps;
  char sprite_from[BG_MAX_SPRITE_MAPS][5];
  char sprite_to[BG_MAX_SPRITE_MAPS][5];
  int* sprite_map;                        // per original sprite number: replacement or -1
} bg_class_t;

static const char* bg_class_sound_words[BG_NUM_CLASS_SOUNDS] = { "pain", "death", "xdeath", "oof", "noway", "jump" };

static bg_class_t classes[BG_MAX_CLASSES];
static int num_classes;
int bg_player_class[MAX_MAXPLAYERS];

// Built-in defaults; identical to data/lumps/bgclass.lmp.
static const char bg_class_default_text[] =
  "class soldier \"Soldier\"\n"
  "  health 100\n"
  "  armor 0\n"
  "  speed 100\n"
  "  jump 28\n"
  "  aircontrol 15\n"
  "  maxhealth 100\n"
  "  maxbonus 200\n"
  "  maxsoul 200\n"
  "  megahealth 200\n"
  "  weapons fist pistol\n"
  "  ammo bullets 50\n"
  "  damage firearm 115\n"
  "  damage magic 85\n"
  "  firerate firearm 110\n"
  "  firerate magic 90\n"
  "  spread firearm 85\n"
  "  spread magic 115\n"
  "  swapspeed firearm 150\n"
  "  swapspeed magic 75\n"
  "class witch \"Witch\"\n"
  "  health 90\n"
  "  armor 0\n"
  "  speed 104\n"
  "  jump 37\n"
  "  aircontrol 30\n"
  "  maxhealth 90\n"
  "  maxbonus 180\n"
  "  maxsoul 180\n"
  "  megahealth 180\n"
  "  weapons fist pistol plasma\n"
  "  ammo bullets 40\n"
  "  ammo fire 20\n"
  "  maxammo tempest 125\n"
  "  maxammo fire 125\n"
  "  maxammo spirit 125\n"
  "  damage firearm 85\n"
  "  damage magic 125\n"
  "  firerate firearm 90\n"
  "  firerate magic 110\n"
  "  spread firearm 120\n"
  "  spread magic 85\n"
  "  swapspeed magic 150\n"
  "  swapspeed firearm 75\n"
  "class swordsman \"Swordsman\"\n"
  "  health 120\n"
  "  armor 0\n"
  "  speed 100\n"
  "  jump 56\n"
  "  aircontrol 80\n"
  "  maxhealth 120\n"
  "  maxbonus 220\n"
  "  maxsoul 220\n"
  "  megahealth 220\n"
  "  parry 7\n"
  "  foeaim 0\n"
  "  unlock campaign\n"
  "  meleeonly\n"
  "  blade 45 55\n"
  "  charge 70 1 100\n"
  "  lunge 10 24\n"
  "  riposte 200\n"
  "  weapons fist\n";

static const char* category_words[DC_NUMCATEGORIES] = { "melee", "firearm", "magic", "special" };
static const char* kind_words[DK_NUMKINDS] = {
  "blade", "bullet", "shell", "belt", "fire", "tempest", "spirit", "hellfire"
};

// The upward speed (fixed point) that makes a jump rise `height` map units with the engine's
// normal gravity (1 unit per tic per tic): z += momz, then momz -= gravity, every tic.
// Integer arithmetic only, so every machine gets the same number.
fixed_t bg_JumpSpeedFor(int height)
{
  long long target = (long long) height * FRACUNIT;
  fixed_t lo = 0, hi = 40 * FRACUNIT;

  if (height <= 0)
    return 0;
  while (hi - lo > 1)
  {
    fixed_t mid = lo + (hi - lo) / 2;
    long long z = 0, apex = 0;
    fixed_t m = mid;

    while (m > 0)
    {
      z += m;
      if (z > apex)
        apex = z;
      m -= FRACUNIT;
    }
    if (apex >= target)
      hi = mid;
    else
      lo = mid;
  }
  return hi;
}

static void bg_ClassDefaults(bg_class_t* c)
{
  int i;

  free(c->sprite_map);
  memset(c, 0, sizeof(*c));
  c->health = 100;
  c->speed = 100;
  c->foeaim = 100;
  for (i = 0; i < NUMAMMO; ++i)
    c->maxammo[i] = 100;
  for (i = 0; i < DK_NUMKINDS; ++i)
    c->damage_kind[i] = PCT_UNSET;
  for (i = 0; i < DC_NUMCATEGORIES; ++i)
    c->damage_category[i] = c->rate_category[i] = c->spread_category[i] = c->swap_category[i] = 100;
  for (i = 0; i < 4; ++i)
    c->maxhealth[i] = PCT_UNSET;
}

// "firearm" -> category 1, "bullet" -> kind 1.  Returns 0 if neither.
static int bg_FindKindOrCategory(const char* word, int* index, int* is_category)
{
  int i;

  for (i = 0; i < DC_NUMCATEGORIES; ++i)
    if (!strcasecmp(word, category_words[i]))
    {
      *index = i;
      *is_category = 1;
      return 1;
    }
  for (i = 0; i < DK_NUMKINDS; ++i)
    if (!strcasecmp(word, kind_words[i]))
    {
      *index = i;
      *is_category = 0;
      return 1;
    }
  return 0;
}

static bg_class_t* current_class; // the class the lines being read belong to

static void bg_ParseClassLine(char* line, int lumpnum, int lineno)
{
  char* tok[16];
  char* hash;
  char* p;
  int count = 0;
  bg_class_t* c = current_class;

  if ((hash = strchr(line, '#')))
    *hash = '\0';
  if ((hash = strstr(line, "//")))
    *hash = '\0';

  // quoted display names are one token
  for (p = line; *p && count < 16;)
  {
    while (*p && (isspace((unsigned char) *p) || *p == '=' || *p == ','))
      ++p;
    if (!*p)
      break;
    if (*p == '"')
    {
      tok[count++] = ++p;
      while (*p && *p != '"')
        ++p;
      if (*p)
        *p++ = '\0';
    }
    else
    {
      tok[count++] = p;
      while (*p && !isspace((unsigned char) *p) && *p != '=' && *p != ',')
        ++p;
      if (*p)
        *p++ = '\0';
    }
  }

  if (!count)
    return;

  if (!strcasecmp(tok[0], "edit") && count >= 2)
  {
    // reopen an existing class without resetting it: the lines that follow add to or change it
    int i;

    for (i = 0; i < num_classes; ++i)
      if (!strcasecmp(tok[1], classes[i].word))
      {
        current_class = &classes[i];
        return;
      }
    goto bad;
  }
  if (!strcasecmp(tok[0], "class") && count >= 2)
  {
    int i;

    // redefining a class replaces its numbers in place
    for (i = 0; i < num_classes; ++i)
      if (!strcasecmp(classes[i].word, tok[1]))
        break;
    if (i == num_classes)
    {
      if (num_classes >= BG_MAX_CLASSES)
        goto bad;
      ++num_classes;
    }
    c = current_class = &classes[i];
    bg_ClassDefaults(c);
    strncpy(c->word, tok[1], sizeof(c->word) - 1);
    strncpy(c->name, count >= 3 ? tok[2] : tok[1], sizeof(c->name) - 1);
    return;
  }

  if (!c)
    goto bad;

  if (!strcasecmp(tok[0], "name") && count >= 2)
  {
    strncpy(c->name, tok[1], sizeof(c->name) - 1);
    return;
  }
  if (!strcasecmp(tok[0], "health") && count >= 2) { c->health = atoi(tok[1]); return; }
  if (!strcasecmp(tok[0], "armor") && count >= 2) { c->armor = atoi(tok[1]); return; }
  if (!strcasecmp(tok[0], "speed") && count >= 2) { c->speed = atoi(tok[1]); return; }
  if (!strcasecmp(tok[0], "parry") && count >= 2) { c->parry = atoi(tok[1]); return; }
  if (!strcasecmp(tok[0], "foeaim") && count >= 2) { c->foeaim = atoi(tok[1]); if (c->foeaim < 0) c->foeaim = 0; return; }
  if (!strcasecmp(tok[0], "unlock") && count >= 2) { c->locked = !strcasecmp(tok[1], "campaign"); return; }
  if (!strcasecmp(tok[0], "meleeonly")) { c->meleeonly = count < 2 || atoi(tok[1]) != 0; return; }
  if (!strcasecmp(tok[0], "blade") && count >= 3)
  {
    c->blade_min = atoi(tok[1]);
    c->blade_max = atoi(tok[2]);
    if (c->blade_max < c->blade_min)
      c->blade_max = c->blade_min;
    return;
  }
  if (!strcasecmp(tok[0], "charge") && count >= 4)
  {
    c->charge_tics = atoi(tok[1]);
    c->charge_min = atoi(tok[2]);
    c->charge_max = atoi(tok[3]);
    if (c->charge_tics < 0) c->charge_tics = 0;
    if (c->charge_max < c->charge_min) c->charge_max = c->charge_min;
    return;
  }
  if (!strcasecmp(tok[0], "lunge") && count >= 3)
  {
    c->lunge_min = atoi(tok[1]);
    c->lunge_max = atoi(tok[2]);
    if (c->lunge_max < c->lunge_min) c->lunge_max = c->lunge_min;
    return;
  }
  if (!strcasecmp(tok[0], "riposte") && count >= 2) { c->riposte = atoi(tok[1]); if (c->riposte < 0) c->riposte = 0; return; }
  if (!strcasecmp(tok[0], "aircontrol") && count >= 2) { c->aircontrol = atoi(tok[1]); if (c->aircontrol < 0) c->aircontrol = 0; return; }
  if (!strcasecmp(tok[0], "maxhealth") && count >= 2) { c->maxhealth[BG_HP_NORMAL] = atoi(tok[1]); return; }
  if (!strcasecmp(tok[0], "maxbonus") && count >= 2) { c->maxhealth[BG_HP_BONUS] = atoi(tok[1]); return; }
  if (!strcasecmp(tok[0], "maxsoul") && count >= 2) { c->maxhealth[BG_HP_SOUL] = atoi(tok[1]); return; }
  if (!strcasecmp(tok[0], "megahealth") && count >= 2) { c->maxhealth[BG_HP_MEGA] = atoi(tok[1]); return; }

  if (!strcasecmp(tok[0], "pose") && count >= 3)
  {
    // pose <stand|run|shoot|jump|parry> <SPRITE|-> [frames [tics]] [weapon <name>]
    int pose, i, nums = 0, weapon = -1, frames = 1, tics = 4, slot = -1;

    for (pose = 0; pose < NUM_POSES; ++pose)
      if (!strcasecmp(tok[1], pose_words[pose]))
        break;
    if (pose == NUM_POSES)
      goto bad;
    for (i = 3; i < count; ++i)
    {
      if (!strcasecmp(tok[i], "weapon") && i + 1 < count)
      {
        weapon = bg_WeaponFromWord(tok[i + 1]);
        if (weapon < 0)
          goto bad;
        ++i;
      }
      else if (isdigit((unsigned char) tok[i][0]) && nums < 2)
      {
        if (nums++ == 0)
          frames = atoi(tok[i]);
        else
          tics = atoi(tok[i]);
      }
      else
        goto bad;
    }
    if (frames < 1 || frames > 26 || tics < 1 || tics > 70)
      goto bad;
    for (i = 0; i < c->num_poses; ++i)
      if (c->pose[i].pose == pose && c->pose[i].weapon == weapon)
        slot = i;
    if (!strcmp(tok[2], "-"))
    {
      if (slot >= 0)
        c->pose[slot] = c->pose[--c->num_poses];   // remove it
      return;
    }
    if (strlen(tok[2]) != 4)
      goto bad;
    if (slot < 0)
    {
      if (c->num_poses >= BG_MAX_POSES)
        goto bad;
      slot = c->num_poses++;
    }
    c->pose[slot].pose = pose;
    c->pose[slot].weapon = weapon;
    strncpy(c->pose[slot].sprite, tok[2], 4);
    c->pose[slot].sprite[4] = '\0';
    c->pose[slot].frames = frames;
    c->pose[slot].tics = tics;
    c->pose[slot].sprnum = -1;
    return;
  }
  if (!strcasecmp(tok[0], "jump") && count >= 2)
  {
    int h = atoi(tok[1]);

    if (h < 0 || h > 512)
      goto bad;
    c->jump = h;
    c->jump_momz = bg_JumpSpeedFor(h);
    return;
  }
  if (!strcasecmp(tok[0], "sound") && count >= 3)
  {
    int slot, id;

    for (slot = 0; slot < BG_NUM_CLASS_SOUNDS; ++slot)
      if (!strcasecmp(tok[1], bg_class_sound_words[slot]))
        break;
    if (slot == BG_NUM_CLASS_SOUNDS)
      goto bad;
    if (!strcmp(tok[2], "-") || !strcasecmp(tok[2], "none"))
    {
      c->sound[slot] = 0;
      return;
    }
    id = bg_SfxFromLump(tok[2]);
    if (!id)
    {
      lprintf(LO_WARN, "BGCLASS (lump %d, line %d): class %s: there is no sound \"%s\"\n", lumpnum, lineno, c->word,
              tok[2]);
      return;
    }
    c->sound[slot] = id;
    return;
  }
  if (!strcasecmp(tok[0], "face") && count >= 2)
  {
    if (strlen(tok[1]) != 3)
      goto bad;
    strncpy(c->face, tok[1], 3);
    c->face[3] = '\0';
    return;
  }
  if (!strcasecmp(tok[0], "sprite") && count >= 3)
  {
    int i;

    if (strlen(tok[1]) != 4 || strlen(tok[2]) != 4)
      goto bad;
    for (i = 0; i < c->num_sprite_maps; ++i)
      if (!strncasecmp(c->sprite_from[i], tok[1], 4))
        break;
    if (i == BG_MAX_SPRITE_MAPS)
      goto bad;
    if (i == c->num_sprite_maps)
      ++c->num_sprite_maps;
    strncpy(c->sprite_from[i], tok[1], 4);
    strncpy(c->sprite_to[i], tok[2], 4);
    c->sprite_from[i][4] = c->sprite_to[i][4] = '\0';
    return;
  }

  if (!strcasecmp(tok[0], "weapons"))
  {
    int i;

    memset(c->weapon_given, 0, sizeof(c->weapon_given));
    for (i = 1; i < count; ++i)
    {
      int w = bg_WeaponFromWord(tok[i]);

      if (w < 0)
        goto bad;
      c->weapon_given[w] = true;
    }
    return;
  }
  if (!strcasecmp(tok[0], "ammo") && count >= 3)
  {
    int a = bg_AmmoFromWord(tok[1]);

    if (a < 0 || a >= NUMAMMO)
      goto bad;
    c->ammo[a] = atoi(tok[2]);
    return;
  }
  if (!strcasecmp(tok[0], "maxammo") && count >= 3)
  {
    int a = bg_AmmoFromWord(tok[1]);

    if (a < 0 || a >= NUMAMMO)
      goto bad;
    c->maxammo[a] = atoi(tok[2]);
    return;
  }
  if (count >= 3 && !strcasecmp(tok[0], "swapspeed"))
  {
    int index, is_category;

    if (!bg_FindKindOrCategory(tok[1], &index, &is_category) || !is_category)
      goto bad; // the weapon change speed is per category
    c->swap_category[index] = atoi(tok[2]);
    return;
  }
  if (count >= 3 && (!strcasecmp(tok[0], "damage") || !strcasecmp(tok[0], "firerate") ||
                     !strcasecmp(tok[0], "spread")))
  {
    int index, is_category;
    int pct = atoi(tok[2]);

    if (!bg_FindKindOrCategory(tok[1], &index, &is_category))
      goto bad;
    if (!strcasecmp(tok[0], "damage"))
    {
      if (is_category)
        c->damage_category[index] = pct;
      else
        c->damage_kind[index] = pct;
      return;
    }
    if (!is_category)
      goto bad; // fire rate and spread are per category
    if (!strcasecmp(tok[0], "firerate"))
      c->rate_category[index] = pct;
    else
      c->spread_category[index] = pct;
    return;
  }

bad:
  lprintf(LO_WARN, "BGCLASS (lump %d \"%s\", line %d): cannot understand \"%s\"\n", lumpnum,
          lumpnum >= 0 ? lumpinfo[lumpnum].name : "built-in", lineno, tok[0]);
}

static void bg_ParseClassText(const char* source, int lumpnum)
{
  char* text = Z_Strdup(source);
  char* line;
  char* next;
  int lineno = 0;

  current_class = NULL; // every lump starts outside any class

  for (line = text; line; line = next)
  {
    next = strchr(line, '\n');
    if (next)
      *next++ = '\0';
    bg_ParseClassLine(line, lumpnum, ++lineno);
  }

  Z_Free(text);
}

// Registers the replacement sprite names and builds the per-class lookup tables.
static void bg_BuildSpriteMaps(void)
{
  int cls, i, orig;

  // names first: registering them grows the sprite table
  for (cls = 0; cls < num_classes; ++cls)
  {
    for (i = 0; i < classes[cls].num_sprite_maps; ++i)
      dsda_AddSpriteName(classes[cls].sprite_to[i]);
    for (i = 0; i < classes[cls].num_poses; ++i)
      dsda_AddSpriteName(classes[cls].pose[i].sprite);
  }

  for (cls = 0; cls < num_classes; ++cls)
    for (i = 0; i < classes[cls].num_poses; ++i)
    {
      bg_pose_t* ps = &classes[cls].pose[i];
      int to;

      ps->sprnum = -1;
      for (to = 0; to < num_sprites; ++to)
        if (sprnames[to] && !strncasecmp(sprnames[to], ps->sprite, 4))
        {
          ps->sprnum = to;
          break;
        }
    }

  for (cls = 0; cls < num_classes; ++cls)
  {
    bg_class_t* c = &classes[cls];

    c->sprite_map = NULL;
    if (!c->num_sprite_maps)
      continue;

    c->sprite_map = malloc(num_sprites * sizeof(*c->sprite_map));
    for (i = 0; i < num_sprites; ++i)
      c->sprite_map[i] = -1;

    for (i = 0; i < c->num_sprite_maps; ++i)
    {
      int to = -1;

      for (orig = 0; orig < num_sprites; ++orig)
        if (sprnames[orig] && !strncasecmp(sprnames[orig], c->sprite_from[i], 4))
          break;
      if (orig == num_sprites)
      {
        lprintf(LO_WARN, "BGCLASS: class %s: there is no sprite \"%s\" to replace\n", c->word,
                c->sprite_from[i]);
        continue;
      }
      for (to = 0; to < num_sprites; ++to)
        if (sprnames[to] && !strncasecmp(sprnames[to], c->sprite_to[i], 4))
          break;
      c->sprite_map[orig] = to < num_sprites ? to : -1;
    }
  }
}

void dsda_InitBgClasses(void)
{
  int i, pass;

  num_classes = 0;
  current_class = NULL;
  bg_ParseClassText(bg_class_default_text, -1);

  // The engine's own BGCLASS (in begotten-data.wad) is read first; then the BGCLASS lumps of the
  // game's wads in load order, so a wad's lump (also one inside begotten.wad itself) always wins.
  for (pass = 0; pass < 2; ++pass)
  for (i = 0; i < numlumps; ++i)
  {
    const char* data;
    int size, is_engine = 0;
    char* text;
    size_t nl, dl = strlen(WAD_DATA);

    if (strncasecmp(lumpinfo[i].name, "BGCLASS", 7))
      continue;

    nl = lumpinfo[i].wadfile && lumpinfo[i].wadfile->name ? strlen(lumpinfo[i].wadfile->name) : 0;
    is_engine = nl >= dl && !strcasecmp(lumpinfo[i].wadfile->name + nl - dl, WAD_DATA);
    if (is_engine != (pass == 0))
      continue;

    size = W_LumpLength(i);
    data = W_LumpByNum(i);
    text = Z_Malloc(size + 1);
    memcpy(text, data, size);
    text[size] = '\0';
    bg_ParseClassText(text, i);
    Z_Free(text);
  }

  bg_BuildSpriteMaps();
}

int bg_ClassCount(void)
{
  return num_classes;
}

const char* bg_ClassName(int cls)
{
  return cls >= 0 && cls < num_classes ? classes[cls].name : "?";
}

const char* bg_ClassWord(int cls)
{
  return cls >= 0 && cls < num_classes ? classes[cls].word : "?";
}

int bg_ClassFromWord(const char* word)
{
  int i;
  char* end;
  long n;

  for (i = 0; i < num_classes; ++i)
    if (!strcasecmp(word, classes[i].word))
      return i;

  n = strtol(word, &end, 10);
  if (*word && !*end && n >= 0 && n < num_classes)
    return (int) n;

  return -1;
}

static const bg_class_t* bg_PlayerClassData(const player_t* player)
{
  if (!player || !bg_weapons || player->bgclass < 0 || player->bgclass >= num_classes)
    return NULL;
  return &classes[player->bgclass];
}

dboolean bg_ClassAvailable(int cls)
{
  if (cls < 0 || cls >= num_classes)
    return false;
  if (!classes[cls].locked)
    return true;
  return dsda_IntConfig(dsda_config_begotten_dev_unlock) || bg_CampaignComplete();
}

void bg_ClassReborn(player_t* player)
{
  const bg_class_t* c;
  int i, best = wp_fist;

  if (!bg_weapons || !num_classes)
    return;

  player->bgclass = bg_player_class[player - players];
  // (in a network game the class was chosen by the machine of that player: availability is a property of his
  // profile, not of this machine's, and the machines must agree)
  if (player->bgclass < 0 || player->bgclass >= num_classes || (!netgame && !bg_ClassAvailable(player->bgclass)))
    player->bgclass = 0;
  c = &classes[player->bgclass];

  player->health = c->health;
  player->armorpoints[ARMOR_ARMOR] = c->armor;
  player->armortype = c->armor ? 1 : 0;

  for (i = 0; i < NUMWEAPONS; ++i)
    player->weaponowned[i] = c->weapon_given[i];
  for (i = 0; i < NUMAMMO; ++i)
  {
    player->maxammo[i] = (int) (((long long) maxammo[i] * c->maxammo[i]) / 100);
    player->ammo[i] = c->ammo[i] < player->maxammo[i] ? c->ammo[i] : player->maxammo[i];
  }

  // start with the best primary weapon of the lowest slot that has one
  for (i = 0; i < NUMWEAPONS; ++i)
    if (c->weapon_given[i] && bg_WeaponSlot((weapontype_t) i) >= bg_WeaponSlot((weapontype_t) best) &&
        (doom_weaponinfo[i].ammo == am_noammo ? i == wp_fist && best == wp_fist
                                              : player->ammo[doom_weaponinfo[i].ammo] > 0))
      best = i;
  player->readyweapon = player->pendingweapon = (weapontype_t) best;
}

int bg_ClassHealthCap(const player_t* player, bg_health_cap_t which, int fallback)
{
  const bg_class_t* c = bg_PlayerClassData(player);

  if (!c || c->maxhealth[which] == PCT_UNSET)
    return fallback;
  return c->maxhealth[which];
}

int bg_ClassSprite(int cls, int sprite, int frame)
{
  int to;

  if (cls < 0 || cls >= num_classes || !classes[cls].sprite_map || sprite < 0 ||
      sprite >= num_sprites)
    return sprite;
  to = classes[cls].sprite_map[sprite];
  if (to < 0 || !sprites || (frame & FF_FRAMEMASK) >= sprites[to].numframes ||
      !sprites[to].spriteframes)
    return sprite; // the class has no art for this frame: use the stock sprite
  return to;
}

// the front of frame A of the player sprite as this class draws it (for the menus)
int bg_ClassPortraitLump(int cls)
{
  int s = bg_ClassSprite(cls, SPR_PLAY, 0), l;

  if (!sprites || s < 0 || s >= num_sprites || !sprites[s].spriteframes || sprites[s].numframes < 1)
    return -1;
  l = sprites[s].spriteframes[0].lump[0];
  return l < 0 ? -1 : l + firstspritelump;
}

const char* bg_ClassFacePrefix(int cls)
{
  return cls >= 0 && cls < num_classes && classes[cls].face[0] ? classes[cls].face : NULL;
}

int bg_ClassDamagePct(const player_t* player, damage_kind_t kind)
{
  const bg_class_t* c = bg_PlayerClassData(player);
  damage_category_t cat;

  if (!c || kind == DK_NONE)
    return 100;
  if (c->damage_kind[kind] != PCT_UNSET)
    return c->damage_kind[kind];
  cat = dsda_KindCategory(kind);
  return c->damage_category[cat];
}

int bg_ClassSpreadPct(const player_t* player)
{
  const bg_class_t* c = bg_PlayerClassData(player);
  damage_kind_t kind;

  if (!c)
    return 100;
  kind = dsda_WeaponKind(player->readyweapon);
  if (kind == DK_NONE)
    return 100;
  return c->spread_category[dsda_KindCategory(kind)];
}

// Monsters' hitscan spread against the class of whoever they are shooting at (a vanilla angle
// offset goes in, the scaled one comes out). Without a player target nothing changes.
int bg_FoeSpread(const mobj_t* attacker, int offset)
{
  const bg_class_t* c;

  if (attacker && bg_weapons)
    offset = (int) (((long long) offset * bg_FoeSpreadPct(attacker)) / 100);
  if (!attacker || !attacker->target || !attacker->target->player)
    return offset;
  c = bg_PlayerClassData(attacker->target->player);
  if (!c || c->foeaim >= 100)
    return offset;
  return (int) (((long long) offset * c->foeaim) / 100);
}

dboolean bg_ClassMeleeOnly(const player_t* player)
{
  const bg_class_t* c = bg_PlayerClassData(player);

  return c && c->meleeonly;
}

// the blade's damage: stock punch damage unless the class defines a range
int bg_ClassBladeDamage(const player_t* player, int stock, int roll)
{
  const bg_class_t* c = bg_PlayerClassData(player);

  if (!c || c->blade_max <= 0)
    return stock;
  return c->blade_min + roll % (c->blade_max - c->blade_min + 1);
}

// held attack ("charge"): tics of a full charge, 0 = the class only slashes
int bg_ClassChargeTics(const player_t* player)
{
  const bg_class_t* c = bg_PlayerClassData(player);

  return c ? c->charge_tics : 0;
}

// damage of a stab released after `held` tics of charging (1 .. charge_tics, clamped)
int bg_ClassChargeDamage(const player_t* player, int held)
{
  const bg_class_t* c = bg_PlayerClassData(player);

  if (!c || c->charge_tics <= 0)
    return 0;
  if (held < 0) held = 0;
  if (held > c->charge_tics) held = c->charge_tics;
  return c->charge_min + (c->charge_max - c->charge_min) * held / c->charge_tics;
}

// stab propulsion in fixed point map units per tic
fixed_t bg_ClassLunge(const player_t* player, int held)
{
  const bg_class_t* c = bg_PlayerClassData(player);
  int v;

  if (!c || c->charge_tics <= 0)
    return 0;
  if (held < 0) held = 0;
  if (held > c->charge_tics) held = c->charge_tics;
  v = c->lunge_min * 16 + (c->lunge_max - c->lunge_min) * 16 * held / c->charge_tics;
  return v * (FRACUNIT / 16);
}

int bg_ClassRiposte(const player_t* player)
{
  const bg_class_t* c = bg_PlayerClassData(player);

  return c ? c->riposte : 0;
}

int bg_ClassAirControl(const player_t* player)
{
  const bg_class_t* c = bg_PlayerClassData(player);

  return c ? c->aircontrol : 0;
}

int bg_ClassParryWindow(const player_t* player)
{
  const bg_class_t* c = bg_PlayerClassData(player);

  return c && c->parry > 0 ? c->parry : 0;
}

int bg_ClassScaleMove(const player_t* player, int move)
{
  const bg_class_t* c = bg_PlayerClassData(player);

  if (!c || c->speed == 100)
    return move;
  return (int) (((long long) move * c->speed) / 100);
}

int bg_ClassScaleTics(const player_t* player, int tics)
{
  const bg_class_t* c = bg_PlayerClassData(player);
  damage_kind_t kind;
  int pct, result;

  if (!c || tics <= 1)
    return tics;
  kind = dsda_WeaponKind(player->readyweapon);
  if (kind == DK_NONE)
    return tics;
  pct = c->rate_category[dsda_KindCategory(kind)];
  if (pct <= 0 || pct == 100)
    return tics;
  result = (int) (((long long) tics * 100 + pct / 2) / pct); // faster rate = shorter states
  return result < 1 ? 1 : result;
}

// raising / lowering speed (fixed point units per tic) of the weapon being changed
fixed_t bg_ClassSwapSpeed(const player_t* player, fixed_t base, int weapon)
{
  const bg_class_t* c = bg_PlayerClassData(player);
  damage_kind_t kind;
  int pct;

  if (!c)
    return base;
  kind = dsda_WeaponKind(weapon);
  if (kind == DK_NONE)
    return base;
  pct = c->swap_category[dsda_KindCategory(kind)];
  if (pct <= 0 || pct == 100)
    return base;
  pct = (int) (((long long) base * pct) / 100);
  return pct < FRACUNIT ? FRACUNIT : pct;
}

dboolean bg_ClassAllowsThing(unsigned int options)
{
  static const unsigned int class_flags[3] = { MTF_FIGHTER, MTF_CLERIC, MTF_MAGE };
  unsigned int all = MTF_FIGHTER | MTF_CLERIC | MTF_MAGE;
  unsigned int mask = 0;
  int p;

  if (!bg_weapons || !(options & all))
    return true; // not class-flagged: for everybody

  if (!netgame)
  {
    int cls = bg_player_class[0];

    return cls >= 0 && cls < 3 ? (options & class_flags[cls]) != 0 : true;
  }

  // co-op: a thing is spawned if it is meant for anybody who is playing
  for (p = 0; p < MAX_MAXPLAYERS; ++p)
    if (playeringame[p] && bg_player_class[p] >= 0 && bg_player_class[p] < 3)
      mask |= class_flags[bg_player_class[p]];
  return mask ? (options & mask) != 0 : true;
}

// Player starts: a start flagged for some classes only is used by players of those classes.  A start that does not fit is
// kept as a fallback (bg_StashStart) and only used when no fitting start exists (bg_FlushStarts), so a map can never end up
// with no start for a class.
dboolean bg_ClassAllowsPlayerStart(int player, unsigned int options)
{
  static const unsigned int class_flags[3] = { MTF_FIGHTER, MTF_CLERIC, MTF_MAGE };
  unsigned int all = MTF_FIGHTER | MTF_CLERIC | MTF_MAGE;
  int cls;

  if (!bg_weapons || !(options & all) || player < 0 || player >= MAX_MAXPLAYERS)
    return true;
  cls = bg_player_class[player];
  return cls >= 0 && cls < 3 ? (options & class_flags[cls]) != 0 : true;
}

static mapthing_t bg_start_fallback[MAX_PLAYER_STARTS][MAX_MAXPLAYERS];

void bg_StashStart(const mapthing_t* mthing, int player)
{
  if (mthing->special_args[0] >= 0 && mthing->special_args[0] < MAX_PLAYER_STARTS && player >= 0 && player < MAX_MAXPLAYERS)
  {
    mapthing_t* f = &bg_start_fallback[mthing->special_args[0]][player];

    if (!f->options)                  // the first one stays
    {
      *f = *mthing;
      f->options = 1;
    }
  }
}

void bg_ClearStashedStarts(void)
{
  memset(bg_start_fallback, 0, sizeof(bg_start_fallback));
}

void bg_FlushStarts(void)
{
  int a, p;

  for (a = 0; a < MAX_PLAYER_STARTS; ++a)
    for (p = 0; p < MAX_MAXPLAYERS; ++p)
      if (bg_start_fallback[a][p].options && !playerstarts[a][p].options)
      {
        mapthing_t m = bg_start_fallback[a][p];

        P_TrySpawnPlayer(&m, p);
      }
  bg_ClearStashedStarts();
}

//
// Self test: BEGOTTEN_SELFTEST=3 runs it together with the weapon test.
//

static int bgclass_failures;

#define CCHECK(cond, what) \
  do { \
    if (!(cond)) { ++bgclass_failures; lprintf(LO_INFO, "FAIL: %s\n", what); } \
    else lprintf(LO_INFO, "ok:   %s\n", what); \
  } while (0)

void dsda_BgClassSelfTest(void)
{
  player_t p;
  int soldier = bg_ClassFromWord("soldier"), witch = bg_ClassFromWord("witch");

  bgclass_failures = 0;
  CCHECK(bg_ClassCount() >= 2, "at least two classes are defined");
  CCHECK(soldier == 0 && witch == 1, "soldier and witch are classes 0 and 1");
  CCHECK(bg_ClassFromWord("1") == 1 && bg_ClassFromWord("nobody") == -1, "class lookup by number / unknown");

  memset(&p, 0, sizeof(p));
  p.bgclass = soldier;
  CCHECK(bg_ClassDamagePct(&p, DK_BULLET) > 100 && bg_ClassDamagePct(&p, DK_TEMPEST) < 100,
         "soldier: firearms up, magic down");
  p.bgclass = witch;
  CCHECK(bg_ClassDamagePct(&p, DK_BULLET) < 100 && bg_ClassDamagePct(&p, DK_TEMPEST) > 100,
         "witch: firearms down, magic up");
  CCHECK(bg_ClassDamagePct(&p, DK_NONE) == 100, "no kind, no change");
  CCHECK(bg_ClassDamagePct(NULL, DK_BULLET) == 100, "no player, no change");
  CCHECK(bg_ClassScaleMove(&p, 50) == 55, "witch moves 10% faster (50 -> 55)");
  p.bgclass = witch;
  CCHECK(bg_ClassHealthCap(&p, BG_HP_NORMAL, 100) == 90, "witch: stimpak cap 90");
  CCHECK(bg_ClassHealthCap(&p, BG_HP_BONUS, 200) == 180, "witch: bonus cap 180");
  CCHECK(bg_ClassHealthCap(&p, BG_HP_SOUL, 200) == 180, "witch: soulsphere cap 180");
  CCHECK(bg_ClassHealthCap(&p, BG_HP_MEGA, 200) == 180, "witch: megasphere 180");
  {
    int sw = bg_ClassFromWord("swordsman"), roll, ge150 = 0;

    p.bgclass = sw;
    dsda_UpdateIntConfig(dsda_config_begotten_dev_unlock, 1, false);
    CCHECK(bg_ClassAvailable(sw) && bg_ClassAvailable(0), "dev unlock: every class is available");
    CCHECK(!bg_ClassAvailable(-1) && !bg_ClassAvailable(99), "class availability: bad index");
    CCHECK(bg_ClassMeleeOnly(&p), "swordsman: blade only");
    {
      mobj_t shooter, victim;
      player_t other;
      memset(&shooter, 0, sizeof shooter); memset(&victim, 0, sizeof victim); memset(&other, 0, sizeof other);
      victim.player = &p;
      shooter.target = &victim;
      CCHECK(bg_FoeSpread(&shooter, 5000000) == 0, "swordsman: monsters' hitscan aim is exact");
      p.bgclass = soldier;
      CCHECK(bg_FoeSpread(&shooter, 5000000) == 5000000, "soldier: monsters' aim is normal");
      p.bgclass = sw;
      shooter.target = NULL;
      CCHECK(bg_FoeSpread(&shooter, 5000000) == 5000000, "no player target: normal spread");
    }
    CCHECK(bg_ClassBladeDamage(&p, 10, 0) == 45 && bg_ClassBladeDamage(&p, 10, 10) == 55, "swordsman: slash 45-55");
    for (roll = 0; roll < 11; ++roll)
      ge150 += bg_ClassBladeDamage(&p, 10, roll) >= 45 && bg_ClassBladeDamage(&p, 10, roll) <= 55;
    CCHECK(ge150 == 11, "swordsman: every slash does 45-55");
    CCHECK(bg_ClassChargeTics(&p) == 70, "swordsman: two seconds to a full charge");
    CCHECK(bg_ClassChargeDamage(&p, 0) == 1 && bg_ClassChargeDamage(&p, 70) == 100 && bg_ClassChargeDamage(&p, 500) == 100,
           "swordsman: stab 1 .. 100 by charge, clamped");
    CCHECK(bg_ClassChargeDamage(&p, 35) > 40 && bg_ClassChargeDamage(&p, 35) < 60, "swordsman: half a charge is about half the damage");
    CCHECK(bg_ClassLunge(&p, 0) == 10 * FRACUNIT && bg_ClassLunge(&p, 70) == 24 * FRACUNIT, "swordsman: lunge 10 .. 24 units a tic");
    CCHECK(bg_ClassRiposte(&p) == 200, "swordsman: riposte 200");
    CCHECK(bg_ClassAirControl(&p) == 80, "swordsman: air control 80%");
    p.bgclass = soldier;
    CCHECK(!bg_ClassMeleeOnly(&p) && bg_ClassBladeDamage(&p, 14, 5) == 14, "soldier: stock punch, guns allowed");
  }
  p.bgclass = soldier;
  CCHECK(bg_ClassHealthCap(&p, BG_HP_NORMAL, 100) == 100 && bg_ClassHealthCap(&p, BG_HP_SOUL, 200) == 200, "soldier: stock caps");
  CCHECK(bg_ClassHealthCap(NULL, BG_HP_NORMAL, 77) == 77, "no player: fallback cap");
  CCHECK(bg_ClassSprite(witch, SPR_PLAY, 0) == SPR_PLAY, "class without art falls back to stock sprite");
  p.bgclass = soldier;
  CCHECK(bg_ClassScaleMove(&p, 50) == 45, "soldier moves 10% slower (50 -> 45)");
  p.readyweapon = wp_pistol;
  CCHECK(bg_ClassScaleTics(&p, 11) < 11, "soldier: firearms fire faster (shorter states)");
  CCHECK(bg_ClassScaleTics(&p, 1) == 1 && bg_ClassScaleTics(&p, 0) == 0, "short states are untouched");
  p.readyweapon = wp_tornado;
  CCHECK(bg_ClassScaleTics(&p, 11) > 11, "soldier: magic fires slower");
  CCHECK(bg_ClassSpreadPct(&p) > 100, "soldier: magic is less accurate");
  CCHECK(bg_ClassSwapSpeed(&p, 6 * FRACUNIT, wp_tornado) < 6 * FRACUNIT, "soldier: magic weapons are changed slowly");
  p.readyweapon = wp_pistol;
  CCHECK(bg_ClassSwapSpeed(&p, 6 * FRACUNIT, wp_pistol) > 6 * FRACUNIT, "soldier: firearms are changed quickly");
  p.bgclass = witch;
  CCHECK(bg_ClassSwapSpeed(&p, 6 * FRACUNIT, wp_pistol) < 6 * FRACUNIT, "witch: firearms are changed slowly");
  CCHECK(bg_ClassSwapSpeed(&p, 6 * FRACUNIT, wp_tornado) > 6 * FRACUNIT, "witch: magic weapons are changed quickly");
  CCHECK(bg_ClassSwapSpeed(&p, 6 * FRACUNIT, wp_fist) == 6 * FRACUNIT, "the knife is changed at the normal speed");
  p.bgclass = soldier;
  p.readyweapon = wp_pistol;
  CCHECK(bg_ClassSpreadPct(&p) < 100, "soldier: firearms are more accurate");

  CCHECK(bg_ClassAllowsThing(0) == true, "a thing without class flags is for everybody");
  bg_player_class[0] = soldier;
  netgame = false;
  CCHECK(bg_ClassAllowsThing(MTF_FIGHTER) && !bg_ClassAllowsThing(MTF_CLERIC),
         "single player soldier sees class 1 things only");
  bg_player_class[0] = witch;
  CCHECK(!bg_ClassAllowsThing(MTF_FIGHTER) && bg_ClassAllowsThing(MTF_CLERIC | MTF_MAGE),
         "single player witch sees class 2 things");
  bg_player_class[0] = soldier;

  {
    int hs[3] = { 28, 37, 56 }, k;

    for (k = 0; k < 3; ++k)
    {
      fixed_t v = bg_JumpSpeedFor(hs[k]);
      long long z = 0, apex = 0;
      fixed_t m = v;

      while (m > 0) { z += m; if (z > apex) apex = z; m -= FRACUNIT; }
      CCHECK(apex >= (long long) hs[k] * FRACUNIT && apex < ((long long) hs[k] + 1) * FRACUNIT / 8 * 8 + FRACUNIT / 2,
             "jump: the speed for a given height rises (just) that high");
    }
    CCHECK(bg_JumpSpeedFor(28) < bg_JumpSpeedFor(37) && bg_JumpSpeedFor(37) < bg_JumpSpeedFor(56), "jump: higher jumps are faster");
    {
      // the numbers of a wad's own BGCLASS lump may differ from the built-in ones, so test the parser
      int before = num_classes, idx;

      bg_ParseClassText("class jumptest \"J\"\n  jump 40\n", -2);
      idx = num_classes - 1;
      CCHECK(num_classes == before + 1 && bg_ClassJumpHeight(idx) == 40, "jump: \"jump <n>\" is read from BGCLASS");
      CCHECK(classes[idx].jump_momz == bg_JumpSpeedFor(40), "jump: and turned into a speed");
      num_classes = before;
    }
    CCHECK(bg_ClassJumpMomz(NULL) == -1, "jump: no class means the stock jump");
  }
  {
    mobj_t m;
    player_t pl;
    int saved = classes[soldier].sound[BG_SND_PAIN];

    memset(&m, 0, sizeof(m));
    memset(&pl, 0, sizeof(pl));
    m.player = &pl;
    pl.bgclass = soldier;
    classes[soldier].sound[BG_SND_PAIN] = sfx_tink;
    CCHECK(bg_ClassSfx(&m, sfx_plpain) == sfx_tink, "class sound: the pain sound is replaced");
    CCHECK(bg_ClassSfx(&m, sfx_pldeth) == sfx_pldeth, "class sound: a slot without a sound stays stock");
    CCHECK(bg_ClassSfx(&m, sfx_pistol) == sfx_pistol, "class sound: other sounds are untouched");
    m.player = NULL;
    CCHECK(bg_ClassSfx(&m, sfx_plpain) == sfx_plpain, "class sound: monsters keep their sounds");
    classes[soldier].sound[BG_SND_PAIN] = saved;
  }


  lprintf(LO_INFO, "BEGOTTEN SELFTEST (classes): %s (%d failures)\n",
          bgclass_failures ? "FAILED" : "PASSED", bgclass_failures);
  if (bgclass_failures)
    exit(1);
}

// Replaces the stock player sounds (pain, death, gibbed, oof, "no way") by the class' own.
int bg_ClassSfx(const void* origin, int sfx)
{
  const mobj_t* mo = (const mobj_t*) origin;
  int slot, cls;

  switch (sfx)
  {
    case sfx_plpain: slot = BG_SND_PAIN; break;
    case sfx_pldeth: slot = BG_SND_DEATH; break;
    case sfx_pdiehi:
    case sfx_slop:   slot = BG_SND_XDEATH; break;
    case sfx_oof:    slot = BG_SND_OOF; break;
    case sfx_noway:  slot = BG_SND_NOWAY; break;
    default: return sfx;
  }
  if (!mo || !mo->player)
    return sfx;
  cls = mo->player->bgclass;
  if (cls < 0 || cls >= num_classes || !classes[cls].sound[slot])
    return sfx;
  return classes[cls].sound[slot];
}

// Upward speed of this player's jump; 0 = this class cannot jump. Players without a class
// (a game that does not use the Begotten classes) get the stock engine jump.
fixed_t bg_ClassJumpMomz(const player_t* player)
{
  int cls = player ? player->bgclass : -1;

  if (cls < 0 || cls >= num_classes)
    return -1;
  return classes[cls].jump_momz;
}

int bg_ClassJumpHeight(int cls)
{
  return cls >= 0 && cls < num_classes ? classes[cls].jump : 0;
}

int bg_ClassJumpSfx(const player_t* player)
{
  int cls = player ? player->bgclass : -1;

  return cls >= 0 && cls < num_classes ? classes[cls].sound[BG_SND_JUMP] : 0;
}

// ---------------------------------------------------------------------------------------------
// Body poses. Everything here is cosmetic and derived from simulated state only (state, momentum,
// the parry clock), so every machine draws every player the same way.
// ---------------------------------------------------------------------------------------------

static int PoseState(const mobj_t* mo)
{
  return mo->state ? (int) (mo->state - states) : -1;
}

static dboolean IsRunState(int st) { return st >= S_PLAY_RUN1 && st <= S_PLAY_RUN4; }
static dboolean IsAttackState(int st) { return st == S_PLAY_ATK1 || st == S_PLAY_ATK2; }

void bg_PoseTick(player_t* player)
{
  int st;

  if (!player || !player->mo)
    return;
  st = PoseState(player->mo);

  if (IsAttackState(st))
  {
    int was = IsAttackState(player->bglaststate);

    // a new attack, or the next shot of a burst (the muzzle flash state starts again)
    if (!was || (st == S_PLAY_ATK2 && player->bglaststate != S_PLAY_ATK2))
      player->bgatktic = 0;
    else if (player->bgatktic >= 0)
      ++player->bgatktic;
  }
  else
    player->bgatktic = -1;

  if (IsRunState(st))
    ++player->bgruntic;
  else if (st == S_PLAY && player->bgruntic > 35)
    player->bgruntic = 0;   // a long rest: the next run starts from the first frame

  if (player->readyweapon == wp_spare || player->bgparry > 0)
    player->bgparrytic = player->bgparrytic < 0 ? 0 : player->bgparrytic + 1;
  else
    player->bgparrytic = -1;

  player->bglaststate = st;
}

static const bg_pose_t* FindPose(const bg_class_t* c, int pose, int weapon)
{
  int i;
  const bg_pose_t* any = NULL;

  for (i = 0; i < c->num_poses; ++i)
  {
    const bg_pose_t* ps = &c->pose[i];

    if (ps->pose != pose || ps->sprnum < 0 || !sprites || ps->sprnum >= num_sprites ||
        !sprites[ps->sprnum].spriteframes || sprites[ps->sprnum].numframes < 1)
      continue;
    if (ps->weapon == weapon)
      return ps;
    if (ps->weapon < 0)
      any = ps;
  }
  return any;
}

// frame index inside a pose, from the clocks
static int PoseFrame(const bg_pose_t* ps, const mobj_t* mo, const player_t* pl)
{
  int f = 0;

  switch (ps->pose)
  {
    case POSE_STAND: f = (leveltime / ps->tics) % ps->frames; break;
    case POSE_RUN:
    case POSE_CHARGERUN: f = (pl->bgruntic / ps->tics) % ps->frames; break;
    case POSE_SHOOT: f = pl->bgatktic < 0 ? 0 : pl->bgatktic / ps->tics; break;
    case POSE_PARRY:
    {
      // with three or more frames the last one is the outward swing, shown for a few tics after a parried blow;
      // the others follow the guard clock (a missed parry never shows the swing)
      int timed = ps->frames >= 3 ? ps->frames - 2 : ps->frames - 1;

      f = pl->bgparrytic < 0 ? 0 : pl->bgparrytic / ps->tics;
      if (f > timed)
        f = timed;
      if (ps->frames >= 3 && pl->bgparryhit > 0 && pl->bgparry >= bg_ClassParryWindow(pl) - 3)
        f = ps->frames - 1;
      break;
    }
    case POSE_JUMP:
    case POSE_CHARGEJUMP:
      f = mo->momz > 2 * FRACUNIT ? 0 : (mo->momz >= -2 * FRACUNIT ? 1 : 2);   // rising, peak, falling
      break;
  }
  if (f >= ps->frames && ps->pose != POSE_JUMP && ps->pose != POSE_CHARGEJUMP)
    f = ps->frames - 1;
  if (f >= sprites[ps->sprnum].numframes)
    f = sprites[ps->sprnum].numframes - 1;
  // a frame the artist did not draw (a gap in the lettering) falls back to the nearest one before it
  while (f > 0 && sprites[ps->sprnum].spriteframes[f].rotate == -1)
    --f;
  return sprites[ps->sprnum].spriteframes[f].rotate == -1 ? -1 : f;
}

dboolean bg_PlayerPose(const struct mobj_s* thing, int* sprite, int* frame)
{
  const player_t* pl = thing ? thing->player : NULL;
  const bg_class_t* c;
  const bg_pose_t* ps = NULL;
  int st, order[NUM_POSES + 4], n = 0, i, weapon;

  if (!pl || thing->health <= 0 || pl->bgclass < 0 || pl->bgclass >= num_classes)
    return false;
  c = &classes[pl->bgclass];
  if (!c->num_poses)
    return false;

  st = PoseState(thing);
  if (st != S_PLAY && !IsRunState(st) && !IsAttackState(st))
    return false;   // pain, death and anything else keep the state's own frame

  weapon = pl->readyweapon;

  // what the player is doing, most specific first; each falls back to the next, and finally to
  // the standing pose of the same weapon
  if (pl->bgparrytic >= 0 && (pl->readyweapon == wp_spare || pl->bgparry > 0))
    order[n++] = POSE_PARRY;
  {
    // the swordsman's stab: the sword drawn back beside her (still, walking, in the air), then the lunge (feet on the
    // ground, or flexed in the air); a class without these pictures falls through to the ordinary poses
    int phase = bg_StabPhase(pl), air = thing->z > thing->floorz && !(thing->flags & MF_NOGRAVITY);

    if (phase == 2)
    {
      if (air)
        order[n++] = POSE_STABAIR;
      order[n++] = POSE_STAB;
    }
    else if (phase == 1)
    {
      if (air)
        order[n++] = POSE_CHARGEJUMP;
      else if (IsRunState(st))
        order[n++] = POSE_CHARGERUN;
      order[n++] = POSE_CHARGE;
    }
  }
  if (IsAttackState(st))
    order[n++] = POSE_SHOOT;
  if (thing->z > thing->floorz && !(thing->flags & MF_NOGRAVITY))
    order[n++] = POSE_JUMP;
  if (IsRunState(st))
    order[n++] = POSE_RUN;
  order[n++] = POSE_STAND;

  for (i = 0; i < n && !ps; ++i)
    ps = FindPose(c, order[i], weapon);
  if (!ps)
    return false;

  i = PoseFrame(ps, thing, pl);
  if (i < 0)
    return false;
  *sprite = ps->sprnum;
  *frame = (*frame & ~FF_FRAMEMASK) | i;
  return true;
}

// Pose resolver test. It needs pose sprites in the loaded wads (tools/make_pose_placeholders.py),
// so it only runs when they are there: BEGOTTEN_SELFTEST=3 ... -file begotten_poses.wad
void dsda_BgPoseSelfTest(void)
{
  int soldier = bg_ClassFromWord("soldier"), sword = bg_ClassFromWord("swordsman");
  int fails = 0, spr = -1, frame, i;
  mobj_t m;
  player_t pl;

  for (i = 0; soldier >= 0 && i < classes[soldier].num_poses; ++i)
    if (classes[soldier].pose[i].pose == POSE_RUN && classes[soldier].pose[i].sprnum >= 0)
      spr = classes[soldier].pose[i].sprnum;
  if (spr < 0 || !sprites || !sprites[spr].spriteframes || sprites[spr].numframes < 1)
  {
    lprintf(LO_INFO, "BEGOTTEN SELFTEST (poses): SKIPPED (no pose sprites loaded)\n");
    return;
  }

#define PT(c, m_) do { if (c) lprintf(LO_INFO, "ok:   pose: %s\n", m_); else { ++fails; lprintf(LO_INFO, "FAIL: pose: %s\n", m_); } } while (0)
#define SETUP(stidx) do { memset(&m, 0, sizeof m); memset(&pl, 0, sizeof pl); m.player = &pl; m.health = 100; \
    m.state = &states[stidx]; m.frame = states[stidx].frame; pl.bgclass = soldier; pl.readyweapon = wp_pistol; \
    pl.bgatktic = -1; pl.bgparrytic = -1; frame = m.frame; spr = -1; } while (0)
  {
    int ss, sf;

    SETUP(S_PLAY_RUN1);
    pl.bgruntic = 0;
    PT(bg_PlayerPose(&m, &spr, &frame) && (frame & FF_FRAMEMASK) == 0, "running: first frame of the run set");
    ss = spr;
    pl.bgruntic = 3 * 4;   // 3 tics per frame: frame 4
    bg_PlayerPose(&m, &spr, &frame);
    PT((frame & FF_FRAMEMASK) == 4 && spr == ss, "running: the cycle advances with the run clock");
    pl.bgruntic = 3 * 6;
    bg_PlayerPose(&m, &spr, &frame);
    PT((frame & FF_FRAMEMASK) == 0, "running: and wraps after 6 frames");

    SETUP(S_PLAY_ATK2);
    pl.bgatktic = 0;
    PT(bg_PlayerPose(&m, &spr, &frame) && (frame & FF_FRAMEMASK) == 0 && spr != ss, "shooting: first recoil frame");
    pl.bgatktic = 3;
    bg_PlayerPose(&m, &spr, &frame);
    PT((frame & FF_FRAMEMASK) == 1, "shooting: second recoil frame");
    pl.bgatktic = 99;
    bg_PlayerPose(&m, &spr, &frame);
    PT((frame & FF_FRAMEMASK) == 2, "shooting: the last frame is held");
    sf = m.frame;
    PT((frame & FF_FULLBRIGHT) == (sf & FF_FULLBRIGHT), "shooting: the muzzle flash brightness of the state is kept");

    SETUP(S_PLAY);
    m.z = 100 * FRACUNIT; m.floorz = 0; m.momz = 5 * FRACUNIT;
    PT(bg_PlayerPose(&m, &spr, &frame) && (frame & FF_FRAMEMASK) == 0, "jumping: rising frame");
    m.momz = 0;
    bg_PlayerPose(&m, &spr, &frame);
    PT((frame & FF_FRAMEMASK) == 1, "jumping: peak frame");
    m.momz = -9 * FRACUNIT;
    bg_PlayerPose(&m, &spr, &frame);
    PT((frame & FF_FRAMEMASK) == 2, "jumping: falling frame (also when dropping off a ledge)");
    m.z = 0;
    spr = -1; frame = m.frame;
    PT(bg_PlayerPose(&m, &spr, &frame) && (frame & FF_FRAMEMASK) == 0, "standing: every weapon has its own stand pose");

    SETUP(S_PLAY);
    pl.readyweapon = wp_shotgun;
    PT(bg_PlayerPose(&m, &spr, &frame), "standing with the rifle: a weapon specific stand pose");
    SETUP(S_PLAY_ATK1);
    pl.readyweapon = wp_shotgun; pl.bgatktic = 0;
    PT(bg_PlayerPose(&m, &spr, &frame), "shooting with the rifle: a weapon specific shoot pose");

    SETUP(S_PLAY_PAIN);
    PT(!bg_PlayerPose(&m, &spr, &frame), "pain keeps the state's own frame");
    SETUP(S_PLAY_RUN1);
    m.health = 0;
    PT(!bg_PlayerPose(&m, &spr, &frame), "a dead player is not posed");

    SETUP(S_PLAY);
    pl.bgclass = sword; pl.readyweapon = wp_spare; pl.bgparrytic = 0;
    PT(bg_PlayerPose(&m, &spr, &frame) && (frame & FF_FRAMEMASK) == 0, "parrying: first guard frame");
    pl.bgparrytic = 4;
    bg_PlayerPose(&m, &spr, &frame);
    PT((frame & FF_FRAMEMASK) == 1, "parrying: the stance changes with the guard clock");
    SETUP(S_PLAY_ATK1);
    pl.bgclass = sword; pl.readyweapon = wp_spare; pl.bgparrytic = 2; pl.bgatktic = 1;
    {
      int p1;

      bg_PlayerPose(&m, &spr, &frame);
      p1 = spr;
      SETUP(S_PLAY_ATK1);
      pl.bgclass = sword; pl.readyweapon = wp_fist; pl.bgatktic = 1;
      bg_PlayerPose(&m, &spr, &frame);
      PT(spr != p1, "parrying wins over attacking: a different sprite set");
    }

    // the swordsman's stab: drawn back, walking with it, in the air, the lunge on the ground and in the air
    {
      int c0, c1, c2, l0, l1;

      SETUP(S_PLAY);
      pl.bgclass = sword; pl.readyweapon = wp_fist; pl.psprites[ps_weapon].state = (state_t*) bg_StabPhaseState(1);
      PT(bg_PlayerPose(&m, &spr, &frame), "stab: the sword drawn back has a pose");
      c0 = spr;
      SETUP(S_PLAY_RUN1);
      pl.bgclass = sword; pl.readyweapon = wp_fist; pl.psprites[ps_weapon].state = (state_t*) bg_StabPhaseState(1);
      pl.bgruntic = 0;
      bg_PlayerPose(&m, &spr, &frame);
      c1 = spr;
      PT(c1 != c0, "stab: walking with the sword drawn back has its own set");
      SETUP(S_PLAY);
      pl.bgclass = sword; pl.readyweapon = wp_fist; pl.psprites[ps_weapon].state = (state_t*) bg_StabPhaseState(1);
      m.z = 100 * FRACUNIT; m.floorz = 0; m.momz = 5 * FRACUNIT;
      bg_PlayerPose(&m, &spr, &frame);
      c2 = spr;
      PT(c2 != c0 && c2 != c1, "stab: a charged sword in the air has its own set");
      SETUP(S_PLAY);
      pl.bgclass = sword; pl.readyweapon = wp_fist; pl.psprites[ps_weapon].state = (state_t*) bg_StabPhaseState(2);
      PT(bg_PlayerPose(&m, &spr, &frame), "stab: the lunge on the ground has a pose");
      l0 = spr;
      PT(l0 != c0, "stab: the lunge differs from the drawn-back sword");
      m.z = 100 * FRACUNIT; m.floorz = 0;
      bg_PlayerPose(&m, &spr, &frame);
      l1 = spr;
      PT(l1 != l0, "stab: the lunge in the air has its own pose (legs flexed)");
      SETUP(S_PLAY);
      pl.bgclass = sword; pl.readyweapon = wp_fist; pl.psprites[ps_weapon].state = &states[S_PUNCH];
      bg_PlayerPose(&m, &spr, &frame);
      PT(spr != c0 && spr != l0, "stab: no stab pose when the blade is not stabbing");
    }
  }
#undef SETUP
#undef PT
  lprintf(LO_INFO, "BEGOTTEN SELFTEST (poses): %s (%d failures)\n", fails ? "FAILED" : "PASSED", fails);
}
