//
// Begotten: the 512-colour palette (see bgcolor.h).
// Licensed under the GNU GPL v2 or later, like the rest of the engine.
//
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doomtype.h"
#include "v_video.h"
#include "w_wad.h"

#include "dsda/bgcolor.h"

#define PAL_BYTES (3 * BG_NUMCOLORS)
#define MAX_SUBPALS 32

static unsigned palette_generation = 1;

unsigned bg_PaletteGeneration(void)
{
  return palette_generation;
}

// The palette is 16 rows of 16 colours: row 0 the greys (dark to light), rows 1..14 the hues, each from light to dark,
// row 15 the glow colours (240..254; 255 is a spare). The in-between colour of a colour is the middle of it and the next
// shade of its own ramp (so every ramp has twice as many steps and light falls off more smoothly); the darkest shade is
// halfway to black. BG_EXTRAS=hue (a developer switch) makes it the middle of it and the same shade one hue row on.
int bg_ExtraPair(int i)
{
  static int mode = -1;
  int row = (i >> 4) & 15, col = i & 15;

  if (mode < 0)
  {
    const char* e = getenv("BG_EXTRAS");

    mode = (e && !strcmp(e, "hue")) ? 1 : 0;
  }
  if (i == 255)
    return 255;
  if (row == 15)
    return i < 254 ? i + 1 : i - 1;
  if (row == 0)
    return col < 15 ? i + 1 : i - 1;
  if (mode)
    return row == 14 ? 16 + col : i + 16;
  return col < 15 ? i + 1 : 0;
}

#if BG_COLORBITS > 8

static byte* wide_cache[MAX_SUBPALS];
static const byte* wide_src;

static void drop_wide_cache(void)
{
  int i;

  for (i = 0; i < MAX_SUBPALS; ++i)
  {
    free(wide_cache[i]);
    wide_cache[i] = NULL;
  }
  wide_src = NULL;
}

// one sub-palette: the 256 colours of src, then the in-between colours (painted by the wad when extra is given)
static void build_wide(byte* w, const byte* src, const byte* extra)
{
  int i;

  memcpy(w, src, 768);
  for (i = 0; i < 256; ++i)
  {
    byte* e = w + 3 * (256 + i);

    if (extra)
    {
      memcpy(e, extra + 3 * i, 3);
    }
    else
    {
      const byte* a = src + 3 * i;
      const byte* b = src + 3 * bg_ExtraPair(i);

      e[0] = (byte) ((a[0] + b[0] + 1) / 2);
      e[1] = (byte) ((a[1] + b[1] + 1) / 2);
      e[2] = (byte) ((a[2] + b[2] + 1) / 2);
    }
  }
}

const byte* bg_WidePalette(const byte* playpal, int count, int pal, const byte* extra, int extra_len)
{
  byte* w;

  if (!playpal || pal < 0 || pal >= count || pal >= MAX_SUBPALS)
    return NULL;
  if (wide_src != playpal)
  {
    drop_wide_cache();
    wide_src = playpal;
  }
  if (wide_cache[pal])
    return wide_cache[pal];

  w = malloc(PAL_BYTES);
  build_wide(w, playpal + (size_t) pal * 768, (extra && extra_len >= (pal + 1) * 768) ? extra + (size_t) pal * 768 : NULL);
  wide_cache[pal] = w;
  return w;
}

// The reference palette: the first palette of the PLAYPAL lump, widened. The remap tables (colormaps, translations,
// colour ranges, translucency) are made relative to it, whichever palette the map is shown with, like the colormap lumps
// of the original game are indices that mean "the same colour, darker" in every palette.
static byte ref_pal[PAL_BYTES];
static int ref_ok;

const byte* bg_ReferencePalette(void)
{
  if (!ref_ok)
  {
    int l = W_CheckNumForName("PLAYPAL"), lx = W_CheckNumForName("PLAYPALX");

    if (l == LUMP_NOT_FOUND || W_LumpLength(l) < 768)
      return NULL;
    build_wide(ref_pal, (const byte*) W_LumpByNum(l),
               (lx != LUMP_NOT_FOUND && W_LumpLength(lx) >= 768) ? (const byte*) W_LumpByNum(lx) : NULL);
    ref_ok = 1;
  }
  return ref_pal;
}

void bg_PaletteChanged(void)
{
  ++palette_generation;
  drop_wide_cache();
}

// ---- exact nearest colour: a cube of 32 x 32 x 32 cells, each with the colours that can be the nearest to some point in it ----
#define NC_BITS 5
#define NC_CELLS (1 << NC_BITS)
#define NC_N (NC_CELLS * NC_CELLS * NC_CELLS)

struct bg_nn_s
{
  unsigned* start;                  // NC_N + 1 offsets into pool
  unsigned short* pool;
  int w[3];                         // weights of the squared differences of red, green and blue
  byte rgb[PAL_BYTES];
};

