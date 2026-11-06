#include "myrias_server.h"

#include <godot_cpp/variant/utility_functions.hpp>

#include "../profiling/myrias_profiling.h"

using namespace godot;

void MyriasServer::finalize_group_deaths(GroupContext &g) {
    tail_trim_bitset(g);
    if (g.last_active > 0 && (g.last_active - g.active_amount) * 2 > g.last_active) {
        GROUP_SET_DIRTY(g.traits);
    }
}

int MyriasServer::create_group(const Ref<MyriasEntityModel> &p_model, int p_pool_size, RID p_canvas_item) {
    if (p_model.is_null()) {
        UtilityFunctions::push_error("MyriasServer::create_group: null model.");
        return -1;
    }
    if (p_pool_size <= 0 || p_pool_size > 65535) {
        UtilityFunctions::push_error("MyriasServer::create_group: pool_size must be in [1, 65535].");
        return -1;
    }

    const Ref<SpriteFrames> sf = p_model->get_sprite_frames();
    const uint16_t gid = (uint16_t)m_next_group_id++;

    MyriasCommand cmd;
    cmd.type = MyriasCommand::CREATE_GROUP;
    cmd.group_id = gid;
    cmd.frame_table = FrameTable(sf);
    cmd.texture = FrameTable::extract_texture(sf);
    cmd.radius = p_model->get_radius();
    cmd.collision_layer = p_model->get_collision_layer();
    cmd.collision_mask = p_model->get_collision_mask();
    cmd.pool_size = (uint16_t)p_pool_size;
    cmd.ci = p_canvas_item;

    m_commands.push_back(cmd);
    connect_tick();
    return gid;
}

void MyriasServer::remove_group(int p_group_id) {
    if (p_group_id < 0 || p_group_id > 65535) {
        return;
    }
    MyriasCommand cmd;
    cmd.type = MyriasCommand::REMOVE_GROUP;
    cmd.group_id = (uint16_t)p_group_id;
    m_commands.push_back(cmd);
}

void MyriasServer::spawn(int p_group_id, Vector2 p_pos, Vector2 p_vel, int p_animation, float p_scale, float p_rot) {
    if (p_group_id < 0 || p_group_id > 65535) {
        return;
    }
    MyriasCommand cmd;
    cmd.type = MyriasCommand::SPAWN;
    cmd.group_id = (uint16_t)p_group_id;
    cmd.pos = p_pos;
    cmd.vel = p_vel;
    cmd.animation = p_animation;
    cmd.scale = p_scale;
    cmd.rot = p_rot;
    m_commands.push_back(cmd);
}

void MyriasServer::despawn(int p_group_id, int p_local_index) {
    if (p_group_id < 0 || p_group_id > 65535) {
        return;
    }
    MyriasCommand cmd;
    cmd.type = MyriasCommand::DESPAWN;
    cmd.group_id = (uint16_t)p_group_id;
    cmd.local_index = p_local_index;
    m_commands.push_back(cmd);
}

void MyriasServer::configure_trait(int p_group_id, MyriasTraitType p_trait, const std::shared_ptr<TraitConfig> &p_config) {
    if (p_group_id < 0 || p_group_id > 65535 || !is_valid_single_behavior_trait((uint32_t)p_trait) || p_config == nullptr) {
        return;
    }
    MyriasCommand cmd;
    cmd.type = MyriasCommand::CONFIGURE_TRAIT;
    cmd.group_id = (uint16_t)p_group_id;
    cmd.trait_flag = (uint32_t)p_trait;
    cmd.trait_cfg = p_config;
    m_commands.push_back(cmd);
}

void MyriasServer::configure_trait_resource(int p_group_id, const Ref<MyriasTrait> &p_trait, Node2D *p_owner) {
    if (p_trait.is_null()) {
        return;
    }
    std::shared_ptr<TraitConfig> cfg = p_trait->build_config(p_owner);
    if (cfg == nullptr) {
        return;
    }
    configure_trait(p_group_id, p_trait->get_trait_type(), cfg);
}

