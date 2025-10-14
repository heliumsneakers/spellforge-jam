#include "raylib.h"
#include "level/level.h"
#include "entity/entity.hpp"
#include "player/player.h"
#include "player/projectile.h"
#include "physics/physics.h"
#include "entity/enemies.hpp"
#include "../lib/box2d/include/box2d/box2d.h"
#include "state.h"

void DrawScoreboard()
{
    const int fontSize = 28;
    const int margin = 20;

    char text[128];
    snprintf(text, sizeof(text), "Score: %d", g_enemiesKilled);

    int textWidth = MeasureText(text, fontSize);
    int x = GetScreenWidth() - textWidth - margin;
    int y = margin;

    DrawText(text, x, y, fontSize, RAYWHITE);
}

void RestartGame(Player* player, EntitySystem* es, Grid* level, b2WorldId* world)
{
    TraceLog(LOG_INFO, "🔄 Restarting game...");

    g_enemiesKilled = 0;
    g_wave = 0;
    g_speedMultiplier = 1.0f;
    g_lastWaveSpawned = 0;

    // 1. Destroy previous world completely
    if (b2World_IsValid(*world)) {
        b2DestroyWorld(*world);
    }

    // 2. Create a brand new world
    *world = InitWorld();
    BuildStaticsFromGrid(*world, level);

    // 3. Clear entities + physics body list
    g_entityBodies.clear();
    es->pool.clear();

    // 4. Reset player
    Player_Init(player, level);
    g_playerBody = CreatePlayer(*world, player->pos, 12.0f, 12.0f, 6.0f);

    // 5. Respawn environment props
    Entities_SpawnBoxesInLevel(es, level, 10, 20, {10.f, 10.f}, 0);
    g_entityBodies = Create_Entity_Bodies(es, *world);


    size_t prevCount = es->pool.size();
    // 6. Spawn fresh enemies
    Enemies_Spawn(es, level, player->pos, 6, 150.0f);
    Enemies_CreateBodies(es, *world, prevCount);

    // 7. Clear global projectile state
    g_projectiles.clear();

    // 8. Reset camera
    player->cam.target = player->pos;
    player->cam.zoom = 1.0f;

    g_gameOver = false;

    TraceLog(LOG_INFO, "✅ Game fully restarted!");
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

    int made = Entities_SpawnBoxesInLevel(&ents, &g, 10, 20, {10.f, 10.f}, 0);

    // INIT PHYSICS
    b2WorldId world = InitWorld();

    BuildStaticsFromGrid(world, &g);

    Player player;
    Player_Init(&player, &g);

    Vector2 spawnpx = player.pos;
    b2BodyId playerBody = CreatePlayer(world, spawnpx, 12.0f, 12.0f);

    g_entityBodies = Create_Entity_Bodies(&ents, world);


    size_t prevCount = ents.pool.size();
    Enemies_Spawn(&ents, &g, player.pos, 10, 300.0f);
    Enemies_CreateBodies(&ents, world, prevCount);

    TraceLog(LOG_INFO, "WORLD BUILT SETUP COMPLETE");

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        Vector2 dir = Build_Input();

        UpdatePlayer(playerBody, tick, dir, 200.0f);

        if(IsKeyDown(KEY_SPACE)) {
            Telekinesis_Hold(player.pos, 50.0f, teleForce, &ents);
        } else if(IsKeyReleased(KEY_SPACE)) {
            Telekinesis_Fire(player.pos, 50.0f, 500.0f, &ents);
        }

        Projectile_HandleSwitch();
        Projectile_Shoot(world, player.pos, player.cam);

        Enemies_Update(&ents, &g, playerBody, tick);

        b2World_Step(world, tick, subSteps);

        Physics_FlushDeletions(world, &ents); 

        Contact_ProcessPlayerEnemy(world, &ents);
        Projectile_Update(world, &ents, tick);
        Entities_Update(&ents, tick);

        Vector2 playerPosPx = GetPlayerPixels(playerBody);
        player.pos = playerPosPx;
        player.cam.target = playerPosPx;

        if (g_enemiesKilled / 2 > g_lastWaveSpawned) {
            g_wave++;
            g_lastWaveSpawned = g_enemiesKilled / 2; // mark wave as handled

            g_speedMultiplier += 0.05f;
            int spawnCount = 4;

            TraceLog(LOG_INFO, "⚔️ Wave %d triggered! Kills=%d Speed x%.2f", 
                     g_wave, g_enemiesKilled, g_speedMultiplier);

            size_t prevCount = ents.pool.size();
            Enemies_Spawn(&ents, &g, playerPosPx, spawnCount, 700.0f);
            Enemies_CreateBodies(&ents, world, prevCount);
        }

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
        Projectile_Draw();
        Player_Draw(&player);


        EndMode2D();

        if (!g_gameOver) {
            DrawScoreboard();
        }

        if (g_gameOver)
        {
            // Fill the screen with opaque black background
            DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), BLACK);

            const char* msg1 = "GAME OVER";
            const char* msg2 = "Press R to restart";

            // Create score text before resetting
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

            // Centered positions
            int x1 = (screenW - textWidth1) / 2;
            int y1 = screenH / 2 - 80;

            int x3 = (screenW - textWidth3) / 2;
            int y3 = screenH / 2 - 20;

            int x2 = (screenW - textWidth2) / 2;
            int y2 = screenH / 2 + 40;

            // Draw text layers
            DrawText(msg1, x1, y1, fontSize1, RED);
            DrawText(msg3, x3, y3, fontSize3, RAYWHITE);
            DrawText(msg2, x2, y2, fontSize2, GRAY);

            // Wait for restart
            if (IsKeyPressed(KEY_R))
            {
                RestartGame(&player, &ents, &g, &world);
                g_gameOver = false;
            }
        }
        EndDrawing();
    }

    grid_free(&g);
    Entities_Clear(&ents);
    CloseWindow();
}
