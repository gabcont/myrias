#include "myrias_server.h"

#include "resources/myrias_trait.h"
#include "simd/myrias_kinematics.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "../profiling/myrias_profiling.h"

using namespace godot;

MyriasServer *MyriasServer::s_singleton = nullptr;

MyriasServer::MyriasServer() {
    s_singleton = this;
    UtilityFunctions::print("[Myrias] Google Highway SIMD active. Dispatched target: ", myrias::get_hwy_target_name());
}

MyriasServer::~MyriasServer() {
    RenderingServer *rs = RenderingServer::get_singleton();
    if (rs != nullptr) {
        for (int i = 0; i < m_groups_ctx.size(); i++) {
            free_group_render_resources(m_groups_ctx.ptrw()[i]);
        }
    }
    if (s_singleton == this) {
        s_singleton = nullptr;
    }
}


String MyriasServer::get_simd_target_name() const {
    return String(myrias::get_hwy_target_name());
}

static inline bool is_valid_single_behavior_trait(uint32_t p_flag) {
    return p_flag != 0 && (p_flag & (p_flag - 1)) == 0 && (p_flag & TRAIT_BEHAVIOR_MASK) == p_flag;
}

static inline void kill_local(GroupContext &g, uint32_t p_local_idx) {
    uint64_t *bits = g.alive_bits.ptrw();
    if (ALIVE_TEST(bits, p_local_idx)) {
        ALIVE_CLEAR(bits, p_local_idx);
        g.active_amount--;
        float *o = g.buffer.ptrw() + (int64_t)p_local_idx * INSTANCE_STRIDE;
        o[0] = 0.0f;
        o[1] = 0.0f;
        o[4] = 0.0f;
        o[5] = 0.0f;
    }
}

static inline void tail_trim_bitset(GroupContext &g) {
    if (g.last_active == 0 || g.active_amount == 0) {
        g.last_active = 0;
        return;
    }
    const uint64_t *bits = g.alive_bits.ptr();
    int32_t w = (int32_t)((g.last_active - 1) >> 6);
    const uint32_t rem = g.last_active & 63;
    uint64_t word = bits[w];
    if (rem != 0) {
        word &= (1ull << rem) - 1ull;
    }
    while (word == 0) {
        w--;
        if (w < 0) {
            g.last_active = 0;
            return;
        }
        word = bits[w];
    }
    const uint32_t highest_bit = 63 - __builtin_clzll(word);
    g.last_active = (uint16_t)((w << 6) + highest_bit + 1);
}

void MyriasServer::finalize_group_deaths(GroupContext &g) {
    tail_trim_bitset(g);
    if (g.last_active > 0 && (g.last_active - g.active_amount) * 2 > g.last_active) {
        GROUP_SET_DIRTY(g.traits);
    }
}

GroupContext *MyriasServer::find_group(int p_group_id) {
    if (p_group_id < 0 || p_group_id > 65535) {
        return nullptr;
    }
    const uint16_t gid = (uint16_t)p_group_id;
    for (int i = 0; i < m_groups_ctx.size(); i++) {
        if (m_groups_ctx[i].group_id == gid) {
            return &m_groups_ctx.ptrw()[i];
        }
    }
    return nullptr;
}

void MyriasServer::free_group_render_resources(GroupContext &r_ctx) {
    RenderingServer *rs = RenderingServer::get_singleton();
    if (rs == nullptr) {
        return;
    }
    for (RID *rid : { &r_ctx.multimesh, &r_ctx.material, &r_ctx.shader }) {
        if (rid->is_valid()) {
            rs->free_rid(*rid);
        }
        *rid = RID();
    }
}

