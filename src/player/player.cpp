#include "player.h"
#include "../level/level.h"
#include "../physics/physics.h"
#include <math.h>
#include <raylib.h>
#include "raymath.h"
#include <time.h>
#include <vector>

static bool HasSpaceAround(const Grid* g, int x, int y, int radius) {
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            int nx = x + dx;
            int ny = y + dy;
            Tile* t = grid_at((Grid*)g, nx, ny);
            if (!t || t->id != TILE_FLOOR) {
                return false; // any wall or out-of-bounds fails
            }
        }
    }
    return true;
}

static Vector2 FindFloorSpawn(const Grid* g) {
    // fallback center if nothing suitable
    Vector2 fallback = { (float)(g->w * TILE_SIZE / 2), (float)(g->h * TILE_SIZE / 2) };

    // collect all floor candidates
    int capacity = g->w * g->h;
    int count = 0;
    int* candidates = (int*)malloc(sizeof(int) * capacity);

    for (int y = 0; y < g->h; ++y) {
        for (int x = 0; x < g->w; ++x) {
            Tile* t = grid_at((Grid*)g, x, y);
            if (t && t->id == TILE_FLOOR && HasSpaceAround(g, x, y, 3)) {
                candidates[count++] = y * g->w + x;
            }
        }
    }

    if (count == 0) {
        free(candidates);
        return fallback;
    }

    // pick random from valid rooms
    srand((unsigned int)time(NULL));
    int pick = candidates[rand() % count];
    free(candidates);

    int px = pick % g->w;
    int py = pick / g->w;

    return (Vector2){
        px * TILE_SIZE + TILE_SIZE * 0.5f,
        py * TILE_SIZE + TILE_SIZE * 0.5f
    };
}

// Exponential "lerp": returns value moved toward target by factor based on dt
static inline float damp(float current, float target, float lambda, float dt) {
    // lambda ~ stiffness per second; avoids framerate dependence
    // alpha = 1 - e^{-lambda * dt}
    float a = 1.0f - expf(-lambda * dt);
    return current + (target - current) * a;
}

void Player_Init(Player* p, const Grid* level) {
    p->pos   = FindFloorSpawn(level);
    p->vel   = (Vector2){ 0, 0 };
    p->halfw = 12.0f;
    p->halfh = 12.0f;
    p->speed = 180.0f;

    // Camera setup
    p->camZoom   = 1.0f;
    p->camSmooth = 8.0f;

    p->cam.target   = p->pos;   // follow player (world coords)
    p->cam.offset   = (Vector2){ GetScreenWidth()*0.5f, GetScreenHeight()*0.5f };
    p->cam.rotation = 0.0f;
    p->cam.zoom     = p->camZoom;
}

Vector2 Build_Input() {
    Vector2 dir = { 0, 0 };
        if (IsKeyDown(KEY_W)) dir.y -= 1;
        if (IsKeyDown(KEY_S)) dir.y += 1;
        if (IsKeyDown(KEY_A)) dir.x -= 1;
        if (IsKeyDown(KEY_D)) dir.x += 1;
    return dir;
}

void apply_ent_force(EntitySystem* es, Vector2 force) {
    const size_t n = es->pool.size();
    for(size_t i = 0; i < n; ++i) {
        b2BodyId body;
        g_entityBodies[i] = body;
        b2Body_ApplyForce(body, b2Vec2 {PxToM(force.x), PxToM(force.y)}, b2Vec2 {10, 10}, true);
    }
}

void Telekenesis_Hold(Vector2 pos, float radius, Vector2 force, EntitySystem* ents) {
    if(IsKeyDown(MOUSE_BUTTON_LEFT)) {
       for(auto entities : ents->pool) {
            Vector2 delta = Vector2Subtract(entities.pos, pos);
            float distance = Vector2Length(delta);
            if (distance < radius) {
                Vector2 normalized = Vector2Scale(delta, 1.0f / distance);
            }
        } 
    }
}

// INSPO FROM VERLET ENGINE
// void PickUpParticles(Vector2 position, float radius, Vector2 force) {
//     for (auto& particle : particles) {
//         Vector2 delta = Vector2Subtract(particle.position, position);
//         float distance = Vector2Length(delta);
//         if (distance < radius) {
//             Vector2 normalized = Vector2Scale(delta, 1.0f / distance);
//             ApplyForce(particle, Vector2Scale(normalized, force.x));
//             ApplyForce(particle, {0, force.y}); // Separate the x and y components of the force
//         }
//     }
// }

void UpdatePlayer(b2BodyId playerId, float dt, Vector2 inputDir, float speedPixelsPerSec) {
    // normalize input (WASD) so diagonals aren’t faster
    float len = sqrt(inputDir.x*inputDir.x + inputDir.y*inputDir.y);
    if (len > 0.0001f) {
        inputDir.x /= len;
        inputDir.y /= len;
    } else {
        inputDir = {0,0};
    }

    b2Vec2 vel = { PxToM(inputDir.x * speedPixelsPerSec),
                   PxToM(inputDir.y * speedPixelsPerSec) };

    b2Body_SetLinearVelocity(playerId, vel);
}


void Player_Draw(const Player* p) {
    DrawRectangleV(
        (Vector2){ p->pos.x - p->halfw, p->pos.y - p->halfh },
        (Vector2){ p->halfw * 2, p->halfh * 2 },
        (Color){ 255, 220, 50, 255 }
    );
}
