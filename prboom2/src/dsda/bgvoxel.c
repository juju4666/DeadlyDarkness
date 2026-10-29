//
// Begotten: voxel props. See bgvoxel.h.
// Licensed under the GNU GPL v2 or later, like the rest of the engine.
//
// How it works
//  * Models: MagicaVoxel .vox lumps. Only the cells that can be seen are kept (a cell with an empty neighbour on some
//    side), each with its colour (a palette index of the current map's palette) and which of its six faces are open.
//    The cells are grouped in blocks of BGVOX_BLOCK cells a side, for the lighting.
//  * Props: the BGVOXEL lump defines a thing type with its state chain (idle, explosion, burning wreck); the sprite of
//    its states is a "voxel sprite": R_ProjectSprite asks bgvox_Project, which makes an instance of the model for this
//    frame and a vissprite for it. So the model is sorted among the other sprites and clipped by the walls in front of
//    it, like any sprite.
//  * Drawing (bgvox_Draw): every cell that has an open face towards the viewer is drawn as a small rectangle into a
//    private depth buffer covering the model's screen box, then the buffer is copied to the screen column by column,
//    within the clipping of the walls. A cell is as big as one art pixel (1 / BGSCALE units), so the models have the
//    resolution of the sprites.
//  * Light: each (block, face direction) is lit once per frame by the same lights as sprites (bg_LightVoxel) and its
//    colormap is used for every cell of that block that shows that face.
//

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _MSC_VER
#include <strings.h>
#endif
#include "SDL.h"
#include "doomtype.h"

#include "doomstat.h"
#include "info.h"
#include "lprintf.h"
#include "m_fixed.h"
#include "p_enemy.h"
#include "p_mobj.h"
#include "r_draw.h"
#include "r_main.h"
#include "r_state.h"
#include "r_things.h"
#include "sounds.h"
#include "tables.h"
#include "v_video.h"
#include "w_wad.h"
#include "z_zone.h"

#include "dsda/begotten.h"
#include "dsda/bglight.h"
#include "dsda/bgscale.h"
#include "dsda/bgvoxel.h"
#include "dsda/mobjinfo.h"
#include "dsda/sprite.h"

#define BGVOX_BLOCK 6          // cells a side of a lighting block
#define BGVOX_MAXMODELS 48
#define BGVOX_MAXPROPS 32
#define BGVOX_MAXINST 48
#define BGVOX_MAXCELLS (6 * 1000 * 1000)

typedef struct
{
  unsigned short x, y, z;
  byte col;                    // palette index (lit through the colormap)
  byte faces;                  // open faces: 1 +x, 2 -x, 4 +y, 8 -y, 16 +z, 32 -z
  unsigned int blk;            // lighting block
} vcell_t;

typedef struct
{
  char lump[9];
  dboolean tried, ok;
  int sx, sy, sz;
  int orgx, orgy;              // the origin (where the thing stands) in cells
  vcell_t* cells;
  int ncells;
  int nblk;
  float* blkpos;               // 3 per block: the middle of its cells (in cells)
} vmodel_t;

typedef struct
{
  char lump[9];
  int model;                   // index into models, -1: not set
  double yaw;                  // degrees added to the thing's angle
  dboolean light;
  double lradius, lstrength, lcol[3];
} vframe_t;

typedef struct
{
  char key[24];
  char sprname[5];
  int ednum, sprite, type;
  int health, radius, height, mass;
  double offx, offy, offz;     // map units
  vframe_t fr[4];
  int idle, death0, death1, wreck0, wreck1;
} vprop_t;

static vmodel_t models[BGVOX_MAXMODELS];
static int num_models;
static vprop_t props[BGVOX_MAXPROPS];
static int num_props;

static double bg_Fix(fixed_t f)
{
  return (double) f / 65536.0;
}

//
// .vox files
//

static unsigned int rd32(const byte* p)
{
  return (unsigned int) p[0] | ((unsigned int) p[1] << 8) | ((unsigned int) p[2] << 16) | ((unsigned int) p[3] << 24);
}

// the game palette index closest to a colour: the ordinary colours (0..239), or the glow ramps (240..254) for the
// emissive slots of the .vox palette (slots 241..255). Index 255 is never used.
static byte vox_Nearest(const byte* pal, int r, int g, int b, dboolean glow)
{
  int i, best = glow ? 240 : 0, lo = glow ? 240 : 0, hi = glow ? 255 : 240;
  long bd = 1L << 30;

  for (i = lo; i < hi; ++i)
  {
    long dr = pal[i * 3] - r, dg = pal[i * 3 + 1] - g, db = pal[i * 3 + 2] - b;
    long d = dr * dr * 3 + dg * dg * 4 + db * db * 2;

    if (d < bd)
    {
      bd = d;
      best = i;
    }
  }
  return (byte) best;
}

// ---- reading the file: models, and the scene graph that places them (MagicaVoxel splits anything bigger than 256 cells
// into several models) ----

#define VOX_MAXRAW 512
#define VOX_MAXNODE 4096

typedef struct { int sx, sy, sz, n; const byte* xyzi; } vraw_t;
typedef struct
{
  int type;                    // 0 none, 1 transform, 2 group, 3 shape
  int child;                   // transform: its child
  int rot[3][3];               // transform: rotation (signed permutation)
  int t[3];                    // transform: translation
  int hidden;
  int nchild, first;           // group: children are kids[first .. first + nchild)
  int nmodels, mfirst;         // shape: models are mods[mfirst .. mfirst + nmodels)
} vnode_t;

static vraw_t raws[VOX_MAXRAW];
static int num_raws;
static vnode_t* vnodes;
static int* kids;
static int num_kids, cap_kids;
static int* mods;
static int num_mods, cap_mods;

static int rd_i32(const byte* d, int size, int* pos, int* ok)
{
  int v = 0;

  if (*pos + 4 > size)
  {
    *ok = 0;
    return 0;
  }
  v = (int) rd32(d + *pos);
  *pos += 4;
  return v;
}

