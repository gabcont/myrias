#include "simd/myrias_kinematics.h"

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "simd/myrias_kinematics-inl.h"
#include "hwy/foreach_target.h"
#include "simd/myrias_kinematics-inl.h"

#include "hwy/per_target.h"
#include "hwy/targets.h"

namespace myrias {

HWY_EXPORT(RunKinematics);

void run_kinematics(float *x, float *y, const float *vx, const float *vy, size_t count, float dt) {
    HWY_DYNAMIC_DISPATCH(RunKinematics)(x, y, vx, vy, count, dt);
}

const char *get_hwy_target_name() {
    return hwy::TargetName(hwy::DispatchedTarget());
}

} // namespace myrias
