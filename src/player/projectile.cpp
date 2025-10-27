#include "projectile.h"
#include "../physics/physics.h"
#include "../entity/enemies.hpp"
#include "raylib.h"
#include "raymath.h"
#include <algorithm>
#include <box2d/box2d.h>

// GLOBAL STATE
std::vector<Projectile> g_projectiles;
ProjectileType g_currentProjectile = ProjectileType::FIRE;

// PROJECTILE LOGIC
void Projectile_HandleSwitch()
{
    if (IsKeyPressed(KEY_Q)) g_currentProjectile = ProjectileType::FIRE;
    if (IsKeyPressed(KEY_E)) g_currentProjectile = ProjectileType::ICE;
}

void Projectile_Shoot(b2WorldId world, Vector2 playerPos, Camera2D cam)
{
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {

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

// PROCESS CONTACT EVENTS FROM BOX2D

void Projectile_ProcessContacts(b2WorldId world, EntitySystem* es)
{
    if (!es) return;

    b2ContactEvents events = b2World_GetContactEvents(world);
    if (events.beginCount == 0 && events.hitCount == 0 && events.endCount == 0)
        return;

    TraceLog(LOG_INFO, "Projectile contacts: begin=%d hit=%d end=%d",
             events.beginCount, events.hitCount, events.endCount);

    auto entityFromBody = [&](b2BodyId body) -> Entity* {
        auto it = g_bodyToEntity.find(body.index1);
        if (it == g_bodyToEntity.end()) return nullptr;
        return Entities_Get(es, it->second);
    };

    auto damageEnemyFromEntity = [&](Entity* e, float dmg, float slowSec, const char* tag)
    {
        if (!e || !e->active || e->kind != EntityKind::Enemy) return false;
        if (Enemy* en = Enemy_FromEntityId(e->id)) {
            en->health -= dmg;
            if (slowSec > 0.0f) en->slowTimer = slowSec;
            TraceLog(LOG_INFO, "%s Enemy %d (HP=%.1f)", tag, e->id, en->health);
            return true;
        }
        return false;
    };

    // --- Handle begin contacts (enough for our gameplay)
    for (int32_t i = 0; i < events.beginCount; ++i)
    {
        const b2ContactBeginTouchEvent* ev = &events.beginEvents[i];
        if (!b2Shape_IsValid(ev->shapeIdA) || !b2Shape_IsValid(ev->shapeIdB)) continue;

        b2BodyId bodyA = b2Shape_GetBody(ev->shapeIdA);
        b2BodyId bodyB = b2Shape_GetBody(ev->shapeIdB);
        if (!b2Body_IsValid(bodyA) || !b2Body_IsValid(bodyB)) continue;

        Entity* entA = entityFromBody(bodyA);
        Entity* entB = entityFromBody(bodyB);

        for (auto& p : g_projectiles)
        {
            if (!p.active || !b2Body_IsValid(p.body)) continue;
            if (p.body.index1 != bodyA.index1 && p.body.index1 != bodyB.index1) continue;

            // Apply projectile effects
            bool hit = false;
            if (p.type == ProjectileType::FIRE) {
                // 50 dmg, no slow
                hit |= damageEnemyFromEntity(entA, 50.0f, 0.0f, "🔥 Enemy hit by FIRE projectile");
                hit |= damageEnemyFromEntity(entB, 50.0f, 0.0f, "🔥 Enemy hit by FIRE projectile");
            } else if (p.type == ProjectileType::ICE) {
                // 25 dmg + 2s slow
                hit |= damageEnemyFromEntity(entA, 25.0f, 2.0f, "❄️ Enemy hit by ICE projectile");
                hit |= damageEnemyFromEntity(entB, 25.0f, 2.0f, "❄️ Enemy hit by ICE projectile");
            }

            // Destroy projectile either way after a contact
            if (b2Body_IsValid(p.body)) b2DestroyBody(p.body);
            p.active = false;

            if (hit) break; // done with this contact for this projectile
        }

        auto applyPropHit = [&](Entity* propEnt, Entity* otherEnt)
        {
            if (!propEnt || !propEnt->active) return;
            if (propEnt->kind == EntityKind::Enemy) return;
            if (propEnt->element == ElementType::NONE) return;

            if (!otherEnt || !otherEnt->active || otherEnt->kind != EntityKind::Enemy) return;

            if (propEnt->element == ElementType::FIRE) {
                if (damageEnemyFromEntity(otherEnt, 100.0f, 0.0f, "🔥 Enemy hit by telekinetic FIRE prop!")) {
                    // destroy the prop entity
                    Physics_QueueDeletion(0, propEnt->pos, propEnt->id, propEnt->kind);
                    propEnt->active = false;
                }
            } else if (propEnt->element == ElementType::ICE) {
                if (damageEnemyFromEntity(otherEnt, 90.0f, 3.0f, "❄️ Enemy hit by telekinetic ICE prop!")) {
                    Physics_QueueDeletion(0, propEnt->pos, propEnt->id, propEnt->kind);
                    propEnt->active = false;
                }
            }
        };

        // Check both directions: prop(A) -> enemy(B), prop(B) -> enemy(A)
        applyPropHit(entA, entB);
        applyPropHit(entB, entA);
    }
}

// UPDATE + DRAW
void Projectile_Update(b2WorldId world, EntitySystem *es, float dt)
{
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
