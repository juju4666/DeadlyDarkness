//
// Begotten: animated pickups and the nine keys. See bgpickup.h.
// Licensed under the GNU GPL v2 or later, like the rest of the engine.
//

#include <stdio.h>
#include <string.h>

#include "doomdef.h"
#include "doomstat.h"
#include "info.h"
#include "p_mobj.h"
#include "p_pspr.h"

#include "r_state.h"

#include "lprintf.h"
#include "dsda/begotten.h"
#include "dsda/bgpickup.h"
#include "dsda/mobjinfo.h"
#include "dsda/sprite.h"

#define BG_PICKUP_FRAMES 8
#define BG_PICKUP_TICS 4

static const char* const key_names[BG_KEYS] = {"blue", "yellow", "red", "green", "cyan", "magenta", "black", "grey", "white"};
static const char* const key_sprite[BG_KEYS] = {NULL, NULL, NULL, "GKEY", "CKEY", "MKEY", "KKEY", "AKEY", "WKEY"};
static const int key_slot[BG_KEYS] = {it_bluecard, it_yellowcard, it_redcard, 6, 7, 8, 9, 10, 11};
static const int key_ednum[BG_KEYS] = {5, 6, 13, 22001, 22002, 22003, 22004, 22005, 22006};
static const int key_lock[BG_KEYS] = {2, 3, 1, 7, 8, 9, 10, 11, 12};
static int key_sprite_index[BG_KEYS];
static char key_message[BG_KEYS][40];

typedef struct { int sprite; int first; } pickup_anim_t;
static pickup_anim_t anims[32];
static int num_anims;

static int animate(int sprite, int bright)
{
  int first = bg_AllocStates(BG_PICKUP_FRAMES), k;

  for (k = 0; k < BG_PICKUP_FRAMES; ++k)
  {
    state_t* st = &states[first + k];

    memset(st, 0, sizeof(*st));
    st->sprite = sprite;
    st->frame = k | (bright ? 0x8000 : 0);
    st->tics = BG_PICKUP_TICS;
    st->nextstate = k == BG_PICKUP_FRAMES - 1 ? first : first + k + 1;
  }
  if (num_anims < (int) (sizeof(anims) / sizeof(anims[0])))
  {
    anims[num_anims].sprite = sprite;
    anims[num_anims].first = first;
    ++num_anims;
  }
  return first;
}

static void animate_type(int type, int bright)
{
  mobjinfo[type].spawnstate = animate(states[mobjinfo[type].spawnstate].sprite, bright);
}

void bg_InitPickups(void)
{
  static const int weapon_pickups[] = {MT_SHOTGUN, MT_CHAINGUN, MT_MISC27, MT_MISC28, MT_MISC25, MT_SUPERSHOTGUN};
  int i;
  mobjinfo_t proto;

  num_anims = 0;
  for (i = 0; i < (int) (sizeof(weapon_pickups) / sizeof(weapon_pickups[0])); ++i)
    animate_type(weapon_pickups[i], false);

  // the three stock colours keep their things and their editor numbers; the skull keys are retired
  animate_type(MT_MISC4, true);
  animate_type(MT_MISC5, true);
  animate_type(MT_MISC6, true);
  mobjinfo[MT_MISC7].doomednum = mobjinfo[MT_MISC8].doomednum = mobjinfo[MT_MISC9].doomednum = -1;
  // no partial invisibility sphere in this game (the pickup is retired; a map that places it gets nothing)
  mobjinfo[MT_INS].doomednum = -1;
  key_sprite_index[0] = states[mobjinfo[MT_MISC4].spawnstate].sprite;
  key_sprite_index[1] = states[mobjinfo[MT_MISC6].spawnstate].sprite;
  key_sprite_index[2] = states[mobjinfo[MT_MISC5].spawnstate].sprite;

  // six new colours: the same kind of thing as a card, with a sprite of its own
  proto = mobjinfo[MT_MISC4];
  for (i = 3; i < BG_KEYS; ++i)
  {
    mobjinfo_t info = proto;
    int spr = dsda_AddSpriteName(key_sprite[i]);

    info.doomednum = key_ednum[i];
    info.spawnstate = animate(spr, true);
    dsda_AppendMobjInfo(&info);
    key_sprite_index[i] = spr;
  }
  for (i = 0; i < BG_KEYS; ++i)
    snprintf(key_message[i], sizeof(key_message[i]), "Picked up a %s key.", key_names[i]);
}

