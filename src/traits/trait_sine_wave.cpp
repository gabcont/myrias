#include "traits/trait_sine_wave.h"

#include "profiling/myrias_profiling.h"
#include "structs/myrias_entity_streams.h"

namespace myrias {

void kernel_sine_wave(GroupContext &r_group, EntitySoA &r_soa, float p_dt) {
    MYRIAS_PROFILE_ZONE("Myrias::trait_sine_wave");

    const int slot = 2; // TRAIT_SINE_WAVE: bit 10 - 8 = 2
    const auto *cfg = static_cast<const SineWaveConfig *>(r_group.trait_configs[slot].get());
    if (cfg == nullptr) {
        return;
    }

    const float amp = cfg->amplitude;
    const float freq = cfg->frequency;
    const float phase = cfg->phase;
    const float omega = (float)godot::Math::TAU * freq;
    const float factor = amp * omega * p_dt;

    const bool is_uniform = (r_group.uniform_traits & TRAIT_SINE_WAVE) != 0;
    const uint64_t *tbits = is_uniform ? nullptr : (r_group.trait_bits[slot].is_empty() ? nullptr : r_group.trait_bits[slot].ptr());
    if (!is_uniform && tbits == nullptr) {
        return;
    }

    const uint64_t *abits = r_group.alive_bits.ptr();
    const uint32_t num_words = (r_group.last_active + 63) / 64;
    float *px = r_soa.phys_x.ptrw() + r_group.offset;
    float *py = r_soa.phys_y.ptrw() + r_group.offset;
    const float *pvx = r_soa.phys_vel_x.ptr() + r_group.offset;
    const float *pvy = r_soa.phys_vel_y.ptr() + r_group.offset;
    const float *lifetime = r_soa.lifetime.ptr() + r_group.offset;

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
            const float speed_sq = vx * vx + vy * vy;
            if (speed_sq > 1e-6f) {
                const float inv_speed = 1.0f / Math::sqrt(speed_sq);
                const float nx = -vy * inv_speed;
                const float ny = vx * inv_speed;
                const float ds = factor * Math::cos(omega * lifetime[j] + phase);
                px[j] += nx * ds;
                py[j] += ny * ds;
            }
        }
    }
}

} // namespace myrias
