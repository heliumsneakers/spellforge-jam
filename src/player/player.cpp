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

// Apply a force at the center of each entity within a radius of `pos`
void Telekinesis_Hold(Vector2 pos, float orbitRadius, Vector2 force, EntitySystem* es)
{
    if (!es) return;

    const size_t n = es->pool.size();
    if (g_entityBodies.size() != n) {
        TraceLog(LOG_INFO, "Entity/body mismatch: %zu vs %zu", g_entityBodies.size(), n);
        return;
    }

    for (size_t i = 0; i < n; ++i)
    {
        Entity& e = es->pool[i];
        if (!e.active) continue;

        b2BodyId body = g_entityBodies[i];
        if (body.index1 == 0) continue;

        // --- Get body position ---
        b2Vec2 bpos = b2Body_GetPosition(body);
        Vector2 bodyPosPx = { MToPx(bpos.x), MToPx(bpos.y) };

        // --- Vector from player to body ---
        Vector2 delta = Vector2Subtract(bodyPosPx, pos);
        float dist = Vector2Length(delta);
        if (dist < 2.0f || dist > orbitRadius * 2.0f) continue; // too close or too far

        // --- Direction from player to body ---
        Vector2 dir = Vector2Scale(delta, 1.0f / dist);

        // --- Tangential direction (perpendicular to dir) ---
        Vector2 tangent = { -dir.y, dir.x }; // rotate 90 degrees CCW for orbit

        // --- Compute radial (in/out) force to keep it on ring ---
        float radialError = dist - orbitRadius;
        Vector2 radialForce = Vector2Scale(dir, -radialError * force.x * 0.02f); // pulls toward orbit ring

        // --- Compute tangential (spin) force ---
        Vector2 tangentialForce = Vector2Scale(tangent, force.y * 0.015f); // adjusts spin speed

        // --- Combine ---
        Vector2 totalForcePx = Vector2Add(radialForce, tangentialForce);
        b2Vec2 totalForceM = { PxToM(totalForcePx.x), PxToM(totalForcePx.y) };

        // --- Apply as impulse for responsiveness ---
        b2Body_ApplyLinearImpulseToCenter(body, totalForceM, true);

        // --- Optional damping for stability ---
        b2Vec2 vel = b2Body_GetLinearVelocity(body);
        vel.x *= 0.97f;
        vel.y *= 0.97f;
        b2Body_SetLinearVelocity(body, vel);
    }
}

void Telekinesis_Fire(Vector2 playerPos, float orbitRadius, float launchForce, EntitySystem* es) {
    if (!es) return;

    const size_t n = es->pool.size();
    if (g_entityBodies.size() != n) {
        TraceLog(LOG_INFO, "Entity/body mismatch: %zu vs %zu", g_entityBodies.size(), n);
        return;
    }

    for (size_t i = 0; i < n; ++i)
    {
        Entity& e = es->pool[i];
        if (!e.active) continue;

        b2BodyId body = g_entityBodies[i];
        if (body.index1 == 0) continue;

        // Get body position
        b2Vec2 bpos = b2Body_GetPosition(body);
        Vector2 bodyPosPx = { MToPx(bpos.x), MToPx(bpos.y) };

        // Vector from player to entity
        Vector2 delta = Vector2Subtract(bodyPosPx, playerPos);
        float dist = Vector2Length(delta);

        // Only fire entities close enough to the orbit radius
        if (dist < orbitRadius * 0.5f || dist > orbitRadius * 1.5f)
            continue;

        // Normalize the outward direction (away from player)
        Vector2 dir = Vector2Scale(delta, 1.0f / dist);

        // Apply impulse outward
        Vector2 impulsePx = Vector2Scale(dir, launchForce);
        b2Vec2 impulseM = { PxToM(impulsePx.x), PxToM(impulsePx.y) };

        b2Body_ApplyLinearImpulseToCenter(body, impulseM, true);

        // Add a bit of random spin for visual effect
        float torque = ((float)GetRandomValue(-100, 100)) * 0.0001f;
        b2Body_ApplyTorque(body, torque, true);

        TraceLog(LOG_INFO, "Telekinesis fired entity %zu impulse=(%.3f, %.3f)", i, impulseM.x, impulseM.y);
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
