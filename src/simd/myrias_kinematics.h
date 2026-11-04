#pragma once
#include <cstddef>

namespace myrias {
void run_kinematics(float *x, float *y, const float *vx, const float *vy, size_t count, float dt);
const char *get_hwy_target_name();
} // namespace myrias
