#pragma once
#include "../../lib/box2d/include/box2d/box2d.h"
#include "raylib.h"
#include "../level/level.h"
#include "../entity/entity.hpp"

const float tick 	= 1.0f / 20.0f;
const int subSteps 	= 4;

static std::vector<b2BodyId> g_entityBodies;

// collision categories (adjust as needed)
enum CollisionBits : uint64_t {
    StaticBit  = 0x0001,
    PlayerBit  = 0x0002,
    DynamicBit    = 0x0004,
    AllBits    = ~0ull
};

// pixels <-> meters (default: 1 tile == 1 meter)
inline float PxToM(float px) { return px / (float)TILE_SIZE; }
inline float MToPx(float m)  { return m  * (float)TILE_SIZE; }
inline Vector2 PxToM(Vector2 p){ return { PxToM(p.x), PxToM(p.y) }; }
inline Vector2 MToPx(b2Vec2   p){ return { MToPx(p.x), MToPx(p.y) }; }

// world lifecycle
b2WorldId InitWorld();
void DestroyWorld(b2WorldId worldId);

// build static colliders from your tile grid (one box per wall tile)
void BuildStaticsFromGrid(b2WorldId worldId, const Grid* g);

std::vector<b2BodyId> Create_Entity_Bodies(EntitySystem* es, b2WorldId worldId); 
void Entities_Update(EntitySystem *es, float dt);

// create a dynamic player body (top-down). returns the Box2D id.
b2BodyId CreatePlayer(b2WorldId worldId, Vector2 spawnPixels,
                           float halfWidthPx, float halfHeightPx,
                           float linearDamping = 10.0f);

// fetch player world position in pixels (center)
Vector2 GetPlayerPixels(b2BodyId playerId);
