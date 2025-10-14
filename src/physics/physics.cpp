#include "physics.h"
#include "../state.h"
#include "../entity/entity.hpp"
#include "../entity/enemies.hpp"
#include "../../lib/box2d/include/box2d/box2d.h"
#include <vector>

std::vector<b2BodyId> g_entityBodies;
b2BodyId g_playerBody;

// --- world -------------------------------------------------------------
b2WorldId InitWorld() {
    b2WorldDef def = b2DefaultWorldDef();
    def.gravity = {0.0f, 0.0f}; // top-down: no gravity
    b2WorldId worldId = b2CreateWorld(&def);

    return worldId;
}

void DestroyWorld(b2WorldId worldId) {
    b2DestroyWorld(worldId);
}

void Physics_QueueDeletion (size_t i, const Vector2& pos, int id, EntityKind kind){
    g_entDelQueue.push_back({i, pos, id, kind});
}

void Physics_FlushDeletions(b2WorldId world, EntitySystem* es)
{
    if (g_entDelQueue.empty()) return;

    for (const Ent_To_Del& d : g_entDelQueue)
    {
        if (d.index >= g_entityBodies.size()) continue;
        b2BodyId body = g_entityBodies[d.index];

        if (b2Body_IsValid(body))
        {
            b2DestroyBody(body);
        }
        g_entityBodies[d.index] = b2_nullBodyId;

        if (d.index < es->pool.size())
        {
            es->pool[d.index].active = false;
        }

        // ✅ Only spawn corpses for enemies
        if (d.kind == EntityKind::Enemy)
        {
            Spawn_Corpse_Prop(es, world, d.pos);
        }

        // Optional: clean AI data
        // if (d.kind == EntityKind::Enemy)
        //     sEnemyAI.erase(d.id);
    }

    g_entDelQueue.clear();
}

static bool IsEnemyBodyDirect(b2BodyId body, EntitySystem* es)
{
    for (size_t i = 0; i < es->pool.size(); ++i)
    {
        Entity& e = es->pool[i];
        if (!e.active || e.kind != EntityKind::Enemy) continue;
        if (i >= g_entityBodies.size()) continue;

        b2BodyId enemyBody = g_entityBodies[i];
        if (enemyBody.index1 == body.index1)
            return true;
    }
    return false;
}

void Contact_ProcessPlayerEnemy(b2WorldId world, EntitySystem* es)
{
    if (g_gameOver || !es) return;

    b2ContactEvents events = b2World_GetContactEvents(world);
    if (events.beginCount == 0 && events.hitCount == 0) return;

    // For debugging:
    TraceLog(LOG_INFO, "PlayerEnemy ContactEvents: begin=%d hit=%d end=%d",
             events.beginCount, events.hitCount, events.endCount);

    // --- Check both begin and hit events for reliability ---
    auto CheckCollision = [&](b2BodyId bodyA, b2BodyId bodyB)
    {
        // Player body touches enemy body?
        if ((bodyA.index1 == g_playerBody.index1 && IsEnemyBodyDirect(bodyB, es)) ||
            (bodyB.index1 == g_playerBody.index1 && IsEnemyBodyDirect(bodyA, es)))
        {
            g_gameOver = true;
            TraceLog(LOG_INFO, "💀 Player touched by enemy — GAME OVER!");
        }
    };

    // --- Begin touch events ---
    for (int32_t i = 0; i < events.beginCount; ++i)
    {
        const b2ContactBeginTouchEvent* ev = &events.beginEvents[i];
        b2BodyId bodyA = b2Shape_GetBody(ev->shapeIdA);
        b2BodyId bodyB = b2Shape_GetBody(ev->shapeIdB);
        CheckCollision(bodyA, bodyB);
        if (g_gameOver) return;
    }

    // --- Hit events (for already-overlapping cases) ---
    for (int32_t i = 0; i < events.hitCount; ++i)
    {
        const b2ContactHitEvent* ev = &events.hitEvents[i];
        b2BodyId bodyA = b2Shape_GetBody(ev->shapeIdA);
        b2BodyId bodyB = b2Shape_GetBody(ev->shapeIdB);
        CheckCollision(bodyA, bodyB);
        if (g_gameOver) return;
    }
}

