#pragma once

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/sprite_frames.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/rect2.hpp>
#include <godot_cpp/variant/rid.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include "resources/myrias_entity_model.h"
#include "server/myrias_command.h"

using namespace godot;

struct PhysicsStream {
    float x, y;
    float vel_x, vel_y;
};

struct RenderingStream {
    float rot, scale;
    int z_index;
    uint16_t cur_animation, cur_frame;
};

struct LogicStream {
    uint64_t spawn_seq; // Global, monotonically increasing. Newer = drawn on top.
    float lifetime;     // Seconds since spawn. Drives the animation frame.
    bool alive;
};

struct EntitySoA {
    Vector<PhysicsStream> physics;
    Vector<RenderingStream> rendering;
    Vector<LogicStream> logic;
};

// One drawable frame: normalized UV rect inside the spritesheet + size in pixels.
struct FrameInfo {
    Rect2 uv;
    Vector2 size;
    float end_rel = 0.0f; // Cumulative end of this frame, in relative duration units (1 unit = 1 / fps).
};

// Playback data of one SpriteFrames animation.
struct AnimInfo {
    int start = 0;       // First index in frame_table.
    int count = 0;
    float fps = 0.0f;    // Animation speed (frames per second). 0 = static, shows frame 0.
    bool loop = false;
    bool uniform = true; // All frames last exactly 1 unit, so the frame is a plain division.
    float total_rel = 0.0f;
};

struct GroupContext {
    int group_id = -1;
    int offset = 0;
    uint16_t active_amount = 0;
    uint32_t pool_size = 0;
    bool released = false;
    bool dirty = false; // Some entity was killed, needs compaction.

    Ref<SpriteFrames> frames;
    RID ci;        // CanvasItem of the owning node.
    RID multimesh; // One MultiMesh per group, drawn on `ci`.
    RID texture;   // Spritesheet.
    RID shader;    // Per-group shader/material (remaps the frame UV from custom data).
    RID material;

    // Flattened frame table: frame_table[anims[anim].start + frame].
    Vector<FrameInfo> frame_table;
    Vector<AnimInfo> anims;

    PackedFloat32Array buffer; // MultiMesh buffer, reused every frame.
};

class MyriasServer : public Object {
    GDCLASS(MyriasServer, Object);

private:
    static MyriasServer *s_singleton;

    EntitySoA m_entities;
    Vector<GroupContext> m_groups_ctx;
    int m_next_group_id = 0;
    uint64_t m_next_spawn_seq = 0;

    Ref<ArrayMesh> m_quad;
    bool m_tick_connected = false;
    uint64_t m_last_process_usec = 0;
    Vector<MyriasCommand> m_commands;

    GroupContext *find_group(int p_group_id);
    void ensure_render_resources();
    void free_group_render_resources(GroupContext &r_ctx);
    void connect_tick();
    void build_frame_table(GroupContext &r_ctx);

    void exec_create_group(int p_group_id, const Ref<MyriasEntityModel> &p_model, int p_pool_size, RID p_canvas_item);
    void exec_remove_group(int p_group_id);
    void exec_spawn(const MyriasCommand &p_cmd);
    void exec_despawn(int p_group_id, int p_local_index);
    void flush_commands();
    void on_process_frame();

    void compact(GroupContext &r_ctx);
    static uint16_t compute_frame(const GroupContext &p_ctx, const AnimInfo &p_anim, float p_lifetime);
    void render(GroupContext &r_ctx);

protected:
    static void _bind_methods();

public:
    static MyriasServer *get_singleton() { return s_singleton; }

    MyriasServer();
    ~MyriasServer();

    // All the calls below only enqueue a command. The queue is executed in order at the
    // start of the next process(), before compaction and rendering.

    // Reserves a group id right away and queues the group creation (SoA growth, offset,
    // MultiMesh on p_canvas_item). Returns the id, or -1 if the arguments are invalid.
    int create_group(const Ref<MyriasEntityModel> &p_model, int p_pool_size, RID p_canvas_item = RID());
    // Queues the group removal: stops processing it and frees its render resources.
    // The SoA slice stays reserved.
    void remove_group(int p_group_id);

    // Queues an entity appended at the end of the group's active range (keeps spawn order).
    // Dropped if the group doesn't exist or its pool is full when executed.
    void spawn(int p_group_id, Vector2 p_pos, Vector2 p_vel, int p_animation = 0, float p_scale = 1.0f, float p_rot = 0.0f);
    // Queues an entity removal by local index, as seen after the last processed frame
    // (indices only shift during compaction, which runs after the queue).
    void despawn(int p_group_id, int p_local_index);

    // Script-facing variant (no CanvasItem attached).
    int create_group_bind(const Ref<MyriasEntityModel> &p_model, int p_pool_size);

    // Active entities in the given group, or in all groups if p_group_id is -1 (default).
    // Unknown or removed groups count 0. Reflects the last processed frame (queued commands not applied yet).
    int get_active_count(int p_group_id = -1) const;

    // Per render frame (SceneTree.process_frame): commands, compaction, simple kinematics,
    // logic and render buffers.
    void process_tick(double p_delta);
};
