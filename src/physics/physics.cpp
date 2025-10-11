#include "physics.h"
#include "../../lib/box2d/include/box2d/box2d.h"


b2WorldId init_phys_world() {
    b2WorldDef worldDef   = b2DefaultWorldDef();
    worldDef.gravity            = (b2Vec2){0.0, 0.0};
    b2WorldId worldId           = b2CreateWorld(&worldDef);

    return worldId;
}
