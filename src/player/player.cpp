#include "player.h"
#include "../level/level.h"
#include "../physics/physics.h"
#include "projectile.h"
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

    for (Entity& e : es->pool)
    {
        if (!e.active) continue;
        if (e.kind == EntityKind::Enemy) continue;

        // Look up body by entity ID
        auto it = g_entityToBody.find(e.id);
        if (it == g_entityToBody.end()) continue;
        b2BodyId body = it->second;
        if (!b2Body_IsValid(body)) continue;

        // Compute position and distance from player
        b2Vec2 bpos = b2Body_GetPosition(body);
        Vector2 bodyPosPx = { MToPx(bpos.x), MToPx(bpos.y) };

        Vector2 delta = Vector2Subtract(bodyPosPx, pos);
        float dist = Vector2Length(delta);
        if (dist < 2.0f || dist > orbitRadius * 2.0f) continue;

        // Assign element + color when first grabbed
        if (!e.telekinetic)
        {
            e.telekinetic = true;
            if (g_currentProjectile == ProjectileType::FIRE) {
                e.element = ElementType::FIRE;
                e.color   = (Color){255, 80, 20, 255};
            } else {
                e.element = ElementType::ICE;
                e.color   = (Color){100, 180, 255, 255};
            }
        }

        // Orbit physics
        Vector2 dir = Vector2Normalize(delta);
        Vector2 tangent = { -dir.y, dir.x };
        float radialError = dist - orbitRadius;

        Vector2 radialForce     = Vector2Scale(dir, -radialError * force.x * 0.02f);
        Vector2 tangentialForce = Vector2Scale(tangent, force.y * 0.015f);
        Vector2 totalForcePx    = Vector2Add(radialForce, tangentialForce);
        b2Vec2 totalForceM      = { PxToM(totalForcePx.x), PxToM(totalForcePx.y) };

        b2Body_ApplyLinearImpulseToCenter(body, totalForceM, true);

        // Dampen angular velocity slightly for stability
        b2Vec2 vel = b2Body_GetLinearVelocity(body);
        vel.x *= 0.97f;
        vel.y *= 0.97f;
        b2Body_SetLinearVelocity(body, vel);
    }
}


void Telekinesis_Fire(Vector2 playerPos, float orbitRadius, float launchForce, EntitySystem* es)
{
    if (!es) return;

    for (Entity& e : es->pool)
    {
        if (!e.active) continue;
        if (e.kind == EntityKind::Enemy) continue;
        if (!e.telekinetic) continue; // only fire held props

        // Look up body by entity ID
        auto it = g_entityToBody.find(e.id);
        if (it == g_entityToBody.end()) continue;
        b2BodyId body = it->second;
        if (!b2Body_IsValid(body)) continue;

        b2Vec2 bpos = b2Body_GetPosition(body);
        Vector2 bodyPosPx = { MToPx(bpos.x), MToPx(bpos.y) };

        Vector2 delta = Vector2Subtract(bodyPosPx, playerPos);
        float dist = Vector2Length(delta);
        if (dist < orbitRadius * 0.5f || dist > orbitRadius * 1.5f)
            continue;

        // Launch toward current direction
        Vector2 dir = Vector2Normalize(delta);
        Vector2 impulsePx = Vector2Scale(dir, launchForce);
        b2Vec2 impulseM = { PxToM(impulsePx.x), PxToM(impulsePx.y) };
        b2Body_ApplyLinearImpulseToCenter(body, impulseM, true);

        float torque = ((float)GetRandomValue(-100, 100)) * 0.0001f;
        b2Body_ApplyTorque(body, torque, true);

        TraceLog(LOG_INFO, "Telekinesis fired prop (Entity %d, %s)",
                 e.id, (e.element == ElementType::FIRE ? "FIRE" : "ICE"));

        // Mark it released
        e.telekinetic = false;
    }
}

void UpdatePlayer(b2BodyId playerId, float dt, Vector2 inputDir, float speedPixelsPerSec) {
    // normalize input so diagonals aren’t faster
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
