#pragma once
#include "raylib.h"
#include "../level/level.h"
#include <cstdint>
#include <vector>

enum class EntityKind : uint8_t {
    Prop    = 0,
    Enemy   = 1,
    Item    = 2,
    Ability = 3,
};

enum class ElementType {
    NONE,
    FIRE,
    ICE
};

struct Entity {
    int id;
    EntityKind kind;
    Vector2 pos;
    Vector2 half;
    Color color;
    bool active;

    float health = 0.0f;
    float maxHealth = 0.0f;
    float slowTimer = 0.0f;

    ElementType element = ElementType::NONE;   // what element it carries
    bool telekinetic = false;                  // whether currently held
};

// Simple container
struct EntitySystem {
    std::vector<Entity> pool;  // packed (inactive removed on destroy)
    int        nextId{1};
    uint32_t   seed{0};        // for deterministic spawns if you want
};


// Init / clear
void Entities_Init(EntitySystem* es, uint32_t seed);
void Entities_Clear(EntitySystem* es);

int         Entities_CreateBox(EntitySystem* es, EntityKind kind, Vector2 posPx, Vector2 halfPx, Color color);
void        Entities_Destroy(EntitySystem* es, int id);
Entity*     Entities_Get(EntitySystem* es, int id);
const Entity*Entities_Get(const EntitySystem* es, int id);

void Entities_Draw(const EntitySystem* es);

// Returns how many spawned. Ensures tiles are floor and provides a small spacing check.
int Entities_SpawnBoxesInLevel(EntitySystem* es,
                               const Grid* g,
                               int minCount, int maxCount,
                               Vector2 halfPx,
                               uint32_t seed);
