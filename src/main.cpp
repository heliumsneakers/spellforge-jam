#include "raylib.h"
#include "level/level.h"
#include "entity/entity.hpp"
#include "player/player.h"
#include "player/projectile.h"
#include "physics/physics.h"
#include "entity/enemies.hpp"
#include "../lib/box2d/include/box2d/box2d.h"
#include "state.h"
#include <cstdio>

void DrawScoreboard() {
    const int fontSize = 28;
    const int margin = 20;

    char text1[128];
    char text2[128];
    snprintf(text1, sizeof(text1), "Score: %d", g_enemiesKilled);
    snprintf(text2, sizeof(text2), "Total Enemies: %d", (int)g_enemies.size());
    int textWidth1 = MeasureText(text1, fontSize);
    int textWidth2 = MeasureText(text2, fontSize);
    int x1 = GetScreenWidth() - textWidth1 - margin;
    int x2 = GetScreenWidth() - textWidth2 - margin;

    DrawText(text1, x1, margin, fontSize, RAYWHITE);
    DrawText(text2, x2, margin + 30, fontSize, RAYWHITE);
}

void RestartGame(Player* player, EntitySystem* es, Grid* level, b2WorldId* world) {
    Enemies_Clear();

    g_enemiesKilled = 0;
    g_wave = 0;
    g_speedMultiplier = 1.0f;
    g_lastWaveSpawned = 0;

    if (b2World_IsValid(*world)) {
        b2DestroyWorld(*world);
    }

    *world = InitWorld();
    BuildStaticsFromGrid(*world, level);

    // New ID-based maps
    g_entityToBody.clear();
    g_bodyToEntity.clear();
    es->pool.clear();

    Player_Init(player, level);
    g_playerBody = CreatePlayer(*world, player->pos, 12.0f, 12.0f, 6.0f);

    // Props / boxes
    Entities_SpawnBoxesInLevel(es, level, 10, 20, (Vector2){10.f, 10.f}, 0);
    Create_Entity_Bodies(es, *world);

    // Enemies
    const size_t prevCount = es->pool.size();
    Enemies_Spawn(es, level, player->pos, 6, 150.0f);
    Enemies_CreateBodies(es, *world, prevCount);

    TraceLog(LOG_INFO, "Restart: entities=%zu enemies=%zu maps: e2b=%zu b2e=%zu",
             es->pool.size(), g_enemies.size(),
             g_entityToBody.size(), g_bodyToEntity.size());

    g_projectiles.clear();

    player->cam.target = player->pos;
    player->cam.zoom = 2.0f;

    g_gameOver = false;
}

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

    // Spawn some props/boxes in the level
    Entities_SpawnBoxesInLevel(&ents, &g, 10, 20, (Vector2){10.f, 10.f}, 0);

    // INIT PHYSICS
    b2WorldId world = InitWorld();
    BuildStaticsFromGrid(world, &g);

    Player player;
    Player_Init(&player, &g);

    Vector2 spawnpx = player.pos;
    b2BodyId playerBody = CreatePlayer(world, spawnpx, 12.0f, 12.0f); // linear damping default

    // Create bodies for existing entities (props, etc.)
    Create_Entity_Bodies(&ents, world);

    // Enemies
    const size_t prevCount = ents.pool.size();
    Enemies_Spawn(&ents, &g, player.pos, 10, 300.0f);
    Enemies_CreateBodies(&ents, world, prevCount);


    TraceLog(LOG_INFO, "WORLD BUILT SETUP COMPLETE: entities=%zu enemies=%zu maps: e2b=%zu b2e=%zu",
             ents.pool.size(), g_enemies.size(),
             g_entityToBody.size(), g_bodyToEntity.size());

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        Vector2 dir = Build_Input();
        UpdatePlayer(&player, playerBody, tick, dir, 125.0f);

        if (IsKeyDown(KEY_SPACE)) {
            Telekinesis_Hold(player.pos, 50.0f, teleForce, &ents);
        } else if (IsKeyReleased(KEY_SPACE)) {
            Telekinesis_Fire(player.pos, 50.0f, 500.0f, &ents);
        }

        Projectile_HandleSwitch();
        Projectile_Shoot(world, player.pos, player.cam);

        Enemies_Update(&ents, &g, playerBody, tick);

        b2World_Step(world, tick, subSteps);

        Contact_ProcessPlayerEnemy(world, &ents);
        Projectile_Update(world, &ents, tick);
        Entities_Update(&ents, tick);

        // Sync player camera
        Vector2 playerPosPx = GetPlayerPixels(playerBody);
        player.pos = playerPosPx;
        player.cam.target = playerPosPx;

        // Spawn waves
        if (g_enemiesKilled / 2 > g_lastWaveSpawned) {
            g_wave++;
            g_lastWaveSpawned = g_enemiesKilled / 2;

            g_speedMultiplier += 0.05f;
            int spawnCount = 4;

            TraceLog(LOG_INFO, "Wave %d triggered! Kills=%d Speed x%.2f",
                     g_wave, g_enemiesKilled, g_speedMultiplier);

            const size_t prevCountWave = ents.pool.size();
            Enemies_Spawn(&ents, &g, playerPosPx, spawnCount, 700.0f);
            Enemies_CreateBodies(&ents, world, prevCountWave);

            TraceLog(LOG_INFO, "Wave spawn: entities=%zu enemies=%zu maps: e2b=%zu b2e=%zu",
                     ents.pool.size(), g_enemies.size(),
                     g_entityToBody.size(), g_bodyToEntity.size());
        }

        // Ensure deletions get flushed AFTER creations
        Physics_FlushDeletions(world, &ents);

        BeginDrawing();
        ClearBackground((Color){30,30,40,255});

        BeginMode2D(player.cam);

        // Draw grid
        for (int y = 0; y < g.h; ++y)
            for (int x = 0; x < g.w; ++x) {
                Tile* t = grid_at(&g, x, y);
                Color c = (t->id == TILE_WALL) ? (Color){60,60,70,255} : (Color){200,200,200,255};
                DrawRectangle(x * TILE_SIZE, y * TILE_SIZE, TILE_SIZE, TILE_SIZE, c);
            }

        Entities_Draw(&ents);
        Projectile_Draw();
        Player_Draw(&player);

        EndMode2D();

        if (!g_gameOver) {
            DrawScoreboard();
        }

        if (g_gameOver) {
            DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), BLACK);

            const char* msg1 = "GAME OVER";
            const char* msg2 = "Press R to restart";

            char msg3[128];
            snprintf(msg3, sizeof(msg3), "Wave: %d   Kills: %d", g_wave, g_enemiesKilled);

            int fontSize1 = 60;
            int fontSize2 = 24;
            int fontSize3 = 28;

            int textWidth1 = MeasureText(msg1, fontSize1);
            int textWidth2 = MeasureText(msg2, fontSize2);
            int textWidth3 = MeasureText(msg3, fontSize3);

            int screenW = GetScreenWidth();
            int screenH = GetScreenHeight();

            int x1 = (screenW - textWidth1) / 2;
            int y1 = screenH / 2 - 80;

            int x3 = (screenW - textWidth3) / 2;
            int y3 = screenH / 2 - 20;

            int x2 = (screenW - textWidth2) / 2;
            int y2 = screenH / 2 + 40;

            DrawText(msg1, x1, y1, fontSize1, RED);
            DrawText(msg3, x3, y3, fontSize3, RAYWHITE);
            DrawText(msg2, x2, y2, fontSize2, GRAY);

            if (IsKeyPressed(KEY_R)) {
                RestartGame(&player, &ents, &g, &world);
                g_gameOver = false;
            }
        }
        EndDrawing();
    }

    grid_free(&g);
    Player_Unload(&player);
    Enemies_Clear();
    Entities_Clear(&ents);
    CloseWindow();
}
