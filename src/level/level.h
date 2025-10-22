#pragma once
#include <stdint.h>
#include <stdbool.h>

#define TILE_SIZE 32   // pixels per tile

typedef enum : uint8_t {
    TILE_WALL  = 0,
    TILE_FLOOR = 1,
} TileID;

enum {
    TF_WALKABLE = 1u << 0,
    TF_OPAQUE   = 1u << 1,
};

typedef struct {
    uint8_t id;     // TileID
    uint8_t flags;  // TF_*
} Tile;

typedef struct {
    int w, h;   // in tiles
    Tile* t;    // length = w*h (row-major)
} Grid;

typedef struct {
    int attempts;        // number of room placement attempts
    int roomMinW, roomMinH;
    int roomMaxW, roomMaxH;
    int corridorMinW;    // min corridor width in tiles (>=1)
    int corridorMaxW;    // max corridor width in tiles (>=corridorMinW)
    uint32_t seed;       // 0 -> time-based fully random
} LevelGenParams;


bool  grid_init(Grid* g, int w, int h);
void  grid_free(Grid* g);
Tile* grid_at(Grid* g, int x, int y);
void  grid_fill(Grid* g, uint8_t id, uint8_t flags);
void  grid_set_rect(Grid* g, int x, int y, int w, int h, uint8_t id, uint8_t flags);

void gen_level(Grid* g, const LevelGenParams* p);

// Collision against WALL tiles. Mutates pos to resolve.
void  collide_aabb_vs_walls(const Grid* g, float* px, float* py, float halfw, float halfh, float vx, float vy);

// Helpers
static inline int grid_idx(const Grid* g, int x, int y) { return y*g->w + x; }
static inline bool in_bounds(const Grid* g, int x, int y) { return x>=0 && y>=0 && x<g->w && y<g->h; }

