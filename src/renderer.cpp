#include "renderer.h"

#include <cstdio>
#include <cstring>
#include <string>

// Chargeur GL minimal embarqué (déjà instancié par imgui_impl_opengl3.cpp).
#include "imgui_impl_opengl3_loader.h"

// Constantes absentes du chargeur minimal d'imgui (valeurs glcorearb).
#ifndef GL_POINTS
#define GL_POINTS 0x0000
#endif
#ifndef GL_LINES
#define GL_LINES 0x0001
#endif
#ifndef GL_TRIANGLE_STRIP
#define GL_TRIANGLE_STRIP 0x0005
#endif
#ifndef GL_PROGRAM_POINT_SIZE
#define GL_PROGRAM_POINT_SIZE 0x8642
#endif
#ifndef GL_DYNAMIC_DRAW
#define GL_DYNAMIC_DRAW 0x88E8
#endif
#ifndef GL_CONTEXT_CORE_PROFILE_BIT
#define GL_CONTEXT_CORE_PROFILE_BIT 0x00000001
#endif
#ifndef GL_NO_ERROR
#define GL_NO_ERROR 0
#endif
#ifndef GL_RGB
#define GL_RGB 0x1907
#endif

namespace mesh {

namespace {

// Fonctions GL absentes du chargeur minimal : on les charge via imgl3wGetProcAddress.
typedef void (APIENTRYP PFNGLDRAWARRAYSPROC)(GLenum mode, GLint first, GLsizei count);
typedef void (APIENTRYP PFNGLUNIFORM4FPROC)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2,
                                            GLfloat v3);
typedef void (APIENTRYP PFNGLUNIFORM1FPROC)(GLint location, GLfloat v0);
typedef void (APIENTRYP PFNGLBINDATTRIBLOCATIONPROC)(GLuint program, GLuint index,
                                                     const GLchar* name);
typedef void (APIENTRYP PFNGLBLENDFUNCPROC)(GLenum sfactor, GLenum dfactor);
typedef void (APIENTRYP PFNGLPOINTSIZEPROC)(GLfloat size);

PFNGLDRAWARRAYSPROC pfnDrawArrays = nullptr;
PFNGLUNIFORM4FPROC pfnUniform4f = nullptr;
PFNGLUNIFORM1FPROC pfnUniform1f = nullptr;
PFNGLBINDATTRIBLOCATIONPROC pfnBindAttribLocation = nullptr;
PFNGLBLENDFUNCPROC pfnBlendFunc = nullptr;
PFNGLPOINTSIZEPROC pfnPointSize = nullptr;

bool loadExtras() {
    pfnDrawArrays = (PFNGLDRAWARRAYSPROC)imgl3wGetProcAddress("glDrawArrays");
    pfnUniform4f = (PFNGLUNIFORM4FPROC)imgl3wGetProcAddress("glUniform4f");
    pfnUniform1f = (PFNGLUNIFORM1FPROC)imgl3wGetProcAddress("glUniform1f");
    pfnBindAttribLocation = (PFNGLBINDATTRIBLOCATIONPROC)imgl3wGetProcAddress("glBindAttribLocation");
    pfnBlendFunc = (PFNGLBLENDFUNCPROC)imgl3wGetProcAddress("glBlendFunc");
    pfnPointSize = (PFNGLPOINTSIZEPROC)imgl3wGetProcAddress("glPointSize");
    return pfnDrawArrays && pfnUniform4f && pfnUniform1f && pfnBindAttribLocation &&
           pfnBlendFunc;
}

// Version GLSL selon la version du contexte : 3.3+ → 330, sinon 140 (Raspberry Pi…).
// Sur Apple, le profil core impose au moins GLSL 150.
std::string glslVersion() {
#ifdef __APPLE__
    return "150";
#else
    int maj = 0, min = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &maj);
    glGetIntegerv(GL_MINOR_VERSION, &min);
    if (maj > 3 || (maj == 3 && min >= 3)) return "330";
    return "140";
#endif
}

std::string vertexShaderSource() {
    return "#version " + glslVersion() +
           "\n"
           "in vec2 aPos;\n"
           "uniform mat4 uProj;\n"
           "uniform float uPointSize;\n"
           "void main() {\n"
           "    gl_Position = uProj * vec4(aPos, 0.0, 1.0);\n"
           "    gl_PointSize = uPointSize;\n"
           "}\n";
}

std::string fragmentShaderSource() {
    return "#version " + glslVersion() +
           "\n"
           "uniform vec4 uColor;\n"
           "out vec4 fragColor;\n"
           "void main() { fragColor = uColor; }\n";
}

// Shaders du programme texturé (calque d'image, 7.7) : position + coordonnée
// de texture ; couleur = échantillon de la texture × teinte (opacité).
std::string vertexShaderTexSource() {
    return "#version " + glslVersion() +
           "\n"
           "in vec2 aPos;\n"
           "in vec2 aUV;\n"
           "uniform mat4 uProj;\n"
           "out vec2 vUV;\n"
           "void main() {\n"
           "    gl_Position = uProj * vec4(aPos, 0.0, 1.0);\n"
           "    vUV = aUV;\n"
           "}\n";
}

