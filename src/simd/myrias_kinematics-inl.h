#if defined(MYRIAS_SIMD_KINEMATICS_INL_H_) == defined(HWY_TARGET_TOGGLE)
#ifdef MYRIAS_SIMD_KINEMATICS_INL_H_
#undef MYRIAS_SIMD_KINEMATICS_INL_H_
#else
#define MYRIAS_SIMD_KINEMATICS_INL_H_
#endif

#include "hwy/highway.h"

HWY_BEFORE_NAMESPACE();
namespace myrias {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

static void RunKinematics(float *HWY_RESTRICT x, float *HWY_RESTRICT y,
                          const float *HWY_RESTRICT vx, const float *HWY_RESTRICT vy,
                          size_t count, float dt) {
    const hn::ScalableTag<float> d;
    const size_t N = hn::Lanes(d);
    const auto vdt = hn::Set(d, dt);

    size_t i = 0;
    for (; i + N <= count; i += N) {
        const auto px = hn::LoadU(d, x + i);
        const auto pvx = hn::LoadU(d, vx + i);
        hn::StoreU(hn::MulAdd(pvx, vdt, px), d, x + i);

        const auto py = hn::LoadU(d, y + i);
        const auto pvy = hn::LoadU(d, vy + i);
        hn::StoreU(hn::MulAdd(pvy, vdt, py), d, y + i);
    }

    for (; i < count; ++i) {
        x[i] += vx[i] * dt;
        y[i] += vy[i] * dt;
    }
}

} // namespace HWY_NAMESPACE
} // namespace myrias
HWY_AFTER_NAMESPACE();

#endif // MYRIAS_SIMD_KINEMATICS_INL_H_
