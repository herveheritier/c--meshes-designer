#pragma once
#include "mesh.h"

#include <vector>

namespace mesh {

// ---------------------------------------------------------------------------
// Rendu OpenGL 3.3 minimal : lignes, triangles pleins, points.
// Utilise le chargeur GL embarqué d'imgui (imgui_impl_opengl3_loader.h),
// donc aucune dépendance GL externe n'est nécessaire.
// ---------------------------------------------------------------------------
class Renderer {
public:
    bool init();
    void shutdown();

    void clear(const Color& c);
    void setViewport(int x, int y, int w, int h);

    // Projection orthographique (coordonnées monde, Y vers le haut).
    void setProjection(float left, float right, float bottom, float top);

    // Primitives immédiates, en coordonnées monde.
    // drawLines : liste applatte de paires (a1,b1,a2,b2,…).
    void drawLines(const std::vector<Vec2>& pairs, const Color& c);
    void drawTriangles(const std::vector<Vec2>& pts, const Color& c);
    void drawPoints(const std::vector<Vec2>& pts, float sizePx, const Color& c);

private:
    bool compileProgram();
    void setupState();

    bool coreProfile_ = true;
    unsigned int prog_ = 0;
    unsigned int vao_ = 0;
    unsigned int vbo_ = 0;
    int locProj_ = -1;
    int locColor_ = -1;
    int locPointSize_ = -1;
    float proj_[16] = {0.0f};
    int vx_ = 0, vy_ = 0, vw_ = 0, vh_ = 0;  // rectangle du viewport (pixels framebuffer)
};

}  // namespace mesh
