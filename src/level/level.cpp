#include "level.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

// --- internal ---
static inline void set_tile(Tile* t, uint8_t id) {
    t->id = id;
    t->flags = 0;
    if (id == TILE_FLOOR) t->flags |= TF_WALKABLE;
    if (id == TILE_WALL)  t->flags |= TF_OPAQUE;
}

// xorshift32 for deterministic gen
static uint32_t xr(uint32_t* s){ uint32_t x=*s; x^=x<<13; x^=x>>17; x^=x<<5; return *s=x; }
static int rrange(uint32_t* s, int a, int b){ return a + (int)(xr(s)% (uint32_t)(b-a+1)); }

bool grid_init(Grid* g, int w, int h) {
    g->w = w; g->h = h;
    g->t = (Tile*)malloc((size_t)w*h*sizeof(Tile));
    return g->t != NULL;
}

void grid_free(Grid* g) {
    free(g->t); g->t = NULL; g->w = g->h = 0;
}

Tile* grid_at(Grid* g, int x, int y) {
    if (!in_bounds(g,x,y)) return NULL;
    return &g->t[grid_idx(g,x,y)];
}

void grid_fill(Grid* g, uint8_t id, uint8_t flags) {
    for (int i=0;i<g->w*g->h;i++){ g->t[i].id=id; g->t[i].flags=flags; }
}

void grid_set_rect(Grid* g, int x, int y, int w, int h, uint8_t id, uint8_t flags) {
    int x1 = x, y1 = y, x2 = x+w-1, y2 = y+h-1;
    if (x1<0) x1=0; if (y1<0) y1=0;
    if (x2>=g->w) x2=g->w-1; if (y2>=g->h) y2=g->h-1;
    for (int yy=y1; yy<=y2; ++yy)
    for (int xx=x1; xx<=x2; ++xx) {
        Tile* t=&g->t[grid_idx(g,xx,yy)];
        t->id=id; t->flags=flags;
    }
}

// --- carving helpers --------------------------------------------------

// Carve a floor rectangle, clamped to bounds (in tiles).
static void carve_floor_rect(Grid* g, int x, int y, int w, int h) {
    int x1 = x, y1 = y, x2 = x + w - 1, y2 = y + h - 1;
    if (x1 < 0) x1 = 0; if (y1 < 0) y1 = 0;
    if (x2 >= g->w) x2 = g->w - 1; if (y2 >= g->h) y2 = g->h - 1;
    for (int yy = y1; yy <= y2; ++yy)
    for (int xx = x1; xx <= x2; ++xx)
        set_tile(grid_at(g, xx, yy), TILE_FLOOR);
}

// Dig a wide corridor along a horizontal or vertical span centered on a line.
// width is in tiles (>=1). Centering keeps the path symmetric around the line.
static void carve_wide_span(Grid* g, int x1, int y1, int x2, int y2, int width) {
    if (width < 1) width = 1;

    if (y1 == y2) {
        // horizontal line y=y1 from x1..x2
        int y = y1;
        if (x2 < x1) { int t = x1; x1 = x2; x2 = t; }
        int half = width / 2;
        int extra = width - 1 - half; // handles odd widths
        for (int x = x1; x <= x2; ++x)
            carve_floor_rect(g, x, y - half, 1, half + 1 + extra);
    } else if (x1 == x2) {
        // vertical line x=x1 from y1..y2
        int x = x1;
        if (y2 < y1) { int t = y1; y1 = y2; y2 = t; }
        int half = width / 2;
        int extra = width - 1 - half;
        for (int y = y1; y <= y2; ++y)
            carve_floor_rect(g, x - half, y, half + 1 + extra, 1);
    }
}

// Carves an L-shaped corridor from (cx0,cy0) to (cx1,cy1) with given width.
// Randomizes whether we go horizontal-then-vertical or the reverse.
static void carve_corridor_wide(Grid* g, int cx0, int cy0, int cx1, int cy1, int width, uint32_t* rng) {
    if (xr(rng) & 1) {
        carve_wide_span(g, cx0, cy0, cx1, cy0, width);
        carve_wide_span(g, cx1, cy0, cx1, cy1, width);
    } else {
        carve_wide_span(g, cx0, cy0, cx0, cy1, width);
        carve_wide_span(g, cx0, cy1, cx1, cy1, width);
    }
}

