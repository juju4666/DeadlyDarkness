//
// Begotten: local split-screen.  See bgsplit.h.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "doomstat.h"
#include "d_player.h"
#include "d_items.h"
#include "d_event.h"
#include "d_ticcmd.h"
#include "g_game.h"
#include "lprintf.h"
#include "m_menu.h"
#include "hu_stuff.h"
#include "dsda/pause.h"
#include "p_mobj.h"
#include "v_video.h"

#include "dsda/begotten.h"
#include "dsda/bgclass.h"
#include "dsda/bgnet.h"
#include "dsda/bgcoop.h"
#include "dsda/bgsplit.h"
#include "dsda/configuration.h"
#include "dsda/excmd.h"
#include "dsda/game_controller.h"

#include "bgsplit.h"

#define BG_SPLIT_MAX 4

void bg_WriteText(int x, int y, const char* string, int cm);

extern int solo_net;
dboolean bg_made_netgame; // netgame was set here (local players), not by a real network game

//
// layout
//

static int bg_ActivePlayers(void)
{
  int i, n = 0;

  for (i = 0; i < BG_SPLIT_MAX; ++i)
    if (playeringame[i])
      ++n;
  return n;
}

// a game of local players only: no network, so menus pause it and it can be ended
// the local players are the slots first .. first + count - 1 (all of them outside network games)
int bg_SplitCount(void)
{
  return bgnet_InGame() ? bgnet_LocalCount() : bg_ActivePlayers();
}

int bg_SplitFirst(void)
{
  return bgnet_InGame() ? bgnet_LocalFirstSlot() : 0;
}

dboolean bg_SplitLocalGame(void)
{
  return bg_made_netgame && netgame;
}

// the first controller belongs to another local player than player 1 (setting "controller 1 drives")
int bg_SplitPadPlayer(void)
{
  int r = dsda_IntConfig(dsda_config_begotten_pad_player) - 1;

  if (r < 1 || r >= BG_SPLIT_MAX || !bg_SplitActive() || r >= bg_SplitCount())
    return 0;
  return r;
}

dboolean bg_SplitPadRoutedAway(void)
{
  return bg_SplitPadPlayer() && !menuactive;
}

dboolean bg_SplitActive(void)
{
  return bg_weapons && !demoplayback && gamestate == GS_LEVEL && bg_SplitCount() > 1;
}

int bg_SplitViews(void)
{
  int slots[4], n = bg_SpectateList(slots);

  if (n)
    return n;
  return bg_SplitActive() ? bg_SplitCount() : 1;
}

// the views drawn this frame: one per local player in a split-screen game, or the players a dead machine is watching
int bg_ViewCount(void)
{
  int slots[4], n = bg_SpectateList(slots);

  if (n)
    return n;
  return bg_SplitActive() ? bg_SplitCount() : 0;
}

int bg_ViewSlot(int vi)
{
  int slots[4], n = bg_SpectateList(slots);

  if (n)
    return slots[vi < n ? vi : 0];
  return bg_SplitFirst() + vi;
}

// 2 players: one above the other (the default, the screen is wide) or side by side (setting);
// 3: one on top, two below; 4: 2 x 2.  v is the view number: 0 for the first local player.
void bg_SplitRect(int v, int* x, int* y, int* w, int* h)
{
  int n = bg_SplitViews();
  int W = SCREENWIDTH, H = SCREENHEIGHT;

  if (n <= 1)
  {
    *x = 0; *y = 0; *w = W; *h = H;
    return;
  }
  if (n == 2)
  {
    if (dsda_IntConfig(dsda_config_begotten_split_layout) == 1)
    {
      *y = 0; *h = H; *w = W / 2; *x = v ? W - *w : 0;
    }
    else
    {
      *x = 0; *w = W; *h = H / 2; *y = v ? H - *h : 0;
    }
    return;
  }
  if (n == 3)
  {
    if (v == 0)
    {
      *x = 0; *y = 0; *w = W; *h = H / 2;
    }
    else
    {
      *w = W / 2; *h = H - H / 2; *y = H / 2; *x = v == 1 ? 0 : W - *w;
    }
    return;
  }
  *w = W / 2; *h = H / 2;
  *x = (v & 1) ? W - *w : 0;
  *y = (v & 2) ? H - *h : 0;
}

