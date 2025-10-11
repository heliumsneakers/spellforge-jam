#include "player.h"
#include "../level/level.h"
#include <math.h>
#include <time.h>

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

void Player_Update(Player* p, const Grid* level, float dt) {
    // Input
    Vector2 dir = {0};
    if (IsKeyDown(KEY_W)) dir.y -= 1;
    if (IsKeyDown(KEY_S)) dir.y += 1;
    if (IsKeyDown(KEY_A)) dir.x -= 1;
    if (IsKeyDown(KEY_D)) dir.x += 1;

    float len = sqrtf(dir.x*dir.x + dir.y*dir.y);
    if (len > 0.001f) { dir.x /= len; dir.y /= len; }

    p->vel.x = dir.x * p->speed * dt;
    p->vel.y = dir.y * p->speed * dt;

    // Grid collision (axis-separated)
    float nx = p->pos.x, ny = p->pos.y;
    collide_aabb_vs_walls(level, &nx, &ny, p->halfw, p->halfh, p->vel.x, p->vel.y);
    p->pos.x = nx; p->pos.y = ny;

    // --- Camera follow (smooth) ---
    // Smoothly move camera target toward player
    p->cam.target.x = damp(p->cam.target.x, p->pos.x, p->camSmooth, dt);
    p->cam.target.y = damp(p->cam.target.y, p->pos.y, p->camSmooth, dt);

    // Keep offset centered on current window size (handles resizes)
    p->cam.offset.x = GetScreenWidth()  * 0.5f;
    p->cam.offset.y = GetScreenHeight() * 0.5f;

    // clamp camera to level bounds
    const float worldW = (float)(level->w * TILE_SIZE);
    const float worldH = (float)(level->h * TILE_SIZE);
    const float halfViewW = p->cam.offset.x / p->cam.zoom;
    const float halfViewH = p->cam.offset.y / p->cam.zoom;

    float minX = halfViewW;
    float minY = halfViewH;
    float maxX = (worldW > halfViewW) ? (worldW - halfViewW) : halfViewW;
    float maxY = (worldH > halfViewH) ? (worldH - halfViewH) : halfViewH;

    if (p->cam.target.x < minX) p->cam.target.x = minX;
    if (p->cam.target.y < minY) p->cam.target.y = minY;
    if (p->cam.target.x > maxX) p->cam.target.x = maxX;
    if (p->cam.target.y > maxY) p->cam.target.y = maxY;
}

void Player_Draw(const Player* p) {
    DrawRectangleV(
        (Vector2){ p->pos.x - p->halfw, p->pos.y - p->halfh },
        (Vector2){ p->halfw * 2, p->halfh * 2 },
        (Color){ 255, 220, 50, 255 }
    );
}
