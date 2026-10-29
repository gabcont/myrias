#include "myrias_server.h"

#include <godot_cpp/classes/atlas_texture.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

// MultiMesh 2D layout without color but with custom data: 8 (transform) + 4 (custom).
static constexpr int INSTANCE_STRIDE = 12;

// Custom data holds the frame UV rect (x, y, w, h), remapped here onto the quad UVs.
static const char *MYRIAS_SHADER_CODE = R"(
shader_type canvas_item;
void vertex() {
    UV = INSTANCE_CUSTOM.xy + UV * INSTANCE_CUSTOM.zw;
}
)";

MyriasServer *MyriasServer::s_singleton = nullptr;

MyriasServer::MyriasServer() {
    s_singleton = this;
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

void MyriasServer::connect_tick() {
    if (m_tick_connected) {
        return;
    }
    SceneTree *tree = Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop());
    if (tree != nullptr) {
        tree->connect("process_frame", Callable(this, "on_process_frame"));
        m_tick_connected = true;
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

void MyriasServer::exec_create_group(const MyriasCommand &p_cmd) {
    uint32_t offset = 0;
    if (!m_entities.allocate_slice(p_cmd.pool_size, offset)) {
        UtilityFunctions::push_error("MyriasServer: failed to resize SoA, group not created.");
        return;
    }

    GroupContext ctx;
    ctx.group_id = p_cmd.group_id;
    ctx.offset = offset;
    ctx.active_amount = 0;
    ctx.pool_size = p_cmd.pool_size;
    ctx.radius = p_cmd.radius;
    ctx.collision_layer = p_cmd.collision_layer;
    ctx.collision_mask = p_cmd.collision_mask;
    ctx.traits = p_cmd.traits & GROUP_FLAGS_USER_MASK;
    ctx.ci = p_cmd.ci;
    ctx.frame_table = p_cmd.frame_table;
    ctx.texture = p_cmd.texture;

    m_spatial_grid.ensure_capacity(m_entities.physics.size());

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
    if (g == nullptr || g->active_amount >= g->pool_size) {
        return;
    }
    const int idx = g->offset + g->active_amount++;

    PhysicsStream &p = m_entities.physics.ptrw()[idx];
    p.x = p_cmd.pos.x;
    p.y = p_cmd.pos.y;
    p.vel_x = p_cmd.vel.x;
    p.vel_y = p_cmd.vel.y;

    RenderingStream &r = m_entities.rendering.ptrw()[idx];
    r.rot = p_cmd.rot;
    r.scale = p_cmd.scale;
    r.z_index = 0;
    r.cur_animation = (uint16_t)MAX(p_cmd.animation, 0);
    r.cur_frame = 0;

    LogicStream &l = m_entities.logic.ptrw()[idx];
    l.spawn_seq = m_next_spawn_seq++;
    l.lifetime = 0.0f;
    l.alive = true;
}

void MyriasServer::exec_despawn(int p_group_id, int p_local_index) {
    GroupContext *g = find_group(p_group_id);
    if (g == nullptr || p_local_index < 0 || p_local_index >= g->active_amount) {
        return;
    }
    m_entities.logic.ptrw()[g->offset + p_local_index].alive = false;
    GROUP_SET_DIRTY(g->traits);
}

void MyriasServer::render_group(GroupContext &r_ctx) {
    if (r_ctx.frame_table.frames.is_empty() || r_ctx.frame_table.anims.is_empty()) {
        return;
    }

    const RenderingStream *rend = m_entities.rendering.ptr() + r_ctx.offset;
    const PhysicsStream *phys = m_entities.physics.ptr() + r_ctx.offset;
    float *out = r_ctx.buffer.ptrw();
    const int last_anim = r_ctx.frame_table.anims.size() - 1;

    for (int i = 0; i < r_ctx.active_amount; i++) {
        const RenderingStream &r = rend[i];
        const int anim = CLAMP((int)r.cur_animation, 0, last_anim);
        const AnimInfo &ai = r_ctx.frame_table.anims[anim];
        if (ai.count <= 0) {
            continue;
        }
        const FrameInfo &fi = r_ctx.frame_table.frames[ai.start + MIN((int)r.cur_frame, ai.count - 1)];

        const float c = Math::cos(r.rot) * r.scale;
        const float s = Math::sin(r.rot) * r.scale;
        float *o = out + (int64_t)i * INSTANCE_STRIDE;

        // Transform2D rows: (col0.x, col1.x, 0, origin.x), (col0.y, col1.y, 0, origin.y)
        o[0] = c * fi.size.x;
        o[1] = -s * fi.size.y;
        o[2] = 0.0f;
        o[3] = phys[i].x;
        o[4] = s * fi.size.x;
        o[5] = c * fi.size.y;
        o[6] = 0.0f;
        o[7] = phys[i].y;
        // Custom data: frame UV rect
        o[8] = fi.uv.position.x;
        o[9] = fi.uv.position.y;
        o[10] = fi.uv.size.x;
        o[11] = fi.uv.size.y;
    }

    RenderingServer *rs = RenderingServer::get_singleton();
    rs->multimesh_set_buffer(r_ctx.multimesh, r_ctx.buffer);
    rs->multimesh_set_visible_instances(r_ctx.multimesh, r_ctx.active_amount);
}

void MyriasServer::flush_commands() {
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

    for (int i = 0; i < batch.size(); i++) {
        const MyriasCommand &c = batch[i];
        if (doomed.has(c.group_id)) {
            continue;
        }
        switch (c.type) {
            case MyriasCommand::CREATE_GROUP:
                exec_create_group(c);
                break;
            case MyriasCommand::REMOVE_GROUP:
                exec_remove_group(c.group_id);
                break;
            case MyriasCommand::SPAWN:
                exec_spawn(c);
                break;
            case MyriasCommand::DESPAWN:
                exec_despawn(c.group_id, c.local_index);
                break;
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
        if (g == nullptr || ev.local_idx >= g->active_amount) {
            continue;
        }
        const int abs_idx = g->offset + ev.local_idx;
        switch (ev.type) {
            case MyriasPhysicsEvent::CHANGE_ANIMATION:
                rend_base[abs_idx].cur_animation = (uint16_t)MAX(ev.param, 0);
                rend_base[abs_idx].cur_frame = 0;
                logic_base[abs_idx].lifetime = 0.0f; // Reset animation playback time.
                break;
            case MyriasPhysicsEvent::DESPAWN:
                logic_base[abs_idx].alive = false;
                GROUP_SET_DIRTY(g->traits);
                break;
            case MyriasPhysicsEvent::COLLISION:
                break;
        }
    }
}

void MyriasServer::process_tick(double p_delta) {
    flush_physics_events();
    flush_commands();

    // 1. Compaction, Kinematics, and Animation Logic
    for (int i = 0; i < m_groups_ctx.size(); i++) {
        GroupContext &g = m_groups_ctx.ptrw()[i];

        if (GROUP_IS_DIRTY(g.traits)) {
            m_entities.compact_group(g.offset, g.active_amount);
            GROUP_CLEAR_DIRTY(g.traits);
        }

        // Kinematics Process
        PhysicsStream *phys = m_entities.physics.ptrw() + g.offset;
        for (int j = 0; j < g.active_amount; j++) {
            if (phys[j].x >= 1280 || phys[j].x <= 0) {
                phys[j].vel_x *= -1;
            }
            if (phys[j].y >= 720 || phys[j].y <= 0) {
                phys[j].vel_y *= -1;
            }
            phys[j].x += phys[j].vel_x * (float)p_delta;
            phys[j].y += phys[j].vel_y * (float)p_delta;
        }

        // Logic Process: age the entities and derive their animation frame from it.
        if (!g.frame_table.anims.is_empty()) {
            LogicStream *logic = m_entities.logic.ptrw() + g.offset;
            RenderingStream *rend = m_entities.rendering.ptrw() + g.offset;
            for (int j = 0; j < g.active_amount; j++) {
                logic[j].lifetime += (float)p_delta;
                rend[j].cur_frame = g.frame_table.compute_frame(rend[j].cur_animation, logic[j].lifetime);
            }
        }
    }

    // 2. Spatial Grid Build & Physics Resolution
    m_spatial_grid.build(m_entities, m_groups_ctx);
    // (When entity-to-entity / entity-to-native physics resolution is added,
    //  it will run on the grid proxies here and call m_spatial_grid.sync_to_soa(...))

    // 3. Render Groups
    for (int i = 0; i < m_groups_ctx.size(); i++) {
        GroupContext &g = m_groups_ctx.ptrw()[i];
        render_group(g);
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

void MyriasServer::_bind_methods() {
    ClassDB::bind_method(D_METHOD("create_group", "model", "pool_size", "canvas_item"), &MyriasServer::create_group, DEFVAL(RID()));
    ClassDB::bind_method(D_METHOD("remove_group", "group_id"), &MyriasServer::remove_group);
    ClassDB::bind_method(D_METHOD("refresh_group_canvas_item", "group_id", "canvas_item"), &MyriasServer::refresh_group_canvas_item);
    ClassDB::bind_method(D_METHOD("spawn", "group_id", "pos", "vel", "animation", "scale", "rot"),
            &MyriasServer::spawn, DEFVAL(0), DEFVAL(1.0f), DEFVAL(0.0f));
    ClassDB::bind_method(D_METHOD("despawn", "group_id", "local_index"), &MyriasServer::despawn);
    ClassDB::bind_method(D_METHOD("get_active_count", "group_id"), &MyriasServer::get_active_count, DEFVAL(-1));
    ClassDB::bind_method(D_METHOD("configure_spatial_grid", "bounds", "cell_size"), &MyriasServer::configure_spatial_grid, DEFVAL(32.0f));
    ClassDB::bind_method(D_METHOD("query_collision", "pos", "radius", "group_id", "collision_mask"), &MyriasServer::query_collision, DEFVAL(-1), DEFVAL(0xFFFF));
    ClassDB::bind_method(D_METHOD("emit_animation_change_event", "group_id", "local_index", "animation"), &MyriasServer::emit_animation_change_event);
    ClassDB::bind_method(D_METHOD("emit_despawn_event", "group_id", "local_index"), &MyriasServer::emit_despawn_event);
    ClassDB::bind_method(D_METHOD("on_process_frame"), &MyriasServer::on_process_frame);
}
