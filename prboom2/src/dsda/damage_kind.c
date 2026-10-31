//
// Begotten: damage kinds and per-monster vulnerabilities.
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
#include "info.h"
#include "lprintf.h"
#include "p_mobj.h"
#include "w_wad.h"
#include "z_zone.h"

#include "begotten.h"
#include "damage_kind.h"
#include "bgclass.h"

#define MAX_WEAPON_KINDS 32

static const char* kind_names[DK_NUMKINDS] = {
  "blade", "bullet", "shell", "belt", "fire", "tempest", "spirit", "hellfire"
};

static const char* category_names[DC_NUMCATEGORIES] = {
  "melee", "firearm", "magic", "special"
};

static const damage_category_t kind_category[DK_NUMKINDS] = {
  DC_MELEE, DC_FIREARM, DC_FIREARM, DC_FIREARM, DC_MAGIC, DC_MAGIC, DC_MAGIC, DC_SPECIAL
};

static const char* weapon_names[] = {
  "fist", "pistol", "shotgun", "chaingun", "missile", "plasma", "bfg", "chainsaw", "supershotgun",
  "dualpistols", "chainfire", "tornado", "arclightning", "snap", "hellfire", "spare"
};

typedef struct {
  short kind[DK_NUMKINDS];          // percent, -1 = not set
  short category[DC_NUMCATEGORIES]; // percent, -1 = not set
} vulnerability_t;

static signed char weapon_kind[MAX_WEAPON_KINDS];
static signed char* projectile_kind;     // per mobj type
static vulnerability_t** vulnerability;  // per mobj type, NULL = takes full damage
static int table_size;
static int trace_enabled;

static int dsda_FindKind(const char* name)
{
  int i;

  for (i = 0; i < DK_NUMKINDS; ++i)
    if (!strcasecmp(name, kind_names[i]))
      return i;
  return -1;
}

static int dsda_FindCategory(const char* name)
{
  int i;

  for (i = 0; i < DC_NUMCATEGORIES; ++i)
    if (!strcasecmp(name, category_names[i]))
      return i;
  return -1;
}

static int dsda_FindWeapon(const char* name)
{
  int i;
  char* end;
  long n;

  for (i = 0; i < (int) (sizeof(weapon_names) / sizeof(weapon_names[0])); ++i)
    if (!strcasecmp(name, weapon_names[i]))
      return i;

  n = strtol(name, &end, 10);
  if (*name && !*end && n >= 0 && n < MAX_WEAPON_KINDS)
    return (int) n;

  return -1;
}

// "thing N" is the DEHACKED thing number, "ednum N" the editor number.
static int dsda_FindMobjType(const char* how, const char* number)
{
  char* end;
  long n;
  int i;

  // begotten's own projectiles are found by name ("projectile name tornado tempest")
  if (!strcasecmp(how, "name"))
    return bg_NamedMobjType(number);

  n = strtol(number, &end, 10);
  if (!*number || *end)
    return -1;

  if (!strcasecmp(how, "thing"))
    return (n >= 1 && n <= table_size) ? (int) n - 1 : -1;

  if (!strcasecmp(how, "ednum"))
  {
    for (i = 0; i < table_size; ++i)
      if (mobjinfo[i].doomednum == n)
        return i;
  }

  return -1;
}

static vulnerability_t* dsda_VulnerabilityFor(int type)
{
  if (!vulnerability[type])
  {
    int i;

    vulnerability[type] = Z_Calloc(1, sizeof(vulnerability_t));
    for (i = 0; i < DK_NUMKINDS; ++i)
      vulnerability[type]->kind[i] = -1;
    for (i = 0; i < DC_NUMCATEGORIES; ++i)
      vulnerability[type]->category[i] = -1;
  }
  return vulnerability[type];
}