void MyriasServer::load_mm_mesh_if_unloaded() {
    if (m_quad.is_null()) {
        PackedVector2Array verts;
        verts.push_back(Vector2(-0.5f, -0.5f));
        verts.push_back(Vector2(0.5f, -0.5f));
        verts.push_back(Vector2(0.5f, 0.5f));
        verts.push_back(Vector2(-0.5f, 0.5f));
        PackedVector2Array uvs;
        uvs.push_back(Vector2(0, 0));
        uvs.push_back(Vector2(1, 0));
        uvs.push_back(Vector2(1, 1));
        uvs.push_back(Vector2(0, 1));
        PackedInt32Array idx;
        for (int i : { 0, 1, 2, 0, 2, 3 }) {
            idx.push_back(i);
        }

        Array arrays;
        arrays.resize(Mesh::ARRAY_MAX);
        arrays[Mesh::ARRAY_VERTEX] = verts;
        arrays[Mesh::ARRAY_TEX_UV] = uvs;
        arrays[Mesh::ARRAY_INDEX] = idx;

        m_quad.instantiate();
        m_quad->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
    }
}

void MyriasServer::refresh_group_canvas_item(int p_group_id, RID p_canvas_item) {
    GroupContext *g = find_group(p_group_id);
    if (g == nullptr) {
        return;
    }
    RenderingServer *rs = RenderingServer::get_singleton();
    if (rs == nullptr) {
        return;
    }

    g->ci = p_canvas_item;
    if (g->ci.is_valid()) {
        if (g->material.is_valid()) {
            rs->canvas_item_set_material(g->ci, g->material);
        }
        if (g->multimesh.is_valid()) {
            rs->canvas_item_add_multimesh(g->ci, g->multimesh, g->texture);
        }
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
    cmd.traits = p_model->get_traits() & GROUP_FLAGS_USER_MASK;
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
    return m_entities.rendering[abs_idx].scale;
}

float MyriasServer::get_entity_rotation(int p_group_id, int p_local_index) const {
    const GroupContext *g = const_cast<MyriasServer *>(this)->find_group(p_group_id);
    if (g == nullptr || p_local_index < 0 || (uint32_t)p_local_index >= g->last_active) {
        return 0.0f;
    }
    const uint32_t abs_idx = g->offset + p_local_index;
    return m_entities.rendering[abs_idx].rot;
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
    ctx.traits = p_cmd.traits & GROUP_FLAGS_USER_MASK;
    ctx.default_traits = p_cmd.traits & GROUP_FLAGS_USER_MASK;
    ctx.uniform_traits = p_cmd.traits & GROUP_FLAGS_USER_MASK;
    ctx.ci = p_cmd.ci;
    ctx.frame_table = p_cmd.frame_table;
    ctx.texture = p_cmd.texture;

    m_spatial_grid.ensure_capacity(m_entities.phys_x.size());

    load_mm_mesh_if_unloaded();
    RenderingServer *rs = RenderingServer::get_singleton();
    ctx.multimesh = rs->multimesh_create();
    rs->multimesh_set_mesh(ctx.multimesh, m_quad->get_rid());
    rs->multimesh_allocate_data(ctx.multimesh, p_cmd.pool_size, RenderingServer::MULTIMESH_TRANSFORM_2D, false, true);
    rs->multimesh_set_visible_instances(ctx.multimesh, 0);
    ctx.buffer.resize((int64_t)p_cmd.pool_size * INSTANCE_STRIDE);
    ctx.buffer.fill(0.0f);

    ctx.shader = rs->shader_create();
    rs->shader_set_code(ctx.shader, MYRIAS_SHADER_CODE);
    ctx.material = rs->material_create();
    rs->material_set_shader(ctx.material, ctx.shader);

    if (ctx.ci.is_valid()) {
        rs->canvas_item_set_material(ctx.ci, ctx.material);
        rs->canvas_item_add_multimesh(ctx.ci, ctx.multimesh, ctx.texture);
    }

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

    RenderingStream &r = m_entities.rendering.ptrw()[abs_idx];
    r.rot = p_cmd.rot;
    r.scale = p_cmd.scale;
    r.z_index = 0;
    r.cur_animation = (uint16_t)MAX(p_cmd.animation, 0);
    r.cur_frame = 0;

    LogicStream &l = m_entities.logic.ptrw()[abs_idx];
    l.spawn_seq = m_next_spawn_seq++;
    l.lifetime = 0.0f;

    update_render_packet(*g, idx_in_group, r.cur_animation, r.cur_frame, r.scale, r.rot);
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

void MyriasServer::exec_despawn(int p_group_id, int p_local_index) {
    GroupContext *g = find_group(p_group_id);
    if (g == nullptr || p_local_index < 0 || (uint32_t)p_local_index >= g->last_active) {
        return;
    }
    kill_local(*g, (uint32_t)p_local_index);
    finalize_group_deaths(*g);
}


void MyriasServer::update_render_packet(GroupContext &g, uint32_t idx, uint16_t anim_idx, uint16_t frame_idx, float scale, float rot) {
    if (g.frame_table.anims.is_empty() || g.frame_table.frames.is_empty()) {
        return;
    }
    const int last_anim = g.frame_table.anims.size() - 1;
    const int anim = CLAMP((int)anim_idx, 0, last_anim);
    const AnimInfo &ai = g.frame_table.anims[anim];
    if (ai.count <= 0) {
        return;
    }
    const FrameInfo &fi = g.frame_table.frames[ai.start + MIN((int)frame_idx, ai.count - 1)];

    const float c = Math::cos(rot) * scale;
    const float s = Math::sin(rot) * scale;

    float *o = g.buffer.ptrw() + (int64_t)idx * INSTANCE_STRIDE;
    o[0] = c * fi.size.x;
    o[1] = -s * fi.size.y;
    o[2] = 0.0f;
    o[4] = s * fi.size.x;
    o[5] = c * fi.size.y;
    o[6] = 0.0f;
    o[8] = fi.uv.position.x;
    o[9] = fi.uv.position.y;
    o[10] = fi.uv.size.x;
    o[11] = fi.uv.size.y;
}

void MyriasServer::render_group(int p_group_idx) {
    GroupContext &r_ctx = m_groups_ctx.ptrw()[p_group_idx];
    if (r_ctx.frame_table.frames.is_empty() || r_ctx.frame_table.anims.is_empty() || r_ctx.active_amount == 0 || !r_ctx.multimesh.is_valid()) {
        if (r_ctx.multimesh.is_valid()) {
            RenderingServer::get_singleton()->multimesh_set_visible_instances(r_ctx.multimesh, 0);
        }
        return;
    }

    const float *phys_x = m_entities.phys_x.ptr() + r_ctx.offset;
    const float *phys_y = m_entities.phys_y.ptr() + r_ctx.offset;
    float *out = r_ctx.buffer.ptrw();
    const uint32_t n = r_ctx.last_active;

    for (uint32_t j = 0; j < n; j++) {
        out[j * INSTANCE_STRIDE + 3] = phys_x[j];
        out[j * INSTANCE_STRIDE + 7] = phys_y[j];
    }

    MYRIAS_PROFILE_ZONE("Myrias::multimesh_buffer_setting");
    RenderingServer *rs = RenderingServer::get_singleton();
    rs->multimesh_set_buffer(r_ctx.multimesh, r_ctx.buffer);
    rs->multimesh_set_visible_instances(r_ctx.multimesh, (int32_t)n);
}

void MyriasServer::flush_commands() {
    MYRIAS_PROFILE_ZONE("Myrias::flush_commands");
    if (m_commands.is_empty()) {
        return;
    }
    const Vector<MyriasCommand> batch = m_commands;
    m_commands.clear();

    HashSet<uint16_t> created;
    HashSet<uint16_t> doomed;
    for (int i = 0; i < batch.size(); i++) {
        const MyriasCommand &c = batch[i];
        if (c.type == MyriasCommand::CREATE_GROUP) {
            created.insert(c.group_id);
        } else if (c.type == MyriasCommand::REMOVE_GROUP && created.has(c.group_id)) {
            doomed.insert(c.group_id);
        }
    }

    // Phase 1: CREATE_GROUP
    for (int i = 0; i < batch.size(); i++) {
        const MyriasCommand &c = batch[i];
        if (!doomed.has(c.group_id) && c.type == MyriasCommand::CREATE_GROUP) {
            exec_create_group(c);
        }
    }

    // Phase 1.5: CONFIGURE_TRAIT / REMOVE_TRAIT
    for (int i = 0; i < batch.size(); i++) {
        const MyriasCommand &c = batch[i];
        if (!doomed.has(c.group_id)) {
            if (c.type == MyriasCommand::CONFIGURE_TRAIT) {
                exec_configure_trait(c);
            } else if (c.type == MyriasCommand::REMOVE_TRAIT) {
                exec_remove_trait(c);
            }
        }
    }

    // Phase 2: DESPAWN
    for (int i = 0; i < batch.size(); i++) {
        const MyriasCommand &c = batch[i];
        if (!doomed.has(c.group_id) && c.type == MyriasCommand::DESPAWN) {
            exec_despawn(c.group_id, c.local_index);
        }
    }

    // Phase 3: SPAWN
    for (int i = 0; i < batch.size(); i++) {
        const MyriasCommand &c = batch[i];
        if (!doomed.has(c.group_id) && c.type == MyriasCommand::SPAWN) {
            exec_spawn(c);
        }
    }

    // Phase 3.5: SET_ENTITY_TRAIT
    for (int i = 0; i < batch.size(); i++) {
        const MyriasCommand &c = batch[i];
        if (!doomed.has(c.group_id) && c.type == MyriasCommand::SET_ENTITY_TRAIT) {
            exec_set_entity_trait(c);
        }
    }

    // Phase 4: REMOVE_GROUP
    for (int i = 0; i < batch.size(); i++) {
        const MyriasCommand &c = batch[i];
        if (!doomed.has(c.group_id) && c.type == MyriasCommand::REMOVE_GROUP) {
            exec_remove_group(c.group_id);
        }
    }
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

void MyriasServer::on_physics_frame() {
    SceneTree *tree = Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop());
    if (tree != nullptr && tree->is_paused()) {
        return;
    }
    Engine *engine = Engine::get_singleton();
    const int tps = MAX(engine->get_physics_ticks_per_second(), 1);
    const double physics_delta = (1.0 / (double)tps) * engine->get_time_scale();
    physics_tick(physics_delta);
}

void MyriasServer::on_process_frame() {
    const uint64_t now = Time::get_singleton()->get_ticks_usec();
    const double delta = m_last_process_usec == 0 ? 0.0 : (now - m_last_process_usec) / 1000000.0;
    m_last_process_usec = now;

    SceneTree *tree = Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop());
    if (tree != nullptr && tree->is_paused()) {
        return;
    }
    process_tick(delta * Engine::get_singleton()->get_time_scale());
}

void MyriasServer::configure_spatial_grid(Rect2 p_bounds, float p_cell_size) {
    m_spatial_grid.configure(p_bounds, p_cell_size);
}

bool MyriasServer::query_collision(Vector2 p_pos, float p_radius, int p_group_id, uint16_t p_collision_mask) const {
    return m_spatial_grid.query_collision(p_pos, p_radius, p_group_id, p_collision_mask);
}

void MyriasServer::push_physics_event(const MyriasPhysicsEvent &p_event) {
    m_physics_events.push_back(p_event);
}

void MyriasServer::emit_animation_change_event(int p_group_id, int p_local_index, int p_animation) {
    MyriasPhysicsEvent ev;
    ev.type = MyriasPhysicsEvent::CHANGE_ANIMATION;
    ev.group_id = (uint16_t)p_group_id;
    ev.local_idx = (uint16_t)p_local_index;
    ev.param = p_animation;
    m_physics_events.push_back(ev);
}

void MyriasServer::emit_despawn_event(int p_group_id, int p_local_index) {
    MyriasPhysicsEvent ev;
    ev.type = MyriasPhysicsEvent::DESPAWN;
    ev.group_id = (uint16_t)p_group_id;
    ev.local_idx = (uint16_t)p_local_index;
    m_physics_events.push_back(ev);
}

void MyriasServer::flush_physics_events() {
    MYRIAS_PROFILE_ZONE("Myrias::flush_physics_events");
    if (m_physics_events.is_empty()) {
        return;
    }
    const Vector<MyriasPhysicsEvent> events = m_physics_events;
    m_physics_events.clear();

    RenderingStream *rend_base = m_entities.rendering.ptrw();
    LogicStream *logic_base = m_entities.logic.ptrw();

    for (int i = 0; i < events.size(); i++) {
        const MyriasPhysicsEvent &ev = events[i];
        GroupContext *g = find_group(ev.group_id);
        if (g == nullptr || (uint32_t)ev.local_idx >= g->last_active) {
            continue;
        }
        const uint32_t abs_idx = g->offset + ev.local_idx;
        switch (ev.type) {
            case MyriasPhysicsEvent::CHANGE_ANIMATION:
                if (ALIVE_TEST(g->alive_bits.ptr(), ev.local_idx)) {
                    RenderingStream &r = rend_base[abs_idx];
                    r.cur_animation = (uint16_t)MAX(ev.param, 0);
                    r.cur_frame = 0;
                    logic_base[abs_idx].lifetime = 0.0f; // Reset animation playback time.
                    update_render_packet(*g, ev.local_idx, r.cur_animation, r.cur_frame, r.scale, r.rot);
                }
                break;
            case MyriasPhysicsEvent::DESPAWN:
                kill_local(*g, ev.local_idx);
                finalize_group_deaths(*g);
                break;
            case MyriasPhysicsEvent::COLLISION:
                break;
        }
    }
}

void MyriasServer::connect_tick() {
    if (m_tick_connected) {
        return;
    }
    SceneTree *tree = Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop());
    if (tree != nullptr) {
        tree->connect("physics_frame", Callable(this, "on_physics_frame"));
        tree->connect("process_frame", Callable(this, "on_process_frame"));
        m_tick_connected = true;
    }
}