bg_nn_t* bg_NNBuild(const byte* pal, int wr, int wg, int wb)
{
  bg_nn_t* nn = calloc(1, sizeof *nn);
  unsigned total = 0, cap = 1 << 18;
  int r, g, b, k;
  float pr[BG_NUMCOLORS], pg[BG_NUMCOLORS], pb[BG_NUMCOLORS];
  int use[BG_NUMCOLORS], nuse = 0;
  const float half = 8.0f / 2.0f;
  const float slack = 2.0f * sqrtf((float) (wr + wg + wb)) * half;   // twice the largest distance from a cell's centre

  nn->start = malloc(sizeof(unsigned) * (NC_N + 1));
  nn->pool = malloc(sizeof(unsigned short) * cap);
  nn->w[0] = wr;
  nn->w[1] = wg;
  nn->w[2] = wb;
  memcpy(nn->rgb, pal, PAL_BYTES);
  for (k = 0; k < BG_NUMCOLORS; ++k)
  {
    pr[k] = pal[3 * k];
    pg[k] = pal[3 * k + 1];
    pb[k] = pal[3 * k + 2];
    if ((k & 255) != 255)
      use[nuse++] = k;
  }
  for (r = 0; r < NC_CELLS; ++r)
    for (g = 0; g < NC_CELLS; ++g)
      for (b = 0; b < NC_CELLS; ++b)
      {
        float cr = r * 8 + half, cg = g * 8 + half, cb = b * 8 + half, dmin = 1e30f, lim;
        float dd[BG_NUMCOLORS];
        int i;

        for (i = 0; i < nuse; ++i)
        {
          float er = pr[use[i]] - cr, eg = pg[use[i]] - cg, eb = pb[use[i]] - cb;

          dd[i] = sqrtf((float) wr * er * er + (float) wg * eg * eg + (float) wb * eb * eb);
          if (dd[i] < dmin)
            dmin = dd[i];
        }
        lim = dmin + slack;
        nn->start[(r * NC_CELLS + g) * NC_CELLS + b] = total;
        for (i = 0; i < nuse; ++i)
          if (dd[i] <= lim)
          {
            if (total >= cap)
            {
              cap *= 2;
              nn->pool = realloc(nn->pool, sizeof(unsigned short) * cap);
            }
            nn->pool[total++] = (unsigned short) use[i];
          }
      }
  nn->start[NC_N] = total;
  return nn;
}

void bg_NNFree(bg_nn_t* nn)
{
  if (!nn)
    return;
  free(nn->start);
  free(nn->pool);
  free(nn);
}

int bg_NNFind(const bg_nn_t* nn, int r, int g, int b)
{
  unsigned cell, i, end;
  int best = 0;
  long long bd = 1LL << 40;

  r = r < 0 ? 0 : r > 255 ? 255 : r;
  g = g < 0 ? 0 : g > 255 ? 255 : g;
  b = b < 0 ? 0 : b > 255 ? 255 : b;
  cell = (unsigned) (((r >> 3) * NC_CELLS + (g >> 3)) * NC_CELLS + (b >> 3));
  end = nn->start[cell + 1];
  for (i = nn->start[cell]; i < end; ++i)
  {
    int k = nn->pool[i];
    long long dr = nn->rgb[3 * k] - r, dg = nn->rgb[3 * k + 1] - g, db = nn->rgb[3 * k + 2] - b;
    long long d = dr * dr * nn->w[0] + dg * dg * nn->w[1] + db * db * nn->w[2];

    if (d < bd || (d == bd && k < best))
    {
      bd = d;
      best = k;
    }
  }
  return best;
}

static bg_nn_t* ref_nn;

int bg_ColorNearest(int r, int g, int b)
{
  if (!ref_nn)
  {
    const byte* pal = bg_ReferencePalette();

    if (!pal)
      return 0;
    ref_nn = bg_NNBuild(pal, 1, 1, 1);
  }
  return bg_NNFind(ref_nn, r, g, b);
}

void bg_WidenRemap(const byte* t, pixel_t* out)
{
  const byte* pal = bg_ReferencePalette();
  int i;

  for (i = 0; i < 256; ++i)
    out[i] = t[i];
  for (i = 0; i < 256; ++i)
  {
    int e = 256 + i, p = bg_ExtraPair(i);

    if (i == 255)
    {
      out[e] = t[255] == 255 ? 511 : t[255];
    }
    else if (t[i] == i && t[p] == p)
    {
      out[e] = (pixel_t) e;                       // untouched stays untouched
    }
    else if (t[i] == t[p])
    {
      out[e] = t[i];
    }
    else if (!pal)
    {
      out[e] = (pixel_t) e;
    }
    else
    {
      const byte* a = pal + 3 * t[i];
      const byte* b = pal + 3 * t[p];

      out[e] = (pixel_t) bg_ColorNearest((a[0] + b[0] + 1) / 2, (a[1] + b[1] + 1) / 2, (a[2] + b[2] + 1) / 2);
    }
  }
}

