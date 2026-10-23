//
// Begotten: co-op rules (tickets, rucksacks, respawn). Licensed under the GNU GPL v2 or later.
// Everything here runs inside the game tic and only reads game state, so it stays in step on every machine.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doomstat.h"
#include "g_game.h"
#include "info.h"
#include "lprintf.h"
#include "p_inter.h"
#include "p_mobj.h"
#include "p_saveg.h"
#include "s_sound.h"
#include "sounds.h"

#include "dsda/begotten.h"
#include "v_video.h"
#include "dsda/bgclass.h"
#include "dsda/bgnet.h"
#include "dsda/bgsplit.h"
#include "dsda/messenger.h"

#include "bgcoop.h"

static int tickets;
static dboolean wiping;                         // the level is being started over for everyone
static dboolean free_respawn[MAX_MAXPLAYERS];   // the player's next reborn is a respawn (not a fresh start)
static int last_refused[MAX_MAXPLAYERS];
static int dead_tics[MAX_MAXPLAYERS];
static dboolean joined_late[MAX_MAXPLAYERS];    // entered the level after it began: the level does not count for them

static int PlayersInGame(void)
{
  int i, n = 0;

  for (i = 0; i < MAX_MAXPLAYERS; ++i)
    n += playeringame[i] ? 1 : 0;
  return n;
}

dboolean bg_CoopOn(void)
{
  return bg_weapons && !deathmatch && PlayersInGame() > 1;
}

dboolean bg_TicketsOn(void)
{
  return bg_CoopOn() && gameskill + 1 >= 4;
}

int bg_Tickets(void)
{
  return tickets;
}

int bg_TicketDisplay(void)
{
  if (getenv("BEGOTTEN_HUDTEST"))   // developer aid: show a made-up pool to look at the layout
    return atoi(getenv("BEGOTTEN_HUDTEST"));
  return bg_TicketsOn() && gamestate == GS_LEVEL ? tickets : -1;
}

void bg_TicketsSet(int n)
{
  tickets = n < 0 ? 0 : n > BG_MAX_TICKETS ? BG_MAX_TICKETS : n;
}

void bg_TicketsAdd(int n)
{
  bg_TicketsSet(tickets + n);
}

void bg_CoopNewGame(void)
{
  tickets = 0;
  wiping = false;
  memset(free_respawn, 0, sizeof free_respawn);
  memset(joined_late, 0, sizeof joined_late);
}

void bg_CoopNewLevel(void)
{
  // each level start grants one ticket per player (nothing for a single player)
  if (PlayersInGame() > 1)
    bg_TicketsAdd(PlayersInGame());
  wiping = false;
  memset(joined_late, 0, sizeof joined_late);   // everybody in the game now was here at the start
}

// A player enters a network game that is already running (the join event: the same tic on every machine).
// In a level he appears at a start point through the normal reborn; between levels he is simply one of the
// players of the next level. Entry costs nothing, on any difficulty.
void bg_CoopJoin(int n, int cls)
{
  if (n < 0 || n >= MAX_MAXPLAYERS || playeringame[n])
    return;
  memset(&players[n], 0, sizeof players[n]);
  bg_player_class[n] = cls >= 0 && cls < bg_ClassCount() ? cls : 0;
  playeringame[n] = true;
  players[n].playerstate = PST_REBORN;
  free_respawn[n] = false;
  dead_tics[n] = 0;
  joined_late[n] = gamestate == GS_LEVEL;
  doom_printf("%s joined the game", bgnet_PlayerName(n));
}

// Does finishing the level count for this machine's players (the level table, the secret class)? Only for
// players who were there when it began and still are when it ends.
dboolean bg_CoopCompletionCounts(void)
{
  int first, count, i;

  if (!bgnet_InGame())
    return true;
  first = bgnet_LocalFirstSlot();
  count = bgnet_LocalCount();
  for (i = first; i < first + count && i < MAX_MAXPLAYERS; ++i)
    if (!playeringame[i] || joined_late[i])
      return false;
  return true;
}