void MyriasServer::physics_tick(double p_physics_delta) {
    MYRIAS_PROFILE_ZONE("Myrias::physics_tick");

    flush_physics_events();
    flush_commands();

    for (int i = 0; i < m_groups_ctx.size(); i++) {
        GroupContext &g = m_groups_ctx.ptrw()[i];

        if (GROUP_IS_DIRTY(g.traits)) {
            m_entities.compact_group(g.offset, g.alive_bits.ptrw(), g.buffer.ptrw(), g.trait_bits, g.active_amount, g.last_active);
            GROUP_CLEAR_DIRTY(g.traits);
        }

        const uint32_t n = g.last_active;

        // Bounds Process: bounce on X, kill on Y
        {
            MYRIAS_PROFILE_ZONE("Myrias::bounds");
            const float *px = m_entities.phys_x.ptr() + g.offset;
            const float *py = m_entities.phys_y.ptr() + g.offset;
            float *pvx = m_entities.phys_vel_x.ptrw() + g.offset;
            for (uint32_t j = 0; j < n; j++) {
                if (px[j] <= 0.0f || px[j] >= 1280.0f) {
                    pvx[j] = -pvx[j];
                }
                if (py[j] <= 0.0f || py[j] >= 720.0f) {
                    kill_local(g, j);
                }
            }
            finalize_group_deaths(g);
        }

        // Advance entity lifetimes before traits and animation
        {
            const uint64_t *bits = g.alive_bits.ptr();
            const uint32_t num_words = (g.last_active + 63) / 64;
            LogicStream *logic = m_entities.logic.ptrw() + g.offset;
            const float dt = (float)p_physics_delta;
            for (uint32_t w = 0; w < num_words; w++) {
                uint64_t word = bits[w];
                if (w == num_words - 1 && (g.last_active & 63) != 0) {
                    word &= (1ull << (g.last_active & 63)) - 1ull;
                }
                while (word) {
                    const uint32_t j = (w << 6) + __builtin_ctzll(word);
                    word &= word - 1;
                    logic[j].lifetime += dt;
                }
            }
        }

        // Behavior & Visual Traits Process
        if ((g.traits & TRAIT_BEHAVIOR_MASK) != 0) {
            MYRIAS_PROFILE_ZONE("Myrias::traits");
            myrias::run_group_traits(g, m_entities, (float)p_physics_delta);
        }
    }

    // Spatial Grid Build & Physics Resolution
    m_spatial_grid.build(m_entities, m_groups_ctx);
}