//
// new game
//

int bg_SplitClassFor(int player)
{
  switch (player)
  {
    case 1: return dsda_IntConfig(dsda_config_begotten_class_p2);
    case 2: return dsda_IntConfig(dsda_config_begotten_class_p3);
    case 3: return dsda_IntConfig(dsda_config_begotten_class_p4);
  }
  return bg_player_class[0];
}

// set by the menus: 1 = single player, 2-4 = split-screen; 0 = use the configuration
int bg_split_request;

void bg_SplitNewGame(void)
{
  int i, n;

  if (bgnet_PendingApply())
  {
    bgnet_ApplyNewGame();
    return;
  }
  if (bgnet_InGame())
    return;                // a running network game keeps its players (a snapshot of it is being put in, say)
  if (!bg_weapons || demoplayback || demorecording)
    return;

  // The saved player count is only for the menus.  A game started from the command line (-warp, UDB's test button)
  // used to pick it up and open in split screen; now it is a single player game unless BEGOTTEN_LOCALPLAYERS says otherwise.
  n = bg_split_request > 0 ? bg_split_request : 1;
  if (getenv("BEGOTTEN_LOCALPLAYERS") && atoi(getenv("BEGOTTEN_LOCALPLAYERS")) > 0)
    n = atoi(getenv("BEGOTTEN_LOCALPLAYERS"));     // developer: a split-screen game without the menus
  if (n < 1) n = 1;
  if (n > BG_SPLIT_MAX) n = BG_SPLIT_MAX;
  if (n > 1 && (netgame && !bg_made_netgame && !solo_net))
    return; // real network games decide their own players

  for (i = 0; i < MAX_MAXPLAYERS; ++i)
    playeringame[i] = i < n;
  for (i = 1; i < n; ++i)
  {
    int cls = bg_SplitClassFor(i);

    bg_player_class[i] = bg_ClassAvailable(cls) ? cls : 0;
  }
  bg_made_netgame = n > 1;
  netgame = n > 1;   // co-op rules: co-op things, class flags of everybody, no pause cheats
  deathmatch = 0;
  consoleplayer = displayplayer = 0;
}

//
// controllers for players 2-4
//

typedef struct
{
  SDL_GameController* pad;
  dboolean is_first; // the pad is the one that normally belongs to player 1
  dboolean prev_next, prev_prev;
} bg_pad_t;

static bg_pad_t pads[BG_SPLIT_MAX];
static int last_scan = -1000;

static void bg_ScanPads(void)
{
  int i, p, n = SDL_NumJoysticks();
  int first_id = dsda_GameControllerInstanceId();
  int routed = bg_SplitPadPlayer();

  for (p = 1; p < BG_SPLIT_MAX; ++p)
  {
    if (pads[p].pad && (!SDL_GameControllerGetAttached(pads[p].pad) || (p == routed) != pads[p].is_first))
    {
      SDL_GameControllerClose(pads[p].pad);
      pads[p].pad = NULL;
    }
  }

  // the first controller drives the routed player instead of player 1
  if (routed && !pads[routed].pad && first_id >= 0)
    for (i = 0; i < n; ++i)
      if (SDL_IsGameController(i) && SDL_JoystickGetDeviceInstanceID(i) == first_id)
      {
        pads[routed].pad = SDL_GameControllerOpen(i);
        pads[routed].is_first = true;
        break;
      }

  for (i = 0; i < n; ++i)
  {
    SDL_JoystickID id;
    int taken = 0;

    if (!SDL_IsGameController(i))
      continue;
    id = SDL_JoystickGetDeviceInstanceID(i);
    if (id == first_id)
      continue;
    for (p = 1; p < BG_SPLIT_MAX; ++p)
      if (pads[p].pad && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[p].pad)) == id)
        taken = 1;
    if (taken)
      continue;
    for (p = 1; p < BG_SPLIT_MAX; ++p)
      if (!pads[p].pad && p != routed)
      {
        pads[p].pad = SDL_GameControllerOpen(i);
        pads[p].is_first = false;
        break;
      }
  }
}

static int bg_Axis(SDL_GameController* pad, SDL_GameControllerAxis axis)
{
  int v = SDL_GameControllerGetAxis(pad, axis);

  if (v > -8000 && v < 8000)
    return 0;
  return v;
}