std::string fragmentShaderTexSource() {
    return "#version " + glslVersion() +
           "\n"
           "uniform sampler2D uTex;\n"
           "uniform vec4 uTint;\n"
           "in vec2 vUV;\n"
           "out vec4 fragColor;\n"
           "void main() { fragColor = texture(uTex, vUV) * uTint; }\n";
}

// Compile un shader et renvoie l'objet (0 en cas d'échec, avec le log).
GLuint compileShader(GLenum type, const std::string& src) {
    const char* c = src.c_str();
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &c, nullptr);
    glCompileShader(sh);
    GLint ok = GL_FALSE;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Erreur de shader : %s\n", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

}  // namespace

bool Renderer::init() {
    // Profil du contexte : core (GL 3.2+) ou compatibilité (ex. Raspberry Pi).
    int mask = 0;
    glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &mask);
    coreProfile_ = (mask & GL_CONTEXT_CORE_PROFILE_BIT) != 0;

    if (!loadExtras()) return false;
    if (!compileProgram()) return false;
    if (!compileTexProgram()) return false;

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, (GLsizei)(2 * sizeof(float)), (void*)0);
    glBindVertexArray(0);

    // VAO texturé : position + UV entrelacés (calque d'image, 7.7).
    glGenVertexArrays(1, &vaoTex_);
    glGenBuffers(1, &vboTex_);
    glBindVertexArray(vaoTex_);
    glBindBuffer(GL_ARRAY_BUFFER, vboTex_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, (GLsizei)(4 * sizeof(float)), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, (GLsizei)(4 * sizeof(float)),
                         (void*)(2 * sizeof(float)));
    glBindVertexArray(0);
    return true;
}

void Renderer::shutdown() {
    if (prog_) glDeleteProgram(prog_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (progTex_) glDeleteProgram(progTex_);
    if (vaoTex_) glDeleteVertexArrays(1, &vaoTex_);
    if (vboTex_) glDeleteBuffers(1, &vboTex_);
    prog_ = vao_ = vbo_ = 0;
    progTex_ = vaoTex_ = vboTex_ = 0;
}

bool Renderer::compileProgram() {
    GLuint vs = compileShader(GL_VERTEX_SHADER, vertexShaderSource());
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource());
    if (!vs || !fs) return false;

    prog_ = glCreateProgram();
    glAttachShader(prog_, vs);
    glAttachShader(prog_, fs);
    pfnBindAttribLocation(prog_, 0, "aPos");
    glLinkProgram(prog_);
    GLint ok = GL_FALSE;
    glGetProgramiv(prog_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(prog_, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Erreur de link : %s\n", log);
        return false;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);

    locProj_ = glGetUniformLocation(prog_, "uProj");
    locColor_ = glGetUniformLocation(prog_, "uColor");
    locPointSize_ = glGetUniformLocation(prog_, "uPointSize");
    return true;
}

bool Renderer::compileTexProgram() {
    GLuint vs = compileShader(GL_VERTEX_SHADER, vertexShaderTexSource());
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragmentShaderTexSource());
    if (!vs || !fs) return false;

    progTex_ = glCreateProgram();
    glAttachShader(progTex_, vs);
    glAttachShader(progTex_, fs);
    pfnBindAttribLocation(progTex_, 0, "aPos");
    pfnBindAttribLocation(progTex_, 1, "aUV");
    glLinkProgram(progTex_);
    GLint ok = GL_FALSE;
    glGetProgramiv(progTex_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(progTex_, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Erreur de link (texturé) : %s\n", log);
        return false;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);

    locProjTex_ = glGetUniformLocation(progTex_, "uProj");
    locTintTex_ = glGetUniformLocation(progTex_, "uTint");
    locSamplerTex_ = glGetUniformLocation(progTex_, "uTex");
    return true;
}

void Renderer::setupState() {
    glUseProgram(prog_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glUniformMatrix4fv(locProj_, 1, GL_FALSE, proj_);
    // Ré-applique le viewport de la scène : main.cpp le remet plein écran avant
    // chaque frame (pour le clear), il faut donc le rétablir ici avant de dessiner.
    glViewport(vx_, vy_, vw_, vh_);
    glEnable(GL_BLEND);
    pfnBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    if (coreProfile_) glEnable(GL_PROGRAM_POINT_SIZE);
}

void Renderer::clear(const Color& c) {
    // Nettoie d'éventuelles erreurs GL résiduelles (ex. atlas de polices d'imgui)
    // afin qu'elles ne masquent pas les erreurs de notre propre rendu.
    while (glGetError() != GL_NO_ERROR) {}
    // Le clear ne touche que la zone du viewport (scissor).
    glEnable(GL_SCISSOR_TEST);
    glScissor(vx_, vy_, vw_, vh_);
    glClearColor(c.r, c.g, c.b, c.a);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
}

void Renderer::setViewport(int x, int y, int w, int h) {
    vx_ = x;
    vy_ = y;
    vw_ = w;
    vh_ = h;
    glViewport(x, y, w, h);
}

void Renderer::setProjection(float left, float right, float bottom, float top) {
    float m[16] = {0.0f};
    m[0] = 2.0f / (right - left);
    m[5] = 2.0f / (top - bottom);
    m[10] = -1.0f;
    m[12] = -(right + left) / (right - left);
    m[13] = -(top + bottom) / (top - bottom);
    m[15] = 1.0f;
    std::memcpy(proj_, m, sizeof(m));
}

void Renderer::drawLines(const std::vector<Vec2>& pairs, const Color& c) {
    if (pairs.empty()) return;
    setupState();
    pfnUniform4f(locColor_, c.r, c.g, c.b, c.a);
    pfnUniform1f(locPointSize_, 1.0f);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(pairs.size() * sizeof(Vec2)), pairs.data(),
                 GL_DYNAMIC_DRAW);
    pfnDrawArrays(GL_LINES, 0, (GLsizei)pairs.size());
}

void Renderer::drawTriangles(const std::vector<Vec2>& pts, const Color& c) {
    if (pts.empty()) return;
    setupState();
    pfnUniform4f(locColor_, c.r, c.g, c.b, c.a);
    pfnUniform1f(locPointSize_, 1.0f);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(pts.size() * sizeof(Vec2)), pts.data(),
                 GL_DYNAMIC_DRAW);
    pfnDrawArrays(GL_TRIANGLES, 0, (GLsizei)pts.size());
}

