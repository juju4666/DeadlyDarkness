//
// Begotten: co-op rules (tickets, rucksacks, respawn). Licensed under the GNU GPL v2 or later.
//
// Difficulty 1-3: a dead player respawns with the inventory, minus active power-ups; deaths are unlimited.
// Difficulty 4-5: a shared pool of tickets, one spent per respawn; the dead player's ammo is left in a rucksack
//   (difficulty 4: only its owner can take it, difficulty 5: anyone). Everyone dead with no tickets = the level
//   starts again from scratch.
//

#ifndef __BGCOOP__
#define __BGCOOP__

#include "d_player.h"
#include "p_mobj.h"

#define BG_MAX_TICKETS 999

dboolean bg_CoopOn(void);              // a co-op game with the Begotten rules
dboolean bg_TicketsOn(void);           // ... and difficulty 4 or 5, where respawning costs tickets
int bg_Tickets(void);                  // the pool
int bg_TicketDisplay(void);            // what the HUD shows, -1 = show nothing
void bg_TicketsAdd(int n);
void bg_TicketsSet(int n);
void bg_CoopNewGame(void);             // a new game starts: empty pool
void bg_CoopNewLevel(void);            // a level has just been set up: one ticket per player
void bg_CoopArchive(void);             // save / load
void bg_CoopUnArchive(void);
unsigned bg_CoopHash(void);

dboolean bg_CoopRespawn(player_t* player);       // the player pressed Use while dead: allowed? (spends a ticket)
void bg_CoopFreeRespawn(int playernum);          // dead at the end of a level: respawn at the next without cost
dboolean bg_CoopReborn(player_t* p, const player_t* old, int playernum);  // after a death-respawn: carry things over
dboolean bg_CoopWiping(void);
void bg_CoopPlayerDied(player_t* player);        // drops the rucksack
void bg_CoopBossDied(mobj_t* boss);
#define BG_BOSS_TID_MIN 30000   // thing TIDs 30000-30999 mark a monster as a boss (Hexen-format and UDMF maps)
#define BG_BOSS_TID_MAX 30999
dboolean bg_IsBoss(const mobj_t* mo);
void bg_CoopCheckpoint(struct line_s* line);     // a player crossed a checkpoint line in a co-op game
void bg_CoopTick(void);                          // once per tic: everyone dead and out of tickets?

dboolean bg_RucksackTouch(mobj_t* pack, player_t* player);    // true = handled (taken or refused)
dboolean bg_RucksackHidden(const mobj_t* pack, const player_t* viewer);  // drawn translucent for this viewer

// ---- death screen and spectating (network games) ----
#define BG_DEATH_SCREEN_TICS (5 * 35)           // how long the cadaver view stays before forced spectating
int bg_CoopDeadTics(int playernum);             // tics this player has been dead (0 when alive)
int bg_SpectateList(int* slots);                // the players this machine must watch (0 = not spectating); room for 4
void bg_SpectateOverlay(void);                  // the text on a death screen / spectator view
void bg_CoopJoin(int n, int cls);                // a player enters a running network game (the same tic everywhere)
dboolean bg_CoopCompletionCounts(void);          // does finishing this level count for this machine's players?
dboolean bg_CoopJoinedLate(int n);
void bg_CoopSelfTest(int* failures);

#endif