dboolean bg_CoopJoinedLate(int n)
{
  return n >= 0 && n < MAX_MAXPLAYERS && joined_late[n];
}

void bg_CoopArchive(void)
{
  P_SAVE_X(tickets);
}

void bg_CoopUnArchive(void)
{
  P_LOAD_X(tickets);
}

unsigned bg_CoopHash(void)
{
  return (unsigned) tickets * 2654435761u;
}

// ---- respawn -------------------------------------------------------------------------------------------

dboolean bg_CoopRespawn(player_t* player)
{
  int n = (int) (player - players);

  if (!bg_CoopOn())
    return true;
  if (bg_TicketsOn())
  {
    if (tickets <= 0)
    {
      if (n >= 0 && n < MAX_MAXPLAYERS && gametic - last_refused[n] > 2 * TICRATE)
      {
        last_refused[n] = gametic;
        dsda_AddPlayerMessage("NO TICKETS LEFT", player);
      }
      return false;
    }
    --tickets;
  }
  if (n >= 0 && n < MAX_MAXPLAYERS)
    free_respawn[n] = true;
  return true;
}

void bg_CoopFreeRespawn(int playernum)
{
  if (playernum >= 0 && playernum < MAX_MAXPLAYERS && bg_CoopOn() && !wiping)
    free_respawn[playernum] = true;
}

dboolean bg_CoopWiping(void)
{
  return wiping;
}

// Called by G_PlayerReborn once the player is a fresh class character again. `old` is the player as it was when
// dying. Returns true if this was a respawn (not a fresh start).
dboolean bg_CoopReborn(player_t* p, const player_t* old, int playernum)
{
  int i;
  dboolean hard = bg_TicketsOn();

  if (playernum < 0 || playernum >= MAX_MAXPLAYERS || !free_respawn[playernum])
    return false;
  free_respawn[playernum] = false;
  if (!bg_CoopOn() || wiping)
    return false;

  // weapons are kept
  for (i = 0; i < NUMWEAPONS; ++i)
    p->weaponowned[i] = p->weaponowned[i] || old->weaponowned[i];
  p->backpack = old->backpack;
  for (i = 0; i < NUMAMMO; ++i)
    p->maxammo[i] = old->maxammo[i] > p->maxammo[i] ? old->maxammo[i] : p->maxammo[i];

  if (!hard)
  {
    // difficulty 1-3: the inventory is untouched, only the active power-ups go
    for (i = 0; i < NUMAMMO; ++i)
      p->ammo[i] = old->ammo[i];
    memcpy(p->armorpoints, old->armorpoints, sizeof p->armorpoints);
    p->armortype = old->armortype;
    if (old->weaponowned[old->readyweapon] &&
        (doom_weaponinfo[old->readyweapon].ammo == am_noammo || old->ammo[doom_weaponinfo[old->readyweapon].ammo] > 0))
      p->readyweapon = p->pendingweapon = old->readyweapon;
  }
  else
  {
    // difficulty 4-5: base ammo only (the rest is in the rucksack where they died), never above the capacity
    for (i = 0; i < NUMAMMO; ++i)
      if (p->ammo[i] > p->maxammo[i])
        p->ammo[i] = p->maxammo[i];
  }
  return true;
}

// ---- rucksack ------------------------------------------------------------------------------------------

void bg_CoopPlayerDied(player_t* player)
{
  int n = (int) (player - players), i, total = 0;
  mobj_t* pack;

  if (!bg_TicketsOn() || !player->mo || n < 0 || n >= MAX_MAXPLAYERS)
    return;
  for (i = 0; i < NUMAMMO; ++i)
    total += player->ammo[i];
  if (!total)
    return;

  pack = P_SpawnMobj(player->mo->x, player->mo->y, ONFLOORZ, MT_MISC24);   // the backpack stands in for the rucksack
  pack->flags |= MF_DROPPED;
  pack->flags &= ~MF_COUNTITEM;
  pack->bg_owner = (unsigned char) (n + 1);
  for (i = 0; i < NUMAMMO; ++i)
    pack->bg_pack[i] = (short) (player->ammo[i] > 32000 ? 32000 : player->ammo[i]);
}