static void dsda_ParseLine(char* line, int lumpnum, int lineno)
{
  char* tok[40];
  int count = 0;
  char* p;
  char* hash;

  if ((hash = strchr(line, '#')))
    *hash = '\0';
  if ((hash = strstr(line, "//")))
    *hash = '\0';

  for (p = strtok(line, " \t\r\n=,"); p && count < 40; p = strtok(NULL, " \t\r\n=,"))
    tok[count++] = p;

  if (!count)
    return;

  if (!strcasecmp(tok[0], "weapon") && count == 3)
  {
    int w = dsda_FindWeapon(tok[1]);
    int k = dsda_FindKind(tok[2]);

    if (w >= 0 && k >= 0)
    {
      weapon_kind[w] = k;
      return;
    }
  }
  else if (!strcasecmp(tok[0], "projectile") && count == 4)
  {
    int t = dsda_FindMobjType(tok[1], tok[2]);
    int k = dsda_FindKind(tok[3]);

    if (t >= 0 && k >= 0)
    {
      projectile_kind[t] = k;
      return;
    }
  }
  else if (!strcasecmp(tok[0], "vulnerability") && count >= 5 && (count % 2) == 1)
  {
    int t = dsda_FindMobjType(tok[1], tok[2]);
    int i;
    int ok = t >= 0;

    if (ok)
    {
      vulnerability_t* v = dsda_VulnerabilityFor(t);

      for (i = 3; i + 1 < count && ok; i += 2)
      {
        int k = dsda_FindKind(tok[i]);
        int c = k < 0 ? dsda_FindCategory(tok[i]) : -1;
        int pct = atoi(tok[i + 1]);

        if (pct < 0 || pct > 10000 || (k < 0 && c < 0))
          ok = 0;
        else if (k >= 0)
          v->kind[k] = pct;
        else
          v->category[c] = pct;
      }
    }

    if (ok)
      return;
  }

  lprintf(LO_WARN, "DMGKINDS (lump %d \"%s\", line %d): cannot understand \"%s\"\n",
          lumpnum, lumpinfo[lumpnum].name, lineno, tok[0]);
}

void dsda_InitDamageKinds(void)
{
  int i;

  table_size = num_mobj_types;
  trace_enabled = getenv("BEGOTTEN_DMGTRACE") != NULL;

  memset(weapon_kind, DK_NONE, sizeof(weapon_kind));
  projectile_kind = Z_Malloc(table_size * sizeof(*projectile_kind));
  memset(projectile_kind, DK_NONE, table_size * sizeof(*projectile_kind));
  vulnerability = Z_Calloc(table_size, sizeof(*vulnerability));

  for (i = 0; i < numlumps; ++i)
  {
    const char* data;
    int size;
    char* text;
    char* line;
    char* next;
    int lineno = 0;

    if (strncasecmp(lumpinfo[i].name, "DMGKINDS", 8))
      continue;

    if (trace_enabled) lprintf(LO_INFO, "DMGKINDS: reading lump %d (%s)\n", i, lumpinfo[i].wadfile ? lumpinfo[i].wadfile->name : "?");
    size = W_LumpLength(i);
    data = W_LumpByNum(i);
    text = Z_Malloc(size + 1);
    memcpy(text, data, size);
    text[size] = '\0';

    for (line = text; line; line = next)
    {
      next = strchr(line, '\n');
      if (next)
        *next++ = '\0';
      dsda_ParseLine(line, i, ++lineno);
    }

    Z_Free(text);
  }
}

damage_category_t dsda_KindCategory(damage_kind_t kind)
{
  return kind >= 0 && kind < DK_NUMKINDS ? kind_category[kind] : DC_SPECIAL;
}

damage_kind_t dsda_WeaponKind(int weapon)
{
  return weapon >= 0 && weapon < MAX_WEAPON_KINDS ? (damage_kind_t) weapon_kind[weapon] : DK_NONE;
}

damage_kind_t dsda_DamageKind(mobj_t* inflictor, mobj_t* source)
{
  if (!projectile_kind)
    return DK_NONE;

  // A projectile (or its explosion) carries its own kind.
  if (inflictor && inflictor->type >= 0 && inflictor->type < table_size &&
      projectile_kind[inflictor->type] != DK_NONE)
    return projectile_kind[inflictor->type];

  // Melee and hitscan attacks of a player take the kind of the weapon in hand.
  if (source && source->player && (!inflictor || inflictor == source))
  {
    int w = source->player->readyweapon;

    if (w >= 0 && w < MAX_WEAPON_KINDS)
      return (damage_kind_t) weapon_kind[w];
  }

  return DK_NONE;
}