// After carving floors, call this to surround floors with walls (cheap “outline”)
static void outline_walls(Grid* g) {
    for (int y=1; y<g->h-1; ++y)
    for (int x=1; x<g->w-1; ++x) {
        Tile* t = grid_at(g,x,y);
        if (t->id == TILE_FLOOR) continue;
        bool neighborFloor =
            (grid_at(g,x+1,y)->id==TILE_FLOOR) ||
            (grid_at(g,x-1,y)->id==TILE_FLOOR) ||
            (grid_at(g,x,y+1)->id==TILE_FLOOR) ||
            (grid_at(g,x,y-1)->id==TILE_FLOOR);
        if (neighborFloor) set_tile(t, TILE_WALL);
        else set_tile(t, TILE_WALL); // keep solid outside too
    }
}

// --- generation --------------------------------------------------------
//
// New: parameterized generator with min/max room size AND min/max corridor width.
// Pass everything via LevelGenParams for clarity.
//

void gen_level(Grid* g, const LevelGenParams* p) {
    uint32_t seed = p->seed ? p->seed : (uint32_t)time(NULL);
    uint32_t rng = seed;

    // Start fully solid
    grid_fill(g, TILE_WALL, TF_OPAQUE);

    int prev_cx = -1, prev_cy = -1;
    for (int i = 0; i < p->attempts; ++i) {
        // Room size & position
        int rw = rrange(&rng, p->roomMinW, p->roomMaxW);
        int rh = rrange(&rng, p->roomMinH, p->roomMaxH);

        // keep a 1-tile border so outline doesn't go OOB
        int rxMin = 1, ryMin = 1;
        int rxMax = g->w - rw - 2;
        int ryMax = g->h - rh - 2;
        if (rxMax < rxMin) rxMax = rxMin;
        if (ryMax < ryMin) ryMax = ryMin;

        int rx = rrange(&rng, rxMin, rxMax);
        int ry = rrange(&rng, ryMin, ryMax);

        // carve room
        carve_floor_rect(g, rx, ry, rw, rh);

        int cx = rx + rw/2;
        int cy = ry + rh/2;

        // Corridor width for this connection
        int cwidth = rrange(&rng, p->corridorMinW, p->corridorMaxW);
        if (cwidth < 1) cwidth = 1;

        if (prev_cx >= 0) {
            carve_corridor_wide(g, prev_cx, prev_cy, cx, cy, cwidth, &rng);
        }

        prev_cx = cx; prev_cy = cy;
    }

    outline_walls(g);
}

// Axis-separated sweep against blocking tiles
static bool tile_blocks(const Grid* g, int tx, int ty) {
    if (tx<0 || ty<0 || tx>=g->w || ty>=g->h) return true; // outside = solid
    return g->t[grid_idx(g,tx,ty)].id == TILE_WALL;
}

void collide_aabb_vs_walls(const Grid* g, float* px, float* py, float halfw, float halfh, float vx, float vy) {
    // Move X then Y; resolve penetration per axis by testing overlapped tile cells
    float x = *px, y = *py;

    // --- X axis ---
    x += vx;
    int left   = (int)((x - halfw) / TILE_SIZE);
    int right  = (int)((x + halfw - 0.001f) / TILE_SIZE);
    int top    = (int)((y - halfh) / TILE_SIZE);
    int bottom = (int)((y + halfh - 0.001f) / TILE_SIZE);

    if (vx > 0) {
        for (int ty=top; ty<=bottom; ++ty) {
            if (tile_blocks(g, right, ty)) {
                x = right*TILE_SIZE - halfw;
                break;
            }
        }
    } else if (vx < 0) {
        for (int ty=top; ty<=bottom; ++ty) {
            if (tile_blocks(g, left, ty)) {
                x = (left+1)*TILE_SIZE + halfw;
                break;
            }
        }
    }

    // --- Y axis ---
    y += vy;
    left   = (int)((x - halfw) / TILE_SIZE);
    right  = (int)((x + halfw - 0.001f) / TILE_SIZE);
    top    = (int)((y - halfh) / TILE_SIZE);
    bottom = (int)((y + halfh - 0.001f) / TILE_SIZE);

    if (vy > 0) {
        for (int tx=left; tx<=right; ++tx) {
            if (tile_blocks(g, tx, bottom)) {
                y = bottom*TILE_SIZE - halfh;
                break;
            }
        }
    } else if (vy < 0) {
        for (int tx=left; tx<=right; ++tx) {
            if (tile_blocks(g, tx, top)) {
                y = (top+1)*TILE_SIZE + halfh;
                break;
            }
        }
    }

    *px = x; *py = y;
}