void MyriasServer::remove_group_trait(int p_group_id, uint32_t p_trait_flag) {
    if (p_group_id < 0 || p_group_id > 65535 || !is_valid_single_behavior_trait(p_trait_flag)) {
        return;
    }
    MyriasCommand cmd;
    cmd.type = MyriasCommand::REMOVE_TRAIT;
    cmd.group_id = (uint16_t)p_group_id;
    cmd.trait_flag = p_trait_flag;
    m_commands.push_back(cmd);
}

void MyriasServer::set_entity_trait(int p_group_id, int p_local_index, uint32_t p_trait_flag, bool p_enabled) {
    if (p_group_id < 0 || p_group_id > 65535 || p_local_index < 0 || !is_valid_single_behavior_trait(p_trait_flag)) {
        return;
    }
    MyriasCommand cmd;
    cmd.type = MyriasCommand::SET_ENTITY_TRAIT;
    cmd.group_id = (uint16_t)p_group_id;
    cmd.local_index = p_local_index;
    cmd.trait_flag = p_trait_flag;
    cmd.trait_enabled = p_enabled;
    m_commands.push_back(cmd);
}

bool MyriasServer::has_entity_trait(int p_group_id, int p_local_index, uint32_t p_trait_flag) const {
    if (!is_valid_single_behavior_trait(p_trait_flag)) {
        return false;
    }
    const GroupContext *g = const_cast<MyriasServer *>(this)->find_group(p_group_id);
    if (g == nullptr || p_local_index < 0 || (uint32_t)p_local_index >= g->last_active) {
        return false;
    }
    if (!ALIVE_TEST(g->alive_bits.ptr(), p_local_index)) {
        return false;
    }
    const int s = __builtin_ctz(p_trait_flag) - TRAIT_BEHAVIOR_FIRST_BIT;
    if (s < 0 || s >= (int)TRAIT_BEHAVIOR_COUNT) {
        return false;
    }
    if (!g->trait_bits[s].is_empty()) {
        return ALIVE_TEST(g->trait_bits[s].ptr(), p_local_index);
    }
    return (g->uniform_traits & p_trait_flag) != 0;
}

uint32_t MyriasServer::get_group_traits(int p_group_id) const {
    const GroupContext *g = const_cast<MyriasServer *>(this)->find_group(p_group_id);
    if (g == nullptr) {
        return 0;
    }
    return g->traits;
}

Vector2 MyriasServer::get_entity_position(int p_group_id, int p_local_index) const {
    const GroupContext *g = const_cast<MyriasServer *>(this)->find_group(p_group_id);
    if (g == nullptr || p_local_index < 0 || (uint32_t)p_local_index >= g->last_active) {
        return Vector2();
    }
    const uint32_t abs_idx = g->offset + p_local_index;
    return Vector2(m_entities.phys_x[abs_idx], m_entities.phys_y[abs_idx]);
}

Vector2 MyriasServer::get_entity_velocity(int p_group_id, int p_local_index) const {
    const GroupContext *g = const_cast<MyriasServer *>(this)->find_group(p_group_id);
    if (g == nullptr || p_local_index < 0 || (uint32_t)p_local_index >= g->last_active) {
        return Vector2();
    }
    const uint32_t abs_idx = g->offset + p_local_index;
    return Vector2(m_entities.phys_vel_x[abs_idx], m_entities.phys_vel_y[abs_idx]);
}

float MyriasServer::get_entity_scale(int p_group_id, int p_local_index) const {
    const GroupContext *g = const_cast<MyriasServer *>(this)->find_group(p_group_id);
    if (g == nullptr || p_local_index < 0 || (uint32_t)p_local_index >= g->last_active) {
        return 0.0f;
    }
    const uint32_t abs_idx = g->offset + p_local_index;
    return m_entities.scale[abs_idx];
}

float MyriasServer::get_entity_rotation(int p_group_id, int p_local_index) const {
    const GroupContext *g = const_cast<MyriasServer *>(this)->find_group(p_group_id);
    if (g == nullptr || p_local_index < 0 || (uint32_t)p_local_index >= g->last_active) {
        return 0.0f;
    }
    const uint32_t abs_idx = g->offset + p_local_index;
    return m_entities.rot[abs_idx];
}

