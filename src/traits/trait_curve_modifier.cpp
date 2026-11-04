#include "traits/trait_curve_modifier.h"

#include "profiling/myrias_profiling.h"
#include "structs/myrias_entity_streams.h"

namespace myrias {

void kernel_curve_modifier(GroupContext &r_group, EntitySoA &r_soa, float p_dt) {
    MYRIAS_PROFILE_ZONE("Myrias::trait_curve_modifier");

    const int slot = 3; // TRAIT_CURVE_MODIFIER: bit 11 - 8 = 3
    const auto *cfg = static_cast<const CurveModifierConfig *>(r_group.trait_configs[slot].get());
    if (cfg == nullptr) {
        return;
    }

    const float duration = cfg->duration;
    const float multiplier = cfg->multiplier;
    const bool loop = cfg->loop;
    const int prop = cfg->target_property;

    const bool is_uniform = (r_group.uniform_traits & TRAIT_CURVE_MODIFIER) != 0;
    const uint64_t *tbits = is_uniform ? nullptr : (r_group.trait_bits[slot].is_empty() ? nullptr : r_group.trait_bits[slot].ptr());
    if (!is_uniform && tbits == nullptr) {
        return;
    }

    const uint64_t *abits = r_group.alive_bits.ptr();
    const uint32_t num_words = (r_group.last_active + 63) / 64;
    float *pvx = r_soa.phys_vel_x.ptrw() + r_group.offset;
    float *pvy = r_soa.phys_vel_y.ptrw() + r_group.offset;
    float *rot = r_soa.rot.ptrw() + r_group.offset;
    float *scale = r_soa.scale.ptrw() + r_group.offset;
    const float *lifetime = r_soa.lifetime.ptr() + r_group.offset;
    const uint16_t *cur_anim = r_soa.cur_animation.ptr() + r_group.offset;
    const uint16_t *cur_frame = r_soa.cur_frame.ptr() + r_group.offset;
    float *buf = r_group.buffer.ptrw();

    for (uint32_t w = 0; w < num_words; w++) {
        uint64_t word = abits[w];
        if (!is_uniform) {
            word &= tbits[w];
        }
        if (w == num_words - 1 && (r_group.last_active & 63) != 0) {
            word &= (1ull << (r_group.last_active & 63)) - 1ull;
        }

        while (word) {
            const uint32_t j = (w << 6) + __builtin_ctzll(word);
            word &= word - 1;

            const float lt = lifetime[j];
            float t_n = (duration > 0.0f) ? (lt / duration) : 0.0f;
            if (loop) {
                t_n = Math::fmod(t_n, 1.0f);
                if (t_n < 0.0f) {
                    t_n += 1.0f;
                }
            } else {
                t_n = CLAMP(t_n, 0.0f, 1.0f);
            }

            const float idx_f = t_n * (float)(CurveModifierConfig::LUT_SIZE - 1);
            const int idx_i = (int)idx_f;
            const float frac = idx_f - (float)idx_i;
            const float val = (idx_i >= CurveModifierConfig::LUT_SIZE - 1) ?
                    (cfg->lut[CurveModifierConfig::LUT_SIZE - 1] * multiplier) :
                    ((cfg->lut[idx_i] * (1.0f - frac) + cfg->lut[idx_i + 1] * frac) * multiplier);

            if (prop == CURVE_TARGET_SCALE) {
                scale[j] = val;
                const Vector2 fsize = get_frame_size(r_group, cur_anim[j], cur_frame[j]);
                const float c = Math::cos(rot[j]) * val;
                const float s = Math::sin(rot[j]) * val;
                float *o = buf + (int64_t)j * 12;
                o[0] = c * fsize.x;
                o[1] = -s * fsize.y;
                o[4] = s * fsize.x;
                o[5] = c * fsize.y;
            } else if (prop == CURVE_TARGET_ROTATION) {
                rot[j] = val;
                const Vector2 fsize = get_frame_size(r_group, cur_anim[j], cur_frame[j]);
                const float c = Math::cos(val) * scale[j];
                const float s = Math::sin(val) * scale[j];
                float *o = buf + (int64_t)j * 12;
                o[0] = c * fsize.x;
                o[1] = -s * fsize.y;
                o[4] = s * fsize.x;
                o[5] = c * fsize.y;
            } else if (prop == CURVE_TARGET_SPEED) {
                const float vx = pvx[j];
                const float vy = pvy[j];
                const float speed_sq = vx * vx + vy * vy;
                if (speed_sq > 1e-6f) {
                    const float inv_s = 1.0f / Math::sqrt(speed_sq);
                    pvx[j] = vx * inv_s * val;
                    pvy[j] = vy * inv_s * val;
                }
            }
        }
    }
}

} // namespace myrias
