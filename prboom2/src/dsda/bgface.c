//
// Begotten: status bar faces made from larger pictures (see bgface.h).
// Licensed under the GNU GPL v2 or later, like the rest of the engine.
//

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "doomtype.h"
#include "doomstat.h"
#include "lprintf.h"
#include "r_patch.h"
#include "v_video.h"
#include "w_wad.h"
#include "z_zone.h"
#include "dsda/stretch.h"

#include "dsda/bgface.h"

#define FIT_MAX 192

typedef struct
{
  int lump, w, h;
  byte* px;        // palette index per screen pixel
  byte* on;        // 1 where the picture is opaque
} fit_t;

static fit_t fits[FIT_MAX];
static int num_fits;
static byte fit_pal[768];
static dboolean fit_pal_set;
static byte* near6;               // palette index nearest to a colour of 6 bits per channel (0xff: not worked out yet)

void bg_FitPatchFlush(void)
{
  int i;

  for (i = 0; i < num_fits; ++i)
  {
    free(fits[i].px);
    free(fits[i].on);
  }
  num_fits = 0;
  if (near6)
    memset(near6, 0xff, 1 << 18);
}

// ordinary colours only (0..239): the glow ramps are for things that shine
static int nearest(int r, int g, int b)
{
  int i, best = 0;
  long bd = 1L << 30;

  for (i = 0; i < 240; ++i)
  {
    long dr = fit_pal[i * 3] - r, dg = fit_pal[i * 3 + 1] - g, db = fit_pal[i * 3 + 2] - b;
    long d = dr * dr * 3 + dg * dg * 4 + db * db * 2;

    if (d < bd)
    {
      bd = d;
      best = i;
    }
  }
  return best;
}

static int nearest_cached(int r, int g, int b)
{
  size_t k = ((size_t) (r >> 2) << 12) | ((size_t) (g >> 2) << 6) | (size_t) (b >> 2);

  if (!near6)
  {
    near6 = malloc(1 << 18);
    memset(near6, 0xff, 1 << 18);
  }
  if (near6[k] == 0xff)
    near6[k] = (byte) nearest((r & ~3) | 2, (g & ~3) | 2, (b & ~3) | 2);
  return near6[k];
}

static void check_palette(void)
{
  const byte* pal = V_GetPlaypal();

  if (!pal)
    return;
  if (!fit_pal_set || memcmp(fit_pal, pal, 768))
  {
    memcpy(fit_pal, pal, 768);
    fit_pal_set = true;
    bg_FitPatchFlush();
  }
}

