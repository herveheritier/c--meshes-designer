// Test d'interface (sans GUI) des opérations ensemblistes (5.12) :
// on pilote les VRAIES méthodes de App (memorizeBoolSet / applyBoolOp /
// undo / redo) sur un scénario de chevauchement partiel A−B, avec un plan
// C hors de la zone qui doit rester intact. Vérifie la géométrie du résultat
// (aire + classification par grille), la conservation des couleurs, la
// sélection résultante, puis l'annulation / le rétablissement.
//
// Compilation :
//   g++ -std=c++17 -O2 -Isrc -Isrc/external -Iexternal -Iexternal/imgui \
//       -Wall -o build/boolui_test src/boolui_test.cpp src/app.cpp \
//       src/camera.cpp src/io.cpp src/renderer.cpp src/stb_image_impl.cpp \
//       src/mesh.cpp src/pngexport.cpp src/svgparse.cpp src/triangulate.cpp \
//       external/imgui/imgui.cpp external/imgui/imgui_draw.cpp \
//       external/imgui/imgui_tables.cpp external/imgui/imgui_widgets.cpp \
//       $(pkg-config --cflags --libs sdl2)

#include "app.h"
#include "triangulate.h"

#include <cstdio>
#include <vector>

using namespace mesh;

namespace {

int failures = 0;

#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (!(cond)) {                                                \
            ++failures;                                               \
            std::printf("ÉCHEC ligne %d : ", __LINE__);               \
            std::printf(__VA_ARGS__);                                 \
            std::printf("\n");                                        \
        }                                                             \
    } while (0)

// Aire totale des faces d'un plan (somme des aires des polygones).
float planeArea(const Mesh2D& m) {
    float area = 0.0f;
    for (const Face& f : m.faces) {
        if (f.verts.size() < 3) continue;
        float a = 0.0f;
        for (size_t i = 0; i < f.verts.size(); ++i) {
            const Vec2& p = m.vertices[f.verts[i]];
            const Vec2& q = m.vertices[f.verts[(i + 1) % f.verts.size()]];
            a += p.x * q.y - q.x * p.y;
        }
        area += 0.5f * std::fabs(a);
    }
    return area;
}

// Vrai si p est dans le rectangle [x0,x1]×[y0,y1].
bool inRect(const Vec2& p, float x0, float y0, float x1, float y1) {
    return p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1;
}

// Vrai si p est couvert par au moins une face du plan.
bool meshCovers(const Mesh2D& m, const Vec2& p) {
    for (const Face& f : m.faces) {
        if (f.verts.size() < 3) continue;
        std::vector<Vec2> pts;
        pts.reserve(f.verts.size());
        for (int v : f.verts) pts.push_back(m.vertices[v]);
        if (pointInPolygon(p, pts)) return true;
    }
    return false;
}

// Rectangle → face(s) triangulée(s) dans le plan, peintes de `color`.
// Retourne les indices des faces créées.
std::vector<int> addPaintedRect(Mesh2D& m, float x0, float y0, float x1, float y1,
                                const Color& color) {
    const int a = m.addVertex({x0, y0});
    const int b = m.addVertex({x1, y0});
    const int c = m.addVertex({x1, y1});
    const int d = m.addVertex({x0, y1});
    const int before = (int)m.faces.size();
    m.addTriangulatedFace({a, b, c, d});
    std::vector<int> out;
    for (int i = before; i < (int)m.faces.size(); ++i) {
        m.faces[i].color = color;
        m.faces[i].hasColor = true;
        out.push_back(i);
    }
    return out;
}

// Réinitialise la scène et construit le scénario de chevauchement partiel :
//   A : rectangle [-2,-1]×[2,1]   (rouge, aire 8)
//   B : rectangle [0,-1.5]×[4,1.5] (bleu, aire 12, chevauche A sur [0,2]×[-1,1])
//   C : carré [5,-0.5]×[6,0.5]    (vert, aire 1, hors de A∪B — doit rester intact)
// Retourne {facesA, facesB, facesC}.
struct Scenario {
    std::vector<int> facesA;
    std::vector<int> facesB;
    std::vector<int> facesC;
};

