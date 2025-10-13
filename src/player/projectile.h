#pragma once
#include "raylib.h"
#include "../../lib/box2d/include/box2d/box2d.h"
#include <vector>

enum class ProjectileType { FIRE, ICE };

struct Projectile {
    ProjectileType type;
    b2BodyId body;
    Color color;
    float lifetime;
    bool active;
};

extern std::vector<Projectile> g_projectiles;
extern ProjectileType g_currentProjectile;

void Projectile_HandleSwitch();
void Projectile_Shoot(b2WorldId world, Vector2 playerPos, Camera2D cam);
void Projectile_Update(b2WorldId world, float dt);
void Projectile_Draw();
