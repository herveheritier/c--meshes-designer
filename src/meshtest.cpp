// Tests headless des opérations mesh et de la triangulation.
// Compilation : cible `meshtest` du CMakeLists.
#include "io.h"
#include "mesh.h"
#include "pngexport.h"
#include "svgparse.h"
#include "triangulate.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
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
    // Octogone régulier (convexe) : la « meilleure oreille » répartit les
    // triangles — AUCUN sommet ne doit apparaître dans tous les triangles
    // (l'ancien éventail de l'outil polygone faisait partager le sommet 0 par
    // tous). L'aire totale est conservée.
    {
        std::vector<Vec2> oct;
        for (int i = 0; i < 8; ++i) {
            const float a = (float)i * 2.0f * 3.14159265f / 8.0f;
            oct.push_back({std::cos(a), std::sin(a)});
        }
        std::vector<int> t;
        CHECK(triangulatePolygon(oct, t));
        CHECK((int)t.size() == 18);  // 6 triangles
        std::vector<int> freq(8, 0);
        double area = 0.0;
        for (size_t i = 0; i + 2 < t.size(); i += 3) {
            ++freq[t[i]];
            ++freq[t[i + 1]];
            ++freq[t[i + 2]];
            const Vec2& a = oct[t[i]];
            const Vec2& b = oct[t[i + 1]];
            const Vec2& c = oct[t[i + 2]];
            area += std::fabs(cross(b - a, c - a)) * 0.5;
        }
        const int maxFreq = *std::max_element(freq.begin(), freq.end());
        CHECK(maxFreq < 6);         // pas d'éventail : aucun sommet dans les 6 triangles
        CHECK(maxFreq <= 3);        // bien réparti (équilatéralité maximale)
        CHECK(std::fabs(area - (2.0 * std::sqrt(2.0))) < 1e-3);  // aire octogone régulier
    }
    // Rectangle très allongé (irrégulier) : la « meilleure oreille » reste
    // propre — aucun triangle d'aire nulle, aire totale conservée (le départage
    // « milieu de boucle » ne doit pas dégrader les formes non symétriques).
    {
        std::vector<Vec2> rect = {{-6, -1}, {6, -1}, {6, 1}, {-6, 1}};
        std::vector<int> t;
        CHECK(triangulatePolygon(rect, t));
        CHECK((int)t.size() == 6);  // 2 triangles
        double area = 0.0;
        double minArea = 1e18;
        for (size_t i = 0; i + 2 < t.size(); i += 3) {
            const Vec2& a = rect[t[i]];
            const Vec2& b = rect[t[i + 1]];
            const Vec2& c = rect[t[i + 2]];
            const double a2 = std::fabs(cross(b - a, c - a)) * 0.5;
            area += a2;
            minArea = std::min(minArea, a2);
        }
        CHECK(std::fabs(area - 24.0) < 1e-3);   // 12 × 2
        CHECK(minArea > 1e-3);                   // aucun triangle dégénéré
    }
    // pointInTriangle
    CHECK(pointInTriangle({0.25f, 0.25f}, {0, 0}, {1, 0}, {0, 1}));
    CHECK(!pointInTriangle({1, 1}, {0, 0}, {1, 0}, {0, 1}));
    CHECK(pointInTriangle({0.5f, 0.0f}, {0, 0}, {1, 0}, {0, 1}));  // sur le bord
}

// Triangulation de la bande d'une couronne (triangulateBand) : le résultat
// doit être un anneau valide — exactement n+m triangles, aucun croisement ni
// chevauchement entre paires, tous orientés dans le même sens, couverture
// totale de la bande (somme des aires) et trou jamais rempli. Régression : la
// couture à 0° reliait au dernier sommet (O(n-1)/I(m-1)) au lieu du connecteur
// courant (O(0)/I(0)) : les deux triangles de fermeture se chevauchaient.
static void testCrownBand() {
    std::printf("[couronne (triangulateBand)]\n");

    const auto rim = [](int n, float r) {
        std::vector<Vec2> pts;
        pts.reserve((size_t)n);
        for (int i = 0; i < n; ++i) {
            const float a = (float)i * 2.0f * 3.14159265f / (float)n;
            pts.push_back({std::cos(a) * r, std::sin(a) * r});
        }
        return pts;
    };
    const auto area2 = [](const Vec2& a, const Vec2& b, const Vec2& c) {
        return cross(b - a, c - a);
    };
    // Croisement propre de deux segments (points d'intersection intérieurs).
    // Les produits vectoriels doivent dépasser un epsilon : en Release, la
    // contraction FMA donne de minuscules valeurs non nulles aux arêtes qui ne
    // font que partager un sommet (faux positifs sinon).
    const auto segCross = [](const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& d) {
        const float eps = 1e-4f;
        const float d1 = cross(d - c, a - c);
        const float d2 = cross(d - c, b - c);
        const float d3 = cross(b - a, c - a);
        const float d4 = cross(b - a, d - a);
        const auto side = [eps](float v) { return v > eps ? 1 : (v < -eps ? -1 : 0); };
        return side(d1) * side(d2) < 0 && side(d3) * side(d4) < 0;
    };
    // Sommet strictement à l'intérieur d'un triangle (chevauchement).
    const auto inTri = [&](const Vec2& p, const Vec2& a, const Vec2& b, const Vec2& c) {
        const float eps = 1e-4f;
        const float d1 = area2(a, b, p);
        const float d2 = area2(b, c, p);
        const float d3 = area2(c, a, p);
        return (d1 > eps && d2 > eps && d3 > eps) || (d1 < -eps && d2 < -eps && d3 < -eps);
    };

    // Cas pathologique : couronne à 3 côtés extérieurs dont des sommets
    // intérieurs tombent exactement sur les cordes extérieures (m pair, trou à
    // mi-rayon) — la bande y a une largeur nulle et le zipper émet des
    // triangles d'aire nulle (jusqu'à 3 : 60°, 180°, 300°). Ces triangles ne
    // se voient pas ; on les tolère (non inversés), mais ils sont exclus des
    // contrôles de croisement (bruit flottant des arêtes colinéaires).
    const float kEpsDeg = 1e-5f * 4.0f;  // ~ seuil d'aire (x2) d'un triangle pincé
    for (int n : {3, 5, 8, 16, 32, 64}) {
        for (int m : {3, 5, 8, 16, 64}) {
            const std::vector<Vec2> outer = rim(n, 2.0f);
            const std::vector<Vec2> inner = rim(m, 1.0f);
            std::vector<int> band;
            triangulateBand(outer, inner, band);
            CHECK((int)band.size() == (n + m) * 6);
            if ((int)band.size() != (n + m) * 6) continue;

            std::vector<std::vector<Vec2>> tris;
            tris.reserve((size_t)n + m);
            std::vector<float> areas;
            areas.reserve((size_t)n + m);
            const auto pt = [&](int ring, int idx) -> const Vec2& {
                return ring == 0 ? outer[idx] : inner[idx];
            };
            for (size_t k = 0; k + 5 < band.size(); k += 6) {
                const Vec2 a = pt(band[k], band[k + 1]);
                const Vec2 b = pt(band[k + 2], band[k + 3]);
                const Vec2 c = pt(band[k + 4], band[k + 5]);
                tris.push_back({a, b, c});
                areas.push_back(area2(a, b, c));
            }

            // Orientation : jamais de face franchement inversée ; au plus 3
            // triangles pincés (aire ~ nulle) dans le cas pathologique.
            int pos = 0, neg = 0, deg = 0;
            for (float s : areas) {
                if (std::fabs(s) <= kEpsDeg) ++deg;
                else if (s > 0) ++pos;
                else ++neg;
            }
            CHECK(neg == 0);
            CHECK(deg <= 3);
            CHECK(pos > 0);

            // Aucune paire de triangles (non dégénérés) ne se croise ni ne se
            // chevauche.
            for (size_t t1 = 0; t1 < tris.size(); ++t1) {
                if (std::fabs(areas[t1]) <= kEpsDeg) continue;
                for (size_t t2 = t1 + 1; t2 < tris.size(); ++t2) {
                    if (std::fabs(areas[t2]) <= kEpsDeg) continue;
                    const Vec2& a = tris[t1][0];
                    const Vec2& b = tris[t1][1];
                    const Vec2& c = tris[t1][2];
                    const Vec2& p = tris[t2][0];
                    const Vec2& q = tris[t2][1];
                    const Vec2& r = tris[t2][2];
                    const Vec2 e1[3][2] = {{a, b}, {b, c}, {c, a}};
                    const Vec2 e2[3][2] = {{p, q}, {q, r}, {r, p}};
                    for (int i = 0; i < 3; ++i)
                        for (int j = 0; j < 3; ++j)
                            CHECK(!segCross(e1[i][0], e1[i][1], e2[j][0], e2[j][1]));
                    CHECK(!(inTri(p, a, b, c) || inTri(q, a, b, c) || inTri(r, a, b, c)));
                    CHECK(!(inTri(a, p, q, r) || inTri(b, p, q, r) || inTri(c, p, q, r)));
                }
            }

            // Couverture complète de la bande (somme des aires) : ni trou
            // rempli, ni zone manquante.
            float outerArea = 0.0f;
            for (int i = 0; i < n; ++i) outerArea += cross(outer[i], outer[(i + 1) % n]);
            float innerArea = 0.0f;
            for (int i = 0; i < m; ++i) innerArea += cross(inner[i], inner[(i + 1) % m]);
            const float bandArea = std::fabs(outerArea - innerArea) * 0.5f;
            float triArea = 0.0f;
            for (size_t i = 0; i < tris.size(); ++i)
                triArea += std::fabs(areas[i]) * 0.5f;
            CHECK(std::fabs(triArea - bandArea) < 1e-3f * std::max(1.0f, bandArea));
        }
    }

    // Boucles trop petites : rien n'est émis.
    {
        std::vector<int> band;
        triangulateBand({{0, 0}, {1, 0}}, {{0, 0}, {1, 0}}, band);
        CHECK(band.empty());
    }
}

