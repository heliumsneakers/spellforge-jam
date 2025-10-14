#pragma once
#include "raylib.h"
#include "../entity/entity.hpp"
#include "../physics/physics.h"
#include "../level/level.h"
#include "../../lib/box2d/include/box2d/box2d.h"

extern int g_enemiesKilled;
extern int g_wave;
extern float g_speedMultiplier;

void Enemies_Spawn(EntitySystem* es, const Grid* g, Vector2 playerPos, int count, float minDist);
void Enemies_CreateBodies(EntitySystem* es, b2WorldId world, size_t startIndex);
void Enemies_Update(EntitySystem* es, const Grid* g, b2BodyId playerBody, float dt);
void Spawn_Corpse_Prop(EntitySystem* es, b2WorldId world, Vector2 pos);