int MyriasServer::get_active_count(int p_group_id) const {
    int total = 0;
    for (int i = 0; i < m_groups_ctx.size(); i++) {
        const GroupContext &g = m_groups_ctx[i];
        if (p_group_id < 0) {
            total += g.active_amount;
        } else if (g.group_id == (uint16_t)p_group_id) {
            return g.active_amount;
        }
    }
    return p_group_id < 0 ? total : 0;
}

void MyriasServer::exec_create_group(const MyriasCommand &p_cmd) {
    MYRIAS_PROFILE_ZONE("Myrias::exec_create_group");
    uint32_t offset = 0;
    if (!m_entities.allocate_slice(p_cmd.pool_size, offset)) {
        UtilityFunctions::push_error("MyriasServer: failed to resize SoA, group not created.");
        return;
    }

    GroupContext ctx;
    ctx.group_id = p_cmd.group_id;
    ctx.offset = offset;
    ctx.active_amount = 0;
    ctx.last_active = 0;
    ctx.pool_size = p_cmd.pool_size;
    ctx.alive_bits.resize((p_cmd.pool_size + 63) / 64);
    ctx.alive_bits.fill(0ull);
    ctx.radius = p_cmd.radius;
    ctx.collision_layer = p_cmd.collision_layer;
    ctx.collision_mask = p_cmd.collision_mask;
    ctx.traits = 0;
    ctx.default_traits = 0;
    ctx.uniform_traits = 0;
    ctx.ci = p_cmd.ci;
    ctx.frame_table = p_cmd.frame_table;
    ctx.texture = p_cmd.texture;

    init_group_render_resources(ctx);

    m_groups_ctx.push_back(ctx);
}

void MyriasServer::exec_remove_group(int p_group_id) {
    MYRIAS_PROFILE_ZONE("Myrias::exec_remove_group");
    if (p_group_id < 0 || p_group_id > 65535) {
        return;
    }
    const uint16_t gid = (uint16_t)p_group_id;
    int g_idx = -1;
    for (int i = 0; i < m_groups_ctx.size(); i++) {
        if (m_groups_ctx[i].group_id == gid) {
            g_idx = i;
            break;
        }
    }
    if (g_idx < 0) {
        return;
    }

    GroupContext &g = m_groups_ctx.ptrw()[g_idx];
    free_group_render_resources(g);

    m_entities.free_slice(g.offset, g.pool_size, m_groups_ctx, (uint16_t)g_idx);
    m_groups_ctx.remove_at(g_idx);
}

void MyriasServer::exec_spawn(const MyriasCommand &p_cmd) {
    GroupContext *g = find_group(p_cmd.group_id);
    if (g == nullptr) {
        return;
    }

    if (g->last_active >= g->pool_size) {
        if (g->active_amount < g->pool_size) {
            m_entities.compact_group(g->offset, g->alive_bits.ptrw(), g->buffer.ptrw(), g->trait_bits, g->active_amount, g->last_active);
            GROUP_CLEAR_DIRTY(g->traits);
        }
        if (g->last_active >= g->pool_size) {
            return;
        }
    }

    const uint32_t idx_in_group = g->last_active++;
    g->active_amount++;
    ALIVE_SET(g->alive_bits.ptrw(), idx_in_group);

    // Apply default trait assignment for any active per-entity bitsets
    for (int s = 0; s < (int)TRAIT_BEHAVIOR_COUNT; s++) {
        if (!g->trait_bits[s].is_empty()) {
            const uint32_t trait_flag = 1u << (TRAIT_BEHAVIOR_FIRST_BIT + s);
            if ((g->default_traits & trait_flag) != 0) {
                ALIVE_SET(g->trait_bits[s].ptrw(), idx_in_group);
            } else {
                ALIVE_CLEAR(g->trait_bits[s].ptrw(), idx_in_group);
            }
        }
    }

    const uint32_t abs_idx = g->offset + idx_in_group;

    m_entities.phys_x.ptrw()[abs_idx] = p_cmd.pos.x;
    m_entities.phys_y.ptrw()[abs_idx] = p_cmd.pos.y;
    m_entities.phys_vel_x.ptrw()[abs_idx] = p_cmd.vel.x;
    m_entities.phys_vel_y.ptrw()[abs_idx] = p_cmd.vel.y;
    m_entities.rot.ptrw()[abs_idx] = p_cmd.rot;
    m_entities.scale.ptrw()[abs_idx] = p_cmd.scale;
    m_entities.lifetime.ptrw()[abs_idx] = 0.0f;
    const uint16_t anim = (uint16_t)MAX(p_cmd.animation, 0);
    m_entities.cur_animation.ptrw()[abs_idx] = anim;
    m_entities.cur_frame.ptrw()[abs_idx] = 0;

    update_render_packet(*g, idx_in_group, anim, 0, p_cmd.scale, p_cmd.rot);
}