// k: the local player's number on this machine (1-3; 0 uses the keyboard and is not built here)
void bg_SplitBuildCmd(int k, ticcmd_t* cmd)
{
  bg_pad_t* b;
  SDL_GameController* pad;
  int ax, ay, rx;
  int player = bg_SplitFirst() + k;

  memset(cmd, 0, sizeof(*cmd));
  if (k < 1 || k >= BG_SPLIT_MAX || player >= MAX_MAXPLAYERS || !playeringame[player] || menuactive)
    return;

  if (gametic - last_scan > 70 || gametic < last_scan)
  {
    last_scan = gametic;
    bg_ScanPads();
  }

  b = &pads[k];
  pad = b->pad;
  if (!pad)
    return;

  ax = bg_Axis(pad, SDL_CONTROLLER_AXIS_LEFTX);
  ay = bg_Axis(pad, SDL_CONTROLLER_AXIS_LEFTY);
  rx = bg_Axis(pad, SDL_CONTROLLER_AXIS_RIGHTX);

  cmd->forwardmove = (signed char) (-ay * 50 / 32768);
  cmd->sidemove = (signed char) (ax * 40 / 32768);
  cmd->angleturn = (short) (-rx * 1100 / 32768);

  if (bg_Axis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 8000)
    cmd->buttons |= BT_ATTACK;
  if (bg_Axis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 8000)
  {
    cmd->buttons |= BT_ATTACK;
    cmd->ex.actions |= XC_ALT;
  }
  if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A) ||
      SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_X))
    cmd->buttons |= BT_USE;
  if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B))
    cmd->ex.actions |= XC_JUMP;

  {
    dboolean next = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    dboolean prev = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    int dir = (next && !b->prev_next) ? 1 : (prev && !b->prev_prev) ? -1 : 0;

    b->prev_next = next;
    b->prev_prev = prev;
    if (dir)
    {
      weapontype_t w = bg_NextWeapon(&players[player], dir);

      cmd->buttons |= BT_CHANGE | (w << BT_WEAPONSHIFT);
    }
  }
}

//
// controller setup screen: who plays with what
//

int bg_SplitPadCount(void)
{
  int i, n = 0;

  if (!SDL_WasInit(SDL_INIT_GAMECONTROLLER))
    return 0;
  for (i = 0; i < SDL_NumJoysticks(); ++i)
    if (SDL_IsGameController(i))
      ++n;
  return n;
}

// "KEYBOARD + MOUSE", "CONTROLLER 2 (NOT FOUND)"... for local player k of `players`, given which player
// the first controller drives (1-4)
void bg_SplitDeviceLabel(int k, int players, int first_pad_player, char* out, int size)
{
  int routed = first_pad_player - 1, next = 2, i, found = bg_SplitPadCount();

  if (routed < 1 || routed >= players)
    routed = 0;
  if (k == 0)
  {
    snprintf(out, size, routed ? "KEYBOARD + MOUSE" : "KEYBOARD + CONTROLLER 1");
    return;
  }
  if (k == routed)
    i = 1;
  else
  {
    int j;

    for (j = 1; j < k; ++j)
      if (j != routed)
        ++next;
    i = next;
  }
  snprintf(out, size, "CONTROLLER %d%s", i, i > found ? " (NOT FOUND)" : "");
}

//
// status text
//