Scenario buildScenario(App& app) {
    app.newDocument();
    Mesh2D& m = app.scene.planes[0];
    Scenario s;
    s.facesA = addPaintedRect(m, -2, -1, 2, 1, rgba(1.0f, 0.2f, 0.2f, 0.6f));
    s.facesB = addPaintedRect(m, 0, -1.5f, 4, 1.5f, rgba(0.2f, 0.2f, 1.0f, 0.6f));
    s.facesC = addPaintedRect(m, 5, -0.5f, 6, 0.5f, rgba(0.2f, 1.0f, 0.2f, 0.6f));
    return s;
}

// Prédicats de région (scénario fixe).
bool inA(const Vec2& p) { return inRect(p, -2, -1, 2, 1); }
bool inB(const Vec2& p) { return inRect(p, 0, -1.5f, 4, 1.5f); }
bool inC(const Vec2& p) { return inRect(p, 5, -0.5f, 6, 0.5f); }

// Comparaison du maillage à l'attendu par grille : couverture = op(p) ∪ C(p),
// en ignorant les points trop proches d'une frontière (bruit flottant).
void checkGrid(const App& app, const char* what, bool (*op)(const Vec2&)) {
    int bad = 0;
    for (float x = -2.5f; x <= 6.5f; x += 0.25f) {
        for (float y = -2.0f; y <= 2.0f; y += 0.25f) {
            const Vec2 p{x, y};
            const bool nearEdge =
                std::fabs(p.x + 2) < 0.06f || std::fabs(p.x - 2) < 0.06f ||
                std::fabs(p.y + 1) < 0.06f || std::fabs(p.y - 1) < 0.06f ||
                std::fabs(p.x - 0) < 0.06f || std::fabs(p.x - 4) < 0.06f ||
                std::fabs(p.y + 1.5f) < 0.06f || std::fabs(p.y - 1.5f) < 0.06f ||
                std::fabs(p.x - 5) < 0.06f || std::fabs(p.x - 6) < 0.06f ||
                std::fabs(p.y + 0.5f) < 0.06f || std::fabs(p.y - 0.5f) < 0.06f;
            if (nearEdge) continue;
            const bool expect = op(p) || inC(p);
            const bool got = meshCovers(app.scene.activePlane(), p);
            if (expect != got) ++bad;
        }
    }
    CHECK(bad == 0, "%s : grille — %d point(s) mal couvert(s)", what, bad);
}

