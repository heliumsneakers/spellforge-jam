#include "projectile.h"
#include "../physics/physics.h"
#include "../entity/enemies.hpp"
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
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {

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
}

// ------------------------------------------------------------
// PROCESS CONTACT EVENTS FROM BOX2D
// ------------------------------------------------------------
void Projectile_ProcessContacts(b2WorldId world, EntitySystem *es)
{
    b2ContactEvents events = b2World_GetContactEvents(world); 

    if (events.beginCount == 0 && events.hitCount == 0 && events.endCount == 0)
        return; // nothing happened this frame

    TraceLog(LOG_INFO, "ContactEvents: begin=%d hit=%d end=%d",
             events.beginCount, events.hitCount, events.endCount);

    // --- Handle Begin Touch ---
    for (int32_t i = 0; i < events.beginCount; ++i) {
        const b2ContactBeginTouchEvent* ev = &events.beginEvents[i];
        b2ShapeId sA = ev->shapeIdA;
        b2ShapeId sB = ev->shapeIdB;
        b2BodyId bodyA = b2Shape_GetBody(sA);
        b2BodyId bodyB = b2Shape_GetBody(sB);

        for (auto& p : g_projectiles)
        {
            if (!p.active) continue;
            b2BodyId pBody = p.body;

            if (pBody.index1 == bodyA.index1 || pBody.index1 == bodyB.index1)
            {
                // Projectile collided with something
                bool hitEnemy = false;

                // check which entity we hit
                if (es)
                {
                    for (size_t j = 0; j < es->pool.size(); ++j)
                    {
                        Entity& e = es->pool[j];
                        if (!e.active || e.kind != EntityKind::Enemy) continue;

                        b2BodyId eBody = g_entityBodies[j];
                        if (eBody.index1 == bodyA.index1 || eBody.index1 == bodyB.index1)
                        {
                            hitEnemy = true;

                            // --- FIRE projectile: high damage ---
                            if (p.type == ProjectileType::FIRE)
                            {
                                e.health -= 50.0f;
                                TraceLog(LOG_INFO, "🔥 Enemy %d hit by fire projectile (HP=%.1f)", e.id, e.health);
                            }
                            // --- ICE projectile: slow effect ---
                            else if (p.type == ProjectileType::ICE)
                            {
                                e.health -= 25.0f;
                                e.slowTimer = 2.0f; // 2 seconds slow
                                TraceLog(LOG_INFO, "❄️ Enemy %d hit by ice projectile (HP=%.1f, slowed)", e.id, e.health);
                            }
 
                            break;
                        }
                    }
                }

                // destroy projectile
                if (b2Body_IsValid(pBody))
                    b2DestroyBody(pBody);
                p.active = false;

                if (hitEnemy)
                    break;
            }
        }

        for (size_t i = 0; i < es->pool.size(); ++i)
        {
            Entity& prop = es->pool[i];
            if (!prop.active || prop.kind == EntityKind::Enemy) continue;
            if (prop.element == ElementType::NONE) continue; // not infused

            b2BodyId propBody = g_entityBodies[i];

            // check for collisions between this prop and enemies
            for (int32_t j = 0; j < events.beginCount; ++j)
            {
                const b2ContactBeginTouchEvent* ev = &events.beginEvents[j];
                b2ShapeId sA = ev->shapeIdA;
                b2ShapeId sB = ev->shapeIdB;
                b2BodyId bodyA = b2Shape_GetBody(sA);
                b2BodyId bodyB = b2Shape_GetBody(sB);

                if (propBody.index1 != bodyA.index1 && propBody.index1 != bodyB.index1)
                    continue;

                // Find the enemy hit
                for (size_t k = 0; k < es->pool.size(); ++k)
                {
                    Entity& enemy = es->pool[k];
                    if (!enemy.active || enemy.kind != EntityKind::Enemy) continue;

                    b2BodyId enemyBody = g_entityBodies[k];
                    if (enemyBody.index1 == bodyA.index1 || enemyBody.index1 == bodyB.index1)
                    {
                        if (prop.element == ElementType::FIRE) {
                            enemy.health -= 100.0f;
                            TraceLog(LOG_INFO, "🔥 Enemy %d hit by telekinetic fire prop! (HP=%.1f)", enemy.id, enemy.health);
                        } else if (prop.element == ElementType::ICE) {
                            enemy.health -= 90.0f;
                            enemy.slowTimer = 3.0f;
                            TraceLog(LOG_INFO, "❄️ Enemy %d hit by telekinetic ice prop! (HP=%.1f, slowed)", enemy.id, enemy.health);
                        }
                        
                        if (prop.active) {
                            Physics_QueueDeletion(i, prop.pos, prop.id);
                            prop.active = false;
                            continue;
                        }

                        break;
                    }
                }
            }
        }
    }
}

// ------------------------------------------------------------
// UPDATE + DRAW
// ------------------------------------------------------------
void Projectile_Update(b2WorldId world, EntitySystem *es, float dt)
{
    // 1. Handle collisions this frame
    Projectile_ProcessContacts(world, es);

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
