// Begotten: queued wall columns and spans, drawn on several threads (see bgdefer.h)

#include <stdlib.h>
#include <string.h>

#include "doomstat.h"
#include "r_main.h"
#include "r_draw.h"
#include "r_state.h"
#include "v_video.h"
#include "dsda/bgdefer.h"
#include "dsda/bglight.h"
#include "dsda/begotten.h"
#include "dsda/bgthreads.h"

#define STRIP 16          // columns to a task
#define BAND 8            // rows to a task

typedef struct
{
  draw_column_vars_t dc;
  R_DrawColumn_f f;
  fixed_t scale;
  const void* wall;       // NULL: not lit
  int next;               // the next command in the same screen column
} colcmd_t;

typedef struct
{
  draw_span_vars_t ds;
  fixed_t pheight;
  int plight;
  int next;               // the next command in the same screen row
} spancmd_t;

static colcmd_t* cols;
static int num_cols, max_cols;
static spancmd_t* spans;
static int num_spans, max_spans;
static int* col_head;     // per screen column: first command, -1 = none
static int* col_tail;
static int* row_head;
static int* row_tail;
static int head_w, head_h;

static fixed_t cur_pheight;
static int cur_plight;

dboolean bgd_Active(void)
{
  static int env = -1;

  if (!V_IsSoftwareMode() || !bg_weapons)
    return false;
  if (env < 0)
    env = bgt_Count() > 1;
  return env != 0;
}

static void EnsureHeads(void)
{
  if (head_w != SCREENWIDTH || head_h != SCREENHEIGHT)
  {
    free(col_head); free(col_tail); free(row_head); free(row_tail);
    head_w = SCREENWIDTH;
    head_h = SCREENHEIGHT;
    col_head = (int*) malloc(head_w * sizeof(int));
    col_tail = (int*) malloc(head_w * sizeof(int));
    row_head = (int*) malloc(head_h * sizeof(int));
    row_tail = (int*) malloc(head_h * sizeof(int));
    memset(col_head, -1, head_w * sizeof(int));
    memset(row_head, -1, head_h * sizeof(int));
  }
}

void bgd_Column(R_DrawColumn_f colfunc, const draw_column_vars_t* dcvars, fixed_t scale, const void* wall)
{
  colcmd_t* c;
  int x = dcvars->x, n;

  if (num_cols >= max_cols)
  {
    max_cols = max_cols ? max_cols * 2 : 4096;
    cols = (colcmd_t*) realloc(cols, max_cols * sizeof(*cols));
  }
  EnsureHeads();
  if (x < 0 || x >= head_w)
    return;
  n = num_cols++;
  c = &cols[n];
  c->dc = *dcvars;
  c->f = colfunc;
  c->scale = scale;
  c->wall = wall;
  c->next = -1;
  if (col_head[x] < 0)
    col_head[x] = n;
  else
    cols[col_tail[x]].next = n;
  col_tail[x] = n;
}

void bgd_Plane(fixed_t height, int lightlevel)
{
  cur_pheight = height;
  cur_plight = lightlevel;
}

void bgd_Span(const draw_span_vars_t* dsvars)
{
  spancmd_t* s;
  int y = dsvars->y, n;

  if (num_spans >= max_spans)
  {
    max_spans = max_spans ? max_spans * 2 : 4096;
    spans = (spancmd_t*) realloc(spans, max_spans * sizeof(*spans));
  }
  EnsureHeads();
  if (y < 0 || y >= head_h)
    return;
  n = num_spans++;
  s = &spans[n];
  s->ds = *dsvars;
  s->pheight = cur_pheight;
  s->plight = cur_plight;
  s->next = -1;
  if (row_head[y] < 0)
    row_head[y] = n;
  else
    spans[row_tail[y]].next = n;
  row_tail[y] = n;
}

static int strips, bands;

static void DrawTask(void* ctx, int task, int worker)
{
  (void) ctx;
  if (worker)
    R_ThreadDrawBegin();
  if (task < strips)
  {
    const void* cur_wall = (const void*) -1;
    int x, x1 = task * STRIP, x2 = x1 + STRIP;

    if (x2 > head_w)
      x2 = head_w;
    for (x = x1; x < x2; ++x)
    {
      int n;

      for (n = col_head[x]; n >= 0; n = cols[n].next)
      {
        colcmd_t* c = &cols[n];

        if (c->wall)
        {
          if (c->wall != cur_wall)
          {
            bg_WallRestore(c->wall);
            cur_wall = c->wall;
          }
          bg_LitColumn(c->f, &c->dc, c->scale);
        }
        else
          c->f(&c->dc);
      }
    }
    R_ResetColumnBuffer();               // this thread's last columns, still in its buffer, go to the screen
  }
  else
  {
    fixed_t ph = 0;
    int pl = -1, have = 0, y, y1 = (task - strips) * BAND, y2 = y1 + BAND;

    if (y2 > head_h)
      y2 = head_h;
    for (y = y1; y < y2; ++y)
    {
      int n;

      for (n = row_head[y]; n >= 0; n = spans[n].next)
      {
        spancmd_t* s = &spans[n];

        if (!have || s->pheight != ph || s->plight != pl)
        {
          bg_LightSetPlane(s->pheight, s->plight);
          ph = s->pheight;
          pl = s->plight;
          have = 1;
        }
        if (!bg_LitSpan(&s->ds))
          R_DrawSpan(&s->ds);
      }
    }
  }
}

void bgd_Flush(void)
{
  if (!num_cols && !num_spans)
    return;
  strips = (head_w + STRIP - 1) / STRIP;
  bands = (head_h + BAND - 1) / BAND;
  // columns: only the strips that have some; rows likewise (a task that finds nothing is quick, but 100 of them add up)
  bgt_Run(DrawTask, NULL, strips + bands);
  // forget the queue (the heads of the columns and rows that were used)
  {
    int i;

    for (i = 0; i < num_cols; ++i)
      col_head[cols[i].dc.x] = -1;
    for (i = 0; i < num_spans; ++i)
      row_head[spans[i].ds.y] = -1;
  }
  num_cols = num_spans = 0;
}
