#include "myrias_server.h"

#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>

#include "../profiling/myrias_profiling.h"

using namespace godot;

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

void MyriasServer::init_group_render_resources(GroupContext &r_ctx) {
    load_mm_mesh_if_unloaded();
    RenderingServer *rs = RenderingServer::get_singleton();
    if (rs == nullptr) {
        return;
    }
    r_ctx.multimesh = rs->multimesh_create();
    rs->multimesh_set_mesh(r_ctx.multimesh, m_quad->get_rid());
    rs->multimesh_allocate_data(r_ctx.multimesh, r_ctx.pool_size, RenderingServer::MULTIMESH_TRANSFORM_2D, false, true);
    rs->multimesh_set_visible_instances(r_ctx.multimesh, 0);
    r_ctx.buffer.resize((int64_t)r_ctx.pool_size * INSTANCE_STRIDE);
    r_ctx.buffer.fill(0.0f);

    r_ctx.shader = rs->shader_create();
    rs->shader_set_code(r_ctx.shader, MYRIAS_SHADER_CODE);
    r_ctx.material = rs->material_create();
    rs->material_set_shader(r_ctx.material, r_ctx.shader);

    if (r_ctx.ci.is_valid()) {
        rs->canvas_item_set_material(r_ctx.ci, r_ctx.material);
        rs->canvas_item_add_multimesh(r_ctx.ci, r_ctx.multimesh, r_ctx.texture);
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

void MyriasServer::step_group_animation(GroupContext &r_group) {
    if (r_group.frame_table.anims.is_empty()) {
        return;
    }
    MYRIAS_PROFILE_ZONE("Myrias::animation");
    const float *lifetime = m_entities.lifetime.ptr() + r_group.offset;
    const uint16_t *cur_anim = m_entities.cur_animation.ptr() + r_group.offset;
    uint16_t *cur_frame = m_entities.cur_frame.ptrw() + r_group.offset;
    const float *scale = m_entities.scale.ptr() + r_group.offset;
    const float *rot = m_entities.rot.ptr() + r_group.offset;
    const uint64_t *bits = r_group.alive_bits.ptr();
    const uint32_t num_words = (r_group.last_active + 63) / 64;
    float *buf = r_group.buffer.ptrw();

    for (uint32_t w = 0; w < num_words; w++) {
        uint64_t word = bits[w];
        if (w == num_words - 1 && (r_group.last_active & 63) != 0) {
            word &= (1ull << (r_group.last_active & 63)) - 1ull;
        }
        while (word) {
            const uint32_t j = (w << 6) + __builtin_ctzll(word);
            word &= word - 1;

            const uint16_t new_frame = r_group.frame_table.compute_frame(cur_anim[j], lifetime[j]);
            if (new_frame != cur_frame[j]) {
                const int anim_idx = CLAMP((int)cur_anim[j], 0, r_group.frame_table.anims.size() - 1);
                const AnimInfo &ai = r_group.frame_table.anims[anim_idx];
                if (ai.count > 0 && !r_group.frame_table.frames.is_empty()) {
                    const FrameInfo &old_fi = r_group.frame_table.frames[ai.start + MIN((int)cur_frame[j], ai.count - 1)];
                    const FrameInfo &new_fi = r_group.frame_table.frames[ai.start + MIN((int)new_frame, ai.count - 1)];
                    cur_frame[j] = new_frame;
                    if (new_fi.size == old_fi.size) {
                        float *o = buf + (int64_t)j * INSTANCE_STRIDE;
                        o[8] = new_fi.uv.position.x;
                        o[9] = new_fi.uv.position.y;
                        o[10] = new_fi.uv.size.x;
                        o[11] = new_fi.uv.size.y;
                    } else {
                        update_render_packet(r_group, j, cur_anim[j], new_frame, scale[j], rot[j]);
                    }
                } else {
                    cur_frame[j] = new_frame;
                }
            }
        }
    }
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
