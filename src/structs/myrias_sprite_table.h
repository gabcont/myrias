#pragma once

#include <godot_cpp/classes/atlas_texture.hpp>
#include <godot_cpp/classes/sprite_frames.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/rect2.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/templates/vector.hpp>

using namespace godot;

// One drawable frame: normalized UV rect inside the spritesheet + size in pixels.
struct FrameInfo {
    Rect2 uv;
    Vector2 size;
    float end_rel = 0.0f; // Cumulative end of this frame, in relative duration units (1 unit = 1 / fps).
};

// Playback data of one SpriteFrames animation.
struct AnimInfo {
    int start = 0;       // First index in frames.
    int count = 0;
    float fps = 0.0f;    // Animation speed (frames per second). 0 = static, shows frame 0.
    bool loop = false;
    bool uniform = true; // All frames last exactly 1 unit, so the frame is a plain division.
    float total_rel = 0.0f;
};

struct FrameTable {
    Vector<FrameInfo> frames;
    Vector<AnimInfo> anims;

    FrameTable() = default;

    explicit FrameTable(const Ref<SpriteFrames> &p_frames) {
        if (p_frames.is_null()) {
            return;
        }

        const PackedStringArray names = p_frames->get_animation_names();
        for (int a = 0; a < names.size(); a++) {
            const StringName anim = names[a];

            AnimInfo ainfo;
            ainfo.start = frames.size();
            ainfo.count = p_frames->get_frame_count(anim);
            ainfo.fps = (float)p_frames->get_animation_speed(anim);
            ainfo.loop = p_frames->get_animation_loop(anim);

            float cumulative = 0.0f;
            for (int f = 0; f < ainfo.count; f++) {
                const float rel = p_frames->get_frame_duration(anim, f);
                cumulative += rel;
                if (!Math::is_equal_approx(rel, 1.0f)) {
                    ainfo.uniform = false;
                }

                FrameInfo info;
                info.end_rel = cumulative;

                Ref<Texture2D> tex = p_frames->get_frame_texture(anim, f);
                if (tex.is_null()) {
                    info.uv = Rect2(0, 0, 0, 0);
                    info.size = Vector2();
                    frames.push_back(info);
                    continue;
                }

                Ref<Texture2D> base = tex;
                Rect2 region(Vector2(), tex->get_size());
                AtlasTexture *atlas = Object::cast_to<AtlasTexture>(tex.ptr());
                if (atlas != nullptr && atlas->get_atlas().is_valid()) {
                    base = atlas->get_atlas();
                    region = atlas->get_region();
                }

                const Vector2 base_size = base->get_size();
                info.uv = Rect2(region.position / base_size, region.size / base_size);
                info.size = region.size;
                frames.push_back(info);
            }
            ainfo.total_rel = cumulative;
            anims.push_back(ainfo);
        }
    }

    uint16_t compute_frame(int p_anim_idx, float p_lifetime) const {
        if (anims.is_empty()) {
            return 0;
        }
        const int anim_idx = CLAMP(p_anim_idx, 0, anims.size() - 1);
        const AnimInfo &anim = anims[anim_idx];

        if (anim.count <= 1 || anim.fps <= 0.0f || anim.total_rel <= 0.0f) {
            return 0;
        }
        float t = p_lifetime * anim.fps;
        if (anim.loop) {
            t = Math::fmod(t, anim.total_rel);
        } else if (t >= anim.total_rel) {
            return (uint16_t)(anim.count - 1);
        }

        if (anim.uniform) {
            return (uint16_t)MIN((int)t, anim.count - 1);
        }
        const FrameInfo *frame_ptr = frames.ptr() + anim.start;
        for (int i = 0; i < anim.count; i++) {
            if (t < frame_ptr[i].end_rel) {
                return (uint16_t)i;
            }
        }
        return (uint16_t)(anim.count - 1);
    }

    static RID extract_texture(const Ref<SpriteFrames> &p_frames) {
        if (p_frames.is_null()) {
            return RID();
        }

        RID texture;
        const PackedStringArray names = p_frames->get_animation_names();
        for (int a = 0; a < names.size(); a++) {
            const StringName anim = names[a];
            const int count = p_frames->get_frame_count(anim);

            for (int f = 0; f < count; f++) {
                Ref<Texture2D> tex = p_frames->get_frame_texture(anim, f);
                if (tex.is_null()) {
                    continue;
                }

                Ref<Texture2D> base = tex;
                AtlasTexture *atlas = Object::cast_to<AtlasTexture>(tex.ptr());
                if (atlas != nullptr && atlas->get_atlas().is_valid()) {
                    base = atlas->get_atlas();
                }

                if (!texture.is_valid()) {
                    texture = base->get_rid();
                } else if (texture != base->get_rid()) {
                    UtilityFunctions::push_warning("MyriasServer: group frames use more than one spritesheet; only the first is drawn correctly.");
                }
            }
        }
        return texture;
    }
};