// --- statics -----------------------------------------------------------
// Build one static chain loop per wall cluster by tracing the perimeter
void BuildStaticsFromGrid(b2WorldId worldId, const Grid* g) {
    if (!g || g->w <= 0 || g->h <= 0) return;

    // One static body to own all chain fixtures
    b2BodyDef bd = b2DefaultBodyDef();
    b2BodyId ground = b2CreateBody(worldId, &bd);

    const int W = g->w, H = g->h;
    const int VX = W + 1, VY = H + 1;
    const int VERT_COUNT = VX * VY;

    // Outgoing directed edges per vertex (Right, Down, Left, Up). -1 means none.
    // Directions: 0=+x, 1=+y, 2=-x, 3=-y  (screen space; y grows down)
    int* out = (int*)malloc(sizeof(int) * VERT_COUNT * 4);
    if (!out) return;
    for (int i = 0; i < VERT_COUNT * 4; ++i) out[i] = -1;

    auto vid = [VX](int vx, int vy) { return vy * VX + vx; };
    auto is_wall = [g,W,H](int x, int y) {
        if (x < 0 || y < 0 || x >= W || y >= H) return false;
        return g->t[y*W + x].id == TILE_WALL;
    };

    // Build the directed edge graph along the wall perimeter
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            if (!is_wall(x,y)) continue;

            // TOP side
            if (!is_wall(x, y-1)) {
                int a = vid(x,   y);
                int b = vid(x+1, y);
                out[a*4 + 0] = b; // Right
            }
            // RIGHT side
            if (!is_wall(x+1, y)) {
                int a = vid(x+1, y);
                int b = vid(x+1, y+1);
                out[a*4 + 1] = b; // Down
            }
            // BOTTOM side
            if (!is_wall(x, y+1)) {
                int a = vid(x+1, y+1);
                int b = vid(x,   y+1);
                out[a*4 + 2] = b; // Left
            }
            // LEFT side
            if (!is_wall(x-1, y)) {
                int a = vid(x,   y+1);
                int b = vid(x,   y);
                out[a*4 + 3] = b; // Up
            }
        }

    // Track used directed edges
    uint8_t* used = (uint8_t*)calloc(VERT_COUNT * 4, 1);
    if (!used) { free(out); return; }

    auto v_to_m = [VX](int v)->b2Vec2 {
        int vx = v % VX;
        int vy = v / VX;
        float px = (float)vx * TILE_SIZE;
        float py = (float)vy * TILE_SIZE;
        return (b2Vec2){ PxToM(px), PxToM(py) };
    };

    auto collinear = [](b2Vec2 a, b2Vec2 b, b2Vec2 c)->bool {
        if (fabsf(a.x - b.x) < 1e-6f && fabsf(b.x - c.x) < 1e-6f) return true; // vertical
        if (fabsf(a.y - b.y) < 1e-6f && fabsf(b.y - c.y) < 1e-6f) return true; // horizontal
        return false;
    };

    // Trace all loops
    for (int v0 = 0; v0 < VERT_COUNT; ++v0)
        for (int d0 = 0; d0 < 4; ++d0) {
            int to = out[v0*4 + d0];
            if (to == -1 || used[v0*4 + d0]) continue;

            const int startV = v0;
            const int startD = d0;
            int v = v0;
            int d = d0;

            int cap = 64, n = 0;
            int* verts = (int*)malloc(sizeof(int) * cap);
            if (!verts) { free(used); free(out); return; }

            auto push_vid = [&](int vv){
                if (n == cap) { cap *= 2; verts = (int*)realloc(verts, sizeof(int)*cap); }
                verts[n++] = vv;
            };

            push_vid(v);

            // Guard to avoid infinite loops in pathological cases
            int safety = VERT_COUNT * 8;

            while (safety-- > 0) {
                used[v*4 + d] = 1;

                int vNext = out[v*4 + d];
                if (vNext == -1) break; // graph error

                push_vid(vNext);

                // Choose next direction at vNext with right/straight/left priority
                int right = (d + 1) & 3;
                int straight = d;
                int left = (d + 3) & 3;

                int nd = -1;
                if (out[vNext*4 + right]   != -1 && !used[vNext*4 + right])   nd = right;
                else if (out[vNext*4 + straight]!= -1 && !used[vNext*4 + straight]) nd = straight;
                else if (out[vNext*4 + left]    != -1 && !used[vNext*4 + left])    nd = left;

                // If we’ve returned to the start and the next dir would be the start dir, close the loop
                if (vNext == startV && nd == startD) {
                    break;
                }

                // Dead end or no unused continuation — stop this contour
                if (nd == -1) break;

                v = vNext;
                d = nd;
            }

            // Convert to meters and simplify collinear points; build chain if enough points
            if (n >= 4) {
                b2Vec2* pts = (b2Vec2*)malloc(sizeof(b2Vec2) * n);
                int m = 0;

                for (int i = 0; i < n; ++i) {
                    b2Vec2 p = v_to_m(verts[i]);
                    if (m < 2) {
                        pts[m++] = p;
                    } else {
                        if (collinear(pts[m-2], pts[m-1], p)) {
                            pts[m-1] = p;
                        } else {
                            pts[m++] = p;
                        }
                    }
                }
                if (m >= 3 && collinear(pts[m-2], pts[m-1], pts[0])) m -= 1;
                if (m >= 3 && collinear(pts[m-1], pts[0], pts[1])) {
                    for (int i = 0; i < m-1; ++i) pts[i] = pts[i+1];
                    m -= 1;
                }

                if (m >= 3) {
                    b2ChainDef cd = b2DefaultChainDef();
                    cd.points = pts;
                    cd.count = m;
                    cd.isLoop = true;
                    // Optional: set filter (depends on your Box2D version)
                    // cd.filter = (b2Filter){ StaticBit, AllBits, 0 };

                    b2CreateChain(ground, &cd);
                    b2Body_EnableContactEvents(ground, true);
                }
                free(pts);
            }

            free(verts);
        }

    free(used);
    free(out);
}