int dsda_ScaleDamage(mobj_t* target, mobj_t* inflictor, mobj_t* source, int damage)
{
  damage_kind_t kind;
  vulnerability_t* v;
  int pct = 100;
  int result;

  if (!vulnerability || damage <= 0 || target->type < 0 || target->type >= table_size)
    return damage;

  kind = dsda_DamageKind(inflictor, source);
  if (kind == DK_NONE)
    return damage;

  v = vulnerability[target->type];
  if (v)
  {
    if (v->kind[kind] >= 0)
      pct = v->kind[kind];
    else if (v->category[kind_category[kind]] >= 0)
      pct = v->category[kind_category[kind]];
  }

  // what the attacker's class does to the damage it deals
  if (source && source->player)
    pct = (int) (((long long) pct * bg_ClassDamagePct(source->player, kind)) / 100);

  if (pct == 100)
    return damage;

  result = (int) (((long long) damage * pct) / 100);
  if (pct > 0 && result < 1)
    result = 1;

  if (trace_enabled)
    lprintf(LO_INFO, "damage: target thing %d, kind %s (%s), %d%% : %d -> %d\n",
            target->type + 1, kind_names[kind], category_names[kind_category[kind]],
            pct, damage, result);

  return result;
}

#include "p_inter.h"
#include "p_map.h"

#define CHECK(cond, what) do { ++total; if (!(cond)) { ++failed; lprintf(LO_INFO, "SELFTEST FAIL: %s\n", what); } } while (0)

static int live_done;

void dsda_DamageKindLiveTest(void)
{
  players[consoleplayer].bgclass = -1; // class multipliers are tested elsewhere; here every number is exact

  mobj_t* player = players[0].mo;
  mobj_t* caco;
  int total = 0, failed = 0, before;
  int i;

  if (live_done)
    return;
  live_done = 1;

  caco = P_SpawnMobj(player->x + (200 << FRACBITS), player->y, player->z, MT_BORE);
  CHECK(caco != NULL, "spawned a cacodemon");

  // cacodemon: shells 150%, bullets 75% (set here: the shipped balance table changes with the game's roster)
  {
    char line[] = "vulnerability ednum 3005 shell 150 bullet 75";

    dsda_ParseLine(line, 0, 1);
  }
  players[0].readyweapon = wp_shotgun;
  before = caco->health;
  P_DamageMobj(caco, NULL, player, 20);
  CHECK(before - caco->health == 30, "real damage: 20 shell damage becomes 30");

  players[0].readyweapon = wp_pistol;
  before = caco->health;
  P_DamageMobj(caco, NULL, player, 20);
  CHECK(before - caco->health == 15, "real damage: 20 bullet damage becomes 15");

  players[0].readyweapon = wp_fist;
  before = caco->health;
  P_DamageMobj(caco, NULL, player, 20);
  CHECK(before - caco->health == 20, "real damage: blade is unchanged");

  // rocket explosion: kind comes from the projectile
  players[0].readyweapon = wp_pistol;
  {
    mobj_t* rocket = P_SpawnMobj(player->x, player->y, player->z, MT_ROCKET);
    before = caco->health;
    P_DamageMobj(caco, rocket, player, 20);
    CHECK(before - caco->health == 20, "rocket: fire has no entry for the cacodemon, unchanged");
  }

  // revenant (66): firearm and magic as in the shipped table (90 and 125 now)
  {
    int rev = -1;
    mobj_t* r;

    for (i = 0; i < num_mobj_types; ++i)
      if (mobjinfo[i].doomednum == 66) rev = i;
    r = P_SpawnMobj(player->x, player->y + (200 << FRACBITS), player->z, rev);
    players[0].readyweapon = wp_pistol;
    before = r->health;
    P_DamageMobj(r, NULL, player, 20);
    CHECK(before - r->health == 18, "revenant: firearm 90%");
    {
      mobj_t* plasma = P_SpawnMobj(player->x, player->y, player->z, MT_PLASMA);
      before = r->health;
      P_DamageMobj(r, plasma, player, 20);
      CHECK(before - r->health == 25, "revenant: fire (magic) 125%");
    }
  }

  lprintf(LO_INFO, "LIVETEST: %d checks, %d failed\n", total, failed);
  exit(failed ? 1 : 0);
}