// Découpe de formes : soustraction de polygones (subtractPolygon) et
// triangulation avec trous (triangulatePolygonHoles) — le résultat doit
// couvrir exactement le sujet moins la découpe : somme des aires attendue,
// aucun croisement ni chevauchement entre triangles.
static void testCutPolygons() {
    std::printf("[découpe de formes]\n");

    const auto area2 = [](const Vec2& a, const Vec2& b, const Vec2& c) {
        return cross(b - a, c - a);
    };
    // Croisement propre (points intérieurs) — même convention que l'anneau.
    const auto segCross = [](const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& d) {
        const float eps = 1e-4f;
        const float d1 = cross(d - c, a - c);
        const float d2 = cross(d - c, b - c);
        const float d3 = cross(b - a, c - a);
        const float d4 = cross(b - a, d - a);
        const auto side = [eps](float v) { return v > eps ? 1 : (v < -eps ? -1 : 0); };
        return side(d1) * side(d2) < 0 && side(d3) * side(d4) < 0;
    };
    // Sommet strictement à l'intérieur d'un triangle (chevauchement).
    const auto inTri = [&](const Vec2& p, const Vec2& a, const Vec2& b, const Vec2& c) {
        const float eps = 1e-4f;
        const float d1 = area2(a, b, p);
        const float d2 = area2(b, c, p);
        const float d3 = area2(c, a, p);
        return (d1 > eps && d2 > eps && d3 > eps) || (d1 < -eps && d2 < -eps && d3 < -eps);
    };
    // Vérifie une triangulation : pas de croisement ni de chevauchement entre
    // triangles, et aire totale égale à `expected`.
    const auto checkTris = [&](const std::vector<Vec2>& pts, const std::vector<int>& tris,
                               double expected) {
        CHECK((int)tris.size() % 3 == 0);
        const int nt = (int)tris.size() / 3;
        std::vector<std::vector<Vec2>> T;
        std::vector<double> areas;
        for (int i = 0; i < nt; ++i) {
            const Vec2 a = pts[tris[i * 3]];
            const Vec2 b = pts[tris[i * 3 + 1]];
            const Vec2 c = pts[tris[i * 3 + 2]];
            T.push_back({a, b, c});
            areas.push_back(std::fabs(area2(a, b, c)) * 0.5);
        }
        for (int t1 = 0; t1 < nt; ++t1) {
            for (int t2 = t1 + 1; t2 < nt; ++t2) {
                const Vec2& a = T[t1][0];
                const Vec2& b = T[t1][1];
                const Vec2& c = T[t1][2];
                const Vec2& p = T[t2][0];
                const Vec2& q = T[t2][1];
                const Vec2& r = T[t2][2];
                const Vec2 e1[3][2] = {{a, b}, {b, c}, {c, a}};
                const Vec2 e2[3][2] = {{p, q}, {q, r}, {r, p}};
                for (int i = 0; i < 3; ++i)
                    for (int j = 0; j < 3; ++j)
                        CHECK(!segCross(e1[i][0], e1[i][1], e2[j][0], e2[j][1]));
                CHECK(!(inTri(p, a, b, c) || inTri(q, a, b, c) || inTri(r, a, b, c)));
                CHECK(!(inTri(a, p, q, r) || inTri(b, p, q, r) || inTri(c, p, q, r)));
            }
        }
        double sum = 0.0;
        for (double a : areas) sum += a;
        CHECK(std::fabs(sum - expected) < 1e-3);
    };

    // Anneau : carré 4×4 percé d'un carré 2×2 (aire restante 12).
    {
        std::vector<Vec2> pts;
        std::vector<int> tris;
        CHECK(subtractPolygon({{0, 0}, {4, 0}, {4, 4}, {0, 4}}, {{1, 1}, {3, 1}, {3, 3}, {1, 3}},
                              pts, tris));
        CHECK(!tris.empty());
        checkTris(pts, tris, 12.0);
    }

    // Découpe qui traverse un triangle (partiel) : le triangle d'aire 8 moins
    // le carré qui le chevauche (intersection d'aire 3,5) → il reste 4,5.
    {
        std::vector<Vec2> pts;
        std::vector<int> tris;
        CHECK(subtractPolygon({{0, 0}, {4, 0}, {0, 4}}, {{1, -1}, {3, -1}, {3, 2}, {1, 2}},
                              pts, tris));
        checkTris(pts, tris, 4.5);
    }

    // Aucune intersection : la découpe ne touche pas le sujet.
    {
        std::vector<Vec2> pts;
        std::vector<int> tris;
        CHECK(!subtractPolygon({{0, 0}, {1, 0}, {0, 1}}, {{5, 5}, {6, 5}, {6, 6}, {5, 6}},
                               pts, tris));
        CHECK(tris.empty());
    }

    // Sujet entièrement recouvert : il disparaît (résultat vide).
    {
        std::vector<Vec2> pts;
        std::vector<int> tris;
        CHECK(subtractPolygon({{0, 0}, {1, 0}, {0, 1}}, {{-1, -1}, {2, -1}, {2, 2}, {-1, 2}},
                              pts, tris));
        CHECK(tris.empty());
    }

    // Entaille de coin débordant du sujet : le carré 8×8 moins son coin
    // 5×5 (la découpe dépasse du bord) doit donner le « L » d'aire 39.
    // Régression : les arêtes internes partagées par les triangles de la
    // découpe n'étaient pas annulées et le tracé des boucles se perdait — le
    // résultat était VIDé.
    {
        std::vector<Vec2> pts;
        std::vector<int> tris;
        CHECK(subtractPolygon({{0, 0}, {8, 0}, {8, 8}, {0, 8}},
                              {{-1, -1}, {5, -1}, {5, 5}, {-1, 5}}, pts, tris));
        checkTris(pts, tris, 39.0);
    }
    // Entaille depuis une arête (la découpe partage le bord du sujet) : aire 56.
    {
        std::vector<Vec2> pts;
        std::vector<int> tris;
        CHECK(subtractPolygon({{0, 0}, {8, 0}, {8, 8}, {0, 8}},
                              {{3, 0}, {5, 0}, {5, 4}, {3, 4}}, pts, tris));
        checkTris(pts, tris, 56.0);
    }
    // Anneau 8×8 percé d'un carré 4×4 centré : triangles répartis (aucun
    // sommet dans tous les triangles) — la « meilleure oreille » s'applique
    // aussi aux découpes, pas seulement à l'outil polygone.
    {
        std::vector<Vec2> pts;
        std::vector<int> tris;
        CHECK(triangulatePolygonHoles({{0, 0}, {8, 0}, {8, 8}, {0, 8}},
                                      {{{2, 2}, {6, 2}, {6, 6}, {2, 6}}}, pts, tris));
        checkTris(pts, tris, 48.0);
        std::vector<int> freq(pts.size(), 0);
        for (int v : tris) ++freq[v];
        const int maxFreq = *std::max_element(freq.begin(), freq.end());
        CHECK(maxFreq <= 4);  // 8 triangles sur 8 sommets : ~3 en moyenne, jamais tous
    }

    // Triangulation directe avec trous.
    {
        std::vector<Vec2> pts;
        std::vector<int> tris;
        CHECK(triangulatePolygonHoles({{0, 0}, {4, 0}, {4, 4}, {0, 4}},
                                      {{{1, 1}, {3, 1}, {3, 3}, {1, 3}}}, pts, tris));
        checkTris(pts, tris, 12.0);
    }

    // Découpe d'un plan entier (Mesh2D::cutPolygon) : la face carrée devient
    // un anneau, la face distante reste intacte, la couleur est conservée, et
    // une découpe sans intersection ne change rien.
    {
        Mesh2D m;
        const int a = m.addVertex({0, 0});
        const int b = m.addVertex({4, 0});
        const int c = m.addVertex({4, 4});
        const int d = m.addVertex({0, 4});
        const int q = m.addFace({a, b, c, d});  // grand carré
        m.faces[q].color = {0.2f, 0.5f, 0.9f, 0.6f};
        m.faces[q].hasColor = true;
        const int e = m.addVertex({10, 0});
        const int f = m.addVertex({11, 0});
        const int g = m.addVertex({10, 1});
        m.addFace({e, f, g});  // petit triangle loin de la découpe (aire 0,5)

        CHECK(m.cutPolygon({{1, 1}, {3, 1}, {3, 3}, {1, 3}}));
        // Aire totale : 12 (anneau) + 0,5 (triangle intact) = 12,5.
        std::vector<int> tris;
        m.triangulated(tris);
        double area = 0.0;
        for (size_t i = 0; i + 2 < tris.size(); i += 3) {
            const Vec2& x = m.vertices[tris[i]];
            const Vec2& y = m.vertices[tris[i + 1]];
            const Vec2& z = m.vertices[tris[i + 2]];
            area += std::fabs(area2(x, y, z)) * 0.5;
        }
        CHECK(std::fabs(area - 12.5) < 1e-3);
        // La couleur de la face découpée est conservée sur les nouveaux triangles.
        bool hasColor = false;
        for (const Face& fc : m.faces)
            if (fc.hasColor && fc.color.r == 0.2f) hasColor = true;
        CHECK(hasColor);

        // Une découpe sans intersection ne modifie pas le plan.
        Mesh2D m2 = m;
        CHECK(!m2.cutPolygon({{50, 50}, {51, 50}, {51, 51}, {50, 51}}));
        CHECK((int)m2.faces.size() == (int)m.faces.size());
        CHECK((int)m2.vertices.size() == (int)m.vertices.size());
    }

    // Découpe de coin DÉBORDANT de la face (chemin réel de l'outil découpe) :
    // le carré 8×8 moins son coin 5×5 (le coin découpé sort de la face) doit
    // laisser un « L » d'aire 39, avec la couleur conservée.
    {
        Mesh2D m;
        const int a = m.addVertex({0, 0});
        const int b = m.addVertex({8, 0});
        const int c = m.addVertex({8, 8});
        const int d = m.addVertex({0, 8});
        const int q = m.addFace({a, b, c, d});
        m.faces[q].color = {0.2f, 0.5f, 0.9f, 0.6f};
        m.faces[q].hasColor = true;
        CHECK(m.cutPolygon({{-1, -1}, {5, -1}, {5, 5}, {-1, 5}}));
        std::vector<int> tris;
        m.triangulated(tris);
        double area = 0.0;
        for (size_t i = 0; i + 2 < tris.size(); i += 3) {
            const Vec2& x = m.vertices[tris[i]];
            const Vec2& y = m.vertices[tris[i + 1]];
            const Vec2& z = m.vertices[tris[i + 2]];
            area += std::fabs(area2(x, y, z)) * 0.5;
        }
        CHECK(std::fabs(area - 39.0) < 1e-3);
        // La couleur de la face découpée est conservée sur les nouveaux triangles.
        bool hasColor = false;
        for (const Face& fc : m.faces)
            if (fc.hasColor && fc.color.r == 0.2f) hasColor = true;
        CHECK(hasColor);
    }
}

