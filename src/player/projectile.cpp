#include "projectile.h"
#include "../physics/physics.h"
#include "raylib.h"
#include "raymath.h"
#include <algorithm>

// ------------------------------------------------------------
// GLOBAL STATE
// ------------------------------------------------------------
std::vector<Projectile> g_projectiles;
ProjectileType g_currentProjectile = ProjectileType::FIRE;

// ------------------------------------------------------------
// PROJECTILE LOGIC
// ------------------------------------------------------------

void Projectile_HandleSwitch()
{
    if (IsKeyPressed(KEY_Q)) g_currentProjectile = ProjectileType::FIRE;
    if (IsKeyPressed(KEY_E)) g_currentProjectile = ProjectileType::ICE;
}

void Projectile_Shoot(b2WorldId world, Vector2 playerPos, Camera2D cam)
{
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return;

    Vector2 mouseWorld = GetScreenToWorld2D(GetMousePosition(), cam);
    Vector2 dir = Vector2Normalize(Vector2Subtract(mouseWorld, playerPos));
    Vector2 spawnPos = Vector2Add(playerPos, Vector2Scale(dir, 16.0f));

    b2BodyDef bd = b2DefaultBodyDef();
    bd.type = b2_dynamicBody;
    bd.position = { PxToM(spawnPos.x), PxToM(spawnPos.y) };
    bd.isBullet = true;
    b2BodyId body = b2CreateBody(world, &bd);

    b2Body_EnableContactEvents(body, true);

    b2ShapeDef sd = b2DefaultShapeDef();
    sd.density = 0.5f;
    sd.filter.categoryBits = ProjectileBit;
    sd.filter.maskBits = AllBits & ~ProjectileBit;

    float radius = PxToM(4.0f);
    b2Circle circle = { {0,0}, radius };
    b2CreateCircleShape(body, &sd, &circle);

    float impulseStrength = 5.0f;
    b2Vec2 impulse = { PxToM(dir.x * impulseStrength), PxToM(dir.y * impulseStrength) };
    b2Body_ApplyLinearImpulseToCenter(body, impulse, true);

    Color color = (g_currentProjectile == ProjectileType::FIRE)
        ? (Color){255, 80, 20, 255}
        : (Color){100, 180, 255, 255};

    g_projectiles.push_back({ g_currentProjectile, body, color, 3.0f, true });
}

// ------------------------------------------------------------
// PROCESS CONTACT EVENTS FROM BOX2D
// ------------------------------------------------------------
void Projectile_ProcessContacts(b2WorldId world)
{
    b2ContactEvents events = b2World_GetContactEvents(world); 

    if (events.beginCount == 0 && events.hitCount == 0 && events.endCount == 0)
        return; // nothing happened this frame
    
    TraceLog(LOG_INFO, "ContactEvents: begin=%d hit=%d end=%d",
             events.beginCount, events.hitCount, events.endCount);

    // --- Handle Begin Touch ---
    for (int32_t i = 0; i < events.beginCount; ++i)
    { 
        TraceLog(LOG_INFO, "Begin Touch #%d", i);
        const b2ContactBeginTouchEvent* ev = &events.beginEvents[i];
        b2ShapeId sA = ev->shapeIdA;
        b2ShapeId sB = ev->shapeIdB;

        for (auto& p : g_projectiles)
        {
            if (!p.active) continue;
            b2BodyId body = p.body;

            if (b2Shape_GetBody(sA).index1 == body.index1 ||
                b2Shape_GetBody(sB).index1 == body.index1)
            {
                TraceLog(LOG_INFO, "Projectile hit something (begin touch)");
                if (b2Body_IsValid(body))
                    b2DestroyBody(body);
                p.active = false;
                break;
            }
        }
    }

    // --- Handle Hit Events ---
    for (int32_t i = 0; i < events.hitCount; ++i)
    {
        const b2ContactHitEvent* ev = &events.hitEvents[i];
        b2ShapeId sA = ev->shapeIdA;
        b2ShapeId sB = ev->shapeIdB;

        for (auto& p : g_projectiles)
        {
            if (!p.active) continue;
            b2BodyId body = p.body;

            if (b2Shape_GetBody(sA).index1 == body.index1 ||
                b2Shape_GetBody(sB).index1 == body.index1)
            {
                TraceLog(LOG_INFO, "Projectile hit something (hit event)");
                if (b2Body_IsValid(body))
                    b2DestroyBody(body);
                p.active = false;
                break;
            }
        }
    }
}

// ------------------------------------------------------------
// UPDATE + DRAW
// ------------------------------------------------------------
void Projectile_Update(b2WorldId world, float dt)
{
    // 1. Handle collisions this frame
    Projectile_ProcessContacts(world);

    for (auto& p : g_projectiles)
    {
        if (!p.active) continue;

        p.lifetime -= dt;
        if (p.lifetime <= 0.0f)
        {
            if (b2Body_IsValid(p.body))
                b2DestroyBody(p.body);
            p.active = false;
            continue;
        }
    }

    // 3. Cleanup inactive projectiles
    g_projectiles.erase(
        std::remove_if(g_projectiles.begin(), g_projectiles.end(),
                       [](const Projectile& p){ return !p.active; }),
        g_projectiles.end()
    );
}

void Projectile_Draw()
{
    for (const auto& p : g_projectiles)
    {
        if (!p.active) continue;
        b2Vec2 pos = b2Body_GetPosition(p.body);
        Vector2 posPx = { MToPx(pos.x), MToPx(pos.y) };
        DrawCircleV(posPx, 4.0f, p.color);
    }
}