// Test complet d'une opération : mémorise A et B, applique l'opération,
// vérifie aire / grille / couleurs / sélection, puis annule et rétablit.
void runOpTest(SetOp op, float expectArea, const char* name,
               bool (*expect)(const Vec2&)) {
    App app;
    const Scenario s = buildScenario(app);

    // Interface : cible triangle, sélection de A, « Mémoriser A ».
    app.selMode = SelMode::Face;
    app.selFaces = s.facesA;
    app.memorizeBoolSet(0);
    CHECK(app.boolSetCount(0) == s.facesA.size(), "%s : A mémorisé (%zu)", name,
          app.boolSetCount(0));
    CHECK(app.boolSetCount(1) == 0, "%s : B pas encore mémorisé", name);

    // Sélection de B, « Mémoriser B ».
    app.selFaces = s.facesB;
    app.memorizeBoolSet(1);
    CHECK(app.boolSetCount(1) == s.facesB.size(), "%s : B mémorisé (%zu)", name,
          app.boolSetCount(1));

    // Tentative d'opération avant mémorisation complète : doit échouer.
    app.clearBoolSets();
    app.applyBoolOp(op);
    CHECK(planeArea(app.scene.activePlane()) == 21.0f,
          "%s : opération sans ensembles → plan intact (%.4f)", name,
          planeArea(app.scene.activePlane()));
    app.selFaces = s.facesA;
    app.memorizeBoolSet(0);
    app.selFaces = s.facesB;
    app.memorizeBoolSet(1);

    const int facesBefore = (int)app.scene.activePlane().faces.size();
    app.applyBoolOp(op);
    const Mesh2D& m = app.scene.activePlane();

    // Aire totale : résultat (A∪B → op) + C intact.
    CHECK(std::fabs(planeArea(m) - expectArea) < 1e-3f,
          "%s : aire après opération = %.4f (attendu %.4f)", name, planeArea(m),
          expectArea);
    CHECK(app.boolSetCount(0) == 0 && app.boolSetCount(1) == 0,
          "%s : ensembles A/B oubliés après l'opération", name);
    CHECK(app.selMode == SelMode::Face, "%s : cible reste « triangle »", name);
    CHECK(!app.selFaces.empty(), "%s : résultat sélectionné (%zu face(s))", name,
          app.selFaces.size());

    // La sélection doit couvrir exactement le résultat (et pas C).
    float selArea = 0.0f;
    for (int fi : app.selFaces) {
        if (fi < 0 || (size_t)fi >= m.faces.size()) continue;
        const Face& f = m.faces[fi];
        float a = 0.0f;
        for (size_t i = 0; i < f.verts.size(); ++i) {
            const Vec2& p = m.vertices[f.verts[i]];
            const Vec2& q = m.vertices[f.verts[(i + 1) % f.verts.size()]];
            a += p.x * q.y - q.x * p.y;
        }
        selArea += 0.5f * std::fabs(a);
    }
    CHECK(std::fabs(selArea - (expectArea - 1.0f)) < 1e-3f,
          "%s : la sélection couvre le résultat seul (%.4f, attendu %.4f)", name,
          selArea, expectArea - 1.0f);

    // Couleurs : au centre du résultat (hors C), la couleur vient de A ou B.
    {
        const Vec2 probe{op == SetOp::Intersection ? 1.0f : -1.0f, 0.0f};
        // Pour l'intersection, le centre du résultat est (1, 0) — zone A∩B.
        if (op == SetOp::Intersection) {
            const Vec2 p{1.0f, 0.0f};
            for (const Face& f : m.faces) {
                if (f.verts.size() < 3) continue;
                std::vector<Vec2> pts;
                for (int v : f.verts) pts.push_back(m.vertices[v]);
                if (pointInPolygon(p, pts)) {
                    CHECK(f.hasColor, "%s : face du résultat colorée", name);
                    break;
                }
            }
        } else {
            // Différence / union / symétrique : un point hors B doit venir de A.
            bool found = false;
            for (const Face& f : m.faces) {
                if (f.verts.size() < 3) continue;
                std::vector<Vec2> pts;
                for (int v : f.verts) pts.push_back(m.vertices[v]);
                if (pointInPolygon(probe, pts)) {
                    found = true;
                    CHECK(f.hasColor && f.color.r > 0.7f && f.color.g < 0.5f,
                          "%s : couleur de A conservée sur le résultat", name);
                    break;
                }
            }
            CHECK(found, "%s : un triangle du résultat couvre (−1, 0)", name);
        }
    }

    // C hors zone : intact (même nombre de faces, aire 1, couleur verte).
    {
        float cArea = 0.0f;
        for (const Face& f : m.faces) {
            if (f.verts.size() < 3) continue;
            std::vector<Vec2> pts;
            for (int v : f.verts) pts.push_back(m.vertices[v]);
            if (meshCovers(m, {5.5f, 0.0f}) && pointInPolygon({5.5f, 0.0f}, pts)) {
                cArea += std::fabs([&] {
                    float a = 0.0f;
                    for (size_t i = 0; i < f.verts.size(); ++i) {
                        const Vec2& p = m.vertices[f.verts[i]];
                        const Vec2& q = m.vertices[f.verts[(i + 1) % f.verts.size()]];
                        a += p.x * q.y - q.x * p.y;
                    }
                    return 0.5f * a;
                }());
            }
        }
        CHECK(std::fabs(cArea - 1.0f) < 1e-3f, "%s : plan C intact (aire %.4f)",
              name, cArea);
    }

    // Grille géométrique.
    checkGrid(app, name, expect);

    // Aucun sommet orphelin : chaque sommet du plan doit être référencé par
    // au moins une face (les sommets des faces remplacées disparaissent).
    {
        std::vector<char> used(m.vertices.size(), 0);
        for (const Face& f : m.faces)
            for (int v : f.verts)
                if (v >= 0 && (size_t)v < used.size()) used[v] = 1;
        int orphans = 0;
        for (char u : used)
            if (!u) ++orphans;
        CHECK(orphans == 0, "%s : %d sommet(s) orphelin(s) après l'opération", name,
              orphans);
    }

    // --- Annuler / rétablir ---
    app.undo();
    CHECK(std::fabs(planeArea(app.scene.activePlane()) - 21.0f) < 1e-3f,
          "%s : après Ctrl+Z → scène d'origine (aire %.4f)", name,
          planeArea(app.scene.activePlane()));
    CHECK((int)app.scene.activePlane().faces.size() == facesBefore,
          "%s : après Ctrl+Z → %d faces (attendu %d)", name,
          (int)app.scene.activePlane().faces.size(), facesBefore);
    CHECK(app.selFaces.empty(), "%s : après Ctrl+Z → sélection vidée", name);
    CHECK(app.boolSetCount(0) == 0 && app.boolSetCount(1) == 0,
          "%s : après Ctrl+Z → ensembles oubliés", name);

    app.redo();
    CHECK(std::fabs(planeArea(app.scene.activePlane()) - expectArea) < 1e-3f,
          "%s : après Ctrl+Y → résultat rétabli (aire %.4f)", name,
          planeArea(app.scene.activePlane()));
    checkGrid(app, name, expect);

    std::printf("OK — %-22s aire %.4f (attendu %.4f), undo/redo conformes\n", name,
                planeArea(app.scene.activePlane()), expectArea);
}