// a DICT: pairs of strings. `want` keys are copied (up to 63 characters) into vals.
static void rd_dict(const byte* d, int size, int* pos, int* ok, const char* const* want, char (*vals)[64], int nwant)
{
  int n = rd_i32(d, size, pos, ok), i, j;

  for (i = 0; i < n && *ok; ++i)
  {
    char key[64];
    int kl = rd_i32(d, size, pos, ok), vl;

    if (!*ok || kl < 0 || *pos + kl > size)
    {
      *ok = 0;
      return;
    }
    memset(key, 0, sizeof(key));
    memcpy(key, d + *pos, (size_t) (kl < 63 ? kl : 63));
    *pos += kl;
    vl = rd_i32(d, size, pos, ok);
    if (!*ok || vl < 0 || *pos + vl > size)
    {
      *ok = 0;
      return;
    }
    for (j = 0; j < nwant; ++j)
      if (!strcmp(key, want[j]))
      {
        memset(vals[j], 0, 64);
        memcpy(vals[j], d + *pos, (size_t) (vl < 63 ? vl : 63));
      }
    *pos += vl;
  }
}

static void rot_from_byte(int r, int m[3][3])
{
  int a = r & 3, b = (r >> 2) & 3, c = 3 - a - b, i, j;

  for (i = 0; i < 3; ++i)
    for (j = 0; j < 3; ++j)
      m[i][j] = 0;
  m[0][a] = (r & 16) ? -1 : 1;
  m[1][b] = (r & 32) ? -1 : 1;
  m[2][c] = (r & 64) ? -1 : 1;
}

static void list_add(int** arr, int* n, int* cap, int v)
{
  if (*n >= *cap)
  {
    *cap = *cap ? *cap * 2 : 64;
    *arr = realloc(*arr, sizeof(int) * (size_t) *cap);
  }
  (*arr)[(*n)++] = v;
}

// walks the scene graph; `fill` NULL: only the bounds (lo, hi) are found
typedef struct
{
  int lo[3], hi[3];
  int sx, sy, sz;
  byte* occ;
  byte* colour;
  const byte* map;
} vfill_t;

static void vox_Walk(int id, const int M[3][3], const int t[3], vfill_t* f, int depth)
{
  const vnode_t* nd;
  int i;

  if (id < 0 || id >= VOX_MAXNODE || depth > 32 || vnodes[id].type == 0)
    return;
  nd = &vnodes[id];
  if (nd->hidden)
    return;
  if (nd->type == 1)
  {
    int M2[3][3], t2[3], j, k;

    for (i = 0; i < 3; ++i)
      for (j = 0; j < 3; ++j)
      {
        M2[i][j] = 0;
        for (k = 0; k < 3; ++k)
          M2[i][j] += M[i][k] * nd->rot[k][j];
      }
    for (i = 0; i < 3; ++i)
      t2[i] = t[i] + M[i][0] * nd->t[0] + M[i][1] * nd->t[1] + M[i][2] * nd->t[2];
    vox_Walk(nd->child, M2, t2, f, depth + 1);
  }
  else if (nd->type == 2)
  {
    for (i = 0; i < nd->nchild; ++i)
      vox_Walk(kids[nd->first + i], M, t, f, depth + 1);
  }
  else if (nd->type == 3)
  {
    for (i = 0; i < nd->nmodels; ++i)
    {
      int mi = mods[nd->mfirst + i], k;
      const vraw_t* r;

      if (mi < 0 || mi >= num_raws)
        continue;
      r = &raws[mi];
      for (k = 0; k < r->n; ++k)
      {
        const byte* v = r->xyzi + k * 4;
        int px = (int) v[0] - r->sx / 2, py = (int) v[1] - r->sy / 2, pz = (int) v[2] - r->sz / 2, w[3], j;

        for (j = 0; j < 3; ++j)
          w[j] = M[j][0] * px + M[j][1] * py + M[j][2] * pz + t[j];
        if (!f->occ)
        {
          for (j = 0; j < 3; ++j)
          {
            if (w[j] < f->lo[j]) f->lo[j] = w[j];
            if (w[j] > f->hi[j]) f->hi[j] = w[j];
          }
        }
        else
        {
          int gx = w[0] - f->lo[0], gy = w[1] - f->lo[1], gz = w[2] - f->lo[2];

          if (gx >= 0 && gy >= 0 && gz >= 0 && gx < f->sx && gy < f->sy && gz < f->sz)
          {
            long at = ((long) gz * f->sy + gy) * f->sx + gx;

            f->occ[at] = 1;
            f->colour[at] = f->map[v[3]];
          }
        }
      }
    }
  }
}