// Shrinks a picture of sw x sh palette indices (opaque where mask != 0) to dw x dh: exact area average of the colours,
// then Floyd-Steinberg error diffusion to the palette. out_px/out_on are dw * dh.
static void fit_shrink(const byte* src, const byte* mask, int sw, int sh, int dw, int dh, byte* out_px, byte* out_on)
{
  float* tmp = malloc((size_t) dw * sh * 4 * sizeof(float));   // r, g, b, coverage
  float* img = malloc((size_t) dw * dh * 4 * sizeof(float));
  float* err = calloc((size_t) (dw + 2) * 2 * 3, sizeof(float));
  int x, y, k;

  // across: every destination column covers source columns [x * sw / dw, (x + 1) * sw / dw)
  for (y = 0; y < sh; ++y)
    for (x = 0; x < dw; ++x)
    {
      double a = (double) x * sw / dw, b = (double) (x + 1) * sw / dw, sum[4] = { 0, 0, 0, 0 };
      int s;
      float* t = &tmp[((size_t) y * dw + x) * 4];

      for (s = (int) a; s < sw && s < b; ++s)
      {
        double lo = s > a ? s : a, hi = s + 1 < b ? s + 1 : b, wgt = hi - lo;

        if (wgt > 0 && mask[y * sw + s])
        {
          const byte* p = &fit_pal[src[y * sw + s] * 3];

          sum[0] += wgt * p[0];
          sum[1] += wgt * p[1];
          sum[2] += wgt * p[2];
          sum[3] += wgt;                               // opaque coverage, in source columns
        }
      }
      t[0] = (float) sum[0];
      t[1] = (float) sum[1];
      t[2] = (float) sum[2];
      t[3] = (float) sum[3];
    }
  // down: the same over rows; the colour is the coverage-weighted mean, the opacity the covered share of the area
  for (x = 0; x < dw; ++x)
    for (y = 0; y < dh; ++y)
    {
      double a = (double) y * sh / dh, b = (double) (y + 1) * sh / dh, sum[4] = { 0, 0, 0, 0 };
      double full = ((double) sw / dw) * ((double) sh / dh);
      float* o = &img[((size_t) y * dw + x) * 4];
      int s;

      for (s = (int) a; s < sh && s < b; ++s)
      {
        double lo = s > a ? s : a, hi = s + 1 < b ? s + 1 : b, wgt = hi - lo;
        const float* t = &tmp[((size_t) s * dw + x) * 4];

        if (wgt <= 0)
          continue;
        sum[0] += wgt * t[0];
        sum[1] += wgt * t[1];
        sum[2] += wgt * t[2];
        sum[3] += wgt * t[3];
      }
      if (sum[3] > 0)
      {
        o[0] = (float) (sum[0] / sum[3]);
        o[1] = (float) (sum[1] / sum[3]);
        o[2] = (float) (sum[2] / sum[3]);
      }
      else
        o[0] = o[1] = o[2] = 0;
      o[3] = (float) (sum[3] / full);                  // 1 = fully opaque
    }
  // error diffusion, serpentine
  for (y = 0; y < dh; ++y)
  {
    int dir = (y & 1) ? -1 : 1;
    float* cur = err + ((size_t) (y & 1) * (dw + 2)) * 3;
    float* nxt = err + ((size_t) ((y + 1) & 1) * (dw + 2)) * 3;

    for (x = (dir > 0 ? 0 : dw - 1); x >= 0 && x < dw; x += dir)
    {
      const float* o = &img[((size_t) y * dw + x) * 4];
      size_t at = (size_t) y * dw + x;
      float c[3];
      int ir, ig, ib, idx;

      if (o[3] < 0.5f)
      {
        out_on[at] = 0;
        out_px[at] = 0;
        continue;
      }
      for (k = 0; k < 3; ++k)
      {
        c[k] = o[k] + cur[(x + 1) * 3 + k];
        if (c[k] < 0) c[k] = 0;
        if (c[k] > 255) c[k] = 255;
      }
      ir = (int) (c[0] + 0.5f);
      ig = (int) (c[1] + 0.5f);
      ib = (int) (c[2] + 0.5f);
      idx = nearest_cached(ir > 255 ? 255 : ir, ig > 255 ? 255 : ig, ib > 255 ? 255 : ib);
      out_px[at] = (byte) idx;
      out_on[at] = 1;
      for (k = 0; k < 3; ++k)
      {
        float e = c[k] - fit_pal[idx * 3 + k];

        cur[(x + 1 + dir) * 3 + k] += e * (7.0f / 16.0f);
        nxt[(x + 1 - dir) * 3 + k] += e * (3.0f / 16.0f);
        nxt[(x + 1) * 3 + k] += e * (5.0f / 16.0f);
        nxt[(x + 1 + dir) * 3 + k] += e * (1.0f / 16.0f);
      }
    }
    memset(cur, 0, (size_t) (dw + 2) * 3 * sizeof(float));
  }
  free(tmp);
  free(img);
  free(err);
}

// the picture of a patch as one palette index and one opaque flag per pixel
static void patch_pixels(const rpatch_t* p, byte** px, byte** mask)
{
  int x, y, i;

  *px = calloc((size_t) p->width * p->height, 1);
  *mask = calloc((size_t) p->width * p->height, 1);
  for (x = 0; x < p->width; ++x)
  {
    const rcolumn_t* col = &p->columns[x];

    for (i = 0; i < col->numPosts; ++i)
    {
      const rpost_t* post = &col->posts[i];

      for (y = post->topdelta; y < post->topdelta + post->length && y < p->height; ++y)
        if (y >= 0)
        {
          (*px)[(size_t) y * p->width + x] = col->pixels[y];
          (*mask)[(size_t) y * p->width + x] = 1;
        }
    }
  }
}

