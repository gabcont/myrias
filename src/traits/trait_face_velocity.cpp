#include "traits/trait_face_velocity.h"

#include "profiling/myrias_profiling.h"
#include "structs/myrias_entity_streams.h"

namespace myrias {

void kernel_face_velocity(GroupContext &r_group, EntitySoA &r_soa, float p_dt) {
    MYRIAS_PROFILE_ZONE("Myrias::trait_face_velocity");

    const int slot = 4; // TRAIT_FACE_VELOCITY: bit 12 - 8 = 4
    const auto *cfg = static_cast<const FaceVelocityConfig *>(r_group.trait_configs[slot].get());
    if (cfg == nullptr) {
        return;
    }

    const float offset_rad = cfg->angle_offset;
    const float c_alpha = Math::cos(offset_rad);
    const float s_alpha = Math::sin(offset_rad);

    const bool is_uniform = (r_group.uniform_traits & TRAIT_FACE_VELOCITY) != 0;
    const uint64_t *tbits = is_uniform ? nullptr : (r_group.trait_bits[slot].is_empty() ? nullptr : r_group.trait_bits[slot].ptr());
    if (!is_uniform && tbits == nullptr) {
        return;
    }

    const uint64_t *abits = r_group.alive_bits.ptr();
    const uint32_t num_words = (r_group.last_active + 63) / 64;
    const float *pvx = r_soa.phys_vel_x.ptr() + r_group.offset;
    const float *pvy = r_soa.phys_vel_y.ptr() + r_group.offset;
    float *rot = r_soa.rot.ptrw() + r_group.offset;
    const float *scale = r_soa.scale.ptr() + r_group.offset;
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

            const float vx = pvx[j];
            const float vy = pvy[j];
            const float len_sq = vx * vx + vy * vy;

            float cos_res = 1.0f;
            float sin_res = 0.0f;

            if (len_sq > 1e-6f) {
                const float inv_len = 1.0f / Math::sqrt(len_sq);
                const float cos_v = vx * inv_len;
                const float sin_v = vy * inv_len;
                cos_res = cos_v * c_alpha - sin_v * s_alpha;
                sin_res = sin_v * c_alpha + cos_v * s_alpha;
                rot[j] = Math::atan2(sin_res, cos_res);
            } else {
                cos_res = Math::cos(rot[j]);
                sin_res = Math::sin(rot[j]);
            }

            const Vector2 fsize = get_frame_size(r_group, cur_anim[j], cur_frame[j]);
            const float sc = scale[j];
            const float c = cos_res * sc;
            const float s = sin_res * sc;

            float *o = buf + (int64_t)j * 12;
            o[0] = c * fsize.x;
            o[1] = -s * fsize.y;
            o[4] = s * fsize.x;
            o[5] = c * fsize.y;
        }
    }
}

} // namespace myrias