#else  // 256 colours: nothing to widen

const byte* bg_WidePalette(const byte* playpal, int count, int pal, const byte* extra, int extra_len)
{
  (void) extra; (void) extra_len;
  return (playpal && pal >= 0 && pal < count) ? playpal + (size_t) pal * 768 : NULL;
}

void bg_PaletteChanged(void)
{
  ++palette_generation;
}

const byte* bg_ReferencePalette(void)
{
  int l = W_CheckNumForName("PLAYPAL");

  return (l != LUMP_NOT_FOUND && W_LumpLength(l) >= 768) ? (const byte*) W_LumpByNum(l) : NULL;
}

int bg_ColorNearest(int r, int g, int b)
{
  const byte* pal = bg_ReferencePalette();
  int i, best = 0;
  long bd = 1L << 30;

  for (i = 0; pal && i < 255; ++i)
  {
    long dr = pal[3 * i] - r, dg = pal[3 * i + 1] - g, db = pal[3 * i + 2] - b;
    long d = dr * dr + dg * dg + db * db;

    if (d < bd)
    {
      bd = d;
      best = i;
    }
  }
  return best;
}

void bg_WidenRemap(const byte* t, pixel_t* out)
{
  int i;

  for (i = 0; i < 256; ++i)
    out[i] = t[i];
}

#endif

void bg_WidenColormap(const byte* lump, int rows, pixel_t* out)
{
  int r;

  for (r = 0; r < rows; ++r)
    bg_WidenRemap(lump + (size_t) r * 256, out + (size_t) r * BG_NUMCOLORS);
}

// ---- self test ----
int bg_SelfTest(char* msg, size_t msglen)
{
#if BG_COLORBITS > 8
  byte pp[768 * 2];
  const byte* w;
  const byte* e;
  int i, fails = 0;

  for (i = 0; i < 256 * 2; ++i)
  {
    pp[3 * i] = (byte) (i * 7);
    pp[3 * i + 1] = (byte) (i * 3 + 20);
    pp[3 * i + 2] = (byte) (255 - i);
  }
  w = bg_WidePalette(pp, 2, 1, NULL, 0);
  if (!w || memcmp(w, pp + 768, 768))
  {
    snprintf(msg, msglen, "wide palette: the first 256 colours are not the PLAYPAL");
    bg_PaletteChanged();
    return 1;
  }
  e = w + 3 * (256 + 20);       // colour 20 = row 1, col 4: middle with its partner, the next shade (21)
  if (e[0] != (byte) ((w[60] + w[63] + 1) / 2) || e[1] != (byte) ((w[61] + w[64] + 1) / 2))
  {
    snprintf(msg, msglen, "wide palette: in-between colour is not the middle of its two colours");
    ++fails;
  }
  for (i = 0; i < 256 && !fails; ++i)
  {
    int p = bg_ExtraPair(i);

    if (p < 0 || p > 255 || (p == i && i != 255))
    {
      snprintf(msg, msglen, "wide palette: colour %d has no partner", i);
      ++fails;
    }
  }
  // the nearest colour search against a plain scan (64-bit distances: a 32-bit long, as on Windows, broke it once)
  if (!fails)
  {
    bg_nn_t* nn = bg_NNBuild(w, 3, 4, 2);
    unsigned seed = 12345;
    int n;

    for (n = 0; n < 300 && !fails; ++n)
    {
      int rgb[3], k, best = -1;
      long long bd = 0;

      for (k = 0; k < 3; ++k)
      {
        seed = seed * 1664525u + 1013904223u;
        rgb[k] = (int) (seed >> 24);
      }
      for (k = 0; k < BG_NUMCOLORS; ++k)
      {
        long long dr, dg, db, d;

        if ((k & 255) == 255)
          continue;
        dr = w[3 * k] - rgb[0]; dg = w[3 * k + 1] - rgb[1]; db = w[3 * k + 2] - rgb[2];
        d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
        if (best < 0 || d < bd)
        {
          best = k;
          bd = d;
        }
      }
      k = bg_NNFind(nn, rgb[0], rgb[1], rgb[2]);
      if (k != best)
      {
        long long dr = w[3 * k] - rgb[0], dg = w[3 * k + 1] - rgb[1], db = w[3 * k + 2] - rgb[2];

        if (dr * dr * 3 + dg * dg * 4 + db * db * 2 != bd)
        {
          snprintf(msg, msglen, "nearest colour: %d,%d,%d gave %d, a scan gives %d", rgb[0], rgb[1], rgb[2], k, best);
          ++fails;
        }
      }
    }
    bg_NNFree(nn);
  }
  bg_PaletteChanged();
  if (!fails)
    snprintf(msg, msglen, "wide palette and nearest colour: ok");
  return fails;
#else
  snprintf(msg, msglen, "wide palette: 256 colours");
  return 0;
#endif
}