static fit_t* get_fit(int lump, int dw, int dh)
{
  int i;
  fit_t* f;
  const rpatch_t* p;
  byte *px, *mask;

  check_palette();
  for (i = 0; i < num_fits; ++i)
    if (fits[i].lump == lump && fits[i].w == dw && fits[i].h == dh)
      return &fits[i];
  if (num_fits >= FIT_MAX)
    bg_FitPatchFlush();
  p = R_PatchByNum(lump);
  if (!p || p->width < 1 || p->height < 1)
    return NULL;
  f = &fits[num_fits++];
  f->lump = lump;
  f->w = dw;
  f->h = dh;
  f->px = malloc((size_t) dw * dh);
  f->on = malloc((size_t) dw * dh);
  patch_pixels(p, &px, &mask);
  fit_shrink(px, mask, p->width, p->height, dw, dh, f->px, f->on);
  free(px);
  free(mask);
  return f;
}

dboolean bg_DrawFitPatch(int lump, int x, int y, int w, int h, int flags)
{
  stretch_param_t* params;
  int sx, sy, sw, sh, ix, iy;
  fit_t* f;

  if (!V_IsSoftwareMode() || lump < 0)
    return false;
  params = dsda_StretchParams(flags);
  sx = params->video->x1lookup[x];
  sy = params->video->y1lookup[y];
  sw = params->video->x2lookup[x + w - 1] - sx + 1;
  sh = params->video->y2lookup[y + h - 1] - sy + 1;
  sx += params->deltax1;
  sy += params->deltay1;
  if (sw < 1 || sh < 1)
    return false;
  f = get_fit(lump, sw, sh);
  if (!f)
    return false;
  for (iy = 0; iy < sh; ++iy)
  {
    int dy = sy + iy;
    byte* row;

    if (dy < 0 || dy >= SCREENHEIGHT)
      continue;
    row = screens[0].data + (size_t) dy * screens[0].pitch;
    for (ix = 0; ix < sw; ++ix)
    {
      int dx = sx + ix;

      if (dx >= 0 && dx < SCREENWIDTH && f->on[(size_t) iy * sw + ix])
        row[dx] = f->px[(size_t) iy * sw + ix];
    }
  }
  return true;
}

// ---- self test: a made-up palette and picture ----

int bg_FitSelfTest(char* msg, size_t msglen)
{
  byte saved[768], px[16 * 16], mask[16 * 16], out[4 * 4], on[4 * 4];
  int i, fails = 0, x, y;

  memcpy(saved, fit_pal, 768);
  for (i = 0; i < 256; ++i)
    fit_pal[i * 3] = fit_pal[i * 3 + 1] = fit_pal[i * 3 + 2] = (byte) i;   // a grey ramp
  if (near6)
    memset(near6, 0xff, 1 << 18);
  // left half black (index 0), right half white-ish (index 200), the top row transparent
  for (y = 0; y < 16; ++y)
    for (x = 0; x < 16; ++x)
    {
      px[y * 16 + x] = x < 8 ? 0 : 200;
      mask[y * 16 + x] = y >= 4;
    }
  fit_shrink(px, mask, 16, 16, 4, 4, out, on);
  if (on[0] || on[3] || !on[4] || !on[15])
  {
    ++fails;
    snprintf(msg, msglen, "face shrink: transparency wrong");
  }
  if (!fails && (out[4] > 20 || out[7] < 180))
  {
    ++fails;
    snprintf(msg, msglen, "face shrink: halves %d %d", out[4], out[7]);
  }
  memcpy(fit_pal, saved, 768);
  if (near6)
    memset(near6, 0xff, 1 << 18);
  if (!fails)
    snprintf(msg, msglen, "face shrink: ok");
  return fails;
}
