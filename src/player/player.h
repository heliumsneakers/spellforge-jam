#pragma once
#include "raylib.h"
#include "../level/level.h"
#include "../entity/entity.hpp"
#include "../../lib/box2d/include/box2d/box2d.h"
#include "../anims/animations.hpp"

typedef struct {
    Vector2 pos;      // center position in pixels
    Vector2 vel;      // per-frame velocity in pixels
    float   halfw;
    float   halfh;
    float   speed;    // pixels/sec

    // Animations
    Animation idleAnim;
    Animation runAnim;
    Animation* currentAnim;
    bool facingRight;

    // Camera
    Camera2D cam;
    float    camSmooth; // ~follow stiffness (higher = snappier)
    float    camZoom;   // 1.0 = 1:1 pixels
    
} Player;

void Telekinesis_Hold(Vector2 pos, float radius, Vector2 force, EntitySystem* es);
void Telekinesis_Fire(Vector2 playerPos, float orbitRadius, float launchForce, EntitySystem* es);
void Player_Init(Player* p, const Grid* level);
Vector2 Build_Input();
void UpdatePlayer(Player* p, b2BodyId playerId, float dt, Vector2 inputDir, float speedPxPerSec);
void Player_Draw(const Player* p);
void Player_Unload(Player* p);