// Prédicats des 4 opérations sur le scénario fixe.
bool pUnion(const Vec2& p) { return inA(p) || inB(p); }
bool pInter(const Vec2& p) { return inA(p) && inB(p); }
bool pDiff(const Vec2& p) { return inA(p) && !inB(p); }
bool pSym(const Vec2& p) { return (inA(p) || inB(p)) && !(inA(p) && inB(p)); }

}  // namespace

int main() {
    std::printf("=== Opérations ensemblistes — test d'interface (sans GUI) ===\n");
    std::printf("Scénario : A=[-2,-1]×[2,1] (aire 8) · B=[0,-1.5]×[4,1.5] (aire 12)\n");
    std::printf("           chevauchement partiel [0,2]×[-1,1] · C=[5,-0.5]×[6,0.5] intact\n\n");

    // Aire totale de la scène de départ.
    {
        App app;
        buildScenario(app);
        CHECK(std::fabs(planeArea(app.scene.activePlane()) - 21.0f) < 1e-3f,
              "scénario initial : aire %.4f (attendu 21)", planeArea(app.scene.activePlane()));
    }

    runOpTest(SetOp::Union, 17.0f, "Union (A ∪ B)", pUnion);
    runOpTest(SetOp::Intersection, 5.0f, "Intersection (A ∩ B)", pInter);
    runOpTest(SetOp::Difference, 5.0f, "Différence (A − B)", pDiff);
    runOpTest(SetOp::SymDiff, 13.0f, "Symétrique (A △ B)", pSym);

    std::printf("\n%s (%d échec(s))\n", failures == 0 ? "TOUT EST VERT" : "ÉCHECS",
                failures);
    return failures == 0 ? 0 : 1;
}