// Builds a model from the bytes of a .vox file. Returns false (and says why) when it is not one we can use.
static dboolean vox_Build(vmodel_t* m, const byte* d, int size, const byte* pal)
{
  const byte* rgba = NULL;
  int sx = 0, sy = 0, sz = 0, pos, i, ok = 1;
  byte* occ;
  byte* colour;
  byte map[256];
  int B = BGVOX_BLOCK, bx, by, bz, *blkid;
  long total;
  vfill_t F;
  int ident[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } }, zero[3] = { 0, 0, 0 };
  int pending[3] = { 0, 0, 0 }, have_size = 0, root = -1, anynode = 0;

  if (size < 20 || memcmp(d, "VOX ", 4))
  {
    lprintf(LO_WARN, "BGVOXEL: %s is not a MagicaVoxel .vox file\n", m->lump);
    return false;
  }
  num_raws = 0;
  num_kids = num_mods = 0;
  vnodes = calloc(VOX_MAXNODE, sizeof(vnode_t));
  kids = NULL;
  mods = NULL;
  cap_kids = cap_mods = 0;

  // chunks: id[4], content size, children size, content, children. MAIN holds the others, so just walk through.
  pos = 8;
  while (pos + 12 <= size)
  {
    const byte* ch = d + pos;
    unsigned int content = rd32(ch + 4), children = rd32(ch + 8);
    int cp = pos + 12;

    if (!memcmp(ch, "MAIN", 4))
    {
      pos += 12;                   // go inside
      continue;
    }
    if ((long) pos + 12 + (long) content > size)
      break;
    if (!memcmp(ch, "SIZE", 4) && content >= 12)
    {
      pending[0] = (int) rd32(ch + 12);
      pending[1] = (int) rd32(ch + 16);
      pending[2] = (int) rd32(ch + 20);
      have_size = 1;
    }
    else if (!memcmp(ch, "XYZI", 4) && content >= 4 && have_size && num_raws < VOX_MAXRAW)
    {
      int n = (int) rd32(ch + 12);

      if (n < 0 || (long) n * 4 + 4 > (long) content)
        n = (int) ((content - 4) / 4);
      raws[num_raws].sx = pending[0];
      raws[num_raws].sy = pending[1];
      raws[num_raws].sz = pending[2];
      raws[num_raws].n = n;
      raws[num_raws].xyzi = ch + 16;
      ++num_raws;
      have_size = 0;
    }
    else if (!memcmp(ch, "RGBA", 4) && content >= 1024)
      rgba = ch + 12;
    else if (!memcmp(ch, "nTRN", 4) || !memcmp(ch, "nGRP", 4) || !memcmp(ch, "nSHP", 4))
    {
      static const char* const want[3] = { "_r", "_t", "_hidden" };
      char vals[3][64];
      int cend = pos + 12 + (int) content, id, k;
      vnode_t* nd;

      anynode = 1;
      memset(vals, 0, sizeof(vals));
      id = rd_i32(d, cend, &cp, &ok);
      if (!ok || id < 0 || id >= VOX_MAXNODE)
        break;
      nd = &vnodes[id];
      memset(nd, 0, sizeof(*nd));
      for (i = 0; i < 3; ++i)
        for (k = 0; k < 3; ++k)
          nd->rot[i][k] = (i == k);
      if (!memcmp(ch, "nTRN", 4))
      {
        int nframes;

        nd->type = 1;
        rd_dict(d, cend, &cp, &ok, want, vals, 3);     // the node's own attributes (_hidden)
        nd->hidden = atoi(vals[2]) != 0;
        memset(vals, 0, sizeof(vals));
        nd->child = rd_i32(d, cend, &cp, &ok);
        (void) rd_i32(d, cend, &cp, &ok);              // reserved
        (void) rd_i32(d, cend, &cp, &ok);              // layer
        nframes = rd_i32(d, cend, &cp, &ok);
        if (ok && nframes > 0)
        {
          rd_dict(d, cend, &cp, &ok, want, vals, 2);   // the first frame
          if (vals[0][0])
            rot_from_byte(atoi(vals[0]), nd->rot);
          if (vals[1][0])
            sscanf(vals[1], "%d %d %d", &nd->t[0], &nd->t[1], &nd->t[2]);
        }
        if (root < 0)
          root = id;
      }
      else if (!memcmp(ch, "nGRP", 4))
      {
        int nc;

        nd->type = 2;
        rd_dict(d, cend, &cp, &ok, want, vals, 0);
        nc = rd_i32(d, cend, &cp, &ok);
        nd->first = num_kids;
        for (k = 0; k < nc && ok && k < 100000; ++k)
        {
          list_add(&kids, &num_kids, &cap_kids, rd_i32(d, cend, &cp, &ok));
          ++nd->nchild;
        }
      }
      else
      {
        int nm;

        nd->type = 3;
        rd_dict(d, cend, &cp, &ok, want, vals, 0);
        nm = rd_i32(d, cend, &cp, &ok);
        nd->mfirst = num_mods;
        for (k = 0; k < nm && ok && k < 100000; ++k)
        {
          list_add(&mods, &num_mods, &cap_mods, rd_i32(d, cend, &cp, &ok));
          ++nd->nmodels;
          rd_dict(d, cend, &cp, &ok, want, vals, 0);
        }
      }
    }
    pos += 12 + (int) content + (int) children;
  }
  if (!num_raws)
  {
    lprintf(LO_WARN, "BGVOXEL: %s has no voxels\n", m->lump);
    free(vnodes); free(kids); free(mods);
    vnodes = NULL; kids = mods = NULL;
    return false;
  }

  // palette: .vox colour v (1..255) is rgba[v - 1]; slots 241..255 are emissive
  for (i = 0; i < 256; ++i)
  {
    int r = 128, g = 128, b = 128;

    if (rgba && i >= 1)
    {
      const byte* c = rgba + (i - 1) * 4;

      r = c[0];
      g = c[1];
      b = c[2];
    }
    map[i] = vox_Nearest(pal, r, g, b, i >= 241);
  }

  // without a scene graph (old files) every model is placed at the origin; with one, the graph decides
  if (!anynode || root < 0)
  {
    vnodes[0].type = 3;
    vnodes[0].nmodels = num_raws;
    vnodes[0].mfirst = num_mods;
    for (i = 0; i < num_raws; ++i)
      list_add(&mods, &num_mods, &cap_mods, i);
    root = 0;
  }
  memset(&F, 0, sizeof(F));
  F.lo[0] = F.lo[1] = F.lo[2] = 1 << 30;
  F.hi[0] = F.hi[1] = F.hi[2] = -(1 << 30);
  vox_Walk(root, ident, zero, &F, 0);
  if (F.hi[0] < F.lo[0])
  {
    lprintf(LO_WARN, "BGVOXEL: %s has no visible voxels\n", m->lump);
    free(vnodes); free(kids); free(mods);
    vnodes = NULL; kids = mods = NULL;
    return false;
  }
  sx = F.hi[0] - F.lo[0] + 1;
  sy = F.hi[1] - F.lo[1] + 1;
  sz = F.hi[2] - F.lo[2] + 1;
  total = (long) sx * sy * sz;
  if (sx > 4096 || sy > 4096 || sz > 4096 || total > 400L * 1000 * 1000)
  {
    lprintf(LO_WARN, "BGVOXEL: %s is too big (%d x %d x %d)\n", m->lump, sx, sy, sz);
    free(vnodes); free(kids); free(mods);
    vnodes = NULL; kids = mods = NULL;
    return false;
  }
  m->sx = sx;
  m->sy = sy;
  m->sz = sz;
  m->orgx = -F.lo[0];              // the MagicaVoxel world origin, in cells: the thing stands on it
  m->orgy = -F.lo[1];

  occ = calloc((size_t) total, 1);
  colour = malloc((size_t) total);
  if (!occ || !colour)
  {
    free(occ);
    free(colour);
    free(vnodes); free(kids); free(mods);
    vnodes = NULL; kids = mods = NULL;
    return false;
  }
  F.sx = sx;
  F.sy = sy;
  F.sz = sz;
  F.occ = occ;
  F.colour = colour;
  F.map = map;
  vox_Walk(root, ident, zero, &F, 0);
  free(vnodes); free(kids); free(mods);
  vnodes = NULL; kids = mods = NULL;
  (void) ok;

  // the visible cells
  m->cells = NULL;
  m->ncells = 0;
  {
    m->cells = malloc(sizeof(vcell_t) * 65536);
  }
  bx = (sx + B - 1) / B;
  by = (sy + B - 1) / B;
  bz = (sz + B - 1) / B;
  blkid = malloc(sizeof(int) * (size_t) bx * by * bz);
  for (i = 0; i < bx * by * bz; ++i)
    blkid[i] = -1;
  m->nblk = 0;
  {
    int x, y, z;
    int cap = 256;
    int cells_cap = 65536;

    m->blkpos = malloc(sizeof(float) * 3 * cap);
    for (z = 0; z < sz; ++z)
      for (y = 0; y < sy; ++y)
        for (x = 0; x < sx; ++x)
        {
          long at = ((long) z * sy + y) * sx + x;
          int faces = 0, b;
          vcell_t* c;

          if (!occ[at])
            continue;
          if (x + 1 >= sx || !occ[at + 1]) faces |= 1;
          if (x == 0 || !occ[at - 1]) faces |= 2;
          if (y + 1 >= sy || !occ[at + sx]) faces |= 4;
          if (y == 0 || !occ[at - sx]) faces |= 8;
          if (z + 1 >= sz || !occ[at + (long) sx * sy]) faces |= 16;
          if (z == 0 || !occ[at - (long) sx * sy]) faces |= 32;
          if (!faces)
            continue;
          b = ((z / B) * by + (y / B)) * bx + (x / B);
          if (blkid[b] < 0)
          {
            if (m->nblk >= cap)
            {
              cap *= 2;
              m->blkpos = realloc(m->blkpos, sizeof(float) * 3 * cap);
            }
            blkid[b] = m->nblk;
            m->blkpos[m->nblk * 3] = m->blkpos[m->nblk * 3 + 1] = m->blkpos[m->nblk * 3 + 2] = 0;
            ++m->nblk;
          }
          if (m->ncells >= cells_cap)
          {
            cells_cap *= 2;
            m->cells = realloc(m->cells, sizeof(vcell_t) * (size_t) cells_cap);
          }
          c = &m->cells[m->ncells++];
          c->x = (unsigned short) x;
          c->y = (unsigned short) y;
          c->z = (unsigned short) z;
          c->col = colour[at];
          c->faces = (byte) faces;
          c->blk = (unsigned int) blkid[b];
        }
  }
  // the middle of each block: the mean of its cells (sums first, then divided by the count)
  {
    int* cnt = calloc((size_t) m->nblk + 1, sizeof(int));
    int k;

    for (k = 0; k < m->ncells; ++k)
    {
      const vcell_t* c = &m->cells[k];

      m->blkpos[c->blk * 3] += c->x + 0.5f;
      m->blkpos[c->blk * 3 + 1] += c->y + 0.5f;
      m->blkpos[c->blk * 3 + 2] += c->z + 0.5f;
      ++cnt[c->blk];
    }
    for (k = 0; k < m->nblk; ++k)
      if (cnt[k] > 0)
      {
        m->blkpos[k * 3] /= (float) cnt[k];
        m->blkpos[k * 3 + 1] /= (float) cnt[k];
        m->blkpos[k * 3 + 2] /= (float) cnt[k];
      }
    free(cnt);
  }
  free(blkid);
  free(occ);
  free(colour);
  if (m->ncells > BGVOX_MAXCELLS)
    lprintf(LO_WARN, "BGVOXEL: %s has %d visible cells; that will be slow\n", m->lump, m->ncells);
  return m->ncells > 0;
}

