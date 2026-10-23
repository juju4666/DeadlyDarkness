//
// Begotten: the episode ending, see bgending.h.
//

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "doomstat.h"
#include "d_main.h"
#include "g_game.h"
#include "hu_stuff.h"
#include "lprintf.h"
#include "r_main.h"
#include "s_sound.h"
#include "v_video.h"
#include "w_wad.h"
#include "z_zone.h"
#include "dsda/bgending.h"
#include "dsda/bgnet.h"
#include "dsda/font.h"

#define HOLD_TICS     (4 * 35)   // the picture at full brightness
#define DIM_TICS      (2 * 35)   // fading down
#define DIM_LEVEL     20         // of 32 colormap rows: roughly 40% of the brightness
#define SKIP_AFTER    35         // buttons are ignored for the first second
#define SCROLL_NUM    1          // text scrolls SCROLL_NUM / SCROLL_DEN virtual pixels per tic
#define SCROLL_DEN    2
#define LINE_HEIGHT   11
#define END_HOLD_TICS (3 * 35)   // after the last line has left

static dboolean active;
static int count;
static char picname[9];
static char* end_lines[512];
static int is_heading[512];
static int num_lines;
static char* text_copy;
static int boost; // extra pixels added by holding a button

static int Find(const char* base, int episode, char* out)
{
  char name[16];

  snprintf(name, sizeof name, "%s%d", base, episode);
  if (W_CheckNumForName(name) >= 0)
  {
    strncpy(out, name, 8);
    out[8] = '\0';
    return 1;
  }
  if (W_CheckNumForName(base) >= 0)
  {
    strncpy(out, base, 8);
    out[8] = '\0';
    return 1;
  }
  return 0;
}

static void SetText(const char* text, int len)
{
  char* p;
  int i;

  free(text_copy);
  text_copy = malloc(len + 1);
  memcpy(text_copy, text, len);
  text_copy[len] = '\0';
  num_lines = 0;
  for (p = text_copy; p && num_lines < 512;)
  {
    char* nl = strchr(p, '\n');

    if (nl)
      *nl = '\0';
    i = (int) strlen(p);
    if (i && p[i - 1] == '\r')
      p[i - 1] = '\0';
    is_heading[num_lines] = (*p == '#');
    end_lines[num_lines] = is_heading[num_lines] ? p + 1 : p;
    ++num_lines;
    p = nl ? nl + 1 : NULL;
  }
}

dboolean bg_EndingStart(int episode)
{
  char txt[9];
  int have_pic = Find("BGPIC", episode, picname);
  int have_txt = Find("BGTXT", episode, txt);

  if (!have_pic && !have_txt)
    return false;

  if (!have_pic)
    picname[0] = '\0';
  if (have_txt)
  {
    int lump = W_CheckNumForName(txt);

    SetText((const char*) W_LumpByNum(lump), W_LumpLength(lump));
  }
  else
    SetText("THE END", 7);

  {
    char mus[9];

    // BGMUS<n> / BGMUS: the ending's music (a music lump, any format the engine plays)
    if (Find("BGMUS", episode, mus))
      S_ChangeMusInfoMusic(W_GetNumForName(mus), true);
  }

  active = true;
  count = 0;
  boost = 0;
  return true;
}

dboolean bg_EndingActive(void)
{
  if (active && gamestate != GS_FINALE)
    active = false;   // the game was ended some other way (menu, new game)
  return active;
}

static int ScrollPixels(void)
{
  int t = count - HOLD_TICS - DIM_TICS;

  return t < 0 ? 0 : t * SCROLL_NUM / SCROLL_DEN;
}

static int TextHeight(void)
{
  return num_lines * LINE_HEIGHT;
}