void dsda_DamageKindSelfTest(void)
{
  mobj_t target, bullet_inflictor, rocket, shooter;
  player_t fake;
  int failed = 0, total = 0;
  int caco = -1, imp = -1, i;

  for (i = 0; i < table_size; ++i)
  {
    if (mobjinfo[i].doomednum == 3005) caco = i;
    if (mobjinfo[i].doomednum == 3001) imp = i;
  }

  memset(&target, 0, sizeof(target));
  memset(&rocket, 0, sizeof(rocket));
  memset(&shooter, 0, sizeof(shooter));
  memset(&fake, 0, sizeof(fake));
  fake.bgclass = -1;   // no class: class multipliers are tested elsewhere, here every number is exact
  memset(&bullet_inflictor, 0, sizeof(bullet_inflictor));
  shooter.player = &fake;
  shooter.type = MT_PLAYER;
  rocket.type = MT_ROCKET;
  CHECK(MT_ROCKET == 33, "rocket is thing 34 in DEHACKED numbering");
  (void) bullet_inflictor;

  CHECK(caco >= 0 && imp >= 0, "cacodemon and imp exist");
  if (caco >= 0) vulnerability[caco] = NULL;   // ignore the shipped table for these two
  if (imp >= 0) vulnerability[imp] = NULL;

  {
    static const char* lines[] = {
      "weapon fist blade", "weapon pistol bullet", "weapon shotgun shell",
      "weapon chaingun belt", "projectile thing 34 fire",
      "vulnerability ednum 3005 shell 150 firearm 70",
      "vulnerability ednum 3001 bullet 0 belt 1",
      "bogus line"
    };
    int n;

    for (n = 0; n < (int) (sizeof(lines) / sizeof(lines[0])); ++n)
    {
      char buf[128];

      strncpy(buf, lines[n], sizeof(buf) - 1);
      buf[sizeof(buf) - 1] = '\0';
      dsda_ParseLine(buf, 0, n + 1);
    }
  }

  // pistol: bullet; shotgun: shell; chaingun: belt; missile (via projectile): fire
  target.type = caco;
  fake.readyweapon = wp_pistol;
  CHECK(dsda_DamageKind(NULL, &shooter) == DK_BULLET, "pistol is bullet");
  CHECK(dsda_DamageKind(&shooter, &shooter) == DK_BULLET, "pistol with inflictor==source is bullet");
  fake.readyweapon = wp_shotgun;
  CHECK(dsda_DamageKind(NULL, &shooter) == DK_SHELL, "shotgun is shell");
  fake.readyweapon = wp_chaingun;
  CHECK(dsda_DamageKind(NULL, &shooter) == DK_BELT, "chaingun is belt");
  fake.readyweapon = wp_fist;
  CHECK(dsda_DamageKind(NULL, &shooter) == DK_BLADE, "fist is blade");
  fake.readyweapon = wp_missile;
  CHECK(dsda_DamageKind(&rocket, &shooter) == DK_FIRE, "rocket projectile is fire");
  CHECK(dsda_DamageKind(NULL, NULL) == DK_NONE, "no source, no kind");
  CHECK(dsda_DamageKind(&target, &target) == DK_NONE, "monster attack has no kind");

  // cacodemon: shells 150, firearm 70 (so bullets/belts 70), fire/magic: see table
  fake.readyweapon = wp_shotgun;
  CHECK(dsda_ScaleDamage(&target, NULL, &shooter, 100) == 150, "caco takes 150% from shells");
  fake.readyweapon = wp_pistol;
  CHECK(dsda_ScaleDamage(&target, NULL, &shooter, 100) == 70, "caco takes 70% from bullets (category)");
  fake.readyweapon = wp_fist;
  CHECK(dsda_ScaleDamage(&target, NULL, &shooter, 100) == 100, "caco takes 100% from blade (unset)");
  CHECK(dsda_ScaleDamage(&target, &rocket, &shooter, 100) == 100, "caco takes 100% from fire (unset)");

  // imp: immune to bullets (0%), but never less than 1 when pct > 0 (belts 1%)
  target.type = imp;
  fake.readyweapon = wp_pistol;
  CHECK(dsda_ScaleDamage(&target, NULL, &shooter, 10) == 0, "imp is immune to bullets (test table)");
  fake.readyweapon = wp_chaingun;
  CHECK(dsda_ScaleDamage(&target, NULL, &shooter, 10) == 1, "1% of 10 is raised to 1");

  // zero or negative damage and out-of-range types are untouched
  CHECK(dsda_ScaleDamage(&target, NULL, &shooter, 0) == 0, "zero damage stays zero");
  target.type = -1;
  CHECK(dsda_ScaleDamage(&target, NULL, &shooter, 10) == 10, "invalid type is untouched");

  lprintf(LO_INFO, "SELFTEST: %d checks, %d failed\n", total, failed);
  exit(failed ? 1 : 0);
}
