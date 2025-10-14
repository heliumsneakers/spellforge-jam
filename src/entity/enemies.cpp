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

// ------------------ A* PATHFIND ------------------
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

// ------------------ SPAWN + BODY ------------------
void Enemies_Spawn(EntitySystem* es, const Grid* g, Vector2 playerPos, int count, float minDist) {
    if (!es || !g) return;

    int W = g->w;
    int H = g->h;

    for (int i = 0; i < count; ++i)
    {
        int tries = 0;
        while (tries++ < 500)
        {
            int x = GetRandomValue(0, W - 1);
            int y = GetRandomValue(0, H - 1);
            Tile* t = grid_at((Grid*)g, x, y);
            if (!t || t->id != TILE_FLOOR) continue;

            Vector2 pos = { x * TILE_SIZE + TILE_SIZE * 0.5f, y * TILE_SIZE + TILE_SIZE * 0.5f };
            if (Vector2Distance(pos, playerPos) < minDist) continue;

            Entity e;
            e.id = es->nextId++;
            e.kind = EntityKind::Enemy;
            e.pos = pos;
            e.half = { 10.0f, 10.0f };
            e.color = GREEN;
            e.active = true;
            e.maxHealth = 100.0f;
            e.health    = 100.0f;
            e.slowTimer = 0.0f;
            es->pool.push_back(e);
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

        b2BodyDef bd = b2DefaultBodyDef();
        bd.type = b2_dynamicBody;
        bd.position = { PxToM(e.pos.x), PxToM(e.pos.y) };

        b2BodyId body = b2CreateBody(world, &bd);

        b2ShapeDef sd = b2DefaultShapeDef();
        sd.density = 1.0f;
        sd.filter.categoryBits = EnemyBit;
        sd.filter.maskBits = AllBits;
        b2Polygon box = b2MakeBox(PxToM(e.half.x), PxToM(e.half.y));
        b2CreatePolygonShape(body, &sd, &box);

        b2Body_EnableContactEvents(body, true);

        g_entityBodies.push_back(body);
    }
}

// ------------------ UPDATE ------------------

void Enemies_Update(EntitySystem* es, const Grid* g, b2BodyId playerBody, float dt) {
    if (!es) return;

    g_lastGrid = g;
    g_lastWorld = b2Body_GetWorld(playerBody);
    b2Vec2 pM = b2Body_GetPosition(playerBody);
    g_lastPlayerPos = { MToPx(pM.x), MToPx(pM.y) };

    // player position in pixels
    Vector2 playerPx = { MToPx(pM.x), MToPx(pM.y) };

    // tuning
    const float repathEvery = 0.35f;   // seconds
    const float waypointReach = 8.0f;  // px
    const float stopRadius = 28.0f;    // px (arrival radius near player)
    const float chaseSpeed = 40.0f;   // px/s
    const float accelGain  = 4.0f;     // force gain toward desired vel
    const float brakeGain  = 6.0f;     // stronger braking when inside stopRadius

    for (size_t i = 0; i < es->pool.size(); ++i) {
        Entity& e = es->pool[i];
        if (!e.active || e.kind != EntityKind::Enemy) continue;
        if (i >= g_entityBodies.size()) continue;

        b2BodyId body = g_entityBodies[i];
        if (body.index1 == 0) continue;


        if (e.health <= 0 && e.active) {
            Physics_QueueDeletion(i, e.pos, e.id, e.kind);
            e.active = false;
            continue;
        }

 
        // current enemy pos (px)
        b2Vec2 eM = b2Body_GetPosition(body);
        Vector2 posPx = { MToPx(eM.x), MToPx(eM.y) };

        // get or create AI state
        EnemyAI& ai = sEnemyAI[e.id];

        // decrement cooldown
        ai.repathCd -= dt;

        // recompute path when cooldown hits 0 or we ran out of waypoints
        bool needPath = (ai.repathCd <= 0.0f) || (ai.waypoint >= (int)ai.path.size());

        // prefer straight line chase if we have line-of-sight
        bool los = LineOfSightFloor(g, posPx, playerPx);

        if (needPath) {
            ai.repathCd = repathEvery;
            ai.path.clear();
            ai.waypoint = 0;

            if (!los) {
                // build grid path around walls
                AStar_FindPath(g, posPx, playerPx, ai.path);
            }
            // if LOS, we’ll just steer directly each frame (no path needed)
        }

        // choose target: next waypoint or player (LOS or no path)
        Vector2 target = playerPx;
        if (!ai.path.empty() && ai.waypoint < (int)ai.path.size()) {
            target = ai.path[ai.waypoint];
            // advance to next waypoint when close
            if (Vector2Distance(posPx, target) < waypointReach) {
                ai.waypoint++;
                if (ai.waypoint < (int)ai.path.size()) {
                    target = ai.path[ai.waypoint];
                } else {
                    target = playerPx;
                }
            }
        }

        // arrival: brake when inside stop radius
        float dToPlayer = Vector2Distance(posPx, playerPx);

        // desired velocity toward target
        Vector2 toTarget = Vector2Subtract(target, posPx);
        float dist = Vector2Length(toTarget);
        Vector2 dir = dist > 1.0f ? Vector2Scale(toTarget, 1.0f / dist) : Vector2{0,0};

        if (e.slowTimer > 0.0f)
        {
            e.slowTimer -= dt;
            if (e.slowTimer < 0.0f) e.slowTimer = 0.0f;
        }

        float slowFactor = (e.slowTimer > 0.0f) ? 0.4f : 1.0f;

        float speed = chaseSpeed * g_speedMultiplier * slowFactor;

        if (e.slowTimer > 0.0f)
            e.color = (Color){120, 200, 255, 255}; // icy blue
        else if (e.health < e.maxHealth * 0.5f)
            e.color = (Color){255, 100, 100, 255}; // darker red
        else
            e.color = GREEN;

        // smooth slowdown near final player radius
        if (dToPlayer < stopRadius * 2.0f) {
            float t = (dToPlayer - stopRadius) / stopRadius; // 1..0 across the last stopRadius
            if (t < 0) t = 0;
            speed *= t;
        }

        b2Vec2 desiredVelM = { PxToM(dir.x * speed), PxToM(dir.y * speed) };
        b2Vec2 curVelM = b2Body_GetLinearVelocity(body);

        // apply force toward desired velocity
        b2Vec2 force = {
            (desiredVelM.x - curVelM.x) * accelGain,
            (desiredVelM.y - curVelM.y) * accelGain
        };

        // extra braking when too close
        if (dToPlayer < stopRadius) {
            force.x += -curVelM.x * brakeGain;
            force.y += -curVelM.y * brakeGain;
        }

        b2Body_ApplyForceToCenter(body, force, true);

        // sync for draw
        e.pos = posPx;
    }
}

void Spawn_Corpse_Prop(EntitySystem* es, b2WorldId world, Vector2 pos)
{
    Entity corpse;
    corpse.id = es->nextId++;
    corpse.kind = EntityKind::Prop;
    corpse.pos = pos;
    corpse.half = { 6.0f, 6.0f };
    corpse.color = BLACK; // black
    corpse.active = true;
    corpse.element = ElementType::NONE;
    corpse.telekinetic = false;

    es->pool.push_back(corpse);

    // Create a simple static/dynamic body
    b2BodyDef bd = b2DefaultBodyDef();
    bd.type = b2_dynamicBody;
    bd.position = { PxToM(pos.x), PxToM(pos.y) };

    b2BodyId body = b2CreateBody(world, &bd);

    b2ShapeDef sd = b2DefaultShapeDef();
    sd.density = 5.0f;
    sd.filter.categoryBits = DynamicBit;
    sd.filter.maskBits = AllBits;

    b2Polygon box = b2MakeBox(PxToM(corpse.half.x), PxToM(corpse.half.y));
    b2CreatePolygonShape(body, &sd, &box);

    b2Body_EnableContactEvents(body, true);

    g_entityBodies.push_back(body);

    TraceLog(LOG_INFO, "🪦 Spawned corpse prop at (%.1f, %.1f)", pos.x, pos.y);
}