void bg_EndingTicker(void)
{
  int i, pressed = 0, t;

  if (!active)
    return;

  for (i = 0; i < g_maxplayers; ++i)
    if (playeringame[i] && players[i].cmd.buttons)
      pressed = 1;

  ++count;
  t = count - HOLD_TICS - DIM_TICS;
  if (t > 0 && pressed)
    boost += 2;   // hold a button to read faster (up to 5x)

  // finished: the text has scrolled out; any button (or the timeout) goes back to the title
  if (t > 0 && ScrollPixels() + boost > 200 + TextHeight())
  {
    static int done_at;

    if (!done_at)
      done_at = count;
    if (count - done_at > END_HOLD_TICS || (pressed && count - done_at > 10))
    {
      done_at = 0;
      active = false;
      boost = 0;
      bgnet_Leave();
      D_StartTitle();
    }
  }
  else if (pressed && count > SKIP_AFTER && t <= 0)
  {
    // a button while the picture is on show moves on to the text
    count = HOLD_TICS + DIM_TICS;
  }
}

static void DimScreen(int level)
{
  int x, y;

  if (level <= 0 || V_IsOpenGLMode() || !colormaps || !colormaps[0])
    return;
  if (level > 31)
    level = 31;
  {
    const byte* map = (const byte*) colormaps[0] + level * 256;

    for (y = 0; y < SCREENHEIGHT; ++y)
    {
      byte* dest = screens[0].data + screens[0].pitch * y;

      for (x = 0; x < SCREENWIDTH; ++x)
        dest[x] = map[dest[x]];
    }
  }
}

static void DrawLine(const char* s, int y, int cm)
{
  int i, w = 0, x;

  for (i = 0; s[i]; ++i)
  {
    int c = toupper((unsigned char) s[i]) - HU_FONTSTART;

    w += (c < 0 || c > HU_FONTSIZE) ? 4 : hud_font.font[c].width;
  }
  x = (320 - w) / 2;
  for (i = 0; s[i]; ++i)
  {
    int c = toupper((unsigned char) s[i]) - HU_FONTSTART;

    if (c < 0 || c > HU_FONTSIZE)
    {
      x += 4;
      continue;
    }
    V_DrawNumPatch(x, y, 0, hud_font.font[c].lumpnum, cm, VPT_STRETCH);
    x += hud_font.font[c].width;
  }
}

void bg_EndingDrawer(void)
{
  int i, level = 0, t;

  if (!active)
    return;

  V_FillRect(0, 0, 0, SCREENWIDTH, SCREENHEIGHT, 0);
  if (picname[0])
    V_DrawRawScreen(picname);

  if (count > HOLD_TICS)
    level = count >= HOLD_TICS + DIM_TICS ? DIM_LEVEL : (count - HOLD_TICS) * DIM_LEVEL / DIM_TICS;
  DimScreen(level);

  t = count - HOLD_TICS - DIM_TICS;
  if (t > 0)
  {
    int top = 200 - (ScrollPixels() + boost);

    for (i = 0; i < num_lines; ++i)
    {
      int y = top + i * LINE_HEIGHT;

      if (y < -LINE_HEIGHT || y > 200)
        continue;
      if (end_lines[i][0])
        DrawLine(end_lines[i], y, is_heading[i] ? CR_GOLD : CR_GRAY);
    }
  }
}

void dsda_BgEndingSelfTest(void)
{
  int fails = 0;

#define T(c, m) do { if (c) lprintf(LO_INFO, "ok:   ending: %s\n", m); else { ++fails; lprintf(LO_INFO, "FAIL: ending: %s\n", m); } } while (0)
  SetText("#TITLE\nline one\n\nline three\n", 28);
  T(num_lines == 5, "five lines (the last one empty)");
  T(is_heading[0] && !strcmp(end_lines[0], "TITLE"), "a # line is a heading, without the mark");
  T(!is_heading[1] && !strcmp(end_lines[1], "line one"), "a normal line");
  T(end_lines[2][0] == '\0', "an empty line is a gap");
  SetText("a\r\nb", 4);
  T(num_lines == 2 && !strcmp(end_lines[0], "a"), "CRLF line ends are handled");
#undef T
  lprintf(LO_INFO, "BEGOTTEN SELFTEST (ending): %s (%d failures)\n", fails ? "FAILED" : "PASSED", fails);
  free(text_copy);
  text_copy = NULL;
  num_lines = 0;
}
