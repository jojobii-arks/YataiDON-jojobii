#pragma once
#include "global_data.h"
#include <algorithm>
#include <cmath>

inline ray::Camera2D compute_camera2d(int virtual_width, int virtual_height) {
    int vw = std::max(virtual_width, 1);
    int vh = std::max(virtual_height, 1);
    int sw = std::max(ray::GetRenderWidth(), 1);
    int sh = std::max(ray::GetRenderHeight(), 1);

    float scale        = std::min((float)sw / vw, (float)sh / vh);
    float base_ox      = (sw - vw * scale) * 0.5f;
    float base_oy      = (sh - vh * scale) * 0.5f;
    float eff_zoom     = scale * global_data.camera.zoom;
    float zoom_ox      = (vw * scale * (global_data.camera.zoom - 1.0f)) * 0.5f;
    float zoom_oy      = (vh * scale * (global_data.camera.zoom - 1.0f)) * 0.5f;

    float h_scale      = global_data.camera.h_scale;
    float v_scale      = global_data.camera.v_scale;
    float hscale_ox    = (vw * scale * (h_scale - 1.0f)) * 0.5f;
    float vscale_oy    = (vh * scale * (v_scale - 1.0f)) * 0.5f;
    float offset_x     = base_ox - zoom_ox - hscale_ox + (global_data.camera.offset.x * scale);
    float offset_y     = base_oy - zoom_oy - vscale_oy + (global_data.camera.offset.y * scale);

    ray::Vector2 target = {vw * 0.5f, vh * 0.5f};
    offset_x += eff_zoom * target.x;
    offset_y += eff_zoom * target.y;

    return {{offset_x, offset_y}, target, global_data.camera.rotation, eff_zoom};
}

inline ray::Camera3D camera2d_to_3d(ray::Camera2D cam) {
    float sw   = (float)ray::GetRenderWidth();
    float sh   = (float)ray::GetRenderHeight();
    float zoom = std::max(cam.zoom, 0.0001f); // guard divide-by-zero/negative zoom
    float rot  = cam.rotation * DEG2RAD;
    float cx   = (sw * 0.5f - cam.offset.x) / zoom + cam.target.x;
    float cy   = (sh * 0.5f - cam.offset.y) / zoom + cam.target.y;
    return {
        {cx, cy, -100.0f},
        {cx, cy,    0.0f},
        {std::sin(rot), -std::cos(rot), 0.0f},
        sh / zoom,
        ray::CAMERA_ORTHOGRAPHIC
    };
}