dboolean bg_RucksackHidden(const mobj_t* pack, const player_t* viewer)
{
  return pack->bg_owner && gameskill + 1 == 4 && viewer && (int) (viewer - players) != pack->bg_owner - 1;
}

dboolean bg_RucksackTouch(mobj_t* pack, player_t* player)
{
  int i, taken = 0, left = 0;

  if (!pack->bg_owner)
    return false;
  if (bg_RucksackHidden(pack, player))
    return true;                              // difficulty 4: someone else's, untouchable

  for (i = 0; i < NUMAMMO; ++i)
  {
    int room = player->maxammo[i] - player->ammo[i];
    int give = pack->bg_pack[i] < room ? pack->bg_pack[i] : room;

    if (give < 0)
      give = 0;
    player->ammo[i] += give;
    pack->bg_pack[i] -= (short) give;
    taken += give;
    left += pack->bg_pack[i];
  }
  if (!taken)
    return true;                              // nothing fits: leave it
  dsda_AddPlayerMessage("PICKED UP THE RUCKSACK", player);
  player->bonuscount += 6;
  S_StartSound(player->mo, sfx_itemup);
  if (!left)
    P_RemoveMobj(pack);
  return true;
}

// ---- tickets from the level ------------------------------------------------------------------------

// Level data marks a boss: any monster whose thing TID (the "Tag" in the editor) is in BG_BOSS_TID_MIN..MAX is a boss,
// whatever its type. The Mecha, the Giant beetle and anything with MF2_BOSS in its definition are bosses without it.
dboolean bg_IsBoss(const mobj_t* mo)
{
  if (mo->tid >= BG_BOSS_TID_MIN && mo->tid <= BG_BOSS_TID_MAX)
    return true;
  return mo->type == MT_MECHA || mo->type == MT_BEETLE || (mobjinfo[mo->type].flags2 & MF2_BOSS);
}

void bg_CoopBossDied(mobj_t* boss)
{
  if (bg_IsBoss(boss))
    lprintf(LO_INFO, "BGBOSS: a boss died (monster type %d, TID %d)%s\n", (int) boss->type, (int) boss->tid,
            bg_TicketsOn() ? ": a ticket for each player" : " (no tickets on this difficulty)");
  if (bg_TicketsOn() && bg_IsBoss(boss))
    bg_TicketsAdd(PlayersInGame());
}

void bg_CoopCheckpoint(struct line_s* line)
{
  if (!bg_TicketsOn())
    return;                                    // difficulty 1-3: nothing happens
  bg_TicketsAdd(PlayersInGame());
  if (line)
    line->special = 0;                         // one use per line, so it cannot be farmed
}

// ---- death screen and spectating ----------------------------------------------------------------------

extern void bg_WriteText(int x, int y, const char* string, int cm);

int bg_CoopDeadTics(int n)
{
  return n >= 0 && n < MAX_MAXPLAYERS ? dead_tics[n] : 0;
}

// A machine spectates when every player on it is dead and has been for the length of the death screen. It then
// watches the living players of the other machines (up to four views, fewer as they die). Games played on one
// machine never spectate: everyone is already looking at the same screen.
int bg_SpectateList(int* slots)
{
  int first, count, i, n = 0, local = 0;

  if (!bg_weapons || deathmatch || demoplayback || gamestate != GS_LEVEL || !bgnet_InGame())
    return 0;
  first = bgnet_LocalFirstSlot();
  count = bgnet_LocalCount();
  if (bgnet_IsObserver())
  {
    // watching a game he has not joined yet: everybody who is alive
    for (i = 0; i < MAX_MAXPLAYERS && n < 4; ++i)
      if (playeringame[i] && players[i].playerstate == PST_LIVE && players[i].mo && players[i].mo->health > 0)
        slots[n++] = i;
    return n;
  }
  for (i = first; i < first + count && i < MAX_MAXPLAYERS; ++i)
    if (playeringame[i])
    {
      ++local;
      if (players[i].playerstate != PST_DEAD || dead_tics[i] < BG_DEATH_SCREEN_TICS)
        return 0;
    }
  if (!local)
    return 0;
  for (i = 0; i < MAX_MAXPLAYERS && n < 4; ++i)
    if (playeringame[i] && (i < first || i >= first + count) && players[i].playerstate == PST_LIVE &&
        players[i].mo && players[i].mo->health > 0)
      slots[n++] = i;
  return n;
}

