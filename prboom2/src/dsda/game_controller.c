//
// Copyright(C) 2022 by Ryan Krafnick
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//	DSDA Game Controller
//

#include "SDL.h"

#include "d_event.h"
#include "d_main.h"
#include "doomstat.h"
#include "lprintf.h"

#include "dsda/args.h"
#include "dsda/configuration.h"

#include "game_controller.h"

static int use_game_controller;
static SDL_GameController* game_controller;
static SDL_JoystickID game_controller_id = -1;

// Left stick drives menu navigation (as a virtual d-pad) when no level is being played / a menu is open
#define MENU_STICK_THRESHOLD 16000

typedef struct {
  SDL_GameControllerAxis axis;
  int deadzone;
  int sensitivity;
} axis_t;

static axis_t left_analog_x = { SDL_CONTROLLER_AXIS_LEFTX };
static axis_t left_analog_y = { SDL_CONTROLLER_AXIS_LEFTY };
static axis_t right_analog_x = { SDL_CONTROLLER_AXIS_RIGHTX };
static axis_t right_analog_y = { SDL_CONTROLLER_AXIS_RIGHTY };
static axis_t left_trigger = { SDL_CONTROLLER_AXIS_TRIGGERLEFT };
static axis_t right_trigger = { SDL_CONTROLLER_AXIS_TRIGGERRIGHT };

static int swap_analogs;

static const char* button_names[] = {
  [DSDA_CONTROLLER_BUTTON_A] = "pad a",
  [DSDA_CONTROLLER_BUTTON_B] = "pad b",
  [DSDA_CONTROLLER_BUTTON_X] = "pad x",
  [DSDA_CONTROLLER_BUTTON_Y] = "pad y",
  [DSDA_CONTROLLER_BUTTON_BACK] = "pad back",
  [DSDA_CONTROLLER_BUTTON_GUIDE] = "pad guide",
  [DSDA_CONTROLLER_BUTTON_START] = "pad start",
  [DSDA_CONTROLLER_BUTTON_LEFTSTICK] = "lstick",
  [DSDA_CONTROLLER_BUTTON_RIGHTSTICK] = "rstick",
  [DSDA_CONTROLLER_BUTTON_LEFTSHOULDER] = "pad l",
  [DSDA_CONTROLLER_BUTTON_RIGHTSHOULDER] = "pad r",
  [DSDA_CONTROLLER_BUTTON_DPAD_UP] = "dpad u",
  [DSDA_CONTROLLER_BUTTON_DPAD_DOWN] = "dpad d",
  [DSDA_CONTROLLER_BUTTON_DPAD_LEFT] = "dpad l",
  [DSDA_CONTROLLER_BUTTON_DPAD_RIGHT] = "dpad r",
  [DSDA_CONTROLLER_BUTTON_MISC1] = "misc 1",
  [DSDA_CONTROLLER_BUTTON_PADDLE1] = "paddle 1",
  [DSDA_CONTROLLER_BUTTON_PADDLE2] = "paddle 2",
  [DSDA_CONTROLLER_BUTTON_PADDLE3] = "paddle 3",
  [DSDA_CONTROLLER_BUTTON_PADDLE4] = "paddle 4",
  [DSDA_CONTROLLER_BUTTON_TOUCHPAD] = "touchpad",
  [DSDA_CONTROLLER_BUTTON_TRIGGERLEFT] = "pad lt",
  [DSDA_CONTROLLER_BUTTON_TRIGGERRIGHT] = "pad rt",
};

const char* dsda_GameControllerButtonName(int button) {
  if (button >= sizeof(button_names) || !button_names[button])
    return "misc";

  return button_names[button];
}

static float dsda_AxisValue(axis_t* axis) {
  int value;

  value = SDL_GameControllerGetAxis(game_controller, axis->axis);

  // the positive axis max is 1 less
  if (value > (axis->deadzone - 1))
    value -= (axis->deadzone - 1);
  else if (value < -axis->deadzone)
    value += axis->deadzone;
  else
    value = 0;

  return (float) value * axis->sensitivity / (32768 - axis->deadzone);
}

static void dsda_PollLeftStick(void) {
  event_t ev;

  ev.type = swap_analogs ? ev_look_analog : ev_move_analog;
  ev.data1.f = dsda_AxisValue(&left_analog_x);
  ev.data2.f = -dsda_AxisValue(&left_analog_y);

  if (ev.data1.f || ev.data2.f)
    D_PostEvent(&ev);
}

