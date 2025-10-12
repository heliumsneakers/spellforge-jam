#pragma once
#include "raylib.h"
#include "../level/level.h"
#include "../../lib/box2d/include/box2d/box2d.h"

typedef struct {
    Vector2 pos;      // center position in pixels
    Vector2 vel;      // per-frame velocity in pixels
    float   halfw;
    float   halfh;
    float   speed;    // pixels/sec

    // Camera
    Camera2D cam;
    float    camSmooth; // ~follow stiffness (higher = snappier)
    float    camZoom;   // 1.0 = 1:1 pixels
} Player;

void Player_Init(Player* p, const Grid* level);
Vector2 Build_Input();
void UpdatePlayer(b2BodyId playerId, float dt, Vector2 inputDir, float speedPxPerSec);
void Player_Draw(const Player* p);
