#include "animations.hpp"

Animation Animation_Load(const char* filepath, int frameCount, float frameTime, bool looping)
{
    Animation anim = {};
    anim.texture = LoadTexture(filepath);
    anim.frameCount = frameCount;
    anim.currentFrame = 0;
    anim.frameTime = frameTime;
    anim.timer = 0.0f;
    anim.looping = looping;
    anim.flipped = false;

    anim.frameWidth  = (float)anim.texture.width / frameCount;
    anim.frameHeight = (float)anim.texture.height;
    return anim;
}

void Animation_Update(Animation* anim, float dt)
{
    if (!anim || anim->frameCount <= 1) return;

    anim->timer += dt;
    if (anim->timer >= anim->frameTime)
    {
        anim->timer -= anim->frameTime;
        anim->currentFrame++;

        if (anim->currentFrame >= anim->frameCount)
        {
            if (anim->looping)
                anim->currentFrame = 0;
            else
                anim->currentFrame = anim->frameCount - 1; // stop on last
        }
    }
}

void Animation_Draw(const Animation* anim, Vector2 position, float scale, Color tint)
{
    if (!anim) return;

    const float fw = anim->frameWidth;
    const float fh = anim->frameHeight;

    Rectangle src;
    src.y = 0.0f;
    src.height = fh;

    if (anim->flipped) {
        // Start at the right-> edge of the frame and read <-left
        src.x = (anim->currentFrame + 1) * fw;
        src.width = -fw;
    } else {
        // Normal: left to right
        src.x = anim->currentFrame * fw;
        src.width = fw;
    }

    Rectangle dest = {
        position.x,
        position.y,
        fw * scale,
        fh * scale
    };

    Vector2 origin = { fw * 0.5f, fh * 0.5f };
    DrawTexturePro(anim->texture, src, dest, origin, 0.0f, tint);
}

void Animation_Unload(Animation* anim)
{
    if (anim && anim->texture.id != 0)
        UnloadTexture(anim->texture);
}

