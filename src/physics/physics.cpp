#include "physics.h"
#include "../../lib/box2d/include/box2d/box2d.h"


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
            }
            free(pts);
        }

        free(verts);
    }

    free(used);
    free(out);
}

// --- player ------------------------------------------------------------
b2BodyId CreatePlayer(b2WorldId worldId, Vector2 spawnPixels,
                           float halfWidthPx, float halfHeightPx,
                           float linearDamping)
{
    b2BodyDef bd = b2DefaultBodyDef();
    bd.type = b2_dynamicBody;
    bd.position = { PxToM(spawnPixels.x), PxToM(spawnPixels.y) };
    bd.linearDamping = linearDamping; // auto-stop when no input
    b2BodyId body = b2CreateBody(worldId, &bd);

    b2ShapeDef sd = b2DefaultShapeDef();
    sd.filter = { PlayerBit, AllBits, 0 };
    sd.density = 1.0f;

    b2Polygon box = b2MakeBox(PxToM(halfWidthPx), PxToM(halfHeightPx));
    b2CreatePolygonShape(body, &sd, &box);

    return body;
}

void UpdatePlayer(b2BodyId playerId, float dt, Vector2 inputDir, float speedPixelsPerSec) {
    (void)dt; // we set velocity directly; Box2D integrates it

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

Vector2 GetPlayerPixels(b2BodyId playerId) {
    b2Transform xf = b2Body_GetTransform(playerId);
    return MToPx(xf.p);
}
