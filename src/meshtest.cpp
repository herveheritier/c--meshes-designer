// Tests headless des opérations mesh et de la triangulation.
// Compilation : cible `meshtest` du CMakeLists.
#include "io.h"
#include "mesh.h"
#include "svgparse.h"
#include "triangulate.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace mesh;

static int g_checks = 0;
static int g_failures = 0;

static IoResult writeTestFile(const std::string& path, const std::string& content) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return {false, "écriture impossible"};
    f << content;
    return {true, ""};
}

#define CHECK(cond)                                                      \
    do {                                                                 \
        ++g_checks;                                                      \
        if (!(cond)) {                                                   \
            ++g_failures;                                                \
            std::printf("  FAIL %s:%d — %s\n", __FILE__, __LINE__, #cond); \
        }                                                                \
    } while (0)

static void testTriangulation() {
    std::printf("[triangulation]\n");

    // Carré (convexe)
    {
        std::vector<Vec2> sq = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        std::vector<int> t;
        CHECK(triangulatePolygon(sq, t));
        CHECK((int)t.size() == 6);  // 2 triangles
    }
    // Forme en L (concave)
    {
        std::vector<Vec2> L = {{0, 0}, {2, 0}, {2, 1}, {1, 1}, {1, 2}, {0, 2}};
        std::vector<int> t;
        CHECK(triangulatePolygon(L, t));
        CHECK((int)t.size() == 12);  // 4 triangles
    }
    // Étoile (très concave, 10 sommets)
    {
        std::vector<Vec2> star;
        for (int i = 0; i < 10; ++i) {
            const float a = (float)i * 3.14159265f / 5.0f - 1.5708f;
            const float r = (i % 2 == 0) ? 1.0f : 0.4f;
            star.push_back({std::cos(a) * r, std::sin(a) * r});
        }
        std::vector<int> t;
        CHECK(triangulatePolygon(star, t));
        CHECK((int)t.size() == 24);  // 8 triangles
    }
    // Polygone dégénéré
    {
        std::vector<Vec2> bad = {{0, 0}, {1, 1}, {2, 2}};  // aligné : aire nulle
        std::vector<int> t;
        CHECK(!triangulatePolygon(bad, t));
    }
    // pointInTriangle
    CHECK(pointInTriangle({0.25f, 0.25f}, {0, 0}, {1, 0}, {0, 1}));
    CHECK(!pointInTriangle({1, 1}, {0, 0}, {1, 0}, {0, 1}));
    CHECK(pointInTriangle({0.5f, 0.0f}, {0, 0}, {1, 0}, {0, 1}));  // sur le bord
}

static void testMeshOps() {
    std::printf("[opérations mesh]\n");

    // Construction + face
    Mesh2D m;
    const int a = m.addVertex({0, 0});
    const int b = m.addVertex({1, 0});
    const int c = m.addVertex({0, 1});
    CHECK(m.addFace({a, b, c}) == 0);
    CHECK(m.triangleCount() == 1);
    CHECK((int)m.edges().size() == 3);

    // Faces invalides
    CHECK(m.addFace({}) < 0);
    CHECK(m.addFace({0}) < 0);
    CHECK(m.addFace({0, 1}) < 0);
    CHECK(m.addFace({0, 0, 1}) < 0);       // doublon
    CHECK(m.addFace({0, 1, 99}) < 0);      // indice hors limites

    // Suppression de sommet (réindexation)
    const int d = m.addVertex({0.5f, 0.5f});
    CHECK((int)m.vertices.size() == 4);
    m.removeVertex(d);
    CHECK((int)m.vertices.size() == 3);
    CHECK((int)m.faces[0].verts.size() == 3);
    for (int v : m.faces[0].verts) CHECK(v >= 0 && v < 3);

    // Insertion sur une arête
    const int nv = m.insertVertexOnEdge(a, b, 0.5f);
    CHECK(nv == 3);
    CHECK((int)m.faces[0].verts.size() == 4);
    CHECK(m.faces[0].verts[1] == nv);  // boucle {a, nv, b, c}

    // Dissolution de l'arête (a, nv) → retour au triangle
    CHECK(m.dissolveEdge(a, nv));
    CHECK((int)m.faces[0].verts.size() == 3);
    CHECK((int)m.vertices.size() == 3);

    // Extrusion
    m.clear();
    const int e0 = m.addVertex({0, 0});
    const int e1 = m.addVertex({1, 0});
    const int e2 = m.addVertex({0, 1});
    m.addFace({e0, e1, e2});
    m.extrudeEdge(e0, e1, {0.0f, 0.5f});
    CHECK((int)m.vertices.size() == 5);
    CHECK((int)m.faces.size() == 2);
    CHECK((int)m.edges().size() == 6);

    // Triangulation du mesh complet (1 triangle + 1 quad = 3 triangles)
    CHECK(m.triangleCount() == 3);

    // addTriangulatedFace : quad → 2 faces triangles
    {
        Mesh2D q;
        const int q0 = q.addVertex({0, 0});
        const int q1 = q.addVertex({1, 0});
        const int q2 = q.addVertex({1, 1});
        const int q3 = q.addVertex({0, 1});
        CHECK(q.addTriangulatedFace({q0, q1, q2, q3}) == 2);
        CHECK((int)q.faces.size() == 2);
        CHECK(q.triangleCount() == 2);
        for (const Face& f : q.faces) CHECK((int)f.verts.size() == 3);
    }
    // addTriangulatedFace : hexagone → 4 faces triangles
    {
        Mesh2D h;
        std::vector<int> loop;
        for (int i = 0; i < 6; ++i) {
            const float a = (float)i * 2.0f * 3.14159265f / 6.0f;
            loop.push_back(h.addVertex({std::cos(a), std::sin(a)}));
        }
        CHECK(h.addTriangulatedFace(loop) == 4);
        CHECK(h.triangleCount() == 4);
    }
    // addTriangulatedFace : étoile concave → 8 faces triangles
    {
        Mesh2D s;
        std::vector<int> star;
        for (int i = 0; i < 10; ++i) {
            const float a = (float)i * 3.14159265f / 5.0f - 1.5708f;
            const float r = (i % 2 == 0) ? 1.0f : 0.4f;
            star.push_back(s.addVertex({std::cos(a) * r, std::sin(a) * r}));
        }
        CHECK(s.addTriangulatedFace(star) == 8);
        CHECK((int)s.faces.size() == 8);
        CHECK(s.triangleCount() == 8);
    }
    // addTriangulatedFace : entrées invalides ou dégénérées
    {
        Mesh2D m2;
        const int a = m2.addVertex({0, 0});
        const int b = m2.addVertex({1, 0});
        const int c = m2.addVertex({0, 1});
        CHECK(m2.addTriangulatedFace({}) < 0);
        CHECK(m2.addTriangulatedFace({a, b}) < 0);
        CHECK(m2.addTriangulatedFace({a, a, b}) < 0);          // doublon
        CHECK(m2.addTriangulatedFace({a, b, 99}) < 0);         // indice invalide
        CHECK(m2.addTriangulatedFace({a, b, c}) == 1);         // déjà un triangle
        CHECK((int)m2.faces.size() == 1);

        // Triangle dégénéré (aligné) : refusé, aucune face ajoutée.
        const int d = m2.addVertex({2, 0});
        CHECK(m2.addTriangulatedFace({a, b, d}) < 0);          // collinéaire
        CHECK((int)m2.faces.size() == 1);
        // Quadrilatère dégénéré (tous alignés) : refusé, aucune face ajoutée.
        const int e = m2.addVertex({3, 0});
        CHECK(m2.addTriangulatedFace({a, b, d, e}) < 0);
        CHECK((int)m2.faces.size() == 1);
    }

    // pointInPolygon
    std::vector<Vec2> poly = {{0, 0}, {2, 0}, {2, 2}, {0, 2}};
    CHECK(pointInPolygon({1, 1}, poly));
    CHECK(!pointInPolygon({3, 3}, poly));
    CHECK(pointInPolygon({2, 1}, poly));  // sur le bord

    // pointSegmentDistance
    CHECK(pointSegmentDistance({0.5f, 0.5f}, {0, 0}, {1, 0}) == 0.5f);

    // Fusion de points superposés (5.5 / 5.6)
    {
        Mesh2D m;
        const int a = m.addVertex({0, 0});
        const int b = m.addVertex({1, 0});
        const int c = m.addVertex({0, 1});
        m.addFace({a, b, c});
        const int c2 = m.addVertex({0, 1});  // doublon de c
        // Le plus petit indice est conservé, déplacé à la position moyenne.
        CHECK(m.mergeVertices({c2, c}, {0.0f, 1.0f}) == c);
        CHECK((int)m.vertices.size() == 3);
        CHECK((int)m.faces.size() == 1);
        CHECK(m.vertices[c].x == 0.0f && m.vertices[c].y == 1.0f);
        // La face référence toujours le sommet conservé.
        for (int v : m.faces[0].verts) CHECK(v != c2 && v >= 0 && v < 3);
    }
    // Fusion qui dégénère une face (boucle en double) → face supprimée
    {
        Mesh2D m;
        const int a = m.addVertex({0, 0});
        const int b = m.addVertex({1, 0});
        const int c = m.addVertex({1, 1});
        const int a2 = m.addVertex({0, 0});  // superposé à a, dans la boucle
        CHECK(m.addFace({a, b, c, a2}) == 0);
        CHECK(m.mergeVertices({a2, a}, {0.0f, 0.0f}) == a);
        CHECK((int)m.faces.size() == 0);
        CHECK((int)m.vertices.size() == 3);  // a, b, c
        CHECK(m.vertices[a].x == 0.0f && m.vertices[a].y == 0.0f);
    }
    // Entrées invalides ou triviales
    {
        Mesh2D m;
        const int a = m.addVertex({0, 0});
        CHECK(m.mergeVertices({}, {0, 0}) == -1);
        CHECK(m.mergeVertices({0, 5}, {0, 0}) == -1);  // indice hors limites
        CHECK(m.mergeVertices({a}, {1, 1}) == a);       // un seul : rien à faire
        CHECK((int)m.vertices.size() == 1);
    }
}

static void testSpecFormats() {
    std::printf("[formats spec (JSON / meshes / préférences)]\n");

    // JSON : aller-retour exact (maillage + vue + grille + couleurs)
    {
        SceneSnapshot snap;
        Mesh2D& m = snap.scene.activePlane();
        const int a = m.addVertex({-1, -1});
        const int b = m.addVertex({1, -1});
        const int c = m.addVertex({1, 1});
        const int d = m.addVertex({-1, 1});
        const int n0 = m.addTriangulatedFace({a, b, c, d});
        CHECK(n0 == 2);
        for (Face& f : m.faces) {
            f.color = {0.2f, 0.4f, 0.6f, 0.45f};
            f.hasColor = true;
        }
        snap.zoomMult = 2.5f;
        snap.cx = 3.0f;
        snap.cy = -4.0f;
        snap.grid = false;
        snap.gridStep = 0.5f;
        snap.name = "essai";

        const IoResult s = saveSceneJson(snap, "/tmp/meshtest_spec");
        CHECK(s.ok);
        SceneSnapshot back;
        const IoResult l = loadSceneJson(back, "/tmp/meshtest_spec");
        CHECK(l.ok);
        CHECK((int)back.scene.planes.size() == 1);
        CHECK((int)back.scene.planes[0].vertices.size() == 4);
        CHECK((int)back.scene.planes[0].faces.size() == 2);
        CHECK(back.scene.planes[0].faces[0].hasColor);
        CHECK(back.scene.planes[0].faces[0].color.r == 0.2f &&
              back.scene.planes[0].faces[0].color.a == 0.45f);
        CHECK(back.zoomMult == 2.5f && back.cx == 3.0f && back.cy == -4.0f);
        CHECK(!back.grid && back.gridStep == 0.5f);
        CHECK(back.name == "essai");
    }
    // JSON multi-plans : ordre d'empilement et plan actif conservés
    {
        SceneSnapshot snap;
        Mesh2D& p1 = snap.scene.activePlane();
        p1.addVertex({0, 0});
        p1.addVertex({1, 1});
        snap.scene.planes.emplace_back();
        Mesh2D& p2 = snap.scene.planes[1];
        p2.addVertex({5, 5});
        p2.addVertex({6, 6});
        snap.scene.active = 1;
        CHECK(saveSceneJson(snap, "/tmp/meshtest_planes").ok);
        SceneSnapshot back;
        CHECK(loadSceneJson(back, "/tmp/meshtest_planes").ok);
        CHECK((int)back.scene.planes.size() == 2);
        CHECK(back.scene.active == 1);
        CHECK((int)back.scene.planes[1].vertices.size() == 2);
        CHECK(back.scene.planes[1].vertices[1].x == 6.0f);
        // Ancien format « mesh » (repli) : toujours lisible
        CHECK(writeTestFile("/tmp/meshtest_legacy.json",
                            "{\"app\":\"meshes-designer\",\"mesh\":{\"verts\":[[0,0],[1,1]],\"faces\":[]}}")
                  .ok);
        SceneSnapshot legacy;
        CHECK(loadSceneJson(legacy, "/tmp/meshtest_legacy").ok);
        CHECK((int)legacy.scene.planes.size() == 1);
    }
    // JSON invalide : signalé sans modification
    {
        SceneSnapshot out;
        const IoResult r = loadSceneJson(out, "/tmp/meshtest_inexistant");
        CHECK(!r.ok);
    }

    // Format « meshes » : une ligne de triplets, déduplication, reliquat filtré
    {
        Scene sc;
        Mesh2D& m = sc.activePlane();
        const int a = m.addVertex({0, 0});
        const int b = m.addVertex({10, 0});
        const int c = m.addVertex({5, 8});
        m.addFace({a, b, c});
        const IoResult s = saveMeshesText(sc, "/tmp/meshtest.meshes");
        CHECK(s.ok);
        Scene back;
        const IoResult l = loadMeshesText(back, "/tmp/meshtest.meshes");
        CHECK(l.ok);
        CHECK((int)back.planes.size() == 1);
        CHECK((int)back.planes[0].vertices.size() == 3);
        CHECK((int)back.planes[0].faces.size() == 1);
        CHECK((int)back.planes[0].faces[0].verts.size() == 3);
    }
    // Format « meshes » : ligne avec doublon + reliquat, multi-lignes = multi-plans
    {
        IoResult r = writeTestFile("/tmp/meshtest2.meshes",
                                   "0,0;0,0;10,0;5,8;7,9\n1,1;2,2;3,3\n");
        CHECK(r.ok);
        Scene back;
        const IoResult l = loadMeshesText(back, "/tmp/meshtest2.meshes");
        CHECK(l.ok);
        CHECK((int)back.planes.size() == 2);   // une ligne = un plan
        CHECK((int)back.planes[0].vertices.size() == 3);  // 0,0 dédupliqué ; 7,9 filtré
        CHECK((int)back.planes[0].faces.size() == 1);     // un seul triplet complet
        CHECK((int)back.planes[1].faces.size() == 1);
        // Coordonnées mal formées → erreur
        CHECK(writeTestFile("/tmp/meshtest3.meshes", "0,0;abc;10,0;5,8\n").ok);
        const IoResult bad = loadMeshesText(back, "/tmp/meshtest3.meshes");
        CHECK(!bad.ok);
    }

    // Préférences : palette + emplacements + opacité
    {
        PrefsData p;
        p.palette = {rgba(1, 0, 0), rgba(0, 1, 0)};
        p.brushOpacity = 0.65f;
        p.circleSides = 12;
        p.edgePickTol = 14.0f;
        p.locations = {"sceneA", "sceneB"};
        CHECK(savePrefsJson(p, "/tmp/meshtest_prefs.json").ok);
        PrefsData back;
        CHECK(loadPrefsJson(back, "/tmp/meshtest_prefs.json").ok);
        CHECK((int)back.palette.size() == 2);
        CHECK(back.palette[0].r == 1.0f && back.palette[1].g == 1.0f);
        CHECK(back.brushOpacity == 0.65f);
        CHECK(back.circleSides == 12);
        CHECK(back.edgePickTol == 14.0f);
        CHECK(back.locations.size() == 2 && back.locations[1] == "sceneB");
    }

    // Autosave : scène multi-plans + undo/redo (scènes complètes)
    {
        SceneSnapshot snap;
        snap.scene.activePlane().addVertex({0, 0});
        snap.scene.activePlane().addVertex({1, 1});
        snap.scene.planes.emplace_back();
        snap.scene.planes[1].addVertex({9, 9});
        snap.scene.active = 1;
        std::vector<Scene> undo, redo;
        Scene u;
        u.activePlane().addVertex({5, 5});
        undo.push_back(u);
        Scene r;
        r.activePlane().addVertex({6, 6});
        redo.push_back(r);
        CHECK(saveAutoJson(snap, undo, redo, "/tmp/meshtest_auto.json").ok);
        SceneSnapshot back;
        std::vector<Scene> bu, br;
        CHECK(loadAutoJson(back, bu, br, "/tmp/meshtest_auto.json").ok);
        CHECK((int)back.scene.planes.size() == 2);
        CHECK(back.scene.active == 1);
        CHECK((int)back.scene.planes[0].vertices.size() == 2);
        CHECK((int)back.scene.planes[1].vertices.size() == 1);
        CHECK((int)bu.size() == 1 && (int)bu[0].planes[0].vertices.size() == 1);
        CHECK((int)br.size() == 1 && (int)br[0].planes[0].vertices.size() == 1);
    }

    // Préférences : mode « toutes couleurs » conservé
    {
        PrefsData p;
        p.allColors = true;
        CHECK(savePrefsJson(p, "/tmp/meshtest_prefs2.json").ok);
        PrefsData back;
        CHECK(loadPrefsJson(back, "/tmp/meshtest_prefs2.json").ok);
        CHECK(back.allColors);
    }
}

// Parse toutes les icônes du dossier assets/ : chaque SVG doit être accepté,
// rester dans son viewBox et produire au moins un élément dessinable.
static void testSVGIcons() {
    std::printf("[icônes SVG]\n");
    std::string dir;
    for (const char* d : {"assets", "../assets", "../../assets"}) {
        if (std::filesystem::is_directory(d)) {
            dir = d;
            break;
        }
    }
    CHECK(!dir.empty());
    if (dir.empty()) return;

    int n = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".svg") continue;
        ++n;
        std::ifstream f(entry.path());
        std::stringstream ss;
        ss << f.rdbuf();
        const svg::Icon icon = svg::parseSvg(ss.str());
        CHECK(icon.ok);
        if (!icon.ok) continue;

        const float x0 = icon.vbMinX, y0 = icon.vbMinY;
        const float x1 = icon.vbMinX + icon.vbW, y1 = icon.vbMinY + icon.vbH;
        const auto inBounds = [&](const svg::Pt& p) {
            return p.x >= x0 - 0.01f && p.x <= x1 + 0.01f && p.y >= y0 - 0.01f &&
                   p.y <= y1 + 0.01f;
        };
        CHECK(!icon.strokes.empty() || !icon.fillCircles.empty() ||
              !icon.fillRects.empty() || !icon.fillPolys.empty());
        for (const auto& st : icon.strokes) {
            CHECK(st.pts.size() >= 2);
            for (const svg::Pt& p : st.pts) CHECK(inBounds(p));
        }
        for (const auto& fc : icon.fillCircles) {
            CHECK(fc.r > 0.0f);
            CHECK(inBounds({fc.c.x - fc.r, fc.c.y - fc.r}));
            CHECK(inBounds({fc.c.x + fc.r, fc.c.y + fc.r}));
        }
        for (const auto& fr : icon.fillRects) {
            CHECK(inBounds(fr.min));
            CHECK(inBounds(fr.max));
        }
        for (const auto& fp : icon.fillPolys) {
            CHECK(fp.pts.size() >= 3);
            for (const svg::Pt& p : fp.pts) CHECK(inBounds(p));
        }
    }
    CHECK(n == 47);  // toutes les icônes du dossier assets/

    // Cas particuliers (mêmes attributs que les vraies icônes de assets/) :
    // undo contient un arc (échantillonné), l'anneau est composé de deux
    // cercles (2 contours fermés).
    const svg::Icon undo = svg::parseSvg(
        "<svg viewBox=\"0 0 16 16\" stroke=\"currentColor\" fill=\"none\">"
        "<path d=\"M3 8h8a3 3 0 0 1 0 6H7\"/></svg>");
    CHECK(undo.ok);
    CHECK(undo.strokes.size() == 1);
    if (undo.strokes.size() == 1)
        CHECK(undo.strokes[0].pts.size() > 20);  // M + h + arc(20) + H
    const svg::Icon ring = svg::parseSvg(
        "<svg viewBox=\"0 0 16 16\" stroke=\"currentColor\" fill=\"none\">"
        "<circle cx=\"8\" cy=\"8\" r=\"6\"/><circle cx=\"8\" cy=\"8\" "
        "r=\"2.5\"/></svg>");
    CHECK(ring.ok);
    CHECK(ring.strokes.size() == 2);
    if (ring.strokes.size() == 2)
        CHECK(ring.strokes[0].closed && ring.strokes[1].closed);
    const svg::Icon filled = svg::parseSvg(
        "<svg viewBox=\"0 0 16 16\" fill=\"currentColor\" stroke=\"none\">"
        "<circle cx=\"8\" cy=\"8\" r=\"1\"/><rect x=\"2\" y=\"2\" "
        "width=\"4\" height=\"3\"/></svg>");
    CHECK(filled.ok);
    CHECK(filled.fillCircles.size() == 1);
    CHECK(filled.fillRects.size() == 1);
    CHECK(filled.strokes.empty());

    // Contour de <rect> (cas de grid.svg) : stocké comme 4 coins distincts
    // marqués `closed` — le segment de fermeture (dernier → premier sommet)
    // est dessiné par le moteur de rendu (strokePolyline).
    const svg::Icon grid = svg::parseSvg(
        "<svg viewBox=\"0 0 16 16\" stroke=\"currentColor\" fill=\"none\">"
        "<rect x=\"2\" y=\"2\" width=\"12\" height=\"12\"/>"
        "<line x1=\"6\" y1=\"2\" x2=\"6\" y2=\"14\"/></svg>");
    CHECK(grid.ok);
    int closedRects = 0;
    for (const auto& st : grid.strokes) {
        if (!st.closed) continue;
        ++closedRects;
        CHECK(st.pts.size() == 4);
        if (st.pts.size() == 4) {
            // Dernier coin ≠ premier : la boucle n'est fermée que par le
            // segment de fermeture du rendu (premier et dernier ont la même
            // abscisse x = 2, ce qui déclencherait un faux positif).
            CHECK(st.pts[0].x == st.pts[3].x);
            CHECK(st.pts[0].y != st.pts[3].y);
        }
    }
    CHECK(closedRects == 1);
}