static void dsda_PollRightStick(void) {
  event_t ev;

  ev.type = swap_analogs ? ev_move_analog : ev_look_analog;
  ev.data1.f = dsda_AxisValue(&right_analog_x);
  ev.data2.f = -dsda_AxisValue(&right_analog_y);

  if (ev.data1.f || ev.data2.f)
    D_PostEvent(&ev);
}

static inline int PollButton(dsda_game_controller_button_t button)
{
  // This depends on enums having same values
  return SDL_GameControllerGetButton(game_controller, (SDL_GameControllerButton) button) << button;
}

void dsda_PollGameControllerButtons(void) {
  event_t ev;
  float trigger;

  if (!game_controller)
    return;

  ev.type = ev_joystick;
  ev.data1.i = PollButton(DSDA_CONTROLLER_BUTTON_A) |
               PollButton(DSDA_CONTROLLER_BUTTON_B) |
               PollButton(DSDA_CONTROLLER_BUTTON_X) |
               PollButton(DSDA_CONTROLLER_BUTTON_Y) |
               PollButton(DSDA_CONTROLLER_BUTTON_BACK) |
               PollButton(DSDA_CONTROLLER_BUTTON_GUIDE) |
               PollButton(DSDA_CONTROLLER_BUTTON_START) |
               PollButton(DSDA_CONTROLLER_BUTTON_LEFTSTICK) |
               PollButton(DSDA_CONTROLLER_BUTTON_RIGHTSTICK) |
               PollButton(DSDA_CONTROLLER_BUTTON_LEFTSHOULDER) |
               PollButton(DSDA_CONTROLLER_BUTTON_RIGHTSHOULDER) |
               PollButton(DSDA_CONTROLLER_BUTTON_DPAD_UP) |
               PollButton(DSDA_CONTROLLER_BUTTON_DPAD_DOWN) |
               PollButton(DSDA_CONTROLLER_BUTTON_DPAD_LEFT) |
               PollButton(DSDA_CONTROLLER_BUTTON_DPAD_RIGHT) |
               PollButton(DSDA_CONTROLLER_BUTTON_MISC1) |
               PollButton(DSDA_CONTROLLER_BUTTON_PADDLE1) |
               PollButton(DSDA_CONTROLLER_BUTTON_PADDLE2) |
               PollButton(DSDA_CONTROLLER_BUTTON_PADDLE3) |
               PollButton(DSDA_CONTROLLER_BUTTON_PADDLE4) |
               PollButton(DSDA_CONTROLLER_BUTTON_TOUCHPAD);

  if (menuactive || gamestate != GS_LEVEL) {
    int lx = SDL_GameControllerGetAxis(game_controller, SDL_CONTROLLER_AXIS_LEFTX);
    int ly = SDL_GameControllerGetAxis(game_controller, SDL_CONTROLLER_AXIS_LEFTY);

    if (lx < -MENU_STICK_THRESHOLD) ev.data1.i |= (1 << DSDA_CONTROLLER_BUTTON_DPAD_LEFT);
    if (lx >  MENU_STICK_THRESHOLD) ev.data1.i |= (1 << DSDA_CONTROLLER_BUTTON_DPAD_RIGHT);
    if (ly < -MENU_STICK_THRESHOLD) ev.data1.i |= (1 << DSDA_CONTROLLER_BUTTON_DPAD_UP);
    if (ly >  MENU_STICK_THRESHOLD) ev.data1.i |= (1 << DSDA_CONTROLLER_BUTTON_DPAD_DOWN);
  }

  trigger = dsda_AxisValue(&left_trigger);
  if (trigger)
    ev.data1.i |= (1 << DSDA_CONTROLLER_BUTTON_TRIGGERLEFT);

  trigger = dsda_AxisValue(&right_trigger);
  if (trigger)
    ev.data1.i |= (1 << DSDA_CONTROLLER_BUTTON_TRIGGERRIGHT);

  // Menus eat their buttons: a button that was pressed while a menu (or any screen that is not the level) was up
  // stays invisible to the game until it is let go.  Without this, confirming "End Game" with A closed the menu
  // with A still held, and the game saw a fresh jump press.
  {
    static int held_in_menu;

    if (menuactive || gamestate != GS_LEVEL)
      held_in_menu |= ev.data1.i;
    else
    {
      held_in_menu &= ev.data1.i;
      ev.data1.i &= ~held_in_menu;
    }
  }

  D_PostEvent(&ev);
}

