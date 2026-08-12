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

    // Texture RGBA (image-calque, 7.7) : 0 = échec (contexte, dimensions…).
    unsigned createTexture(int w, int h, const unsigned char* rgba);
    void destroyTexture(unsigned tex);
    // Quad texturé en coordonnées monde (calque d'image) : les 4 sommets sont
    // donnés dans l'ordre trigonométrique, UV (0,0)→(1,0)→(1,1)→(0,1) — la
    // ligne 0 de la texture correspond au bas (flip fait au chargement).
    void drawTexturedQuad(unsigned tex, const Vec2& p0, const Vec2& p1, const Vec2& p2,
                          const Vec2& p3, const Color& tint);

    // Rectangle du viewport en pixels framebuffer (export d'image).
    int viewportX() const { return vx_; }
    int viewportY() const { return vy_; }
    int viewportW() const { return vw_; }
    int viewportH() const { return vh_; }
    // Pixels RGBA du viewport (bas en haut) — pour l'export PNG.
    std::vector<unsigned char> readPixelsRGBA() const;

private:
    bool compileProgram();
    bool compileTexProgram();
    void setupState();

    bool coreProfile_ = true;
    unsigned int prog_ = 0;
    unsigned int vao_ = 0;
    unsigned int vbo_ = 0;
    int locProj_ = -1;
    int locColor_ = -1;
    int locPointSize_ = -1;
    // Programme texturé (image-calque, 7.7).
    unsigned int progTex_ = 0;
    unsigned int vaoTex_ = 0;
    unsigned int vboTex_ = 0;
    int locProjTex_ = -1;
    int locTintTex_ = -1;
    int locSamplerTex_ = -1;
    float proj_[16] = {0.0f};
    int vx_ = 0, vy_ = 0, vw_ = 0, vh_ = 0;  // rectangle du viewport (pixels framebuffer)
};

}  // namespace mesh