void bg_SpectateOverlay(void)
{
  int slots[4], n, me = consoleplayer, y = 16;
  const char* line1 = NULL;
  const char* line2 = NULL;
  char buf[64];

  if (gamestate == GS_LEVEL && bg_weapons && bgnet_IsObserver())
  {
    int cls = bgnet_ObserverClass();

    snprintf(buf, sizeof buf, "PRESS USE TO JOIN AS %s", bg_ClassName(cls));
    for (n = 0; buf[n]; ++n)
      if (buf[n] >= 'a' && buf[n] <= 'z')
        buf[n] = (char) (buf[n] - 32);
    line1 = "SPECTATING";
    line2 = bgnet_ObserverJoining() ? "JOINING..." : buf;
    bg_WriteText(160 - (int) strlen(line1) * 4, y, line1, CR_RED);
    bg_WriteText(160 - (int) strlen(line2) * 4, y + 10, line2, CR_GOLD);
    if (!bgnet_ObserverJoining())
      bg_WriteText(160 - 26 * 4, y + 20, "STRAFE TO CHANGE CHARACTER", CR_GRAY);
    return;
  }
  if (gamestate != GS_LEVEL || !bg_weapons || !bg_CoopOn() || bg_SplitActive() || me < 0 || me >= MAX_MAXPLAYERS ||
      players[me].playerstate != PST_DEAD)
    return;
  n = bg_SpectateList(slots);
  line1 = n ? "SPECTATING" : "YOU DIED";
  if (bg_TicketsOn() && tickets <= 0)
    line2 = "NO TICKETS - WAIT FOR A CHECKPOINT, A BOSS OR THE NEXT LEVEL";
  else if (bg_TicketsOn())
  {
    snprintf(buf, sizeof buf, "PRESS USE TO RESPAWN (%d TICKETS)", tickets);
    line2 = buf;
  }
  else
    line2 = "PRESS USE TO RESPAWN";
  bg_WriteText(160 - (int) strlen(line1) * 4, y, line1, CR_RED);
  bg_WriteText(160 - (int) strlen(line2) * 4, y + 10, line2, CR_GOLD);
}

// Everyone dead with no tickets left: the level starts again, everyone from scratch.
void bg_CoopTick(void)
{
  int i, alive = 0, dead = 0;
  int spec[4], watching = bg_SpectateList(spec);
  static dboolean was_watching;

  for (i = 0; i < MAX_MAXPLAYERS; ++i)
    dead_tics[i] = playeringame[i] && players[i].playerstate == PST_DEAD ? dead_tics[i] + 1 : 0;
  // what this machine looks at: the first player being watched, or its own player again
  if (watching)
  {
    displayplayer = spec[0];
    was_watching = true;
  }
  else if (was_watching)
  {
    displayplayer = consoleplayer;
    was_watching = false;
  }

  if (!bg_TicketsOn() || tickets > 0 || gamestate != GS_LEVEL || gameaction != ga_nothing)
    return;
  for (i = 0; i < MAX_MAXPLAYERS; ++i)
    if (playeringame[i])
    {
      if (players[i].playerstate == PST_LIVE)
        ++alive;
      else
        ++dead;
    }
  if (alive || !dead)
    return;
  wiping = true;
  memset(free_respawn, 0, sizeof free_respawn);
  doom_printf("EVERYONE FELL - THE LEVEL STARTS OVER");
  gameaction = ga_loadlevel;
}