static void vox_Free(vmodel_t* m)
{
  free(m->cells);
  free(m->blkpos);
  m->cells = NULL;
  m->blkpos = NULL;
  m->ncells = m->nblk = 0;
  m->tried = m->ok = false;
}

// the model of a lump, made when first needed
static vmodel_t* vox_Get(int index)
{
  vmodel_t* m;
  int ln;
  const byte* pal;

  if (index < 0 || index >= num_models)
    return NULL;
  m = &models[index];
  if (m->tried)
    return m->ok ? m : NULL;
  m->tried = true;
  pal = V_GetPlaypal();
  ln = W_CheckNumForName(m->lump);
  if (ln < 0 || !pal)
  {
    lprintf(LO_WARN, "BGVOXEL: lump %s not found\n", m->lump);
    return NULL;
  }
  m->ok = vox_Build(m, (const byte*) W_LumpByNum(ln), W_LumpLength(ln), pal);
  return m->ok ? m : NULL;
}

static int vox_ModelIndex(const char* lump)
{
  int i;

  for (i = 0; i < num_models; ++i)
    if (!strncasecmp(models[i].lump, lump, 8))
      return i;
  if (num_models >= BGVOX_MAXMODELS)
    return -1;
  memset(&models[num_models], 0, sizeof(models[0]));
  strncpy(models[num_models].lump, lump, 8);
  models[num_models].lump[8] = '\0';
  for (i = 0; models[num_models].lump[i]; ++i)
    models[num_models].lump[i] = (char) toupper((unsigned char) models[num_models].lump[i]);
  return num_models++;
}

//
// The BGVOXEL lump and the thing types
//

static vprop_t* prop_by_key(const char* key)
{
  int i;

  for (i = 0; i < num_props; ++i)
    if (!strcmp(props[i].key, key))
      return &props[i];
  return NULL;
}

#define ACT(f) ((actionf_t) (f))

static void A_VoxWreck(mobj_t* mo)
{
  // the burning wreck stays solid and as tall as the car was (death makes a thing a quarter as tall, and a corpse)
  mo->flags &= ~(MF_CORPSE | MF_DROPOFF);
  mo->flags |= MF_SOLID;
  if (mo->info)
    mo->height = mo->info->height;
}

static int vox_State(int spr, int frame, int tics, actionf_t act)
{
  int idx = bg_AllocStates(1);
  state_t* st = &states[idx];

  memset(st, 0, sizeof(*st));
  st->sprite = spr;
  st->frame = frame;
  st->tics = tics;
  st->action = act;
  st->nextstate = idx;
  return idx;
}

