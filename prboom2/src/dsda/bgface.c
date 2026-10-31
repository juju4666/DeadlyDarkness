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
  pixel_t* px;     // palette index per screen pixel
  byte* on;        // 1 where the picture is opaque
} fit_t;

static fit_t fits[FIT_MAX];
static int num_fits;
static byte fit_pal[3 * BG_NUMCOLORS];
static dboolean fit_pal_set;
static pixel_t* near6;            // palette index nearest to a colour of 6 bits per channel (0xffff: not worked out yet)

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
    memset(near6, 0xff, (1 << 18) * sizeof(pixel_t));
}

// ordinary colours only (index & 255 below 240): the glow ramps are for things that shine
static int nearest(int r, int g, int b)
{
  int i, best = 0;
  long bd = 1L << 30;

  for (i = 0; i < BG_NUMCOLORS; ++i)
  {
    long dr, dg, db, d;

    if ((i & 255) >= 240)
      continue;
    dr = fit_pal[i * 3] - r; dg = fit_pal[i * 3 + 1] - g; db = fit_pal[i * 3 + 2] - b;
    d = dr * dr * 3 + dg * dg * 4 + db * db * 2;

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
    near6 = malloc((1 << 18) * sizeof(pixel_t));
    memset(near6, 0xff, (1 << 18) * sizeof(pixel_t));
  }
  if (near6[k] == 0xffff)
    near6[k] = (pixel_t) nearest((r & ~3) | 2, (g & ~3) | 2, (b & ~3) | 2);
  return near6[k];
}

static void check_palette(void)
{
  const byte* pal = V_GetColorPalette();

  if (!pal)
    return;
  if (!fit_pal_set || memcmp(fit_pal, pal, sizeof fit_pal))
  {
    memcpy(fit_pal, pal, sizeof fit_pal);
    fit_pal_set = true;
    bg_FitPatchFlush();
  }
}

// Shrinks a picture of sw x sh palette indices (opaque where mask != 0) to dw x dh: exact area average of the colours,
// then Floyd-Steinberg error diffusion to the palette. out_px/out_on are dw * dh.
static void fit_shrink(const pixel_t* src, const byte* mask, int sw, int sh, int dw, int dh, pixel_t* out_px, byte* out_on)
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
      out_px[at] = (pixel_t) idx;
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
static void patch_pixels(const rpatch_t* p, pixel_t** px, byte** mask)
{
  int x, y, i;

  *px = calloc((size_t) p->width * p->height, sizeof(pixel_t));
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
  pixel_t *px;
  byte *mask;

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
  f->px = malloc((size_t) dw * dh * sizeof(pixel_t));
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
    pixel_t* row;

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

int bg_PaletteNearest(int r, int g, int b)
{
  check_palette();
  r = r < 0 ? 0 : r > 255 ? 255 : r;
  g = g < 0 ? 0 : g > 255 ? 255 : g;
  b = b < 0 ? 0 : b > 255 ? 255 : b;
  return nearest_cached(r, g, b);
}

// For a flat colour: any palette colour (the glow ramps too: the status bar is not lit), judged on colour as much as on
// brightness, so that a dark green falls on the most greenish dark colour the palette has and not on a grey.
int bg_PaletteNearestFlat(int r, int g, int b)
{
  int i, best = 0;
  double bd = 1e30, lt = (r + g + b) / 3.0;

  check_palette();
  for (i = 0; i < BG_NUMCOLORS; ++i)
  {
    double pr, pg, pb, pl, d;

    if (i == 255)
      continue;
    pr = fit_pal[i * 3]; pg = fit_pal[i * 3 + 1]; pb = fit_pal[i * 3 + 2]; pl = (pr + pg + pb) / 3.0;
    d = 3 * (pr - r) * (pr - r) + 4 * (pg - g) * (pg - g) + 2 * (pb - b) * (pb - b);

    d += 6.0 * (((pr - pl) - (r - lt)) * ((pr - pl) - (r - lt)) + ((pg - pl) - (g - lt)) * ((pg - pl) - (g - lt)) +
                ((pb - pl) - (b - lt)) * ((pb - pl) - (b - lt)));
    if (d < bd)
    {
      bd = d;
      best = i;
    }
  }
  return best;
}

void bg_FillSlot(int x, int y, int w, int h, int flags, int colour)
{
  stretch_param_t* params;
  int sx, sy, sw, sh, iy;

  if (!V_IsSoftwareMode())
    return;
  params = dsda_StretchParams(flags);
  sx = params->video->x1lookup[x];
  sy = params->video->y1lookup[y];
  sw = params->video->x2lookup[x + w - 1] - sx + 1;
  sh = params->video->y2lookup[y + h - 1] - sy + 1;
  sx += params->deltax1;
  sy += params->deltay1;
  if (sx < 0) { sw += sx; sx = 0; }
  if (sy < 0) { sh += sy; sy = 0; }
  if (sx + sw > SCREENWIDTH) sw = SCREENWIDTH - sx;
  if (sy + sh > SCREENHEIGHT) sh = SCREENHEIGHT - sy;
  if (sw <= 0 || sh <= 0)
    return;
  for (iy = 0; iy < sh; ++iy)
  {
    pixel_t* row = screens[0].data + (size_t) (sy + iy) * screens[0].pitch + sx;
    int k;

    for (k = 0; k < sw; ++k)
      row[k] = (pixel_t) colour;
  }
}

// ---- self test: a made-up palette and picture ----

int bg_FitSelfTest(char* msg, size_t msglen)
{
  byte saved[3 * BG_NUMCOLORS], mask[16 * 16], on[4 * 4];
  pixel_t px[16 * 16], out[4 * 4];
  int i, fails = 0, x, y;

  memcpy(saved, fit_pal, sizeof saved);
  for (i = 0; i < BG_NUMCOLORS; ++i)
    fit_pal[i * 3] = fit_pal[i * 3 + 1] = fit_pal[i * 3 + 2] = (byte) (i * 255 / (BG_NUMCOLORS - 1));   // a grey ramp
  if (near6)
    memset(near6, 0xff, (1 << 18) * sizeof(pixel_t));
  // left half black (index 0), right half white-ish (index 200), the top row transparent
  for (y = 0; y < 16; ++y)
    for (x = 0; x < 16; ++x)
    {
      px[y * 16 + x] = x < 8 ? 0 : (BG_NUMCOLORS * 200 / 256);
      mask[y * 16 + x] = y >= 4;
    }
  fit_shrink(px, mask, 16, 16, 4, 4, out, on);
  if (on[0] || on[3] || !on[4] || !on[15])
  {
    ++fails;
    snprintf(msg, msglen, "face shrink: transparency wrong");
  }
  if (!fails && (out[4] > 20 * BG_NUMCOLORS / 256 || out[7] < 180 * BG_NUMCOLORS / 256))
  {
    ++fails;
    snprintf(msg, msglen, "face shrink: halves %d %d", out[4], out[7]);
  }
  memcpy(fit_pal, saved, sizeof saved);
  if (near6)
    memset(near6, 0xff, (1 << 18) * sizeof(pixel_t));
  if (!fails)
    snprintf(msg, msglen, "face shrink: ok");
  return fails;
}
