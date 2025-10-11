#pragma once
#include "raylib.h"
#include "../level/level.h" // for Grid, TILE_SIZE, grid_at, TILE_FLOOR
#include <cstdint>
#include <vector>

// Kinds you can expand later
enum class EntityKind : uint8_t {
    Prop    = 0,
    Enemy   = 1,
    Item    = 2,
    Ability = 3,
};

struct Entity {
    int        id{-1};         // stable id
    EntityKind kind{EntityKind::Prop};
    Vector2    pos{0,0};       // center in pixels
    Vector2    half{8,8};      // half extents in pixels
    Color      color{0,0,0,255};
    bool       active{true};
};

// Simple container
struct EntitySystem {
    std::vector<Entity> pool;  // packed (inactive removed on destroy)
    int        nextId{1};
    uint32_t   seed{0};        // for deterministic spawns if you want
};

// --- API ---

// Init / clear
void Entities_Init(EntitySystem* es, uint32_t seed);
void Entities_Clear(EntitySystem* es);

// CRUD
int         Entities_CreateBox(EntitySystem* es, EntityKind kind, Vector2 posPx, Vector2 halfPx, Color color);
void        Entities_Destroy(EntitySystem* es, int id);
Entity*     Entities_Get(EntitySystem* es, int id);
const Entity*Entities_Get(const EntitySystem* es, int id);

void Entities_Draw(const EntitySystem* es);

// Spawner: add randomized black rectangles (“props”) on FLOOR tiles
// Returns how many spawned. Ensures tiles are floor and provides a small spacing check.
int Entities_SpawnBoxesInLevel(EntitySystem* es,
                               const Grid* g,
                               int minCount, int maxCount,
                               Vector2 halfPx,
                               uint32_t seed);
