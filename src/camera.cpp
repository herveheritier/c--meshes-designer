#include "camera.h"

#include <algorithm>

namespace mesh {

Vec2 Camera2D::screenToWorld(const Vec2& s, const Vec2& vp) const {
    return {cx + (s.x - vp.x * 0.5f) / zoom, cy - (s.y - vp.y * 0.5f) / zoom};
}

Vec2 Camera2D::worldToScreen(const Vec2& w, const Vec2& vp) const {
    return {(w.x - cx) * zoom + vp.x * 0.5f, vp.y * 0.5f - (w.y - cy) * zoom};
}

void Camera2D::pan(float dxPx, float dyPx, const Vec2&) {
    cx -= dxPx / zoom;
    cy += dyPx / zoom;
}

// Bornes du zoom : zoom ×1 = 40 px/unité, borné entre ×0,1 et ×10 (spec 8.1).
static constexpr float kZoomMin = 4.0f;
static constexpr float kZoomMax = 400.0f;

void Camera2D::zoomAt(float factor, const Vec2& sp, const Vec2& vp) {
    const Vec2 w = screenToWorld(sp, vp);
    zoom = std::clamp(zoom * factor, kZoomMin, kZoomMax);
    // Re-centre pour que le point monde sous le curseur reste fixe.
    cx = w.x - (sp.x - vp.x * 0.5f) / zoom;
    cy = w.y + (sp.y - vp.y * 0.5f) / zoom;
}

void Camera2D::frame(const Vec2& center, float extent, const Vec2& vp) {
    cx = center.x;
    cy = center.y;
    const float minDim = std::min(vp.x, vp.y);
    zoom = extent > 1e-6f ? std::clamp(minDim * 0.45f / extent, kZoomMin, kZoomMax) : 40.0f;
}

}  // namespace mesh
