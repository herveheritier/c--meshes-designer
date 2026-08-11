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

}  // namespace

bool Renderer::init() {
    // Profil du contexte : core (GL 3.2+) ou compatibilité (ex. Raspberry Pi).
    int mask = 0;
    glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &mask);
    coreProfile_ = (mask & GL_CONTEXT_CORE_PROFILE_BIT) != 0;

    if (!loadExtras()) return false;
    if (!compileProgram()) return false;

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, (GLsizei)(2 * sizeof(float)), (void*)0);
    glBindVertexArray(0);
    return true;
}

void Renderer::shutdown() {
    if (prog_) glDeleteProgram(prog_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    prog_ = vao_ = vbo_ = 0;
}

bool Renderer::compileProgram() {
    auto compile = [](GLenum type, const std::string& src) -> GLuint {
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
    };

    GLuint vs = compile(GL_VERTEX_SHADER, vertexShaderSource());
    GLuint fs = compile(GL_FRAGMENT_SHADER, fragmentShaderSource());
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