static void testRoundTrip() {
    std::printf("[sauvegarde/export]\n");
    Mesh2D m;
    const int a = m.addVertex({-1, -1});
    const int b = m.addVertex({1, -1});
    const int c = m.addVertex({1, 1});
    const int d = m.addVertex({-1, 1});
    m.addFace({a, b, c, d});

    // Sauvegarde + rechargement
    const IoResult s = saveNative(m, "/tmp/meshtest.mesh");
    CHECK(s.ok);
    Mesh2D m2;
    const IoResult l = loadNative(m2, "/tmp/meshtest.mesh");
    CHECK(l.ok);
    CHECK((int)m2.vertices.size() == 4);
    CHECK((int)m2.faces.size() == 1);
    CHECK(m2.vertices[2].x == 1.0f && m2.vertices[2].y == 1.0f);

    // Exports
    const IoResult o = exportOBJ(m, "/tmp/meshtest.obj");
    CHECK(o.ok);
    const IoResult q = exportQB64(m, "/tmp/meshtest.txt");
    CHECK(q.ok);

    // Chargement d'un fichier invalide
    Mesh2D m3;
    const IoResult bad = loadNative(m3, "/tmp/fichier-inexistant.mesh");
    CHECK(!bad.ok);
}

int main() {
    testTriangulation();
    testMeshOps();
    testRoundTrip();
    testSpecFormats();
    testSVGIcons();

    std::printf("\nRésultat : %d/%d vérifications OK\n", g_checks - g_failures, g_checks);
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