static void vox_BuildProp(vprop_t* p)
{
  mobjinfo_t info = mobjinfo[MT_BARREL];
  int hasD = p->fr[3].model >= 0;

  p->sprite = dsda_AddSpriteName(p->sprname);
  p->idle = vox_State(p->sprite, 0, -1, NULL);
  p->death0 = vox_State(p->sprite, 1, 4, ACT(A_Scream));
  p->death1 = vox_State(p->sprite, 1, 1, ACT(A_Explode));
  p->wreck0 = vox_State(p->sprite, 2, 6, ACT(A_VoxWreck));
  p->wreck1 = hasD ? vox_State(p->sprite, 3, 6, ACT(A_VoxWreck)) : p->wreck0;
  states[p->death0].nextstate = p->death1;
  states[p->death1].nextstate = p->wreck0;
  states[p->wreck0].nextstate = p->wreck1;
  states[p->wreck1].nextstate = p->wreck0;
  if (!hasD)
    states[p->wreck0].nextstate = p->wreck0;

  info.doomednum = p->ednum;
  info.spawnstate = p->idle;
  info.seestate = info.painstate = info.meleestate = info.missilestate = info.raisestate = 0;
  info.xdeathstate = 0;
  info.deathstate = p->death0;
  info.spawnhealth = p->health;
  info.painchance = 0;
  info.radius = p->radius * FRACUNIT;
  info.height = p->height * FRACUNIT;
  info.mass = p->mass;
  info.flags = MF_SOLID | MF_SHOOTABLE | MF_NOBLOOD;
  info.deathsound = sfx_barexp;
  p->type = dsda_AppendMobjInfo(&info);
}

static void vox_Parse(const char* text)
{
  const char* t = text;

  while (*t)
  {
    char line[256], *tok[16], *c;
    int n = 0, i = 0;

    while (*t && *t != '\n' && i < (int) sizeof(line) - 1)
      line[i++] = *t++;
    line[i] = '\0';
    if (*t == '\n')
      ++t;
    for (c = line; *c; ++c)
      if (*c == '#' || (c[0] == '/' && c[1] == '/'))
      {
        *c = '\0';
        break;
      }
    for (c = line; *c;)
    {
      while (*c && isspace((unsigned char) *c))
        ++c;
      if (!*c)
        break;
      if (n < 16)
        tok[n++] = c;
      while (*c && !isspace((unsigned char) *c))
        ++c;
      if (*c)
        *c++ = '\0';
    }
    if (n < 2)
      continue;
    if (!strcasecmp(tok[0], "prop") && n >= 3)
    {
      vprop_t* p;
      int k;

      if (num_props >= BGVOX_MAXPROPS || prop_by_key(tok[1]))
        continue;
      p = &props[num_props++];
      memset(p, 0, sizeof(*p));
      strncpy(p->key, tok[1], sizeof(p->key) - 1);
      p->ednum = atoi(tok[2]);
      p->health = 150;
      p->radius = 48;
      p->height = 48;
      p->mass = 1000;
      for (k = 0; k < 4; ++k)
        p->fr[k].model = -1;
      for (k = 3; k < n; ++k)
      {
        if (!strcasecmp(tok[k], "sprite") && k + 1 < n)
        {
          strncpy(p->sprname, tok[++k], 4);
          for (i = 0; i < 4; ++i)
            p->sprname[i] = (char) toupper((unsigned char) p->sprname[i]);
        }
        else if (!strcasecmp(tok[k], "radius") && k + 1 < n) p->radius = atoi(tok[++k]);
        else if (!strcasecmp(tok[k], "height") && k + 1 < n) p->height = atoi(tok[++k]);
        else if (!strcasecmp(tok[k], "health") && k + 1 < n) p->health = atoi(tok[++k]);
        else if (!strcasecmp(tok[k], "mass") && k + 1 < n) p->mass = atoi(tok[++k]);
        else if (!strcasecmp(tok[k], "offset") && k + 3 < n)
        {
          p->offx = atof(tok[k + 1]);
          p->offy = atof(tok[k + 2]);
          p->offz = atof(tok[k + 3]);
          k += 3;
        }
      }
      if (strlen(p->sprname) != 4)
      {
        lprintf(LO_WARN, "BGVOXEL: prop %s needs `sprite XXXX` (four letters)\n", p->key);
        --num_props;
      }
    }
    else if (!strcasecmp(tok[0], "model") && n >= 4)
    {
      vprop_t* p = prop_by_key(tok[1]);
      int f = toupper((unsigned char) tok[2][0]) - 'A', k;
      vframe_t* fr;

      if (!p || f < 0 || f > 3)
      {
        lprintf(LO_WARN, "BGVOXEL: `model %s %s` : unknown prop or frame (A..D)\n", tok[1], tok[2]);
        continue;
      }
      fr = &p->fr[f];
      strncpy(fr->lump, tok[3], 8);
      fr->model = vox_ModelIndex(tok[3]);
      fr->yaw = 0;
      for (k = 4; k < n; ++k)
      {
        if (!strcasecmp(tok[k], "yaw") && k + 1 < n)
          fr->yaw = atof(tok[++k]);
        else if (!strcasecmp(tok[k], "light") && k + 5 < n)
        {
          fr->light = true;
          fr->lradius = atof(tok[k + 1]);
          fr->lstrength = atof(tok[k + 2]);
          fr->lcol[0] = atof(tok[k + 3]);
          fr->lcol[1] = atof(tok[k + 4]);
          fr->lcol[2] = atof(tok[k + 5]);
          k += 5;
        }
      }
    }
  }
}

void bgvox_Init(void)
{
  int i;

  num_props = 0;
  num_models = 0;
  for (i = 0; i < numlumps; ++i)
  {
    char* text;
    int size;

    if (strncasecmp(lumpinfo[i].name, "BGVOXEL", 7))
      continue;
    size = W_LumpLength(i);
    text = malloc((size_t) size + 1);
    memcpy(text, W_LumpByNum(i), (size_t) size);
    text[size] = '\0';
    vox_Parse(text);
    free(text);
  }
  for (i = 0; i < num_props; ++i)
  {
    if (props[i].fr[0].model < 0)
    {
      lprintf(LO_WARN, "BGVOXEL: prop %s has no `model %s A ...` (the idle model)\n", props[i].key, props[i].key);
      continue;
    }
    if (props[i].fr[1].model < 0)
      props[i].fr[1] = props[i].fr[0];
    if (props[i].fr[2].model < 0)
      props[i].fr[2] = props[i].fr[1];
    vox_BuildProp(&props[i]);
    lprintf(LO_INFO, "BGVOXEL: %s (%d) uses %s\n", props[i].key, props[i].ednum, props[i].fr[0].lump);
  }
}

