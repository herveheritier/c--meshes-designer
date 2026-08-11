#pragma once
#include "mesh.h"

namespace mesh {

// ---------------------------------------------------------------------------
// Caméra 2D orthographique : conversions écran <-> monde et navigation.
// L'axe Y pointe vers le haut dans le monde ; à l'écran, Y descend.
// ---------------------------------------------------------------------------
class Camera2D {
public:
    // Zoom : pixels par unité monde. ×1 (100 %) = 40 px/unité ; borné ×0,1–×10.
    float zoom = 40.0f;
    float cx = 0.0f;       // centre de la vue (monde)
    float cy = 0.0f;

    // viewport = taille en pixels (logiques) de la zone de rendu.
    Vec2 screenToWorld(const Vec2& s, const Vec2& viewport) const;
    Vec2 worldToScreen(const Vec2& w, const Vec2& viewport) const;

    // Translation : dx/dy en pixels écran.
    void pan(float dxPx, float dyPx, const Vec2& viewport);
    // Zoom autour du point écran (molette) : factor > 1 = zoom avant.
    void zoomAt(float factor, const Vec2& screenPt, const Vec2& viewport);
    // Cadre la vue sur un centre et une demi-largeur monde.
    void frame(const Vec2& center, float extent, const Vec2& viewport);
    void reset() { cx = cy = 0.0f; zoom = 40.0f; }
};

}  // namespace mesh
