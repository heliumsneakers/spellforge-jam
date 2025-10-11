#include "raylib.h"
#include "level/level.h"
#include "entity/entity.hpp"
#include "player/player.h"
#include "physics/physics.h"
#include "../lib/box2d/include/box2d/box2d.h"
#include <numeric>

int main() {
    InitWindow(1280, 720, "SpellForge");
    SetTargetFPS(60);

    Grid g;
    grid_init(&g, 80, 45);

    LevelGenParams params = {
        .attempts = 18,
        .roomMinW = 6, .roomMinH = 6,
        .roomMaxW = 12, .roomMaxH = 10,
        .corridorMinW = 2,
        .corridorMaxW = 4,
        .seed = 0       
    };

    gen_level(&g, &params);

    EntitySystem ents;
    Entities_Init(&ents, 0);

    int made = Entities_SpawnBoxesInLevel(&ents, &g, 10, 20, {10.f, 10.f}, 0);

    // INIT PHYSICS
    b2WorldId world = InitWorld();
    
    BuildStaticsFromGrid(world, &g);

    Player player;
    Player_Init(&player, &g);

    Vector2 spawnpx = player.pos;
    b2BodyId playerBody = CreatePlayer(world, spawnpx, 12.0f, 12.0f);

    g_entityBodies = Create_Entity_Bodies(&ents, world);

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        Vector2 dir = {0,0};
        if (IsKeyDown(KEY_W)) dir.y -= 1;
        if (IsKeyDown(KEY_S)) dir.y += 1;
        if (IsKeyDown(KEY_A)) dir.x -= 1;
        if (IsKeyDown(KEY_D)) dir.x += 1;

        UpdatePlayer(playerBody, tick, dir, 200.0f);
        Entities_Update(&ents, tick);

        b2World_Step(world, tick, subSteps);

        Vector2 playerPosPx = GetPlayerPixels(playerBody);
        player.pos = playerPosPx;
        player.cam.target = playerPosPx;

        BeginDrawing();
        ClearBackground((Color){30,30,40,255});
        
        BeginMode2D(player.cam);

            // Draw grid
            for (int y=0; y<g.h; ++y)
            for (int x=0; x<g.w; ++x) {
                Tile* t = grid_at(&g,x,y);
                Color c = (t->id == TILE_WALL)? (Color){60,60,70,255} : (Color){200,200,200,255};
                DrawRectangle(x*TILE_SIZE, y*TILE_SIZE, TILE_SIZE, TILE_SIZE, c);
            }

        Entities_Draw(&ents);
        Player_Draw(&player);

        EndMode2D();

        EndDrawing();
    }

    grid_free(&g);
    Entities_Clear(&ents);
    CloseWindow();
}