void MyriasServer::exec_despawn(int p_group_id, int p_local_index) {
    GroupContext *g = find_group(p_group_id);
    if (g == nullptr || p_local_index < 0 || (uint32_t)p_local_index >= g->last_active) {
        return;
    }
    kill_local(*g, (uint32_t)p_local_index);
    finalize_group_deaths(*g);
}

void MyriasServer::exec_configure_trait(const MyriasCommand &p_cmd) {
    if (p_cmd.trait_cfg == nullptr || !is_valid_single_behavior_trait(p_cmd.trait_flag)) {
        return;
    }
    GroupContext *g = find_group(p_cmd.group_id);
    if (g == nullptr) {
        return;
    }
    const uint32_t flag = p_cmd.trait_flag;
    g->traits |= flag;
    g->default_traits |= flag;

    const int s = __builtin_ctz(flag) - TRAIT_BEHAVIOR_FIRST_BIT;
    if (s >= 0 && s < (int)TRAIT_BEHAVIOR_COUNT) {
        if (g->trait_bits[s].is_empty()) {
            g->uniform_traits |= flag;
        }
        g->trait_configs[s] = p_cmd.trait_cfg;
    }
}

void MyriasServer::exec_remove_trait(const MyriasCommand &p_cmd) {
    if (!is_valid_single_behavior_trait(p_cmd.trait_flag)) {
        return;
    }
    GroupContext *g = find_group(p_cmd.group_id);
    if (g == nullptr) {
        return;
    }
    const uint32_t flag = p_cmd.trait_flag;
    g->traits &= ~flag;
    g->default_traits &= ~flag;
    g->uniform_traits &= ~flag;

    const int s = __builtin_ctz(flag) - TRAIT_BEHAVIOR_FIRST_BIT;
    if (s >= 0 && s < (int)TRAIT_BEHAVIOR_COUNT) {
        g->trait_bits[s].clear();
        g->trait_configs[s] = nullptr;
    }
}

void MyriasServer::exec_set_entity_trait(const MyriasCommand &p_cmd) {
    if (!is_valid_single_behavior_trait(p_cmd.trait_flag)) {
        return;
    }
    GroupContext *g = find_group(p_cmd.group_id);
    if (g == nullptr || p_cmd.local_index < 0 || (uint32_t)p_cmd.local_index >= g->last_active) {
        return;
    }
    const uint32_t flag = p_cmd.trait_flag;
    const int s = __builtin_ctz(flag) - TRAIT_BEHAVIOR_FIRST_BIT;
    if (s < 0 || s >= (int)TRAIT_BEHAVIOR_COUNT) {
        return;
    }

    if (g->trait_bits[s].is_empty()) {
        const uint32_t num_words = (g->pool_size + 63) / 64;
        g->trait_bits[s].resize(num_words);
        const uint64_t fill_val = ((g->uniform_traits & flag) != 0 || (g->default_traits & flag) != 0) ? ~0ull : 0ull;
        g->trait_bits[s].fill(fill_val);
        g->uniform_traits &= ~flag;
    }

    uint64_t *tbits = g->trait_bits[s].ptrw();
    if (p_cmd.trait_enabled) {
        ALIVE_SET(tbits, p_cmd.local_index);
        g->traits |= flag;
    } else {
        ALIVE_CLEAR(tbits, p_cmd.local_index);
    }
}
