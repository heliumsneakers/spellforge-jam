#pragma once
#include "raylib.h"

struct Animation {
    Texture2D texture;
    int frameCount;
    int currentFrame;
    float frameWidth;
    float frameHeight;
    float frameTime;   // seconds per frame
    float timer;
    bool looping;
    bool flipped;      // for left/right facing
};


// Load an animation from a spritesheet file
Animation Animation_Load(const char* filepath, int frameCount, float frameTime, bool looping = true);

// Update animation frame timer
void Animation_Update(Animation* anim, float dt);

// Draw animation centered at position
void Animation_Draw(const Animation* anim, Vector2 position, float scale = 1.0f, Color tint = WHITE);

// Unload the animation’s texture
void Animation_Unload(Animation* anim);

// Reset animation to first frame
inline void Animation_Reset(Animation* anim) {
    anim->currentFrame = 0;
    anim->timer = 0.0f;
}

