#pragma once
#include "../../lib/box2d/include/box2d/box2d.h"

const float tick 	= 1.0f / 20.0f;
const int subSteps 	= 4; 

b2WorldId init_physics_system();