void bgvox_NewLevel(void)
{
  int i;

  for (i = 0; i < num_models; ++i)
    vox_Free(&models[i]);
}

int bgvox_Count(void)
{
  return num_props;
}

static const vprop_t* prop_of_sprite(int sprite)
{
  int i;

  for (i = 0; i < num_props; ++i)
    if (props[i].sprite == sprite && props[i].type > 0)
      return &props[i];
  return NULL;
}

dboolean bgvox_FrameLight(const mobj_t* thing, double* radius, double* strength, double* col)
{
  const vprop_t* p;
  const vframe_t* fr;
  int f;

  if (!num_props || !thing->state || !(p = prop_of_sprite(thing->state->sprite)))
    return false;
  f = thing->state->frame & 0x7fff;
  if (f < 0 || f > 3 || !p->fr[f].light)
    return false;
  fr = &p->fr[f];
  *radius = fr->lradius;
  *strength = fr->lstrength;
  col[0] = fr->lcol[0];
  col[1] = fr->lcol[1];
  col[2] = fr->lcol[2];
  return true;
}

//
// Instances (one per model drawn this frame)
//

typedef struct
{
  const vmodel_t* m;
  double ox, oy, oz;           // the model's origin (the middle of its footprint, on its floor) in the world
  double ca, sa;               // thing angle plus the frame's yaw
  double vs;                   // one cell, in map units
  double cx0, cy0;             // the origin of the model in cells
  double tx0, tz0;             // the origin in view space
  double xx, xz, yx, yz;       // view-space (tx, tz) per local unit along x and y
  double camx, camy, camz;     // the viewer in model space (map units, origin at the model origin)
  int bx0, by0, bx1, by1;      // screen box
  const lighttable_t* base;
  const sector_t* sector;
} vinst_t;

static vinst_t inst[BGVOX_MAXINST];
static int num_inst;
static int cells_drawn;
static long stat_light_evals;
static double stat_ms_light, stat_ms_total;
static int stat_frames;

static double now_ms(void)
{
  return 1000.0 * (double) SDL_GetPerformanceCounter() / (double) SDL_GetPerformanceFrequency();
}

static float* zbuf;
static byte* pbuf;
static size_t zcap;
static const lighttable_t** lcache;
static size_t lcap;

void bgvox_BeginFrame(void)
{
  num_inst = 0;
  cells_drawn = 0;
}

int bgvox_CellsDrawn(void)
{
  return cells_drawn;
}

int bgvox_Project(const mobj_t* thing, fixed_t fx, fixed_t fy, fixed_t fz, int* x1, int* x2, fixed_t* scale,
                  fixed_t* gz, fixed_t* gzt)
{
  const vprop_t* p;
  const vframe_t* fr;
  vmodel_t* m;
  vinst_t* I;
  int f, i;
  double ang, vcos, vsin, proj, projy, cx, cy, vz, vs;
  double lo[3], hi[3];
  double minx = 1e9, maxx = -1e9, miny = 1e9, maxy = -1e9;
  int behind = 0;
  double tzsum = 0;

  if (!num_props || !thing->state || thing->player)
    return 0;
  p = prop_of_sprite(thing->state->sprite);
  if (!p)
    return 0;
  f = thing->state->frame & 0x7fff;
  if (f < 0 || f > 3 || p->fr[f].model < 0 || num_inst >= BGVOX_MAXINST)
    return 0;
  fr = &p->fr[f];
  m = vox_Get(fr->model);
  if (!m)
    return 0;

  I = &inst[num_inst];
  vs = 1.0 / bg_Fix(bg_scale_sprites);
  I->m = m;
  I->vs = vs;
  ang = (double) thing->angle * (2.0 * M_PI / 4294967296.0) + fr->yaw * M_PI / 180.0;
  I->ca = cos(ang);
  I->sa = sin(ang);
  I->cx0 = m->orgx;
  I->cy0 = m->orgy;
  I->ox = bg_Fix(fx) + p->offx * I->ca - p->offy * I->sa;
  I->oy = bg_Fix(fy) + p->offx * I->sa + p->offy * I->ca;
  I->oz = bg_Fix(fz) + p->offz;
  vcos = bg_Fix(viewcos);
  vsin = bg_Fix(viewsin);
  proj = bg_Fix(projection);
  projy = bg_Fix(projectiony);
  cx = bg_Fix(centerxfrac);
  cy = bg_Fix(centeryfrac);
  vz = bg_Fix(viewz);
  {
    double dx = I->ox - bg_Fix(viewx), dy = I->oy - bg_Fix(viewy);

    I->tz0 = dx * vcos + dy * vsin;
    I->tx0 = dx * vsin - dy * vcos;
  }
  I->xz = I->ca * vcos + I->sa * vsin;
  I->xx = I->ca * vsin - I->sa * vcos;
  I->yz = -I->sa * vcos + I->ca * vsin;
  I->yx = -I->sa * vsin - I->ca * vcos;
  {
    double dx = bg_Fix(viewx) - I->ox, dy = bg_Fix(viewy) - I->oy;

    I->camx = dx * I->ca + dy * I->sa;
    I->camy = -dx * I->sa + dy * I->ca;
    I->camz = vz - I->oz;
  }

  // screen box: the eight corners of the model's box
  lo[0] = -I->cx0 * vs;  hi[0] = (m->sx - I->cx0) * vs;
  lo[1] = -I->cy0 * vs;  hi[1] = (m->sy - I->cy0) * vs;
  lo[2] = 0;             hi[2] = m->sz * vs;
  for (i = 0; i < 8; ++i)
  {
    double lx = (i & 1) ? hi[0] : lo[0], ly = (i & 2) ? hi[1] : lo[1], lz = (i & 4) ? hi[2] : lo[2];
    double tz = I->tz0 + lx * I->xz + ly * I->yz, tx = I->tx0 + lx * I->xx + ly * I->yx;

    tzsum += tz;
    if (tz < 8.0)
    {
      behind = 1;
      continue;
    }
    {
      double sx = cx + tx * proj / tz, sy = cy - (I->oz + lz - vz) * projy / tz;

      if (sx < minx) minx = sx;
      if (sx > maxx) maxx = sx;
      if (sy < miny) miny = sy;
      if (sy > maxy) maxy = sy;
    }
  }
  if (behind)
  {
    // a corner is behind the viewer: the box could be anywhere, so take the whole view (the cells decide what shows)
    minx = 0; maxx = viewwidth - 1; miny = 0; maxy = viewheight - 1;
  }
  I->bx0 = (int) floor(minx) - 1;
  I->bx1 = (int) ceil(maxx) + 1;
  I->by0 = (int) floor(miny) - 1;
  I->by1 = (int) ceil(maxy) + 1;
  if (I->bx0 < 0) I->bx0 = 0;
  if (I->by0 < 0) I->by0 = 0;
  if (I->bx1 >= viewwidth) I->bx1 = viewwidth - 1;
  if (I->by1 >= viewheight) I->by1 = viewheight - 1;
  if (I->bx1 < I->bx0 || I->by1 < I->by0)
    return 0;
  {
    double tzc = tzsum / 8.0;

    if (tzc < 8.0)
      tzc = 8.0;
    *scale = (fixed_t) (projy / tzc * 65536.0);
  }
  *x1 = I->bx0;
  *x2 = I->bx1;
  *gz = (fixed_t) ((I->oz) * 65536.0);
  *gzt = (fixed_t) ((I->oz + m->sz * vs) * 65536.0);
  I->base = NULL;
  I->sector = NULL;
  ++num_inst;
  return num_inst;
}