std::vector<b2BodyId> Create_Entity_Bodies(EntitySystem* es, b2WorldId worldId) {
    g_entityBodies.clear();
    if (!es) return g_entityBodies;

    const size_t n = es->pool.size();
    g_entityBodies.resize(n, b2_nullBodyId);

    for (size_t i = 0; i < n; ++i) {
        Entity& E = es->pool[i];
        if (!E.active) continue;

        // Body
        b2BodyDef bd = b2DefaultBodyDef();
        bd.type = b2_dynamicBody;
        bd.linearDamping  = 6.0f;       
        bd.angularDamping = 6.0f;
        bd.position = { PxToM(E.pos.x), PxToM(E.pos.y) };

        b2BodyId body = b2CreateBody(worldId, &bd);

        // Shape/fixture
        b2ShapeDef sd = b2DefaultShapeDef();
        sd.density     = 0.5f;
        sd.filter      = {DynamicBit, AllBits, 0};

        b2Polygon box = b2MakeBox(PxToM(E.half.x), PxToM(E.half.y));
        b2CreatePolygonShape(body, &sd, &box);

        g_entityBodies[i] = body;
    }
    TraceLog(LOG_INFO, "Created %zu entity bodies", g_entityBodies.size());
    return g_entityBodies; 
}

void Entities_Update(EntitySystem* es, float dt) {
    if (!es) return;

    const size_t n = es->pool.size();
    if (g_entityBodies.size() != n) return; // if you add/remove entities, rebuild bodies

    for (size_t i = 0; i < n; ++i) {
        Entity& E = es->pool[i];
        if (!E.active) continue;

        b2BodyId body = g_entityBodies[i];
        if (body.index1 == 0) continue; // null/invalid

        b2Vec2 p = b2Body_GetPosition(body);
        E.pos.x = MToPx(p.x);
        E.pos.y = MToPx(p.y);
        // rotation? add here 
    }
}

// --- player ------------------------------------------------------------
b2BodyId CreatePlayer(b2WorldId worldId, Vector2 spawnPixels, float halfWidthPx, float halfHeightPx, float linearDamping) {
    b2BodyDef bd = b2DefaultBodyDef();
    bd.type = b2_dynamicBody;
    bd.position = { PxToM(spawnPixels.x), PxToM(spawnPixels.y) };
    bd.linearDamping = linearDamping; // auto-stop when no input
    b2BodyId body = b2CreateBody(worldId, &bd);
    
    g_playerBody = body;

    b2ShapeDef sd = b2DefaultShapeDef();
    sd.filter = { PlayerBit, AllBits, 0 };
    sd.density = 1.0f;

    b2Polygon box = b2MakeBox(PxToM(halfWidthPx), PxToM(halfHeightPx));
    b2CreatePolygonShape(body, &sd, &box);
    
    b2Body_EnableContactEvents(body, true);

    return body;
}

Vector2 GetPlayerPixels(b2BodyId playerId) {
    b2Transform xf = b2Body_GetTransform(playerId);
    return MToPx(xf.p);
}