#ifdef BEGOTTEN_PADTEST
void dsda_PadSelfTestTick(void);
#endif

void dsda_PollGameController(void) {
#ifdef BEGOTTEN_PADTEST
  dsda_PadSelfTestTick();
#endif

  if (!game_controller)
    return;

  dsda_PollGameControllerButtons();
  dsda_PollLeftStick();
  dsda_PollRightStick();
}

void dsda_InitGameControllerParameters(void) {
  left_analog_x.deadzone = dsda_IntConfig(dsda_config_left_analog_deadzone);
  left_analog_x.sensitivity = dsda_IntConfig(dsda_config_left_analog_sensitivity_x);
  left_analog_y.deadzone = left_analog_x.deadzone;
  left_analog_y.sensitivity = dsda_IntConfig(dsda_config_left_analog_sensitivity_y);

  right_analog_x.deadzone = dsda_IntConfig(dsda_config_right_analog_deadzone);
  right_analog_x.sensitivity = dsda_IntConfig(dsda_config_right_analog_sensitivity_x);
  right_analog_y.deadzone = right_analog_x.deadzone;
  right_analog_y.sensitivity = dsda_IntConfig(dsda_config_right_analog_sensitivity_y);

  left_trigger.deadzone = dsda_IntConfig(dsda_config_left_trigger_deadzone);
  left_trigger.sensitivity = 1;
  right_trigger.deadzone = dsda_IntConfig(dsda_config_right_trigger_deadzone);
  right_trigger.sensitivity = 1;

  swap_analogs = dsda_IntConfig(dsda_config_swap_analogs);
}

static void dsda_CloseGameController(void) {
  if (game_controller) {
    lprintf(LO_INFO, "Closed game controller %s\n", SDL_GameControllerName(game_controller));
    SDL_GameControllerClose(game_controller);
  }

  game_controller = NULL;
  game_controller_id = -1;
}

// Open the Nth (1-based, per use_game_controller) connected game controller, or the first one if
// fewer are connected. Does nothing if one is already open or the feature is off.
static void dsda_OpenGameController(void) {
  int i, count = 0, pick, num_joysticks;

  if (!use_game_controller || game_controller)
    return;

  num_joysticks = SDL_NumJoysticks();

  for (i = 0; i < num_joysticks; ++i)
    if (SDL_IsGameController(i))
      ++count;

  if (!count) {
    lprintf(LO_DEBUG, "dsda_InitGameController: no game controller connected (yet)\n");
    return;
  }

  pick = (use_game_controller > count ? count : use_game_controller) - 1;

  for (i = 0; i < num_joysticks; ++i) {
    if (!SDL_IsGameController(i))
      continue;

    if (pick-- > 0)
      continue;

    game_controller = SDL_GameControllerOpen(i);

    if (!game_controller) {
      lprintf(LO_ERROR, "dsda_InitGameController: error opening game controller %d (%s)\n",
              i, SDL_GetError());
      return;
    }

    game_controller_id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(game_controller));
    lprintf(LO_INFO, "Opened game controller %s\n", SDL_GameControllerName(game_controller));
    return;
  }
}

int dsda_GameControllerInstanceId(void) {
  return game_controller ? (int) game_controller_id : -1;
}

// Hot-plug: called from the SDL event loop
void dsda_GameControllerDeviceEvent(const SDL_Event* e) {
  if (!use_game_controller)
    return;

  if (e->type == SDL_CONTROLLERDEVICEADDED) {
    dsda_OpenGameController();
  }
  else if (e->type == SDL_CONTROLLERDEVICEREMOVED && e->cdevice.which == game_controller_id) {
    dsda_CloseGameController();
    dsda_OpenGameController(); // adopt another connected controller, if any
  }
}

void dsda_InitGameController(void) {
  dsda_CloseGameController();

  use_game_controller =
    dsda_IntConfig(dsda_config_use_game_controller) && !dsda_Flag(dsda_arg_nojoy);

  if (!use_game_controller)
    return;

  dsda_InitGameControllerParameters();

  if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) < 0) {
    lprintf(LO_WARN, "dsda_InitGameController: %s\n", SDL_GetError());
    return;
  }

  dsda_OpenGameController();
}

