#pragma once

#include <cstdint>
#include <godot_cpp/classes/sprite_frames.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/rid.hpp>
#include "structs/myrias_sprite_table.h"

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


struct GroupContext {
    int group_id = -1;
    int offset = 0;
    uint16_t active_amount = 0;
    uint32_t pool_size = 0;
    bool released = false;
    bool dirty = false; // Some entity was killed, needs compaction.

    Ref<SpriteFrames> frames;
    RID ci;        // CanvasItem of the owning node.
    RID multimesh; // MultiMesh drawn on `ci`.
    RID texture;   // Spritesheet.
    RID shader;    // Per-group shader/material.
    RID material;

    // Flattened frame table: frame_table[anims[anim].start + frame].
    FrameTable frame_table;

    PackedFloat32Array buffer; // MultiMesh buffer.
};