void bgvox_SetBase(int inst_no, const lighttable_t* colormap, const sector_t* sector)
{
  if (inst_no < 1 || inst_no > num_inst)
    return;
  inst[inst_no - 1].base = colormap;
  inst[inst_no - 1].sector = sector;
}

void bgvox_Draw(int inst_no, const int* floorclip, const int* ceilingclip)
{
  const vinst_t* I;
  const vmodel_t* m;
  int w, h, x, y, k;
  double t_start = 0, t_light = 0;
  size_t need;
  double vs, hs, proj, projy, cx, cy, vz, hwk, hhk;

  if (inst_no < 1 || inst_no > num_inst)
    return;
  I = &inst[inst_no - 1];
  m = I->m;
  w = I->bx1 - I->bx0 + 1;
  h = I->by1 - I->by0 + 1;
  if (w <= 0 || h <= 0 || !I->base)
    return;
  t_start = now_ms();
  need = (size_t) w * (size_t) h;
  if (need > zcap)
  {
    free(zbuf);
    free(pbuf);
    zbuf = malloc(need * sizeof(float));
    pbuf = malloc(need);
    zcap = need;
  }
  {
    size_t nl = (size_t) m->nblk * 6;

    if (nl > lcap)
    {
      free(lcache);
      lcache = malloc(nl * sizeof(*lcache));
      lcap = nl;
    }
    memset(lcache, 0, nl * sizeof(*lcache));
  }
  for (k = 0; k < (int) need; ++k)
    zbuf[k] = 1e30f;

  vs = I->vs;
  hs = 0.5 * vs;
  proj = bg_Fix(projection);
  projy = bg_Fix(projectiony);
  cx = bg_Fix(centerxfrac);
  cy = bg_Fix(centeryfrac);
  vz = bg_Fix(viewz);
  hwk = hs * (fabs(I->xx) + fabs(I->yx)) * proj;
  hhk = hs * projy;

  for (k = 0; k < m->ncells; ++k)
  {
    const vcell_t* c = &m->cells[k];
    double lx = (c->x + 0.5 - I->cx0) * vs, ly = (c->y + 0.5 - I->cy0) * vs, lz = (c->z + 0.5) * vs;
    double ddx = I->camx - lx, ddy = I->camy - ly, ddz = I->camz - lz;
    int vis = 0, face = -1;
    double best = -1;
    double tz, tx, sx, sy, hw, hh, inv;
    int px0, px1, py0, py1;
    const lighttable_t* cm;
    size_t cache_at;

    // which open faces point at the viewer, and the most face-on of them (the one that is lit)
    if ((c->faces & 1) && ddx > hs)  { vis = 1; if (ddx > best) { best = ddx; face = 0; } }
    if ((c->faces & 2) && ddx < -hs) { vis = 1; if (-ddx > best) { best = -ddx; face = 1; } }
    if ((c->faces & 4) && ddy > hs)  { vis = 1; if (ddy > best) { best = ddy; face = 2; } }
    if ((c->faces & 8) && ddy < -hs) { vis = 1; if (-ddy > best) { best = -ddy; face = 3; } }
    if ((c->faces & 16) && ddz > hs) { vis = 1; if (ddz > best) { best = ddz; face = 4; } }
    if ((c->faces & 32) && ddz < -hs){ vis = 1; if (-ddz > best) { best = -ddz; face = 5; } }
    if (!vis)
      continue;

    tz = I->tz0 + lx * I->xz + ly * I->yz;
    if (tz < 4.0)
      continue;
    inv = 1.0 / tz;
    tx = I->tx0 + lx * I->xx + ly * I->yx;
    sx = cx + tx * proj * inv;
    sy = cy - (I->oz + lz - vz) * projy * inv;
    hw = hwk * inv;
    hh = hhk * inv;
    px0 = (int) (sx - hw + 4096.0) - 4096;      // floor, for the range the screen can reach
    px1 = (int) (sx + hw + 4096.0) - 4096;
    py0 = (int) (sy - hh + 4096.0) - 4096;
    py1 = (int) (sy + hh + 4096.0) - 4096;
    if ((double) px1 == sx + hw) --px1;
    if ((double) py1 == sy + hh) --py1;
    if (px1 < px0) px1 = px0;
    if (py1 < py0) py1 = py0;
    if (px1 < I->bx0 || px0 > I->bx1 || py1 < I->by0 || py0 > I->by1)
      continue;
    if (px0 < I->bx0) px0 = I->bx0;
    if (px1 > I->bx1) px1 = I->bx1;
    if (py0 < I->by0) py0 = I->by0;
    if (py1 > I->by1) py1 = I->by1;

    // the light of this block and face, once per frame
    cache_at = (size_t) c->blk * 6 + (size_t) face;
    cm = lcache[cache_at];
    if (!cm)
    {
      double n[3], bx, by, bz, wx, wy, wz;
      static const int sgn[6] = { 1, -1, 1, -1, 1, -1 };

      // block middle in the model (map units), then the world; the point is a cell out from the surface
      bx = (m->blkpos[c->blk * 3] - I->cx0) * vs;
      by = (m->blkpos[c->blk * 3 + 1] - I->cy0) * vs;
      bz = m->blkpos[c->blk * 3 + 2] * vs;
      switch (face)
      {
        case 0: case 1: n[0] = sgn[face] * I->ca;  n[1] = sgn[face] * I->sa;  n[2] = 0; break;
        case 2: case 3: n[0] = -sgn[face] * I->sa; n[1] = sgn[face] * I->ca;  n[2] = 0; break;
        default:        n[0] = 0; n[1] = 0; n[2] = sgn[face]; break;
      }
      wx = I->ox + bx * I->ca - by * I->sa + n[0] * vs * 2.0;
      wy = I->oy + bx * I->sa + by * I->ca + n[1] * vs * 2.0;
      wz = I->oz + bz + n[2] * vs * 2.0;
      {
        double t0 = now_ms();

        cm = bg_LightVoxel(I->base, wx, wy, wz, n, I->sector);
        t_light += now_ms() - t0;
        ++stat_light_evals;
      }
      lcache[cache_at] = cm;
    }

    {
      byte pix = cm[c->col];
      int yy, xx;

      if (px0 == px1 && py0 == py1)
      {
        size_t at = (size_t) (py0 - I->by0) * (size_t) w + (size_t) (px0 - I->bx0);

        if ((float) tz < zbuf[at])
        {
          zbuf[at] = (float) tz;
          pbuf[at] = pix;
        }
        ++cells_drawn;
        continue;
      }
      for (yy = py0; yy <= py1; ++yy)
      {
        size_t row = (size_t) (yy - I->by0) * (size_t) w;

        for (xx = px0; xx <= px1; ++xx)
        {
          size_t at = row + (size_t) (xx - I->bx0);

          if ((float) tz < zbuf[at])
          {
            zbuf[at] = (float) tz;
            pbuf[at] = pix;
          }
        }
      }
    }
    ++cells_drawn;
  }

  // to the screen, inside the clipping of the walls
  for (x = I->bx0; x <= I->bx1; ++x)
  {
    int y0 = I->by0, y1 = I->by1;

    if (y0 <= ceilingclip[x]) y0 = ceilingclip[x] + 1;
    if (y1 >= floorclip[x]) y1 = floorclip[x] - 1;
    for (y = y0; y <= y1; ++y)
    {
      size_t at = (size_t) (y - I->by0) * (size_t) w + (size_t) (x - I->bx0);

      if (zbuf[at] < 1e29f)
        drawvars.topleft[y * drawvars.pitch + x] = pbuf[at];
    }
  }
  stat_ms_light += t_light;
  stat_ms_total += now_ms() - t_start;
  if (getenv("BGVOX_STATS") && inst_no == num_inst && ++stat_frames >= 60)
  {
    lprintf(LO_INFO, "BGVOX: %.1f ms per frame (light %.1f ms), %d cells drawn, %ld light evaluations over 60 frames\n", stat_ms_total / stat_frames, stat_ms_light / stat_frames, cells_drawn, stat_light_evals);
    stat_frames = 0; stat_ms_total = stat_ms_light = 0; stat_light_evals = 0;
  }
}