void MyriasServer::process_tick(double p_delta) {
    MYRIAS_PROFILE_ZONE("Myrias::process_tick");

    flush_physics_events();
    flush_commands();

    // 1. Compaction, Kinematics, and Animation Logic
    int total_active = 0;
    for (int i = 0; i < m_groups_ctx.size(); i++) {
        GroupContext &g = m_groups_ctx.ptrw()[i];

        if (GROUP_IS_DIRTY(g.traits)) {
            m_entities.compact_group(g.offset, g.alive_bits.ptrw(), g.buffer.ptrw(), g.trait_bits, g.active_amount, g.last_active);
            GROUP_CLEAR_DIRTY(g.traits);
        }
        total_active += g.active_amount;

        const uint32_t n = g.last_active;

        // Kinematics Process: pure integration, branchless, SIMD-ready
        {
            MYRIAS_PROFILE_ZONE("Myrias::kinematics");
            const float dt = (float)p_delta;
            myrias::run_kinematics(
                m_entities.phys_x.ptrw() + g.offset,
                m_entities.phys_y.ptrw() + g.offset,
                m_entities.phys_vel_x.ptr() + g.offset,
                m_entities.phys_vel_y.ptr() + g.offset,
                n,
                dt
            );
        }

        // Animation Process: derive animation frame from aged lifetime
        if (!g.frame_table.anims.is_empty()) {
            MYRIAS_PROFILE_ZONE("Myrias::animation");
            const LogicStream *logic = m_entities.logic.ptr() + g.offset;
            RenderingStream *rend = m_entities.rendering.ptrw() + g.offset;
            const uint64_t *bits = g.alive_bits.ptr();
            const uint32_t num_words = (g.last_active + 63) / 64;
            float *buf = g.buffer.ptrw();

            for (uint32_t w = 0; w < num_words; w++) {
                uint64_t word = bits[w];
                if (w == num_words - 1 && (g.last_active & 63) != 0) {
                    word &= (1ull << (g.last_active & 63)) - 1ull;
                }
                while (word) {
                    const uint32_t j = (w << 6) + __builtin_ctzll(word);
                    word &= word - 1;

                    const uint16_t new_frame = g.frame_table.compute_frame(rend[j].cur_animation, logic[j].lifetime);
                    if (new_frame != rend[j].cur_frame) {
                        const int anim_idx = CLAMP((int)rend[j].cur_animation, 0, g.frame_table.anims.size() - 1);
                        const AnimInfo &ai = g.frame_table.anims[anim_idx];
                        if (ai.count > 0 && !g.frame_table.frames.is_empty()) {
                            const FrameInfo &old_fi = g.frame_table.frames[ai.start + MIN((int)rend[j].cur_frame, ai.count - 1)];
                            const FrameInfo &new_fi = g.frame_table.frames[ai.start + MIN((int)new_frame, ai.count - 1)];
                            rend[j].cur_frame = new_frame;
                            if (new_fi.size == old_fi.size) {
                                float *o = buf + (int64_t)j * INSTANCE_STRIDE;
                                o[8] = new_fi.uv.position.x;
                                o[9] = new_fi.uv.position.y;
                                o[10] = new_fi.uv.size.x;
                                o[11] = new_fi.uv.size.y;
                            } else {
                                update_render_packet(g, j, rend[j].cur_animation, new_frame, rend[j].scale, rend[j].rot);
                            }
                        } else {
                            rend[j].cur_frame = new_frame;
                        }
                    }
                }
            }
        }
    }

    // 2. Render Groups
    {
        MYRIAS_PROFILE_ZONE("Myrias::render_group");
        for (int i = 0; i < m_groups_ctx.size(); i++) {
            render_group(i);
        }
    }

    MYRIAS_PROFILE_PLOT("Active Entities", total_active);
    MYRIAS_PROFILE_FRAME();
}

