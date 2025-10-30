#pragma once
#include "raylib.h"
#include "../entity/entity.hpp"
#include "../physics/physics.h"
#include "../level/level.h"
#include "../anims/animations.hpp"
#include "../../lib/box2d/include/box2d/box2d.h"
#include <cstdint>

extern int g_enemiesKilled;
extern int g_wave;
extern float g_speedMultiplier;

enum class EnemyAnimState : uint8_t {Run};

struct Enemy {
    int entId = 0;
    float health = 0.0f;
    float maxHealth = 0.0f;
    float slowTimer = 0.0f;

    Animation runAnim;
    EnemyAnimState animState = EnemyAnimState::Run;
    bool facingRight;

    std::vector<Vector2> path;
    int waypoint = 0;
    float repathCd = 0.0f;
};

extern std::vector<Enemy> g_enemies;
extern std::unordered_map<int, size_t> g_enemyIndexByEntId;

void Enemies_Clear();
Enemy* Enemy_FromEntityId(int entId);
void Enemies_Spawn(EntitySystem* es, const Grid* g, Vector2 playerPos, int count, float minDist);
void Enemies_CreateBodies(EntitySystem* es, b2WorldId world, size_t startIndex);
void Enemies_Update(EntitySystem* es, const Grid* g, b2BodyId playerBody, float dt);
void Spawn_Corpse_Prop(EntitySystem* es, b2WorldId world, Vector2 pos);
void Enemies_Draw(const EntitySystem* es);

