#include "myrias_server.h"

#include <godot_cpp/classes/atlas_texture.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
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
            if (!m_groups_ctx[i].released) {
                free_group_render_resources(m_groups_ctx.ptrw()[i]);
            }
        }
    }
    if (s_singleton == this) {
        s_singleton = nullptr;
    }
}

void MyriasServer::_bind_methods() {
    ClassDB::bind_method(D_METHOD("create_group", "model", "pool_size"), &MyriasServer::create_group_bind);
    ClassDB::bind_method(D_METHOD("spawn", "group_id", "pos", "vel", "animation", "scale", "rot"),
            &MyriasServer::spawn, DEFVAL(0), DEFVAL(1.0f), DEFVAL(0.0f));
    ClassDB::bind_method(D_METHOD("remove_group", "group_id"), &MyriasServer::remove_group);
    ClassDB::bind_method(D_METHOD("despawn", "group_id", "local_index"), &MyriasServer::despawn);
    ClassDB::bind_method(D_METHOD("get_active_count", "group_id"), &MyriasServer::get_active_count, DEFVAL(-1));
    ClassDB::bind_method(D_METHOD("on_process_frame"), &MyriasServer::on_process_frame);
}

GroupContext *MyriasServer::find_group(int p_group_id) {
    for (int i = 0; i < m_groups_ctx.size(); i++) {
        if (m_groups_ctx[i].group_id == p_group_id) {
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

void MyriasServer::ensure_render_resources() {
    if (m_quad.is_null()) {
        // Explicit quad (y-down, UV 0..1) so the image is not flipped in 2D.
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

void MyriasServer::build_frame_table(GroupContext &r_ctx) {
    if (r_ctx.frames.is_null()) {
        return;
    }
    const PackedStringArray names = r_ctx.frames->get_animation_names();
    for (int a = 0; a < names.size(); a++) {
        const StringName anim = names[a];

        AnimInfo ainfo;
        ainfo.start = r_ctx.frame_table.size();
        ainfo.count = r_ctx.frames->get_frame_count(anim);
        ainfo.fps = (float)r_ctx.frames->get_animation_speed(anim);
        ainfo.loop = r_ctx.frames->get_animation_loop(anim);

        float cumulative = 0.0f;
        for (int f = 0; f < ainfo.count; f++) {
            // Relative duration: 1.0 means one full frame at the animation speed.
            const float rel = r_ctx.frames->get_frame_duration(anim, f);
            cumulative += rel;
            if (!Math::is_equal_approx(rel, 1.0f)) {
                ainfo.uniform = false;
            }

            FrameInfo info;
            info.end_rel = cumulative;

            Ref<Texture2D> tex = r_ctx.frames->get_frame_texture(anim, f);
            if (tex.is_null()) {
                info.uv = Rect2(0, 0, 0, 0);
                info.size = Vector2();
                r_ctx.frame_table.push_back(info);
                continue;
            }

            Ref<Texture2D> base = tex;
            Rect2 region(Vector2(), tex->get_size());
            AtlasTexture *atlas = Object::cast_to<AtlasTexture>(tex.ptr());
            if (atlas != nullptr && atlas->get_atlas().is_valid()) {
                base = atlas->get_atlas();
                region = atlas->get_region();
            }

            if (!r_ctx.texture.is_valid()) {
                r_ctx.texture = base->get_rid();
            } else if (r_ctx.texture != base->get_rid()) {
                UtilityFunctions::push_warning("MyriasServer: group frames use more than one spritesheet; only the first is drawn correctly.");
            }

            const Vector2 base_size = base->get_size();
            info.uv = Rect2(region.position / base_size, region.size / base_size);
            info.size = region.size;
            r_ctx.frame_table.push_back(info);
        }
        ainfo.total_rel = cumulative;
        r_ctx.anims.push_back(ainfo);
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

    MyriasCommand cmd;
    cmd.type = MyriasCommand::CREATE_GROUP;
    cmd.group_id = m_next_group_id++;
    cmd.model = p_model;
    cmd.pool_size = p_pool_size;
    cmd.ci = p_canvas_item;
    m_commands.push_back(cmd);
    connect_tick();
    return cmd.group_id;
}

void MyriasServer::remove_group(int p_group_id) {
    MyriasCommand cmd;
    cmd.type = MyriasCommand::REMOVE_GROUP;
    cmd.group_id = p_group_id;
    m_commands.push_back(cmd);
}

void MyriasServer::spawn(int p_group_id, Vector2 p_pos, Vector2 p_vel, int p_animation, float p_scale, float p_rot) {
    MyriasCommand cmd;
    cmd.type = MyriasCommand::SPAWN;
    cmd.group_id = p_group_id;
    cmd.pos = p_pos;
    cmd.vel = p_vel;
    cmd.animation = p_animation;
    cmd.scale = p_scale;
    cmd.rot = p_rot;
    m_commands.push_back(cmd);
}

void MyriasServer::despawn(int p_group_id, int p_local_index) {
    MyriasCommand cmd;
    cmd.type = MyriasCommand::DESPAWN;
    cmd.group_id = p_group_id;
    cmd.local_index = p_local_index;
    m_commands.push_back(cmd);
}

void MyriasServer::exec_create_group(int p_group_id, const Ref<MyriasEntityModel> &p_model, int p_pool_size, RID p_canvas_item) {

    // New group's slice starts where the SoA currently ends.
    const int offset = m_entities.physics.size();
    const int new_size = offset + p_pool_size;

    if (m_entities.physics.resize(new_size) != OK ||
            m_entities.rendering.resize(new_size) != OK ||
            m_entities.logic.resize(new_size) != OK) {
        UtilityFunctions::push_error("MyriasServer: failed to resize SoA, group not created.");
        // Roll back so all streams stay the same length.
        m_entities.physics.resize(offset);
        m_entities.rendering.resize(offset);
        m_entities.logic.resize(offset);
        return;
    }

    GroupContext ctx;
    ctx.group_id = p_group_id;
    ctx.offset = offset;
    ctx.active_amount = 0;
    ctx.pool_size = p_pool_size;
    ctx.frames = p_model->get_sprite_frames();
    ctx.ci = p_canvas_item;
    build_frame_table(ctx);

    // Render setup: one MultiMesh per group, drawn on the group's CanvasItem.
    ensure_render_resources();
    RenderingServer *rs = RenderingServer::get_singleton();
    ctx.multimesh = rs->multimesh_create();
    rs->multimesh_set_mesh(ctx.multimesh, m_quad->get_rid());
    rs->multimesh_allocate_data(ctx.multimesh, p_pool_size, RenderingServer::MULTIMESH_TRANSFORM_2D, false, true);
    rs->multimesh_set_visible_instances(ctx.multimesh, 0);
    ctx.buffer.resize((int64_t)p_pool_size * INSTANCE_STRIDE);
    ctx.buffer.fill(0.0f);

    ctx.shader = rs->shader_create();
    rs->shader_set_code(ctx.shader, MYRIAS_SHADER_CODE);
    ctx.material = rs->material_create();
    rs->material_set_shader(ctx.material, ctx.shader);

    if (ctx.ci.is_valid()) {
        rs->canvas_item_set_material(ctx.ci, ctx.material);
        rs->canvas_item_add_multimesh(ctx.ci, ctx.multimesh, ctx.texture);
    }

    // Appending to the context list starts its processing.
    m_groups_ctx.push_back(ctx);
}

int MyriasServer::create_group_bind(const Ref<MyriasEntityModel> &p_model, int p_pool_size) {
    return create_group(p_model, p_pool_size, RID());
}

void MyriasServer::exec_remove_group(int p_group_id) {
    GroupContext *g = find_group(p_group_id);
    if (g == nullptr || g->released) {
        return;
    }
    // The owning node clears its own CanvasItem (it may already be freed here).
    free_group_render_resources(*g);
    g->ci = RID();
    g->active_amount = 0;
    g->released = true;
}

void MyriasServer::exec_spawn(const MyriasCommand &p_cmd) {
    GroupContext *g = find_group(p_cmd.group_id);
    if (g == nullptr || g->released || g->active_amount >= g->pool_size) {
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
    if (g == nullptr || g->released || p_local_index < 0 || p_local_index >= g->active_amount) {
        return;
    }
    m_entities.logic.ptrw()[g->offset + p_local_index].alive = false;
    g->dirty = true;
}

// Stable compaction: surviving entities keep their relative (spawn) order,
// so each group's slice stays sorted by spawn_seq.
void MyriasServer::compact(GroupContext &r_ctx) {
    PhysicsStream *phys = m_entities.physics.ptrw() + r_ctx.offset;
    RenderingStream *rend = m_entities.rendering.ptrw() + r_ctx.offset;
    LogicStream *logic = m_entities.logic.ptrw() + r_ctx.offset;

    int write = 0;
    for (int read = 0; read < r_ctx.active_amount; read++) {
        if (!logic[read].alive) {
            continue;
        }
        if (write != read) {
            phys[write] = phys[read];
            rend[write] = rend[read];
            logic[write] = logic[read];
        }
        write++;
    }
    r_ctx.active_amount = (uint16_t)write;
    r_ctx.dirty = false;
}

// Frame shown after p_lifetime seconds, from the animation's fps, loop flag and per-frame durations.
uint16_t MyriasServer::compute_frame(const GroupContext &p_ctx, const AnimInfo &p_anim, float p_lifetime) {
    if (p_anim.count <= 1 || p_anim.fps <= 0.0f || p_anim.total_rel <= 0.0f) {
        return 0;
    }
    float t = p_lifetime * p_anim.fps; // Relative units elapsed.
    if (p_anim.loop) {
        t = Math::fmod(t, p_anim.total_rel);
    } else if (t >= p_anim.total_rel) {
        return (uint16_t)(p_anim.count - 1); // Finished: hold the last frame.
    }

    if (p_anim.uniform) {
        return (uint16_t)MIN((int)t, p_anim.count - 1);
    }
    const FrameInfo *frames = p_ctx.frame_table.ptr() + p_anim.start;
    for (int i = 0; i < p_anim.count; i++) {
        if (t < frames[i].end_rel) {
            return (uint16_t)i;
        }
    }
    return (uint16_t)(p_anim.count - 1);
}

void MyriasServer::render(GroupContext &r_ctx) {
    if (r_ctx.frame_table.is_empty() || r_ctx.anims.is_empty()) {
        return;
    }
    const RenderingStream *rend = m_entities.rendering.ptr() + r_ctx.offset;
    const PhysicsStream *phys = m_entities.physics.ptr() + r_ctx.offset;
    float *out = r_ctx.buffer.ptrw();
    const int last_anim = r_ctx.anims.size() - 1;

    for (int i = 0; i < r_ctx.active_amount; i++) {
        const RenderingStream &r = rend[i];
        const int anim = MIN((int)r.cur_animation, last_anim);
        const AnimInfo &ai = r_ctx.anims[anim];
        if (ai.count <= 0) {
            continue;
        }
        const FrameInfo &fi = r_ctx.frame_table[ai.start + MIN((int)r.cur_frame, ai.count - 1)];

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
        // Custom data: frame UV rect.
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
    // Commands queued while flushing go to the next frame.
    const Vector<MyriasCommand> batch = m_commands;
    m_commands.clear();

    // Groups created and removed within the same batch are skipped entirely: their
    // CanvasItem may already be freed, and the net effect is nothing.
    HashSet<int> created;
    HashSet<int> doomed;
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
                exec_create_group(c.group_id, c.model, c.pool_size, c.ci);
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
        if (g.released) {
            continue;
        }
        if (p_group_id < 0) {
            total += g.active_amount;
        } else if (g.group_id == p_group_id) {
            return g.active_amount;
        }
    }
    return p_group_id < 0 ? total : 0;
}

void MyriasServer::on_process_frame() {
    // SceneTree.process_frame carries no delta, so measure it (scaled like Godot's own delta).
    const uint64_t now = Time::get_singleton()->get_ticks_usec();
    const double delta = m_last_process_usec == 0 ? 0.0 : (now - m_last_process_usec) / 1000000.0;
    m_last_process_usec = now;

    SceneTree *tree = Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop());
    if (tree != nullptr && tree->is_paused()) {
        return;
    }
    process_tick(delta * Engine::get_singleton()->get_time_scale());
}

void MyriasServer::process_tick(double p_delta) {
    flush_commands();

    for (int i = 0; i < m_groups_ctx.size(); i++) {
        GroupContext &g = m_groups_ctx.ptrw()[i];
        if (g.released) {
            continue;
        }

        if (g.dirty) {
            compact(g);
        }

        // Kinematics Process
        PhysicsStream *phys = m_entities.physics.ptrw() + g.offset;
        for (int j = 0; j < g.active_amount; j++) {
            if (phys[j].x >= 1280 or phys[j].x <= 0) {
                phys[j].vel_x *= -1;
            }
            if (phys[j].y >= 720 or phys[j].y <= 0) {
                phys[j].vel_y *= -1;
            }
            phys[j].x += phys[j].vel_x * (float)p_delta;
            phys[j].y += phys[j].vel_y * (float)p_delta;

        }

        // Logic Process: age the entities and derive their animation frame from it.
        if (!g.anims.is_empty()) {
            LogicStream *logic = m_entities.logic.ptrw() + g.offset;
            RenderingStream *rend = m_entities.rendering.ptrw() + g.offset;
            const int last_anim = g.anims.size() - 1;
            for (int j = 0; j < g.active_amount; j++) {
                logic[j].lifetime += (float)p_delta;
                const AnimInfo &ai = g.anims[MIN((int)rend[j].cur_animation, last_anim)];
                rend[j].cur_frame = compute_frame(g, ai, logic[j].lifetime);
            }
        }

        render(g);
    }
}