void Renderer::drawPoints(const std::vector<Vec2>& pts, float sizePx, const Color& c) {
    if (pts.empty()) return;
    setupState();
    pfnUniform4f(locColor_, c.r, c.g, c.b, c.a);
    if (coreProfile_)
        pfnUniform1f(locPointSize_, sizePx);  // taille via gl_PointSize (profil core)
    else
        pfnPointSize(sizePx);  // taille legacy (profil compatibilité)
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(pts.size() * sizeof(Vec2)), pts.data(),
                 GL_DYNAMIC_DRAW);
    pfnDrawArrays(GL_POINTS, 0, (GLsizei)pts.size());
}

unsigned Renderer::createTexture(int w, int h, const unsigned char* rgba) {
    if (w <= 0 || h <= 0 || !rgba) return 0;
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

void Renderer::destroyTexture(unsigned tex) {
    if (tex) glDeleteTextures(1, &tex);
}

void Renderer::drawTexturedQuad(unsigned tex, const Vec2& p0, const Vec2& p1, const Vec2& p2,
                                const Vec2& p3, const Color& tint) {
    if (!tex || !progTex_) return;
    // Sommets entrelacés (position, UV). Ordre en zigzag du quad p0(p1-p3)-p2
    // pour un TRIANGLE_STRIP : un strip crée les triangles (v0,v1,v2) et
    // (v1,v2,v3) ; dans l'ordre cyclique p0,p1,p2,p3 ils partageraient l'arête
    // p1-p2 (le bord droit) et ne couvriraient que la moitié droite du quad.
    // Ligne 0 de la texture = bas (flip au chargement), donc v=1 = haut.
    struct V {
        float x, y, u, v;
    };
    const V verts[4] = {
        {p0.x, p0.y, 0.0f, 0.0f}, {p1.x, p1.y, 1.0f, 0.0f},
        {p3.x, p3.y, 0.0f, 1.0f}, {p2.x, p2.y, 1.0f, 1.0f}};
    glUseProgram(progTex_);
    glBindVertexArray(vaoTex_);
    glBindBuffer(GL_ARRAY_BUFFER, vboTex_);
    glUniformMatrix4fv(locProjTex_, 1, GL_FALSE, proj_);
    // Ré-applique le viewport de la scène (main.cpp le remet plein écran
    // avant chaque frame) comme dans setupState().
    glViewport(vx_, vy_, vw_, vh_);
    glEnable(GL_BLEND);
    pfnBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    pfnUniform4f(locTintTex_, tint.r, tint.g, tint.b, tint.a);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(locSamplerTex_, 0);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
    pfnDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindTexture(GL_TEXTURE_2D, 0);
}

bool Renderer::readPixel(int px, int py, Color& out) const {
    if (vw_ <= 0 || vh_ <= 0 || px < 0 || py < 0 || px >= vw_ || py >= vh_) return false;
    unsigned char rgb[3] = {0, 0, 0};
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(vx_ + px, vy_ + (vh_ - 1 - py), 1, 1, GL_RGB, GL_UNSIGNED_BYTE, rgb);
    out = {rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f, 1.0f};
    return true;
}

std::vector<unsigned char> Renderer::readPixelsRGBA() const {
    std::vector<unsigned char> px((size_t)vw_ * (size_t)vh_ * 4u);
    if (vw_ <= 0 || vh_ <= 0) return {};
    // Lit le contenu actuel du tampon d'affichage (la scène vient d'être
    // dessinée, l'interface n'est pas encore rendue) — lignes bas en haut.
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(vx_, vy_, vw_, vh_, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    return px;
}

}  // namespace mesh