void MyriasServer::_bind_methods() {
    ClassDB::bind_method(D_METHOD("create_group", "model", "pool_size", "canvas_item"), &MyriasServer::create_group, DEFVAL(RID()));
    ClassDB::bind_method(D_METHOD("remove_group", "group_id"), &MyriasServer::remove_group);
    ClassDB::bind_method(D_METHOD("refresh_group_canvas_item", "group_id", "canvas_item"), &MyriasServer::refresh_group_canvas_item);
    ClassDB::bind_method(D_METHOD("spawn", "group_id", "pos", "vel", "animation", "scale", "rot"),
            &MyriasServer::spawn, DEFVAL(0), DEFVAL(1.0f), DEFVAL(0.0f));
    ClassDB::bind_method(D_METHOD("despawn", "group_id", "local_index"), &MyriasServer::despawn);

    ClassDB::bind_method(D_METHOD("configure_trait", "group_id", "trait", "owner"),
            &MyriasServer::configure_trait_resource, DEFVAL(nullptr));
    ClassDB::bind_method(D_METHOD("remove_group_trait", "group_id", "trait_flag"), &MyriasServer::remove_group_trait);
    ClassDB::bind_method(D_METHOD("set_entity_trait", "group_id", "local_index", "trait_flag", "enabled"), &MyriasServer::set_entity_trait);
    ClassDB::bind_method(D_METHOD("has_entity_trait", "group_id", "local_index", "trait_flag"), &MyriasServer::has_entity_trait);
    ClassDB::bind_method(D_METHOD("get_group_traits", "group_id"), &MyriasServer::get_group_traits);

    ClassDB::bind_method(D_METHOD("get_entity_position", "group_id", "local_index"), &MyriasServer::get_entity_position);
    ClassDB::bind_method(D_METHOD("get_entity_velocity", "group_id", "local_index"), &MyriasServer::get_entity_velocity);
    ClassDB::bind_method(D_METHOD("get_entity_scale", "group_id", "local_index"), &MyriasServer::get_entity_scale);
    ClassDB::bind_method(D_METHOD("get_entity_rotation", "group_id", "local_index"), &MyriasServer::get_entity_rotation);

    ClassDB::bind_method(D_METHOD("get_active_count", "group_id"), &MyriasServer::get_active_count, DEFVAL(-1));
    ClassDB::bind_method(D_METHOD("get_simd_target_name"), &MyriasServer::get_simd_target_name);
    ClassDB::bind_method(D_METHOD("configure_spatial_grid", "bounds", "cell_size"), &MyriasServer::configure_spatial_grid, DEFVAL(32.0f));
    ClassDB::bind_method(D_METHOD("query_collision", "pos", "radius", "group_id", "collision_mask"), &MyriasServer::query_collision, DEFVAL(-1), DEFVAL(0xFFFF));
    ClassDB::bind_method(D_METHOD("emit_animation_change_event", "group_id", "local_index", "animation"), &MyriasServer::emit_animation_change_event);
    ClassDB::bind_method(D_METHOD("emit_despawn_event", "group_id", "local_index"), &MyriasServer::emit_despawn_event);
    ClassDB::bind_method(D_METHOD("on_physics_frame"), &MyriasServer::on_physics_frame);
    ClassDB::bind_method(D_METHOD("on_process_frame"), &MyriasServer::on_process_frame);

    BIND_CONSTANT(TRAIT_FOLLOW_STEERING);
    BIND_CONSTANT(TRAIT_FOLLOW_ANGULAR);
    BIND_CONSTANT(TRAIT_SINE_WAVE);
    BIND_CONSTANT(TRAIT_CURVE_MODIFIER);
    BIND_CONSTANT(TRAIT_FACE_VELOCITY);
    BIND_CONSTANT(TRAIT_FACE_NODE);

    BIND_CONSTANT(CURVE_TARGET_SCALE);
    BIND_CONSTANT(CURVE_TARGET_ROTATION);
    BIND_CONSTANT(CURVE_TARGET_SPEED);
}