#ifdef BEGOTTEN_PADTEST
// ---------------------------------------------------------------------------------------------
// Test-only (build with -DBEGOTTEN_PADTEST, never in release builds): attaches a *virtual*
// controller after the game has started (so it also tests hot-plug), drives it on a schedule and
// checks the real game state. Output lines start with "PADTEST".
// ---------------------------------------------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include "d_player.h"
#include "p_mobj.h"
#include "p_tick.h"
#include "m_misc.h"
#include "dsda/configuration.h"

extern short itemOn;

enum { PT_ATTACH, PT_CHECK_OPEN, PT_FWD_ON, PT_FWD_OFF, PT_CHECK_MOVED, PT_TURN_ON, PT_TURN_OFF,
       PT_CHECK_TURNED, PT_RB_ON, PT_RB_OFF, PT_CHECK_WEAPON_NEXT, PT_LB_ON, PT_LB_OFF,
       PT_CHECK_WEAPON_PREV, PT_FIRE_ON, PT_FIRE_OFF, PT_CHECK_AMMO, PT_START_ON, PT_START_OFF,
       PT_CHECK_MENU_OPEN, PT_DOWN_ON, PT_DOWN_OFF, PT_CHECK_MENU_MOVED, PT_START2_ON,
       PT_START2_OFF, PT_CHECK_MENU_CLOSED, PT_SHOT_BASE, PT_LOOK_OFF_ON, PT_LOOK_OFF_OFF, PT_CHECK_LOOK_OFF,
       PT_LOOK_ENABLE, PT_LOOK_ON, PT_LOOK_STOP, PT_CHECK_LOOK_ON, PT_SHOT_LOOK, PT_DONE };

static const struct { int ms; int id; } pt_script[] = {
  {    0, PT_ATTACH },        {  600, PT_CHECK_OPEN },
  {  700, PT_FWD_ON },        { 1700, PT_FWD_OFF },      { 1800, PT_CHECK_MOVED },
  { 1900, PT_TURN_ON },       { 2400, PT_TURN_OFF },     { 2500, PT_CHECK_TURNED },
  { 2600, PT_RB_ON },         { 2700, PT_RB_OFF },       { 4600, PT_CHECK_WEAPON_NEXT },
  { 4700, PT_LB_ON },         { 4800, PT_LB_OFF },       { 6700, PT_CHECK_WEAPON_PREV },
  { 6800, PT_FIRE_ON },       { 7400, PT_FIRE_OFF },     { 7600, PT_CHECK_AMMO },
  { 7700, PT_START_ON },      { 7800, PT_START_OFF },    { 8200, PT_CHECK_MENU_OPEN },
  { 8300, PT_DOWN_ON },       { 8450, PT_DOWN_OFF },     { 8900, PT_CHECK_MENU_MOVED },
  { 9000, PT_START2_ON },     { 9100, PT_START2_OFF },   { 9600, PT_CHECK_MENU_CLOSED },
  { 9700, PT_SHOT_BASE },
  { 9800, PT_LOOK_OFF_ON },   {10300, PT_LOOK_OFF_OFF }, {10500, PT_CHECK_LOOK_OFF},
  {10600, PT_LOOK_ENABLE},    {10700, PT_LOOK_ON},       {11300, PT_LOOK_STOP},
  {11500, PT_CHECK_LOOK_ON},  {11600, PT_SHOT_LOOK},
  {11800, PT_DONE },
};

static SDL_Joystick* pt_joy;
static int pt_cross_stage;
static Uint32 pt_cross_t0;

static Uint32 pt_t0;
static unsigned pt_step;
static int pt_pass, pt_fail, pt_weapon, pt_ammo, pt_item;
static fixed_t pt_x, pt_y;
static angle_t pt_angle;

#define PT(cond, msg) do { if (cond) { ++pt_pass; printf("PADTEST PASS: %s\n", msg); } \
  else { ++pt_fail; printf("PADTEST FAIL: %s\n", msg); } fflush(stdout); } while (0)