void bg_FixPickupFrames(void)
{
  int a, k;

  for (a = 0; a < num_anims; ++a)
  {
    int spr = anims[a].sprite, first = anims[a].first, n;

    if (spr < 0 || spr >= num_sprites || !sprites)
      continue;
    n = sprites[spr].numframes;
    if (n < 1)
      n = 1;
    if (n > BG_PICKUP_FRAMES)
      n = BG_PICKUP_FRAMES;
    for (k = 0; k < BG_PICKUP_FRAMES; ++k)
    {
      state_t* st = &states[first + k];
      int fr = k < n ? k : n - 1;

      st->frame = (st->frame & ~FF_FRAMEMASK) | fr;
      st->nextstate = (k + 1 < n) ? first + k + 1 : first;
      if (k >= n)
        st->nextstate = first;
    }
  }
}

int bg_KeyForSprite(int sprite)
{
  int i;

  for (i = 0; i < BG_KEYS; ++i)
    if (key_sprite_index[i] == sprite && key_sprite_index[i] != 0)
      return key_slot[i];
  return -1;
}

int bg_KeySlot(int colour)
{
  return colour >= 0 && colour < BG_KEYS ? key_slot[colour] : -1;
}

const char* bg_KeyName(int colour)
{
  return colour >= 0 && colour < BG_KEYS ? key_names[colour] : "?";
}

int bg_KeyColourOfSlot(int slot)
{
  int i;

  for (i = 0; i < BG_KEYS; ++i)
    if (key_slot[i] == slot)
      return i;
  return -1;
}

const char* bg_KeyMessage(int slot)
{
  int c = bg_KeyColourOfSlot(slot);

  return c >= 0 ? key_message[c] : "Picked up a key.";
}

int bg_LockSlot(int lock)
{
  int i;

  for (i = 0; i < BG_KEYS; ++i)
    if (key_lock[i] == lock)
      return key_slot[i];
  return -1;
}

// Self test: returns the number of failures.  Every colour has a thing, a sprite, a distinct slot and a lock number; every
// animated pickup is a closed loop of 8 states or fewer.
int bg_PickupSelfTest(void)
{
  int fails = 0, i, k;

#define PCHECK(cond, what) do { if (!(cond)) { ++fails; lprintf(LO_INFO, "FAIL: %s (%d)\n", what, i); } } while (0)
  for (i = 0; i < BG_KEYS; ++i)
  {
    int t, found = -1, slot = bg_KeySlot(i), spr;

    for (t = 0; t < num_mobj_types; ++t)
      if (mobjinfo[t].doomednum == key_ednum[i])
        found = t;
    PCHECK(found >= 0, "key thing exists");
    if (found < 0)
      continue;
    spr = states[mobjinfo[found].spawnstate].sprite;
    PCHECK(bg_KeyForSprite(spr) == slot, "key sprite gives its slot");
    PCHECK(bg_LockSlot(key_lock[i]) == slot, "key lock number maps to its slot");
    PCHECK(slot >= 0 && slot < NUMCARDS, "key slot is inside cards[]");
    PCHECK(bg_KeyColourOfSlot(slot) == i, "slot maps back to the colour");
    PCHECK(mobjinfo[found].flags & MF_SPECIAL, "key is a special thing");
  }
  for (i = 0; i < num_anims; ++i)
  {
    int s = anims[i].first, n = 0;

    do { s = states[s].nextstate; ++n; } while (s != anims[i].first && n <= BG_PICKUP_FRAMES);
    PCHECK(n == BG_PICKUP_FRAMES || n < BG_PICKUP_FRAMES, "pickup animation loops");
    PCHECK(states[anims[i].first].tics > 0, "pickup frames have a duration");
  }
  for (i = 0; i < BG_KEYS; ++i)
    for (k = i + 1; k < BG_KEYS; ++k)
      if (key_slot[i] == key_slot[k] || key_lock[i] == key_lock[k] || key_ednum[i] == key_ednum[k])
      {
        ++fails;
        lprintf(LO_INFO, "FAIL: key colours %d and %d share a slot, lock or editor number\n", i, k);
      }
#undef PCHECK
  lprintf(LO_INFO, "BEGOTTEN SELFTEST (pickups): %s (%d failures)\n", fails ? "FAILED" : "PASSED", fails);
  return fails;
}

int bg_KeyType(int colour)
{
  int t;

  if (colour < 0 || colour >= BG_KEYS)
    return -1;
  for (t = 0; t < num_mobj_types; ++t)
    if (mobjinfo[t].doomednum == key_ednum[colour])
      return t;
  return -1;
}

int bg_KeyLockNumber(int colour)
{
  return colour >= 0 && colour < BG_KEYS ? key_lock[colour] : 0;
}
