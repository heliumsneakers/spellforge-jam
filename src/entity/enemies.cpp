#include "raylib.h"
#include "raymath.h"
#include "enemies.hpp"
#include "../physics/physics.h"
#include "../level/level.h"
#include "../../lib/box2d/include/box2d/box2d.h"
#include <cmath>
#include <cstdlib>
#include <queue>
#include <vector>
#include <unordered_map>
#include <algorithm>

// Define a collision filter for enemies

std::vector<Enemy> g_enemies;
std::unordered_map<int, size_t> g_enemyIndexByEntId;

int g_enemiesKilled = 0;
int g_wave = 0;
float g_speedMultiplier = 1.0f;

static b2WorldId g_lastWorld;
static const Grid* g_lastGrid = nullptr;
static Vector2 g_lastPlayerPos = {0};

struct EnemyAI {
    std::vector<Vector2> path;
    int waypoint = 0;
    float repathCd = 0.0f;
};

static std::unordered_map<int, EnemyAI> sEnemyAI;

struct Node {
    int x, y;
    float g, h;
    int parentX, parentY;
};

Enemy* Enemy_FromEntityId(int entId) {
    auto it = g_enemyIndexByEntId.find(entId);
    if (it == g_enemyIndexByEntId.end()) return nullptr;
    return &g_enemies[it->second];
}

void Enemies_Clear() {
    g_enemies.clear();
    g_enemyIndexByEntId.clear();
}