static void pt_axis(int axis, int v) { SDL_JoystickSetVirtualAxis(pt_joy, axis, v); }
static void pt_btn(int b, int v) { SDL_JoystickSetVirtualButton(pt_joy, b, v); }

static void pt_cross_test(player_t* pl, Uint32 now, int linenum);
static void pt_eat_test(player_t* pl, Uint32 now);

void dsda_PadSelfTestTick(void) {
  Uint32 now = SDL_GetTicks();
  player_t* pl = &players[consoleplayer];
  fixed_t dx, dy;

  if (gamestate != GS_LEVEL || !pl->mo || leveltime < 70)
    return;

  if (getenv("BEGOTTEN_PADTEST_EAT")) {
    pt_eat_test(pl, now);
    return;
  }

  if (getenv("BEGOTTEN_PADTEST_CROSS")) {
    pt_cross_test(pl, now, atoi(getenv("BEGOTTEN_PADTEST_CROSS")));
    return;
  }

  if (!pt_t0)
    pt_t0 = now;

  if (pt_step >= sizeof(pt_script) / sizeof(pt_script[0]) || now - pt_t0 < (Uint32) pt_script[pt_step].ms)
    return;

  switch (pt_script[pt_step++].id) {
    case PT_ATTACH: {
      SDL_VirtualJoystickDesc desc;
      int idx;

      SDL_zero(desc);
      desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
      desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
      desc.naxes = 6;
      desc.nbuttons = 15;
      desc.name = "Begotten Test Pad";
      idx = SDL_JoystickAttachVirtualEx(&desc);
      printf("PADTEST: attached virtual joystick, device index %d (game controller: %d)\n", idx,
             idx >= 0 ? SDL_IsGameController(idx) : 0);
      pt_joy = SDL_JoystickOpen(idx);
      pt_axis(4, -32768); // triggers rest at the axis minimum
      pt_axis(5, -32768);
      fflush(stdout);
      break;
    }
    case PT_CHECK_OPEN:
      PT(game_controller != NULL, "controller connected AFTER startup was picked up (hot-plug)");
      PT(game_controller && SDL_GameControllerGetAxis(game_controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) == 0,
         "idle trigger reads 0 (no phantom fire)");
      pt_x = pl->mo->x; pt_y = pl->mo->y; pt_angle = pl->mo->angle;
      pt_weapon = pl->readyweapon; pt_ammo = pl->ammo[am_clip];
      break;
    case PT_FWD_ON:  pt_axis(SDL_CONTROLLER_AXIS_LEFTY, -32768); break;
    case PT_FWD_OFF: pt_axis(SDL_CONTROLLER_AXIS_LEFTY, 0); break;
    case PT_CHECK_MOVED:
      dx = pl->mo->x - pt_x; dy = pl->mo->y - pt_y;
      PT(abs(dx) + abs(dy) > 20 * FRACUNIT, "left stick forward moves the player");
      break;
    case PT_TURN_ON:  pt_axis(SDL_CONTROLLER_AXIS_RIGHTX, 32767); break;
    case PT_TURN_OFF: pt_axis(SDL_CONTROLLER_AXIS_RIGHTX, 0); break;
    case PT_CHECK_TURNED:
      PT(pl->mo->angle != pt_angle, "right stick turns the player");
      break;
    case PT_RB_ON:  pt_btn(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 1); break;
    case PT_RB_OFF: pt_btn(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 0); break;
    case PT_CHECK_WEAPON_NEXT:
      PT(pl->readyweapon != pt_weapon, "right bumper selects the next weapon");
      break;
    case PT_LB_ON:  pt_btn(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, 1); break;
    case PT_LB_OFF: pt_btn(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, 0); break;
    case PT_CHECK_WEAPON_PREV:
      PT(pl->readyweapon == pt_weapon, "left bumper goes back to the previous weapon");
      break;
    case PT_FIRE_ON:  pt_axis(5, 32767); break;
    case PT_FIRE_OFF: pt_axis(5, -32768); break;
    case PT_CHECK_AMMO:
      PT(pl->ammo[am_clip] < pt_ammo, "right trigger fires (ammo went down)");
      break;
    case PT_START_ON:  pt_btn(SDL_CONTROLLER_BUTTON_START, 1); break;
    case PT_START_OFF: pt_btn(SDL_CONTROLLER_BUTTON_START, 0); break;
    case PT_CHECK_MENU_OPEN:
      PT(menuactive, "Start opens the menu");
      pt_item = itemOn;
      break;
    case PT_DOWN_ON:  pt_axis(SDL_CONTROLLER_AXIS_LEFTY, 32767); break;
    case PT_DOWN_OFF: pt_axis(SDL_CONTROLLER_AXIS_LEFTY, 0); break;
    case PT_CHECK_MENU_MOVED:
      PT(itemOn != pt_item, "left stick navigates the menu");
      break;
    case PT_START2_ON:  pt_btn(SDL_CONTROLLER_BUTTON_START, 1); break;
    case PT_START2_OFF: pt_btn(SDL_CONTROLLER_BUTTON_START, 0); break;
    case PT_CHECK_MENU_CLOSED:
      PT(!menuactive, "Start closes the menu again");
      break;
    case PT_SHOT_BASE:
      M_DoScreenShot("/tmp/padtest_base.png");
      break;
    case PT_LOOK_OFF_ON:  pt_axis(SDL_CONTROLLER_AXIS_RIGHTY, -32768); break; // stick up, free look still OFF
    case PT_LOOK_OFF_OFF: pt_axis(SDL_CONTROLLER_AXIS_RIGHTY, 0); break;
    case PT_CHECK_LOOK_OFF:
      PT(pl->mo->pitch == 0, "free look OFF (default): right stick up does not tilt the view");
      break;
    case PT_LOOK_ENABLE:
      dsda_UpdateIntConfig(dsda_config_freelook, 1, false);
      break;
    case PT_LOOK_ON:   pt_axis(SDL_CONTROLLER_AXIS_RIGHTY, -32768); break;
    case PT_LOOK_STOP: pt_axis(SDL_CONTROLLER_AXIS_RIGHTY, 0); break;
    case PT_CHECK_LOOK_ON:
      PT(pl->mo->pitch != 0, "free look ON: right stick up tilts the view");
      printf("PADTEST: view pitch is now %d degrees (negative = up)\n", (int) ((int) pl->mo->pitch / (int) ANG1));
      break;
    case PT_SHOT_LOOK:
      M_DoScreenShot("/tmp/padtest_look.png");
      break;
    case PT_DONE:
      printf("PADTEST RESULT: %d passed, %d failed\n", pt_pass, pt_fail);
      fflush(stdout);
      exit(pt_fail ? 1 : 0);
  }
}
#endif