void bg_SplitDrawHud(int v)
{
  int x, y, w, h;
  int slot = bg_ViewSlot(v);
  player_t* p;
  char buf[96], who[16];
  int vx, vy;

  if (slot >= MAX_MAXPLAYERS || !playeringame[slot])
    return;
  p = &players[slot];
  if (!p->mo)
    return;
  bg_SplitRect(v, &x, &y, &w, &h);

  // every view has its own crosshair in the middle of it
  if (HU_CrosshairEnabled() && !automap_full && !menuactive && !dsda_Paused() && p->playerstate != PST_DEAD)
    HU_DrawCrosshairAt(x + w / 2, y + h / 2, p->health);

  // test aid (BEGOTTEN_TESTFLASH=1): the first local player is always flashing red
  if (v == 0 && getenv("BEGOTTEN_TESTFLASH"))
    p->damagecount = 40;

  // the player's name when there is one
  if (bgnet_InGame())
    snprintf(who, sizeof who, "%.10s", bgnet_PlayerName(slot));
  else
  {
    const char* nm = slot == 0 ? dsda_StringConfig(dsda_config_begotten_player_name)
                               : slot <= 3 ? dsda_StringConfig(dsda_config_begotten_player_name_p2 + slot - 1) : NULL;

    if (nm && nm[0])
      snprintf(who, sizeof who, "%.10s", nm);
    else
      snprintf(who, sizeof who, "P%d", slot + 1);
  }

  // narrow views (three or four players) have no room for the name, and the ticket number needs a place too
  {
    dboolean narrow = (w * 320 / SCREENWIDTH) < 200;

    if (p->playerstate == PST_DEAD)
    {
      if (narrow)
        snprintf(buf, sizeof buf, "DEAD - USE");
      else
        snprintf(buf, sizeof buf, "%s  DEAD - PRESS USE", who);
    }
    else
    {
      int ammo_type = doom_weaponinfo[p->readyweapon].ammo;
      int n = narrow ? snprintf(buf, sizeof buf, "HP%d SH%d", p->health, p->armorpoints[ARMOR_ARMOR])
                     : snprintf(buf, sizeof buf, "%s  HP %d  SH %d", who, p->health, p->armorpoints[ARMOR_ARMOR]);

      if (ammo_type >= 0 && ammo_type < NUMAMMO)
        snprintf(buf + n, sizeof buf - n, narrow ? " AM%d" : "  AM %d", p->ammo[ammo_type]);
    }
    if (v == 0 && bg_TicketDisplay() >= 0)
    {
      int room = (w * 320 / SCREENWIDTH - 12) / 8 - 2 - (bg_TicketDisplay() > 99 ? 3 : bg_TicketDisplay() > 9 ? 2 : 1);

      if (room > 0 && (int) strlen(buf) > room)
        buf[room] = '\0';
    }
  }

  // a dark strip along the bottom of the view keeps the text readable on any picture
  {
    int strip = 14 * SCREENHEIGHT / 200;

    V_FillRect(0, x, y + h - strip, w, strip, 0);
  }

  // 320x200 virtual coordinates; the text sits fully inside the view
  vx = (x + 6) * 320 / SCREENWIDTH;
  vy = (y + h) * 200 / SCREENHEIGHT - 12;
  bg_WriteText(vx, vy, buf, slot == 0 ? CR_GREEN : slot == 1 ? CR_BLUE : slot == 2 ? CR_BROWN : CR_RED);

  // the ticket pool is shown once, at the bottom right of the first view (the middle of the screen)
  if (v == 0 && bg_TicketDisplay() >= 0)
  {
    char t[8];

    snprintf(t, sizeof t, "%d", bg_TicketDisplay());
    bg_WriteText((x + w) * 320 / SCREENWIDTH - 6 - (int) strlen(t) * 8, vy, t, CR_GOLD);
  }
}

static int split_failures;
#define SCHECK(c, msg) do { if (c) lprintf(LO_INFO, "ok:   %s\n", msg); \
  else { lprintf(LO_INFO, "FAIL: %s\n", msg); ++split_failures; } } while (0)