//
// Self test: a .vox made in memory (a solid 3 x 3 x 3 cube plus a floating cell) must give 26 + 1 visible cells, and
// the middle cell of the cube must be hidden.
//

static void put32(byte* p, unsigned int v)
{
  p[0] = (byte) v; p[1] = (byte) (v >> 8); p[2] = (byte) (v >> 16); p[3] = (byte) (v >> 24);
}

int bgvox_SelfTest(char* msg, size_t msglen)
{
  byte file[4096];
  int pos = 0, n = 0, x, y, z, fails = 0;
  vmodel_t m;
  byte pal[768];
  int i;
  int main_children_at;

  memset(file, 0, sizeof(file));
  memcpy(file, "VOX ", 4);
  put32(file + 4, 150);
  pos = 8;
  memcpy(file + pos, "MAIN", 4);
  put32(file + pos + 4, 0);
  main_children_at = pos + 8;
  pos += 12;
  memcpy(file + pos, "SIZE", 4);
  put32(file + pos + 4, 12); put32(file + pos + 8, 0);
  put32(file + pos + 12, 5); put32(file + pos + 16, 5); put32(file + pos + 20, 5);
  pos += 24;
  memcpy(file + pos, "XYZI", 4);
  {
    byte* v = file + pos + 16;

    for (z = 0; z < 3; ++z)
      for (y = 0; y < 3; ++y)
        for (x = 0; x < 3; ++x, ++n, v += 4)
          v[0] = (byte) x, v[1] = (byte) y, v[2] = (byte) z, v[3] = 1;
    v[0] = 4; v[1] = 4; v[2] = 4; v[3] = 2;   // a cell on its own
    ++n;
    put32(file + pos + 12, (unsigned) n);
    put32(file + pos + 4, 4 + n * 4); put32(file + pos + 8, 0);
    pos += 12 + 4 + n * 4;
  }
  put32(file + main_children_at, (unsigned) (pos - 20));
  for (i = 0; i < 255; ++i)
    pal[i * 3] = pal[i * 3 + 1] = pal[i * 3 + 2] = (byte) i;
  pal[765] = pal[766] = pal[767] = 0;
  memset(&m, 0, sizeof(m));
  strcpy(m.lump, "TEST");
  if (!vox_Build(&m, file, pos, pal))
    ++fails;
  else
  {
    if (m.ncells != 27)
    {
      ++fails;
      snprintf(msg, msglen, "voxel test: %d visible cells, expected 27", m.ncells);
    }
    if (m.sx != 5 || m.sz != 5)
      ++fails;
  }
  vox_Free(&m);
  if (!fails)
    snprintf(msg, msglen, "voxel loader: ok");
  return fails;
}