#ifdef BEGOTTEN_PADTEST
#include "p_map.h"
#include "p_maputl.h"
#include "r_main.h"
#include "r_state.h"
// Walks the player across one linedef (from its right/front side) with the virtual controller's left stick.
// A pressed while a menu is up, the menu closed with A still held: the game must not see a jump.
static void pt_eat_test(player_t* pl, Uint32 now) {
  static Uint32 t0;
  static int stage, maxz;
  Uint32 t;

  if (!t0) t0 = now;
  t = now - t0;
  if (pl->mo->momz > maxz) maxz = pl->mo->momz;
  switch (stage) {
    case 0: {
      SDL_VirtualJoystickDesc desc;
      SDL_zero(desc);
      desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
      desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
      desc.naxes = 6; desc.nbuttons = 15; desc.name = "Begotten Test Pad";
      pt_joy = SDL_JoystickOpen(SDL_JoystickAttachVirtualEx(&desc));
      pt_axis(4, -32768); pt_axis(5, -32768);
      ++stage;
      break;
    }
    case 1: if (t > 800)  { pt_btn(SDL_CONTROLLER_BUTTON_START, 1); ++stage; } break;
    case 2: if (t > 900)  { pt_btn(SDL_CONTROLLER_BUTTON_START, 0); ++stage; } break;
    case 3: if (t > 1500) { printf("PADTEST EAT: menu open = %d\n", menuactive); pt_btn(SDL_CONTROLLER_BUTTON_A, 1); ++stage; } break; // held from here on
    case 4: if (t > 2300) { pt_btn(SDL_CONTROLLER_BUTTON_B, 1); ++stage; } break;
    case 5: if (t > 2400) { pt_btn(SDL_CONTROLLER_BUTTON_B, 0); ++stage; } break;
    case 6: if (t > 2700) { pt_btn(SDL_CONTROLLER_BUTTON_B, 1); ++stage; } break;
    case 7: if (t > 2800) { pt_btn(SDL_CONTROLLER_BUTTON_B, 0); ++stage; } break;
    case 8: if (t > 3100) { pt_btn(SDL_CONTROLLER_BUTTON_B, 1); ++stage; } break;
    case 9: if (t > 3200) { pt_btn(SDL_CONTROLLER_BUTTON_B, 0); ++stage; } break;
    case 10: if (t > 3500) { pt_btn(SDL_CONTROLLER_BUTTON_B, 1); ++stage; } break;
    case 11: if (t > 3600) { pt_btn(SDL_CONTROLLER_BUTTON_B, 0); ++stage; maxz = 0; } break;
    case 12:
      if (t > 6000) {
        printf("PADTEST EAT: menu open = %d, A still held, highest upward speed since the menu closed = %d\n", menuactive, maxz >> 16);
        PT(!menuactive && maxz == 0, "A held through the menu does not jump when the menu closes");
        pt_btn(SDL_CONTROLLER_BUTTON_A, 0);
        ++stage;
        t0 = now;
      }
      break;
    case 13:
      if (t > 300) { pt_btn(SDL_CONTROLLER_BUTTON_A, 1); ++stage; maxz = 0; }
      break;
    case 14:
      if (t > 1200) {
        PT(maxz > 0, "A pressed afterwards (menu closed) still jumps");
        printf("PADTEST EAT: %d passed, %d failed\n", pt_pass, pt_fail);
        fflush(stdout);
        exit(pt_fail ? 1 : 0);
      }
      break;
  }
}