static bool LineOfSightFloor(const Grid* g, Vector2 aPx, Vector2 bPx) {
    int x0 = (int)(aPx.x / TILE_SIZE), y0 = (int)(aPx.y / TILE_SIZE);
    int x1 = (int)(bPx.x / TILE_SIZE), y1 = (int)(bPx.y / TILE_SIZE);
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (true) {
        Tile* t = grid_at((Grid*)g, x0, y0);
        if (!t || t->id != TILE_FLOOR) return false;
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
    return true;
}

static inline float Heuristic(int x1, int y1, int x2, int y2) {
    return fabsf((float)(x1 - x2)) + fabsf((float)(y1 - y2)); // Manhattan
}

// A* PATHFIND 
static bool AStar_FindPath(const Grid* g, Vector2 startPx, Vector2 goalPx, std::vector<Vector2>& outPath) {
    outPath.clear();

    const int W = g->w, H = g->h;
    int sx = (int)(startPx.x / TILE_SIZE);
    int sy = (int)(startPx.y / TILE_SIZE);
    int gx = (int)(goalPx.x / TILE_SIZE);
    int gy = (int)(goalPx.y / TILE_SIZE);

    auto in_bounds = [&](int x, int y) { return x >= 0 && y >= 0 && x < W && y < H; };
    auto passable  = [&](int x, int y) { Tile* t = grid_at((Grid*)g, x, y); return t && t->id == TILE_FLOOR; };

    struct CellKey { int x, y; bool operator==(const CellKey& o) const { return x==o.x && y==o.y; } };
    struct Hash { size_t operator()(const CellKey& c) const { return (size_t)c.x * 73856093 ^ (size_t)c.y * 19349663; } };

    struct PQEntry { float f; CellKey key; };
    struct PQCompare { bool operator()(const PQEntry& a, const PQEntry& b) const { return a.f > b.f; } };

    std::priority_queue<PQEntry, std::vector<PQEntry>, PQCompare> open;
    std::unordered_map<CellKey, Node, Hash> nodes;

    CellKey start{sx, sy};
    Node startNode{sx, sy, 0, Heuristic(sx, sy, gx, gy), -1, -1};
    nodes[start] = startNode;
    open.push({ startNode.g + startNode.h, start });
    const int dirs[4][2] = { {1,0},{-1,0},{0,1},{0,-1} };

    bool found = false;
    while (!open.empty()) {
        CellKey curKey = open.top().key;
        open.pop();

        Node& cur = nodes[curKey];
        if (cur.x == gx && cur.y == gy) { found = true; break; }

        for (auto& d : dirs) {
            int nx = cur.x + d[0];
            int ny = cur.y + d[1];
            if (!in_bounds(nx, ny) || !passable(nx, ny)) continue;

            CellKey nk{nx, ny};
            float newG = cur.g + 1.0f;
            auto it = nodes.find(nk);
            if (it == nodes.end() || newG < it->second.g) {
                Node next{nx, ny, newG, Heuristic(nx, ny, gx, gy), cur.x, cur.y};
                nodes[nk] = next;
                open.push({ newG + next.h, nk });
            }
        }
    }

    if (!found) return false;

    // Backtrack path
    std::vector<Vector2> rev;
    Node cur = nodes[{gx, gy}];
    while (cur.parentX != -1) {
        rev.push_back({ cur.x * (float)TILE_SIZE + TILE_SIZE * 0.5f,
            cur.y * (float)TILE_SIZE + TILE_SIZE * 0.5f });
        cur = nodes[{cur.parentX, cur.parentY}];
    }
    std::reverse(rev.begin(), rev.end());
    outPath = rev;
    return true;
}

// Init and spawn enemies.
void Enemies_Spawn(EntitySystem* es, const Grid* g, Vector2 playerPos, int count, float minDist) {
    if (!es || !g) return;

    int W = g->w;
    int H = g->h;

    for (int i = 0; i < count; ++i) {
        int tries = 0;
        while (tries++ < 500) {
            int x = GetRandomValue(0, W - 1);
            int y = GetRandomValue(0, H - 1);
            Tile* t = grid_at((Grid*)g, x, y);
            if (!t || t->id != TILE_FLOOR) continue;

            Vector2 pos = { x * TILE_SIZE + TILE_SIZE * 0.5f, y * TILE_SIZE + TILE_SIZE * 0.5f };
            if (Vector2Distance(pos, playerPos) < minDist) continue;

            // Create base entity
            Entity e;
            e.id = es->nextId++;
            e.kind = EntityKind::Enemy;
            e.pos = pos;
            e.half = { 10.0f, 10.0f };
            e.color = GREEN;
            e.active = true;
            e.element = ElementType::NONE;
            e.telekinetic = false;
            es->pool.push_back(e);

            // Reference to that entity (stable since std::vector won’t reallocate until capacity is exceeded)
            Entity* entPtr = &es->pool.back();

            // Create Enemy data
            Enemy en;
            en.entId = e.id;
            en.health = 100.f;
            en.maxHealth = 100.f;
            en.slowTimer = 0.f;
            g_enemyIndexByEntId[e.id] = g_enemies.size();
            g_enemies.push_back(en);

            break;
        }
    }
}


void Enemies_CreateBodies(EntitySystem* es, b2WorldId world, size_t startIndex)
{
    if (!es) return;

    for (size_t i = startIndex; i < es->pool.size(); ++i)
    {
        Entity& e = es->pool[i];
        if (!e.active || e.kind != EntityKind::Enemy) continue;

        // Create dynamic body
        b2BodyDef bd = b2DefaultBodyDef();
        bd.type = b2_dynamicBody;
        bd.position = { PxToM(e.pos.x), PxToM(e.pos.y) };

        b2BodyId body = b2CreateBody(world, &bd);

        // Define shape
        b2ShapeDef sd = b2DefaultShapeDef();
        sd.density = 1.0f;
        sd.filter.categoryBits = EnemyBit;
        sd.filter.maskBits = AllBits;

        b2Polygon box = b2MakeBox(PxToM(e.half.x), PxToM(e.half.y));
        b2CreatePolygonShape(body, &sd, &box);

        b2Body_EnableContactEvents(body, true);

        // Register body with the physics system
        Physics_RegisterBody(e, body);
    }

    TraceLog(LOG_INFO, "Created enemy bodies from index %zu to %zu", startIndex, es->pool.size());
}

// Update enemies within the physics system.
void Enemies_Update(EntitySystem* es, const Grid* g, b2BodyId playerBody, float dt)
{
    if (!es || g_enemies.empty()) return;

    g_lastGrid  = g;
    g_lastWorld = b2Body_GetWorld(playerBody);
    b2Vec2 pM   = b2Body_GetPosition(playerBody);
    Vector2 playerPx = { MToPx(pM.x), MToPx(pM.y) };
    g_lastPlayerPos = playerPx;

    const float repathEvery   = 0.35f;
    const float waypointReach = 8.0f;
    const float stopRadius    = 28.0f;
    const float chaseSpeed    = 40.0f;
    const float accelGain     = 4.0f;
    const float brakeGain     = 6.0f;

    for (size_t k = 0; k < g_enemies.size(); /* no ++ */)
    {
        Enemy& en = g_enemies[k];

        // Resolve owning entity safely
        Entity* e = Entities_Get(es, en.entId);
        if (!e)
        {
            // Defensive: if entity no longer exists at all
            size_t last = g_enemies.size() - 1;
            if (k != last)
            {
                g_enemyIndexByEntId[g_enemies[last].entId] = k;
                std::swap(g_enemies[k], g_enemies[last]);
            }
            g_enemyIndexByEntId.erase(en.entId);
            g_enemies.pop_back();
            continue;
        }

        // Handle inactive entity (probably queued for deletion)
        if (!e->active)
        {
            size_t last = g_enemies.size() - 1;
            if (k != last)
            {
                g_enemyIndexByEntId[g_enemies[last].entId] = k;
                std::swap(g_enemies[k], g_enemies[last]);
            }
            g_enemyIndexByEntId.erase(en.entId);
            g_enemies.pop_back();
            continue;
        }

        // Get Box2D body (skip if destroyed)
        auto bIt = g_entityToBody.find(e->id);
        if (bIt == g_entityToBody.end() || !b2Body_IsValid(bIt->second))
        {
            k++;
            continue;
        }
        b2BodyId body = bIt->second;

        // ----------------------------------------------------
        // Death check
        // ----------------------------------------------------
        if (en.health <= 0.f)
        {
            const int deadId = en.entId; // <-- capture before swap!
            Physics_QueueDeletion(0, e->pos, e->id, e->kind);
            g_enemiesKilled++;

            // swap-erase safely
            size_t last = g_enemies.size() - 1;
            if (k != last)
            {
                g_enemyIndexByEntId[g_enemies[last].entId] = k;
                std::swap(g_enemies[k], g_enemies[last]);
            }

            g_enemyIndexByEntId.erase(deadId);
            g_enemies.pop_back();
            continue;
        }

        // ----------------------------------------------------
        // Movement + pathfinding
        // ----------------------------------------------------
        b2Vec2 eM = b2Body_GetPosition(body);
        Vector2 posPx = { MToPx(eM.x), MToPx(eM.y) };

        en.repathCd -= dt;
        bool needPath = (en.repathCd <= 0.0f) || (en.waypoint >= (int)en.path.size());
        bool los = LineOfSightFloor(g, posPx, playerPx);

        if (needPath)
        {
            en.repathCd = repathEvery;
            en.path.clear();
            en.waypoint = 0;
            if (!los) AStar_FindPath(g, posPx, playerPx, en.path);
        }

        Vector2 target = playerPx;
        if (!en.path.empty() && en.waypoint < (int)en.path.size())
        {
            target = en.path[en.waypoint];
            if (Vector2Distance(posPx, target) < waypointReach)
                en.waypoint++;
        }

        float dToPlayer = Vector2Distance(posPx, playerPx);
        Vector2 toTarget = Vector2Subtract(target, posPx);
        float dist = Vector2Length(toTarget);
        Vector2 dir = (dist > 1.0f) ? Vector2Scale(toTarget, 1.0f / dist) : Vector2{0,0};

        // ----------------------------------------------------
        // Speed modifiers (slowed / damaged / near player)
        // ----------------------------------------------------
        if (en.slowTimer > 0.0f)
        {
            en.slowTimer -= dt;
            if (en.slowTimer < 0.0f) en.slowTimer = 0.0f;
        }

        float slowFactor = (en.slowTimer > 0.0f) ? 0.4f : 1.0f;
        float speed = chaseSpeed * slowFactor;

        // visual feedback
        if (en.slowTimer > 0.0f)
            e->color = (Color){120, 200, 255, 255};
        else if (en.health < en.maxHealth * 0.5f)
            e->color = (Color){255, 100, 100, 255};
        else
            e->color = GREEN;

        if (dToPlayer < stopRadius * 2.0f)
        {
            float t = (dToPlayer - stopRadius) / stopRadius;
            if (t < 0.0f) t = 0.0f;
            speed *= t;
        }

        b2Vec2 desiredVelM = { PxToM(dir.x * speed), PxToM(dir.y * speed) };
        b2Vec2 curVelM = b2Body_GetLinearVelocity(body);

        b2Vec2 force = {
            (desiredVelM.x - curVelM.x) * accelGain,
            (desiredVelM.y - curVelM.y) * accelGain
        };

        if (dToPlayer < stopRadius)
        {
            force.x += -curVelM.x * brakeGain;
            force.y += -curVelM.y * brakeGain;
        }

        b2Body_ApplyForceToCenter(body, force, true);

        // sync position for renderer
        e->pos = posPx;

        k++; // advance loop
    }
}

void Spawn_Corpse_Prop(EntitySystem* es, b2WorldId world, Vector2 pos)
{
    Entity corpse;
    corpse.id = es->nextId++;
    corpse.kind = EntityKind::Prop;
    corpse.pos = pos;
    corpse.half = { 6.0f, 6.0f };
    corpse.color = BLACK;
    corpse.active = true;
    corpse.element = ElementType::NONE;
    corpse.telekinetic = false;

    es->pool.push_back(corpse);
    Entity& e = es->pool.back(); // get reference to stored entity

    // Create physical body
    b2BodyDef bd = b2DefaultBodyDef();
    bd.type = b2_dynamicBody;
    bd.position = { PxToM(e.pos.x), PxToM(e.pos.y) };

    b2BodyId body = b2CreateBody(world, &bd);

    b2ShapeDef sd = b2DefaultShapeDef();
    sd.density = 5.0f;
    sd.filter.categoryBits = DynamicBit;
    sd.filter.maskBits = AllBits;

    b2Polygon box = b2MakeBox(PxToM(e.half.x), PxToM(e.half.y));
    b2CreatePolygonShape(body, &sd, &box);

    b2Body_EnableContactEvents(body, true);

    // Register corpse body
    Physics_RegisterBody(e, body);

    TraceLog(LOG_INFO, "🪦 Spawned corpse prop (Entity ID %d) at (%.1f, %.1f)",
             e.id, e.pos.x, e.pos.y);
}