void dsda_BgSplitSelfTest(void)
{
  dboolean in_saved[MAX_MAXPLAYERS];
  int cls_saved[MAX_MAXPLAYERS];
  gamestate_t gs_saved = gamestate;
  dboolean net_saved = netgame, made_saved = bg_made_netgame;
  int n, i, j, count_saved = dsda_IntConfig(dsda_config_begotten_local_players);
  dboolean weapons_saved = bg_weapons;

  memcpy(in_saved, playeringame, sizeof in_saved);
  memcpy(cls_saved, bg_player_class, sizeof cls_saved);
  bg_weapons = true;
  gamestate = GS_LEVEL;

  for (n = 2; n <= 4; ++n)
  {
    long area = 0;
    int ok_inside = 1, ok_apart = 1;
    char msg[96];

    for (i = 0; i < MAX_MAXPLAYERS; ++i)
      playeringame[i] = i < n;
    SCHECK(bg_SplitViews() == n, "split: the number of views is the number of players");

    for (i = 0; i < n; ++i)
    {
      int x, y, w, h;

      bg_SplitRect(i, &x, &y, &w, &h);
      area += (long) w * h;
      if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > SCREENWIDTH || y + h > SCREENHEIGHT)
        ok_inside = 0;
      for (j = i + 1; j < n; ++j)
      {
        int x2, y2, w2, h2;

        bg_SplitRect(j, &x2, &y2, &w2, &h2);
        if (x < x2 + w2 && x2 < x + w && y < y2 + h2 && y2 < y + h)
          ok_apart = 0;
      }
    }
    snprintf(msg, sizeof msg, "split %d players: views stay on the screen", n);
    SCHECK(ok_inside, msg);
    snprintf(msg, sizeof msg, "split %d players: views do not overlap", n);
    SCHECK(ok_apart, msg);
    snprintf(msg, sizeof msg, "split %d players: views fill the screen", n);
    SCHECK(area == (long) SCREENWIDTH * SCREENHEIGHT, msg);
  }

  // two players, side by side (setting)
  {
    int x0, y0, w0, h0, x1, y1, w1, h1, layout_saved = dsda_IntConfig(dsda_config_begotten_split_layout);

    for (i = 0; i < MAX_MAXPLAYERS; ++i)
      playeringame[i] = i < 2;
    dsda_UpdateIntConfig(dsda_config_begotten_split_layout, 1, false);
    bg_SplitRect(0, &x0, &y0, &w0, &h0);
    bg_SplitRect(1, &x1, &y1, &w1, &h1);
    SCHECK(x0 == 0 && y0 == 0 && h0 == SCREENHEIGHT && h1 == SCREENHEIGHT && x1 == w0 && w0 + w1 == SCREENWIDTH,
           "split: vertical layout puts two players side by side");
    dsda_UpdateIntConfig(dsda_config_begotten_split_layout, 0, false);
    bg_SplitRect(1, &x1, &y1, &w1, &h1);
    SCHECK(x1 == 0 && y1 > 0 && w1 == SCREENWIDTH, "split: horizontal layout puts two players one above the other");
    dsda_UpdateIntConfig(dsda_config_begotten_split_layout, layout_saved, false);
  }

  // per-view flashes: a view tinted red turns its pixels reddish, the rest of the screen stays
  if (V_IsSoftwareMode() && screens[0].data && V_GetPlaypalCount() > 1)
  {
    const unsigned char* pp = V_GetColorPalette();
    pixel_t* p0 = screens[0].data;
    pixel_t* p1 = screens[0].data + 1;
    pixel_t keep0 = *p0, keep1 = *p1;
    int c = V_BestColor(pp, 120, 120, 120);
    int pal_red = 1, r, g, b;

    *p0 = (pixel_t) c;
    *p1 = (pixel_t) c;
    V_TintRect(0, 0, 1, 1, pal_red);
    r = pp[*p0 * 3]; g = pp[*p0 * 3 + 1]; b = pp[*p0 * 3 + 2];
    SCHECK(r > g + 20 && r > b + 20, "split: a view with a damage flash looks red");
    SCHECK(*p1 == (pixel_t) c, "split: the next view is not tinted");
    *p0 = keep0;
    *p1 = keep1;
  }

  // new game: players are created from the configuration
  netgame = false;
  bg_made_netgame = false;
  dsda_UpdateIntConfig(dsda_config_begotten_local_players, 3, false);
  bg_SplitNewGame();
  SCHECK(playeringame[0] && playeringame[1] && playeringame[2] && !playeringame[3], "split: three local players");
  SCHECK(netgame, "split: local co-op runs as a co-op game");
  SCHECK(bg_player_class[1] == bg_SplitClassFor(1), "split: player 2 gets the class chosen for player 2");
  dsda_UpdateIntConfig(dsda_config_begotten_local_players, 1, false);
  bg_SplitNewGame();
  SCHECK(playeringame[0] && !playeringame[1] && !netgame, "split: back to one player");

  // the first controller can be given to another local player (for testing with one controller)
  {
    int pad_saved = dsda_IntConfig(dsda_config_begotten_pad_player), menu_saved = menuactive;

    for (i = 0; i < MAX_MAXPLAYERS; ++i)
      playeringame[i] = i < 2;
    menuactive = 0;
    dsda_UpdateIntConfig(dsda_config_begotten_pad_player, 1, false);
    SCHECK(bg_SplitPadPlayer() == 0 && !bg_SplitPadRoutedAway(), "split: the first controller drives player 1 by default");
    dsda_UpdateIntConfig(dsda_config_begotten_pad_player, 2, false);
    SCHECK(bg_SplitPadPlayer() == 1 && bg_SplitPadRoutedAway(), "split: the first controller can drive player 2");
    menuactive = 1;
    SCHECK(!bg_SplitPadRoutedAway(), "split: the menu still uses the controller");
    menuactive = 0;
    dsda_UpdateIntConfig(dsda_config_begotten_pad_player, 3, false);
    SCHECK(bg_SplitPadPlayer() == 0, "split: a player who is not in the game gets no controller");
    dsda_UpdateIntConfig(dsda_config_begotten_pad_player, pad_saved, false);
    menuactive = menu_saved;
  }

  // a controller button press must not reach a player that is not in the game
  {
    ticcmd_t cmd;

    memset(&cmd, 1, sizeof cmd);
    bg_SplitBuildCmd(2, &cmd);
    SCHECK(cmd.forwardmove == 0 && cmd.buttons == 0, "split: no input for a player who is not in the game");
  }

