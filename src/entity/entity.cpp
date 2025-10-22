#include "entity.hpp"
#include <cmath>
#include <algorithm>

// --- tiny RNG (xorshift32) -------------------------------------------------
static uint32_t xr(uint32_t* s){ uint32_t x=*s; x^=x<<13; x^=x>>17; x^=x<<5; return *s=x; }
static int rrange(uint32_t* s, int a, int b){ if (b < a){int t=a;a=b;b=t;} return a + int(xr(s) % (uint32_t)(b - a + 1)); }

// --- helpers ---------------------------------------------------------------
static inline Vector2 v2(float x,float y){ return {x,y}; }
static inline bool aabb_overlap(Vector2 aPos, Vector2 aHalf, Vector2 bPos, Vector2 bHalf){
    return std::fabs(aPos.x - bPos.x) <= (aHalf.x + bHalf.x) &&
    std::fabs(aPos.y - bPos.y) <= (aHalf.y + bHalf.y);
}

// 3x3 all-floor clearance around tile (keeps props off walls/corridor edges)
static bool has_clearance_1(const Grid* g, int tx, int ty){
    for (int dy=-1; dy<=1; ++dy)
        for (int dx=-1; dx<=1; ++dx){
            Tile* t = grid_at((Grid*)g, tx+dx, ty+dy);
            if (!t || t->id != TILE_FLOOR) return false;
        }
    return true;
}

// --- internal lookup -------------------------------------------------------
static Entity* find_by_id(EntitySystem* es, int id){
    if (id <= 0) return nullptr;
    for (auto& e : es->pool) if (e.id == id && e.active) return &e;
    return nullptr;
}
static const Entity* find_by_id(const EntitySystem* es, int id){
    if (id <= 0) return nullptr;
    for (auto& e : es->pool) if (e.id == id && e.active) return &e;
    return nullptr;
}

void Entities_Init(EntitySystem* es, uint32_t seed){
    es->pool.clear();
    es->nextId = 1;
    es->seed = seed ? seed : (uint32_t)time(nullptr);
}

void Entities_Clear(EntitySystem* es){
    es->pool.clear();
    es->nextId = 1;
}

int Entities_CreateBox(EntitySystem* es, EntityKind kind, Vector2 posPx, Vector2 halfPx, Color color){
    Entity e;
    e.id    = es->nextId++;
    e.kind  = kind;
    e.pos   = posPx;
    e.half  = halfPx;
    e.color = color;
    e.active= true;
    es->pool.emplace_back(e);
    return e.id;
}

void Entities_Destroy(EntitySystem* es, int id){
    for (auto& e : es->pool){
        if (e.id == id){
            e.active = false;
            break;
        }
    }
    es->pool.erase(std::remove_if(es->pool.begin(), es->pool.end(),
                                  [](const Entity& E){ return !E.active; }),
                   es->pool.end());
}

Entity* Entities_Get(EntitySystem* es, int id){ return find_by_id(es, id); }
const Entity* Entities_Get(const EntitySystem* es, int id){ return find_by_id(es, id); }


void Entities_Draw(const EntitySystem* es){
    for (const auto& e : es->pool){
        if (!e.active) continue;
        DrawRectangleV(
            v2(e.pos.x - e.half.x, e.pos.y - e.half.y),
            v2(e.half.x*2.f,       e.half.y*2.f),
            e.color
        );
    }
}

int Entities_SpawnBoxesInLevel(EntitySystem* es,
                               const Grid* g,
                               int minCount, int maxCount,
                               Vector2 halfPx,
                               uint32_t seed)
{
    if (!g || g->w <= 0 || g->h <= 0) return 0;
    if (minCount < 0) minCount = 0;
    if (maxCount < minCount) maxCount = minCount;

    uint32_t rng = seed ? seed : es->seed;

    // Collect candidate floor tiles with 1-tile clearance
    std::vector<int> candidates;
    candidates.reserve(g->w * g->h);
    for (int y=0; y<g->h; ++y)
        for (int x=0; x<g->w; ++x){
            Tile* t = grid_at((Grid*)g, x, y);
            if (t && t->id == TILE_FLOOR && has_clearance_1(g,x,y)){
                candidates.push_back(y*g->w + x);
            }
        }
    if (candidates.empty()) return 0;

    // Choose target count
    const int target = rrange(&rng, minCount, maxCount);

    // Light shuffle via Fisher-Yates / Knuth shuffle (thanks to my TAOCP book)
    for (int i=(int)candidates.size()-1; i>0; --i){
        int j = rrange(&rng, 0, i);
        std::swap(candidates[i], candidates[j]);
    }

    // Spawn up to 'target' boxes, avoid overlapping previously spawned boxes
    int spawned = 0;
    for (int i=0; i<(int)candidates.size() && spawned < target; ++i){
        int idx = candidates[i];
        int tx = idx % g->w;
        int ty = idx / g->w;
        Vector2 posPx = v2(tx * (float)TILE_SIZE + TILE_SIZE*0.5f,
                           ty * (float)TILE_SIZE + TILE_SIZE*0.5f);

        // Avoid overlapping any existing entity AABBs
        bool overlaps = false;
        for (const auto& e : es->pool){
            if (!e.active) continue;
            if (aabb_overlap(posPx, halfPx, e.pos, e.half)){ overlaps = true; break; }
        }
        if (overlaps) continue;

        Entities_CreateBox(es, EntityKind::Prop, posPx, halfPx, BLACK);
        ++spawned;
    }

    // Advance system seed so next call differs
    es->seed = xr(&rng);
    return spawned;
}
