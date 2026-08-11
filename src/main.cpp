// ============================================================================
//  Éditeur de Meshes 2D — cross-platform (Windows / Linux / Mac)
//  C++17 · SDL2 · OpenGL 3.3 · dear imgui
// ============================================================================
#include <SDL.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_opengl3_loader.h>  // déclarations GL (glClear, glViewport…)
#include <imgui_impl_sdl2.h>

#include <cstdio>

#include "app.h"
#include "ui.h"

namespace {

SDL_Window* g_window = nullptr;
SDL_GLContext g_glContext = nullptr;

// Crée le contexte OpenGL avec repli automatique : on demande d'abord un profil
// core 3.3 (desktops Windows/Linux/Mac), puis un profil compatibilité 3.1
// (par ex. Raspberry Pi, VMs). `glsl` reçoit la version GLSL à utiliser.
bool createGLContext(const char*& glsl) {
    const struct { int maj, min; int profile; } attempts[] = {
        {3, 3, SDL_GL_CONTEXT_PROFILE_CORE},
        {3, 1, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY},
    };
    std::string lastErr;
    for (const auto& a : attempts) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, a.maj);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, a.min);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, a.profile);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        g_glContext = SDL_GL_CreateContext(g_window);
        if (g_glContext) {
            // Charger les fonctions GL dès maintenant (imgl3wInit est idempotent ;
            // imgui_impl_opengl3.cpp ne le refera que si nécessaire).
            if (imgl3wInit() != 0) {
                std::fprintf(stderr, "Échec d'initialisation du chargeur OpenGL.\n");
                SDL_GL_DeleteContext(g_glContext);
                g_glContext = nullptr;
                lastErr = "imgl3wInit failed";
                continue;
            }
            int maj = 0, min = 0;
            glGetIntegerv(GL_MAJOR_VERSION, &maj);
            glGetIntegerv(GL_MINOR_VERSION, &min);
#if defined(__APPLE__)
            glsl = "#version 150";
#else
            static std::string s;
            s = (maj > 3 || (maj == 3 && min >= 3)) ? "#version 330" : "#version 140";
            glsl = s.c_str();
#endif
            std::printf("Contexte OpenGL %d.%d (%s)\n", maj, min,
                        a.profile == SDL_GL_CONTEXT_PROFILE_CORE ? "core" : "compatibilité");
            return true;
        }
        lastErr = SDL_GetError();
    }
    std::fprintf(stderr, "Erreur SDL_GL_CreateContext : %s\n", lastErr.c_str());
    return false;
}

bool initSDL(int w, int h) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "Erreur SDL_Init : %s\n", SDL_GetError());
        return false;
    }
    g_window = SDL_CreateWindow(
        "Éditeur de Meshes 2D", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!g_window) {
        std::fprintf(stderr, "Erreur SDL_CreateWindow : %s\n", SDL_GetError());
        return false;
    }
    SDL_GL_SetSwapInterval(1);  // vsync
    return true;
}

// Charge une police système pour les accents (é, è, ç, …).
void loadUiFont(ImGuiIO& io) {
    const char* candidates[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
    };
    static const ImWchar ranges[] = {
        0x20, 0x7E,   // latin de base
        0xA0, 0x17F,  // latin-1 supplément + latin étendu A
        0x2013, 0x203A,
        0x20AC, 0x20AC,
        0x2190, 0x21FF,  // flèches
        0,
    };
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;
    for (const char* path : candidates) {
        FILE* f = std::fopen(path, "rb");
        if (!f) continue;
        std::fclose(f);
        io.Fonts->AddFontFromFileTTF(path, 16.0f, &cfg, ranges);
        return;
    }
    io.Fonts->AddFontDefault();
}

}  // namespace

int main(int, char**) {
    if (!initSDL(1560, 940)) return 1;
    const char* glsl = nullptr;
    if (!createGLContext(glsl)) return 1;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    // NB : la navigation clavier d'ImGui (NavEnableKeyboard) est volontairement
    // laissée désactivée : les flèches servent au déplacement de la sélection
    // (nudge) et aux raccourcis Alt+flèches, sans que le focus ne circule
    // entre les widgets.
    loadUiFont(io);
    ImGui::StyleColorsDark();

    ImGui_ImplSDL2_InitForOpenGL(g_window, g_glContext);
    ImGui_ImplOpenGL3_Init(glsl);

    mesh::App app;
    app.window = g_window;
    app.init();

    bool running = true;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL2_ProcessEvent(&ev);
            // Fermeture (bouton de fenêtre, Alt+F4…) : si la scène est
            // modifiée, la confirmation « Quitter sans enregistrer ? » s'ouvre
            // au lieu de quitter ; la boucle s'arrête quand elle est confirmée.
            if (ev.type == SDL_QUIT) mesh::ui::requestQuit(app);
            if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_CLOSE)
                mesh::ui::requestQuit(app);
            // Glisser-déposer d'un fichier JSON ou OBJ : déclenche l'import
            // (spec 12.2).
            if (ev.type == SDL_DROPFILE) {
                if (ev.drop.file) {
                    std::string path = ev.drop.file;
                    const std::string jsonExt = ".json";
                    const std::string objExt = ".obj";
                    const bool isJson =
                        path.size() > jsonExt.size() &&
                        path.compare(path.size() - jsonExt.size(), jsonExt.size(),
                                     jsonExt) == 0;
                    const bool isObj =
                        path.size() > objExt.size() &&
                        path.compare(path.size() - objExt.size(), objExt.size(),
                                     objExt) == 0;
                    if (isJson) app.openImportDialog(1, path);
                    else if (isObj) app.openImportDialog(2, path);
                    SDL_free(ev.drop.file);
                }
            }
        }
        if (mesh::ui::quitRequested()) running = false;
        if (!running) break;

        // Fenêtre minimisée : on attend pour économiser le CPU.
        if (SDL_GetWindowFlags(g_window) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(10);
            continue;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        mesh::ui::frame(app);

        // Rendu
        int fbW = 0, fbH = 0;
        SDL_GL_GetDrawableSize(g_window, &fbW, &fbH);
        glViewport(0, 0, fbW, fbH);
        glClearColor(0.078f, 0.086f, 0.102f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // Scène OpenGL : dessinée entre le clear et l'interface. La fenêtre
        // viewport étant transparente, la scène apparaît à travers ; les panneaux
        // opaques (dessinés ensuite) restent au-dessus.
        app.drawScene();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(g_window);
    }

    app.shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(g_glContext);
    SDL_DestroyWindow(g_window);
    SDL_Quit();
    return 0;
}