#if SDL_VERSION_ATLEAST(2, 0, 14)
  // a virtual game controller drives player 2
  if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) == 0)
  {
    int vid = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, SDL_CONTROLLER_AXIS_MAX,
                                        SDL_CONTROLLER_BUTTON_MAX, 0);

    if (vid >= 0)
    {
      SDL_Joystick* joy = SDL_JoystickOpen(vid);
      ticcmd_t cmd;
      int k;

      playeringame[0] = playeringame[1] = true;
      menuactive = 0;
      last_scan = -1000;
      gametic = 100;
      SDL_JoystickUpdate();
      SDL_JoystickSetVirtualAxis(joy, SDL_CONTROLLER_AXIS_LEFTY, -32767);   // stick up = forward
      SDL_JoystickSetVirtualAxis(joy, SDL_CONTROLLER_AXIS_LEFTX, 32767);    // right = strafe right
      SDL_JoystickSetVirtualAxis(joy, SDL_CONTROLLER_AXIS_RIGHTX, -32767);  // turn left
      SDL_JoystickSetVirtualAxis(joy, SDL_CONTROLLER_AXIS_TRIGGERLEFT, 32767);
      SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_A, 1);
      SDL_JoystickUpdate();
      for (k = 0; k < 3; ++k)
      {
        SDL_PumpEvents();
        SDL_JoystickUpdate();
        bg_SplitBuildCmd(1, &cmd);
      }
      SCHECK(cmd.forwardmove > 40, "split: stick up walks player 2 forward");
      SCHECK(cmd.sidemove > 30, "split: stick right strafes player 2");
      SCHECK(cmd.angleturn > 0, "split: right stick turns player 2");
      SCHECK((cmd.buttons & BT_ATTACK) && (cmd.ex.actions & XC_ALT), "split: left trigger is alt fire");
      SCHECK(cmd.buttons & BT_USE, "split: A uses");
      bg_SplitBuildCmd(0, &cmd);
      SCHECK(cmd.forwardmove == 0 && cmd.buttons == 0, "split: player 1 is not read here");
      for (k = 1; k < BG_SPLIT_MAX; ++k)
        if (pads[k].pad)
        {
          SDL_GameControllerClose(pads[k].pad);
          pads[k].pad = NULL;
        }
      SDL_JoystickClose(joy);
      SDL_JoystickDetachVirtual(vid);
    }
  }
#endif

  dsda_UpdateIntConfig(dsda_config_begotten_local_players, count_saved, false);
  memcpy(playeringame, in_saved, sizeof in_saved);
  memcpy(bg_player_class, cls_saved, sizeof cls_saved);
  gamestate = gs_saved;
  netgame = net_saved;
  bg_made_netgame = made_saved;
  bg_weapons = weapons_saved;

  lprintf(LO_INFO, "BEGOTTEN SELFTEST (split-screen): %s (%d failures)\n",
          split_failures ? "FAILED" : "PASSED", split_failures);
  if (split_failures)
    exit(1);
}