// Résultat d'une découpe entièrement triangulé : chaque pièce issue de la
// soustraction est un triangle (les arêtes internes de la triangulation sont
// conservées, aucun réassemblage) — une entaille devient un assemblage de
// triangles, un trou n'est jamais comblé, les faces non touchées restent
// intactes et la couleur de la face découpée est conservée.
static void testCutTriangulated() {
    std::printf("[découpe triangulée]\n");

    const auto faceArea = [](const Mesh2D& m, const Face& f) {
        double s = 0;
        for (size_t i = 0; i < f.verts.size(); ++i) {
            const Vec2& a = m.vertices[(size_t)f.verts[i]];
            const Vec2& b = m.vertices[(size_t)f.verts[(i + 1) % f.verts.size()]];
            s += (double)a.x * b.y - (double)a.y * b.x;
        }
        return std::fabs(s) * 0.5;
    };
    const auto totalArea = [&](const Mesh2D& m) {
        double s = 0;
        for (const Face& f : m.faces) s += faceArea(m, f);
        return s;
    };
    const auto square = [](Mesh2D& m, float x0, float y0, float x1, float y1) {
        const int a = m.addVertex({x0, y0});
        const int b = m.addVertex({x1, y0});
        const int c = m.addVertex({x1, y1});
        const int d = m.addVertex({x0, y1});
        return m.addFace({a, b, c, d});
    };
    const auto allTriangles = [](const Mesh2D& m) {
        for (const Face& f : m.faces)
            if ((int)f.verts.size() != 3) return false;
        return true;
    };

    // Entaille de coin : le « L » (6 sommets) devient 4 triangles, aire 39.
    {
        Mesh2D m;
        square(m, 0, 0, 8, 8);
        CHECK(m.cutPolygon({{-1, -1}, {5, -1}, {5, 5}, {-1, 5}}));
        CHECK(allTriangles(m));
        CHECK((int)m.faces.size() == 4);  // L à 6 sommets → 4 triangles
        CHECK(std::fabs(totalArea(m) - 39.0) < 1e-3);
    }
    // Entaille depuis une arête : l'octogone (8 sommets) devient 6 triangles.
    {
        Mesh2D m;
        square(m, 0, 0, 8, 8);
        CHECK(m.cutPolygon({{3, 0}, {5, 0}, {5, 4}, {3, 4}}));
        CHECK(allTriangles(m));
        CHECK((int)m.faces.size() == 6);  // octogone à 8 sommets → 6 triangles
        CHECK(std::fabs(totalArea(m) - 56.0) < 1e-3);
    }
    // Trou intérieur : anneau de 8 triangles, le trou n'est JAMAIS comblé —
    // aucun triangle n'englobe un point du trou.
    {
        Mesh2D m;
        square(m, 0, 0, 8, 8);
        CHECK(m.cutPolygon({{2, 2}, {6, 2}, {6, 6}, {2, 6}}));
        CHECK(allTriangles(m));
        CHECK((int)m.faces.size() == 8);  // 4 sommets ext. + 4 int. → 8 triangles
        for (const Face& f : m.faces) {
            std::vector<Vec2> pts;
            pts.reserve(f.verts.size());
            for (int v : f.verts) pts.push_back(m.vertices[(size_t)v]);
            CHECK(m.validFace(f.verts));
            // Centre du trou : jamais englobé (un triangle ne peut couvrir une
            // zone découpée — les coins du trou restent sur la frontière des
            // triangles de l'anneau, où pointInPolygon répond « bord »).
            CHECK(!pointInPolygon({4, 4}, pts));
        }
        CHECK(std::fabs(totalArea(m) - 48.0) < 1e-3);
    }
    // Découpe qui sépare une face en deux morceaux disjoints : chaque moitié
    // est triangulée (2 rectangles → 4 triangles), aire conservée.
    {
        Mesh2D m;
        square(m, 0, 0, 8, 8);
        CHECK(m.cutPolygon({{3, -1}, {5, -1}, {5, 9}, {3, 9}}));
        CHECK(allTriangles(m));
        CHECK((int)m.faces.size() == 4);  // 2 rectangles → 4 triangles
        CHECK(std::fabs(totalArea(m) - 48.0) < 1e-3);
    }
    // Couleur conservée : toutes les pièces de la face découpée gardent la
    // couleur d'origine (aucune perte pendant la triangulation).
    {
        Mesh2D m;
        const int a = m.addVertex({0, 0});
        const int b = m.addVertex({4, 0});
        const int c = m.addVertex({4, 4});
        const int d = m.addVertex({0, 4});
        const int q = m.addFace({a, b, c, d});
        m.faces[(size_t)q].color = {0.2f, 0.5f, 0.9f, 0.6f};
        m.faces[(size_t)q].hasColor = true;
        CHECK(m.cutPolygon({{1, 1}, {3, 1}, {3, 3}, {1, 3}}));  // trou dans la face
        CHECK(allTriangles(m));
        for (const Face& fc : m.faces)
            CHECK(fc.hasColor && fc.color.r == 0.2f);  // la couleur survit à la découpe
        CHECK(std::fabs(totalArea(m) - 12.0) < 1e-3);
    }
    // Deux faces voisines de couleurs différentes : seule la face découpée
    // produit des triangles ; la voisine intacte reste un quad (une face non
    // touchée n'est jamais triangulée d'office), et aucune face ne mélange
    // les couleurs.
    {
        Mesh2D m;
        const int a = m.addVertex({0, 0});
        const int b = m.addVertex({4, 0});
        const int c = m.addVertex({4, 4});
        const int d = m.addVertex({0, 4});
        const int q = m.addFace({a, b, c, d});
        m.faces[(size_t)q].color = {1.0f, 0.0f, 0.0f, 1.0f};
        m.faces[(size_t)q].hasColor = true;
        const int e = m.addVertex({8, 0});
        const int f = m.addVertex({8, 4});
        const int q2 = m.addFace({b, e, f, c});  // partage l'arête b-c avec la face A
        m.faces[(size_t)q2].color = {0.0f, 0.0f, 1.0f, 1.0f};
        m.faces[(size_t)q2].hasColor = true;
        CHECK(m.cutPolygon({{1, 1}, {3, 1}, {3, 3}, {1, 3}}));  // trou dans la face A
        int blue = 0;
        for (const Face& fc : m.faces) {
            const bool red = fc.hasColor && fc.color.r == 1.0f;
            const bool bl = fc.hasColor && fc.color.b == 1.0f;
            CHECK(red != bl);  // rouge ou bleu, jamais les deux
            if (bl) ++blue;
        }
        CHECK(blue == 1);  // la face bleue voisine reste unique
        for (const Face& fc : m.faces) {
            if (fc.hasColor && fc.color.b == 1.0f)
                CHECK((int)fc.verts.size() == 4);  // intacte : quad conservé
            else
                CHECK((int)fc.verts.size() == 3);  // découpée : triangles
        }
        CHECK(std::fabs(totalArea(m) - 28.0) < 1e-3);  // 16 + 16 − 4
    }
    // Découpe à cheval sur la couture de deux faces de MÊME couleur : toutes
    // les pièces sont des triangles et aucune face ne contient des sommets
    // des deux côtés de la couture x = 4 (les pièces de chaque face restent
    // séparées).
    {
        Mesh2D m;
        const int a = m.addVertex({0, 0});
        const int b = m.addVertex({4, 0});
        const int c = m.addVertex({4, 4});
        const int d = m.addVertex({0, 4});
        const int q = m.addFace({a, b, c, d});
        const int e = m.addVertex({8, 0});
        const int f = m.addVertex({8, 4});
        const int q2 = m.addFace({b, e, f, c});  // partage l'arête b-c, même couleur
        for (const int fi : {q, q2}) {
            m.faces[(size_t)fi].color = {0.9f, 0.2f, 0.2f, 1.0f};
            m.faces[(size_t)fi].hasColor = true;
        }
        CHECK(m.cutPolygon({{2, 2}, {6, 2}, {6, 6}, {2, 6}}));  // trou à cheval
        CHECK(allTriangles(m));
        CHECK((int)m.faces.size() >= 2);  // jamais une seule face traversante
        for (const Face& fc : m.faces) {
            // Aucune face ne chevauche la couture : sommets d'un seul côté
            // (x ≤ 4 ou x ≥ 4, avec une tolérance pour les points de coupe).
            bool left = false, right = false;
            for (int v : fc.verts) {
                const float x = m.vertices[(size_t)v].x;
                if (x < 4.0f - 1e-3f) left = true;
                if (x > 4.0f + 1e-3f) right = true;
            }
            CHECK(!(left && right));
        }
        CHECK(std::fabs(totalArea(m) - 24.0) < 1e-3);  // 16 + 16 − 8 (moitié du trou par face)
    }
    // Aucun triangle dégénéré dans le résultat (chemin rendu / export).
    {
        Mesh2D m;
        square(m, 0, 0, 8, 8);
        CHECK(m.cutPolygon({{-1, -1}, {5, -1}, {5, 5}, {-1, 5}}));
        for (const Face& f : m.faces) {
            CHECK((int)f.verts.size() == 3);
            const Vec2& a = m.vertices[(size_t)f.verts[0]];
            const Vec2& b = m.vertices[(size_t)f.verts[1]];
            const Vec2& c = m.vertices[(size_t)f.verts[2]];
            CHECK(std::fabs(cross(b - a, c - a)) > 1e-7);  // pas de triangle dégénéré
        }
    }
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
    // JSON multi-plans : ordre d'empilement, plan actif et opacité par plan
    // (7.8) conservés ; opacité absente = 1.0 (compatibilité ascendante).
    {
        SceneSnapshot snap;
        Mesh2D& p1 = snap.scene.activePlane();
        p1.addVertex({0, 0});
        p1.addVertex({1, 1});
        p1.opacity = 0.35f;
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
        CHECK(back.scene.planes[0].opacity == 0.35f);
        CHECK(back.scene.planes[1].opacity == 1.0f);
        // Fichier ancien (sans « opacity ») : chaque plan charge à 1.0.
        CHECK(writeTestFile(
                  "/tmp/meshtest_planes_legacy.json",
                  "{\"mesh\":{\"verts\":[[0,0],[1,1]],\"faces\":[],\"opacity\":0.7}}")
                  .ok);
        SceneSnapshot leg;
        CHECK(loadSceneJson(leg, "/tmp/meshtest_planes_legacy").ok);
        CHECK(leg.scene.planes[0].opacity == 0.7f);
        // Ancien format « mesh » (repli) : toujours lisible
        CHECK(writeTestFile("/tmp/meshtest_legacy.json",
                            "{\"app\":\"meshes-designer\",\"mesh\":{\"verts\":[[0,0],[1,1]],\"faces\":[]}}")
                  .ok);
        SceneSnapshot legacy;
        CHECK(loadSceneJson(legacy, "/tmp/meshtest_legacy").ok);
        CHECK((int)legacy.scene.planes.size() == 1);
    }
    // Calque d'image (7.7) : persisté avec la scène (JSON) — toutes les
    // dimensions de l'état, et compatibilité ascendante (absent = aucun calque).
    {
        SceneSnapshot snap;
        snap.scene.activePlane().addVertex({0, 0});
        snap.scene.image.path = "ref.png";
        snap.scene.image.visible = false;
        snap.scene.image.center = {3.5f, -2.0f};
        snap.scene.image.rotation = 1.2f;
        snap.scene.image.scaleX = 0.25f;
        snap.scene.image.scaleY = 0.5f;
        snap.scene.image.opacity = 0.6f;
        snap.scene.image.w = 640;
        snap.scene.image.h = 480;
        CHECK(saveSceneJson(snap, "/tmp/meshtest_layer").ok);
        SceneSnapshot back;
        CHECK(loadSceneJson(back, "/tmp/meshtest_layer").ok);
        CHECK(back.scene.image.path == "ref.png");
        CHECK(!back.scene.image.visible);
        CHECK(back.scene.image.center.x == 3.5f && back.scene.image.center.y == -2.0f);
        CHECK(back.scene.image.rotation == 1.2f);
        CHECK(back.scene.image.scaleX == 0.25f && back.scene.image.scaleY == 0.5f);
        CHECK(back.scene.image.opacity == 0.6f);
        CHECK(back.scene.image.w == 640 && back.scene.image.h == 480);
        // Scène sans champ « image » (ancien fichier) : aucun calque.
        CHECK(writeTestFile("/tmp/meshtest_layer_legacy.json",
                            "{\"mesh\":{\"verts\":[[0,0]],\"faces\":[]}}")
                  .ok);
        SceneSnapshot leg;
        CHECK(loadSceneJson(leg, "/tmp/meshtest_layer_legacy").ok);
        CHECK(leg.scene.image.path.empty());
        CHECK(leg.scene.image.visible);
        // Autosave : l'état du calque accompagne aussi les scènes d'historique.
        Scene u;
        u.activePlane().addVertex({5, 5});
        u.image.path = "hist.png";
        std::vector<Scene> undo{u}, redo;
        CHECK(saveAutoJson(snap, undo, redo, "/tmp/meshtest_layer_auto.json").ok);
        SceneSnapshot ab;
        std::vector<Scene> bu, br;
        CHECK(loadAutoJson(ab, bu, br, "/tmp/meshtest_layer_auto.json").ok);
        CHECK((int)bu.size() == 1 && bu[0].image.path == "hist.png");
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

    // Préférences : palette + emplacements + opacité + fond du canvas (8.5)
    {
        PrefsData p;
        p.palette = {rgba(1, 0, 0), rgba(0, 1, 0)};
        p.brushOpacity = 0.65f;
        p.circleSides = 12;
        p.edgePickTol = 14.0f;
        p.vertexPickTol = 11.0f;
        p.locations = {"sceneA", "sceneB"};
        p.bgColor = {0.2f, 0.4f, 0.6f, 1.0f};
        // Paquets de la barre d'outils (3.2) : un bit par paquet, certains
        // repliés — l'état doit revenir tel quel.
        p.toolbarPacks = 0x5A5A5Au;
        // Calque mémorisé (7.9) : chemin + position / rotation / échelle.
        p.image.path = "/data/logo.png";
        p.image.center = {3.5f, -2.25f};
        p.image.rotation = 0.7f;
        p.image.scaleX = 0.04f;
        p.image.scaleY = 0.09f;
        p.image.opacity = 0.8f;
        p.image.visible = false;
        p.image.w = 640;
        p.image.h = 480;
        CHECK(savePrefsJson(p, "/tmp/meshtest_prefs.json").ok);
        PrefsData back;
        CHECK(loadPrefsJson(back, "/tmp/meshtest_prefs.json").ok);
        CHECK((int)back.palette.size() == 2);
        CHECK(back.palette[0].r == 1.0f && back.palette[1].g == 1.0f);
        CHECK(back.brushOpacity == 0.65f);
        CHECK(back.circleSides == 12);
        CHECK(back.edgePickTol == 14.0f);
        CHECK(back.vertexPickTol == 11.0f);
        CHECK(back.locations.size() == 2 && back.locations[1] == "sceneB");
        CHECK(back.bgColor.r == 0.2f && back.bgColor.g == 0.4f && back.bgColor.b == 0.6f);
        CHECK(back.toolbarPacks == 0x5A5A5Au);
        // Le calque mémorisé revient tel quel (position, rotation, échelle).
        CHECK(back.image.path == "/data/logo.png");
        CHECK(back.image.center.x == 3.5f && back.image.center.y == -2.25f);
        CHECK(back.image.rotation == 0.7f);
        CHECK(back.image.scaleX == 0.04f && back.image.scaleY == 0.09f);
        CHECK(back.image.opacity == 0.8f && !back.image.visible);
        CHECK(back.image.w == 640 && back.image.h == 480);
        // Préférences sans calque : aucun calque rappelé (compat ascendante).
        PrefsData empty;
        CHECK(savePrefsJson(empty, "/tmp/meshtest_prefs2.json").ok);
        PrefsData back2;
        CHECK(loadPrefsJson(back2, "/tmp/meshtest_prefs2.json").ok);
        CHECK(back2.image.path.empty());
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

    // Préférences : modes d'affichage « toutes couleurs » et « filaire »
    // (7.6) conservés.
    {
        PrefsData p;
        p.allColors = true;
        p.wireframe = true;
        CHECK(savePrefsJson(p, "/tmp/meshtest_prefs2.json").ok);
        PrefsData back;
        CHECK(loadPrefsJson(back, "/tmp/meshtest_prefs2.json").ok);
        CHECK(back.allColors);
        CHECK(back.wireframe);
    }

    // Préférences : compatibilité ascendante — un fichier ANCIEN (sans les
    // champs bgColor / toolbarPacks) charge le fond par défaut et tous les
    // paquets ouverts (le chemin « clé absente » de loadPrefsJson est exercé).
    {
        CHECK(writeTestFile("/tmp/meshtest_prefs_legacy.json",
                            "{\"allColors\": true, \"snapOn\": false}\n").ok);
        PrefsData back;
        CHECK(loadPrefsJson(back, "/tmp/meshtest_prefs_legacy.json").ok);
        CHECK(back.allColors);
        CHECK(!back.snapOn);
        CHECK(!back.wireframe);  // champ absent dans un fichier ancien → défaut
        CHECK(back.bgColor.r == kBgDefault.r && back.bgColor.g == kBgDefault.g &&
              back.bgColor.b == kBgDefault.b);
        // Paquets de la barre d'outils : champ absent → tous ouverts (défaut).
        CHECK(back.toolbarPacks == 0xFFFFFFFFu);
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
    CHECK(n == 80);  // toutes les icônes du dossier assets/ (ajoutées : shape-polygon, linked, set-a, set-b, bool)

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

static void testObjSvg() {
    std::printf("[obj / svg]\n");

    // OBJ : triangles + quad, formats d'indices variés (a, a/b, a//b, a/b/c).
    {
        const char* obj =
            "# commentaire\n"
            "v 0 0\n"
            "v 1 0 0\n"
            "v 0 1\n"
            "v 2 0\n"
            "v 2 1 0\n"
            "f 1 2 3\n"
            "f 1/1/2 4//3 5/2\n"
            "f 1 2 4 5\n"
            "f 1 2\n";  // face à 2 indices : ignorée
        CHECK(writeTestFile("/tmp/meshtest_obj.obj", obj).ok);
        Mesh2D m;
        const IoResult r = loadObj(m, "/tmp/meshtest_obj.obj");
        CHECK(r.ok);
        CHECK((int)m.vertices.size() == 5);
        CHECK((int)m.faces.size() == 3);  // 2 triangles + 1 quad
        CHECK((int)m.faces[0].verts.size() == 3);
        CHECK((int)m.faces[1].verts.size() == 3);
        CHECK((int)m.faces[2].verts.size() == 4);
    }

    // Aller-retour exportOBJ → loadObj.
    {
        Mesh2D m;
        const int a = m.addVertex({-1, -1});
        const int b = m.addVertex({1, -1});
        const int c = m.addVertex({1, 1});
        m.addFace({a, b, c});
        CHECK(exportOBJ(m, "/tmp/meshtest_roundtrip.obj").ok);
        Mesh2D m2;
        const IoResult r = loadObj(m2, "/tmp/meshtest_roundtrip.obj");
        CHECK(r.ok);
        CHECK((int)m2.vertices.size() == 3);
        CHECK((int)m2.faces.size() == 1);
        CHECK(m2.vertices[2].x == 1.0f && m2.vertices[2].y == 1.0f);
        CHECK(m2.faces[0].verts[0] == 0 && m2.faces[0].verts[1] == 1 &&
              m2.faces[0].verts[2] == 2);
    }

    // OBJ inexistant : refusé.
    {
        Mesh2D m;
        CHECK(!loadObj(m, "/tmp/fichier-inexistant.obj").ok);
    }

    // Export SVG : polygones + couleur, Y inversé.
    {
        Mesh2D m;
        const int a = m.addVertex({0, 0});
        const int b = m.addVertex({1, 0});
        const int c = m.addVertex({0, 1});
        const int fi = m.addFace({a, b, c});
        m.faces[fi].color = {1.0f, 0.0f, 0.0f, 0.5f};
        m.faces[fi].hasColor = true;
        const IoResult r = exportPlaneSVG(m, "/tmp/meshtest.svg");
        CHECK(r.ok);
        std::ifstream f("/tmp/meshtest.svg");
        std::stringstream buf;
        buf << f.rdbuf();
        const std::string svg = buf.str();
        CHECK(svg.find("<svg") != std::string::npos);
        CHECK(svg.find("<polygon points=\"0,0 1,0 0,-1 \"") != std::string::npos);
        CHECK(svg.find("fill=\"rgba(255,0,0,0.5)\"") != std::string::npos);
        // Plan vide : refusé.
        Mesh2D empty;
        CHECK(!exportPlaneSVG(empty, "/tmp/meshtest_vide.svg").ok);
    }
}

static void testPngExport() {
    std::printf("[png export]\n");
    const std::string path = "/tmp/meshtest.png";
    std::remove(path.c_str());
    // Image 3×2 : bas = rouge/vert/bleu, haut = noir/blanc/gris.
    unsigned char px[3 * 2 * 4] = {
        255, 0,   0,   255, 0, 255, 0, 255, 0, 0, 255, 255,   // ligne 0 (bas)
        0,   0,   0,   255, 255, 255, 255, 255, 128, 128, 128, 255,  // ligne 1 (haut)
    };
    CHECK(writePng(path, 3, 2, px));
    CHECK(writePng("/tmp/meshtest-invalide.png", 0, 2, px) == false);
    CHECK(writePng("/tmp/meshtest-invalide2.png", 3, 2, nullptr) == false);
    CHECK(writePng("", 3, 2, px) == false);

    std::ifstream f(path, std::ios::binary);
    CHECK(f.good());
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(f)),
                                    std::istreambuf_iterator<char>());
    CHECK(data.size() > 40);
    // Signature PNG.
    const unsigned char sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    CHECK(data.size() >= 8 && std::equal(sig, sig + 8, data.begin()));
    // En-tête IHDR : dimensions 3×2 aux octets 16..23.
    CHECK(data[16] == 0 && data[17] == 0 && data[18] == 0 && data[19] == 3);
    CHECK(data[20] == 0 && data[21] == 0 && data[22] == 0 && data[23] == 2);
    CHECK(data[24] == 8 && data[25] == 2);  // 8 bits, RGB
    // Fin de fichier : le type « IEND » occupe les octets -8..-5 (le bloc est
    // longueur(4) + type(4) + CRC(4)).
    CHECK(data.size() >= 12 &&
          data[data.size() - 8] == 'I' && data[data.size() - 7] == 'E' &&
          data[data.size() - 6] == 'N' && data[data.size() - 5] == 'D');
}

// Indice de la face dont la boucle est {v0,v1,v2} dans un ordre cyclique.
static int faceIndexWith(const Mesh2D& m, int v0, int v1, int v2) {
    for (int i = 0; i < (int)m.faces.size(); ++i) {
        const std::vector<int>& v = m.faces[i].verts;
        if (v.size() != 3) continue;
        const bool match =
            (v[0] == v0 && v[1] == v1 && v[2] == v2) ||
            (v[0] == v1 && v[1] == v2 && v[2] == v0) ||
            (v[0] == v2 && v[1] == v0 && v[2] == v1);
        if (match) return i;
    }
    return -1;
}

// Ordre z des faces (devant / derrière, FONCTIONNALITES.md 5.9) : shiftFaces
// sélectionnées d'un cran et retourne leurs nouveaux indices.
static void testFaceOrder() {
    std::printf("[ordre z des faces]\n");
    // 4 triangles dans un carré : f0 {a,b,c}, f1 {b,d,c}, f2 {a,c,d}, f3 {a,b,d}.
    auto build = [](Mesh2D& m) {
        const int a = m.addVertex({0, 0});
        const int b = m.addVertex({1, 0});
        const int c = m.addVertex({0, 1});
        const int d = m.addVertex({1, 1});
        m.addFace({a, b, c});
        m.addFace({b, d, c});
        m.addFace({a, c, d});
        m.addFace({a, b, d});
    };

    // Une seule face vers l'avant : {2} → {3}, la face a changé de place.
    {
        Mesh2D m;
        build(m);
        std::vector<int> sel = {2};
        sel = m.shiftFaces(sel, +1);
        CHECK(sel == std::vector<int>({3}));
        CHECK(faceIndexWith(m, 0, 2, 3) == 3);  // f2 désormais au premier plan
        CHECK(faceIndexWith(m, 0, 1, 3) == 2);  // f3 recule d'un cran
    }
    // Une seule face vers l'arrière : {1} → {0}.
    {
        Mesh2D m;
        build(m);
        std::vector<int> sel = {1};
        sel = m.shiftFaces(sel, -1);
        CHECK(sel == std::vector<int>({0}));
        CHECK(faceIndexWith(m, 1, 3, 2) == 0);  // f1 désormais au dernier plan
        CHECK(faceIndexWith(m, 0, 1, 2) == 1);  // f0 avance d'un cran
    }
    // Bloc contigu {1,2} vers l'avant : les deux faces montent, ordre conservé.
    {
        Mesh2D m;
        build(m);
        std::vector<int> sel = {1, 2};
        sel = m.shiftFaces(sel, +1);
        CHECK(sel == std::vector<int>({2, 3}));
        CHECK(faceIndexWith(m, 1, 3, 2) == 2);
        CHECK(faceIndexWith(m, 0, 2, 3) == 3);
        CHECK(faceIndexWith(m, 0, 1, 2) == 0);
        CHECK(faceIndexWith(m, 0, 1, 3) == 1);
    }
    // Bloc contigu {1,2} vers l'arrière : les deux faces descendent — le bloc
    // occupe les positions {0,1} et pousse f0 à la position 2.
    {
        Mesh2D m;
        build(m);
        std::vector<int> sel = {1, 2};
        sel = m.shiftFaces(sel, -1);
        CHECK(sel == std::vector<int>({0, 1}));
        CHECK(faceIndexWith(m, 1, 3, 2) == 0);
        CHECK(faceIndexWith(m, 0, 2, 3) == 1);
        CHECK(faceIndexWith(m, 0, 1, 2) == 2);
        CHECK(faceIndexWith(m, 0, 1, 3) == 3);
    }
    // Faces disjointes {0,2} vers l'avant : ordre relatif conservé (0 puis 2).
    {
        Mesh2D m;
        build(m);
        std::vector<int> sel = {0, 2};
        sel = m.shiftFaces(sel, +1);
        CHECK(sel == std::vector<int>({1, 3}));
        CHECK(faceIndexWith(m, 0, 1, 2) == 1);
        CHECK(faceIndexWith(m, 0, 2, 3) == 3);
    }
    // Aux bornes : rien ne bouge, les indices retournés sont inchangés.
    {
        Mesh2D n;
        const int x0 = n.addVertex({0, 0});
        const int x1 = n.addVertex({1, 0});
        const int x2 = n.addVertex({0, 1});
        n.addFace({x0, x1, x2});
        n.addFace({x0, x2, x1});
        n.addFace({x0, x1, x2});  // doublon géométrique : l'ordre seul compte ici
        std::vector<int> top = {2};
        CHECK(n.shiftFaces(top, +1) == std::vector<int>({2}));
        std::vector<int> bottom = {0};
        CHECK(n.shiftFaces(bottom, -1) == std::vector<int>({0}));
    }
    // Plan à une seule face : rien ne bouge et la sélection reste intacte
    // (régression : un retour précoce vidait la sélection).
    {
        Mesh2D n;
        const int x0 = n.addVertex({0, 0});
        const int x1 = n.addVertex({1, 0});
        const int x2 = n.addVertex({0, 1});
        n.addFace({x0, x1, x2});
        std::vector<int> single = {0};
        CHECK(n.shiftFaces(single, +1) == std::vector<int>({0}));
        CHECK(n.shiftFaces(single, -1) == std::vector<int>({0}));
    }
    // Indices invalides ignorés ; sélection vide → vide ; tout sélectionné :
    // aucun déplacement possible.
    {
        Mesh2D m;
        build(m);
        std::vector<int> bad = {5, -1};
        CHECK(m.shiftFaces(bad, +1).empty());
        std::vector<int> empty;
        CHECK(m.shiftFaces(empty, +1).empty());
        std::vector<int> all = {0, 1, 2, 3};
        CHECK(m.shiftFaces(all, +1) == all);
    }
}

// Opérations ensemblistes (booléennes) par frontière entre deux ensembles de
// triangles : A et B sont traités comme des régions polygonales, le résultat
// est un ensemble de composantes (extérieur + trous) — l'aire est vérifiée, et
// la triangulation finale (triangulatePolygonHoles, faite une seule fois) doit
// rester minimale : pas de coutures internes comme l'ancien découpage en
// cellules.
static void testSetBoolean() {
    std::printf("[opérations ensemblistes]\n");

    auto polyArea = [](const std::vector<Vec2>& p) {
        float a = 0.0f;
        for (size_t i = 0; i < p.size(); ++i) {
            const Vec2& x = p[i];
            const Vec2& y = p[(i + 1) % p.size()];
            a += x.x * y.y - y.x * x.y;
        }
        return a * 0.5f;
    };
    // Aire du résultat : extérieur moins ses trous.
    auto keptArea = [&](const std::vector<BoolRegion>& regions) {
        float s = 0.0f;
        for (const BoolRegion& r : regions) {
            s += std::fabs(polyArea(r.outer));
            for (const auto& h : r.holes) s -= std::fabs(polyArea(h));
        }
        return s;
    };
    auto regionCount = [](const std::vector<BoolRegion>& regions) {
        return (int)regions.size();
    };
    // Nombre de triangles de la triangulation finale (minimalité).
    auto triCount = [](const std::vector<BoolRegion>& regions) {
        int n = 0;
        for (const BoolRegion& r : regions) {
            std::vector<Vec2> pts;
            std::vector<int> tris;
            if (!triangulatePolygonHoles(r.outer, r.holes, pts, tris)) continue;
            n += (int)(tris.size() / 3);
        }
        return n;
    };
    auto close = [](float a, float b) { return std::fabs(a - b) < 1e-3f; };

    // Deux carrés qui se chevauchent sur 1×1 : A = [0,2]², B = [1,3]².
    {
        const std::vector<Vec2> a = {{0, 0}, {2, 0}, {2, 2}, {0, 0}, {2, 2}, {0, 2}};
        const std::vector<Vec2> b = {{1, 1}, {3, 1}, {3, 3}, {1, 1}, {3, 3}, {1, 3}};
        std::vector<BoolRegion> r;
        triangleSetBoolean(SetOp::Union, a, b, r);
        CHECK(close(keptArea(r), 7.0f));          // 4 + 4 − 1
        CHECK(triCount(r) <= 10);                 // triangulation minimale
        triangleSetBoolean(SetOp::Intersection, a, b, r);
        CHECK(close(keptArea(r), 1.0f));
        CHECK(regionCount(r) == 1);
        triangleSetBoolean(SetOp::Difference, a, b, r);
        CHECK(close(keptArea(r), 3.0f));          // A ∖ B
        triangleSetBoolean(SetOp::SymDiff, a, b, r);
        CHECK(close(keptArea(r), 6.0f));          // 3 + 3
    }

    // Ensembles disjoints : intersection vide.
    {
        const std::vector<Vec2> a = {{0, 0}, {1, 0}, {1, 1}, {0, 0}, {1, 1}, {0, 1}};
        const std::vector<Vec2> b = {{2, 2}, {3, 2}, {3, 3}, {2, 2}, {3, 3}, {2, 3}};
        std::vector<BoolRegion> r;
        triangleSetBoolean(SetOp::Union, a, b, r);
        CHECK(close(keptArea(r), 2.0f));
        triangleSetBoolean(SetOp::Intersection, a, b, r);
        CHECK(regionCount(r) == 0);
        CHECK(close(keptArea(r), 0.0f));
        triangleSetBoolean(SetOp::Difference, a, b, r);
        CHECK(close(keptArea(r), 1.0f));
        triangleSetBoolean(SetOp::SymDiff, a, b, r);
        CHECK(close(keptArea(r), 2.0f));
    }

    // B entièrement dans A : l'union vaut A, la différence A−B est la couronne
    // (une seule composante avec un trou).
    {
        const std::vector<Vec2> a = {{0, 0}, {4, 0}, {4, 4}, {0, 0}, {4, 4}, {0, 4}};
        const std::vector<Vec2> b = {{1, 1}, {2, 1}, {2, 2}, {1, 1}, {2, 2}, {1, 2}};
        std::vector<BoolRegion> r;
        triangleSetBoolean(SetOp::Union, a, b, r);
        CHECK(close(keptArea(r), 16.0f));
        triangleSetBoolean(SetOp::Intersection, a, b, r);
        CHECK(close(keptArea(r), 1.0f));
        triangleSetBoolean(SetOp::Difference, a, b, r);
        CHECK(close(keptArea(r), 15.0f));
        CHECK(regionCount(r) == 1 && r[0].holes.size() == 1);  // couronne : un trou
        triangleSetBoolean(SetOp::SymDiff, a, b, r);
        CHECK(close(keptArea(r), 15.0f));
    }

    // Ensembles identiques : union = A, intersection = A, différence vide.
    {
        const std::vector<Vec2> a = {{0, 0}, {2, 0}, {2, 2}, {0, 0}, {2, 2}, {0, 2}};
        std::vector<BoolRegion> r;
        triangleSetBoolean(SetOp::Union, a, a, r);
        CHECK(close(keptArea(r), 4.0f));
        triangleSetBoolean(SetOp::Intersection, a, a, r);
        CHECK(close(keptArea(r), 4.0f));
        triangleSetBoolean(SetOp::Difference, a, a, r);
        CHECK(regionCount(r) == 0);
        triangleSetBoolean(SetOp::SymDiff, a, a, r);
        CHECK(regionCount(r) == 0);
    }

    // Deux triangles qui se touchent le long d'une arête (aucun chevauchement
    // d'aire) : l'union est un quadrilatère (2 triangles), la frontière
    // commune n'apparaît pas, la différence vaut A.
    {
        const std::vector<Vec2> a = {{0, 0}, {2, 0}, {0, 2}};
        const std::vector<Vec2> b = {{0, 2}, {2, 0}, {0, 4}};
        std::vector<BoolRegion> r;
        triangleSetBoolean(SetOp::Union, a, b, r);
        CHECK(close(keptArea(r), 4.0f));
        CHECK(regionCount(r) == 1 && triCount(r) <= 4);
        triangleSetBoolean(SetOp::Intersection, a, b, r);
        CHECK(regionCount(r) == 0);   // seulement une arête commune : rien
        triangleSetBoolean(SetOp::Difference, a, b, r);
        CHECK(close(keptArea(r), 2.0f));
        triangleSetBoolean(SetOp::SymDiff, a, b, r);
        CHECK(close(keptArea(r), 4.0f));
    }

    // Deux carrés qui se touchent en un seul coin : union = deux composantes.
    {
        const std::vector<Vec2> a = {{0, 0}, {1, 0}, {1, 1}, {0, 0}, {1, 1}, {0, 1}};
        const std::vector<Vec2> b = {{1, 1}, {2, 1}, {2, 2}, {1, 1}, {2, 2}, {1, 2}};
        std::vector<BoolRegion> r;
        triangleSetBoolean(SetOp::Union, a, b, r);
        CHECK(close(keptArea(r), 2.0f));
        CHECK(regionCount(r) == 2);   // deux composantes disjointes
        triangleSetBoolean(SetOp::Intersection, a, b, r);
        CHECK(regionCount(r) == 0);
        triangleSetBoolean(SetOp::Difference, a, b, r);
        CHECK(close(keptArea(r), 1.0f));
    }
}

int main() {
    testTriangulation();
    testCrownBand();
    testCutPolygons();
    testCutTriangulated();
    testMeshOps();
    testFaceOrder();
    testRoundTrip();
    testSpecFormats();
    testSVGIcons();
    testObjSvg();
    testPngExport();
    testSetBoolean();

    std::printf("\nRésultat : %d/%d vérifications OK\n", g_checks - g_failures, g_checks);
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