static void pt_cross_test(player_t* pl, Uint32 now, int linenum) {
  line_t* ln = &lines[linenum];

  if (!pt_cross_t0) pt_cross_t0 = now;

  switch (pt_cross_stage) {
    case 0: { // attach pad
      SDL_VirtualJoystickDesc desc;
      SDL_zero(desc);
      desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
      desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
      desc.naxes = 6; desc.nbuttons = 15; desc.name = "Begotten Test Pad";
      pt_joy = SDL_JoystickOpen(SDL_JoystickAttachVirtualEx(&desc));
      pt_axis(4, -32768); pt_axis(5, -32768);
      ++pt_cross_stage;
      break;
    }
    case 1: // place the player 64 units in front of the middle of the line, facing it
      if (now - pt_cross_t0 > 600) {
        fixed_t mx = (ln->v1->x + ln->v2->x) / 2, my = (ln->v1->y + ln->v2->y) / 2;
        fixed_t ldx = ln->v2->x - ln->v1->x, ldy = ln->v2->y - ln->v1->y;
        double len = sqrt((double) ldx * ldx + (double) ldy * ldy);
        // right-hand normal of v1->v2 is (ldy, -ldx); the player stands on that side and faces back toward the line
        fixed_t px = mx + (fixed_t) (ldy / len * 64 * FRACUNIT), py = my + (fixed_t) (-ldx / len * 64 * FRACUNIT);
        angle_t ang = R_PointToAngle2(px, py, mx, my);
        P_MapStart();
        P_TeleportMove(pl->mo, px, py, false);
        P_MapEnd();
        pl->mo->angle = ang;
        printf("PADTEST CROSS: line %d special=%d from (%d,%d) to (%d,%d); player placed at (%d,%d) side=%d\n", linenum,
               ln->special, ln->v1->x >> 16, ln->v1->y >> 16, ln->v2->x >> 16, ln->v2->y >> 16, px >> 16, py >> 16,
               P_PointOnLineSide(pl->mo->x, pl->mo->y, ln));
        fflush(stdout);
        pt_axis(SDL_CONTROLLER_AXIS_LEFTY, -32768); // walk forward
        ++pt_cross_stage;
      }
      break;
    case 2:
      if (now - pt_cross_t0 > 4500) {
        pt_axis(SDL_CONTROLLER_AXIS_LEFTY, 0);
        printf("PADTEST CROSS: after 4s of walking the player is at (%d,%d), side of line now = %d (0 = start side)\n",
               pl->mo->x >> 16, pl->mo->y >> 16, P_PointOnLineSide(pl->mo->x, pl->mo->y, ln));
        printf("PADTEST CROSS: level still running (no exit triggered)\n");
        fflush(stdout);
        exit(0);
      }
      break;
  }
}
#endif
