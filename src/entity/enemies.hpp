#pragma once
#include "raylib.h"
#include "../entity/entity.hpp"
#include "../physics/physics.h"
#include "../level/level.h"
#include "../../lib/box2d/include/box2d/box2d.h"

void Enemies_Spawn(EntitySystem* es, const Grid* g, Vector2 playerPos, int count, float minDist);
void Enemies_CreateBodies(EntitySystem* es, b2WorldId world);
void Enemies_Update(EntitySystem* es, const Grid* g, b2BodyId playerBody, float dt);

