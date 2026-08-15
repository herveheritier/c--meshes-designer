// Stress test temporaire pour reproduire le défaut de l'opération booléenne.
#include "mesh.h"
#include "triangulate.h"

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <random>
#include <vector>

using namespace mesh;

static float polyArea(const std::vector<Vec2>& p) {
    float a = 0.0f;
    for (size_t i = 0; i < p.size(); ++i) {
        const Vec2& x = p[i];
        const Vec2& y = p[(i + 1) % p.size()];
        a += x.x * y.y - y.x * x.y;
    }
    return a * 0.5f;
}

static float regionArea(const std::vector<BoolRegion>& regions) {
    float s = 0.0f;
    for (const BoolRegion& r : regions) {
        s += std::fabs(polyArea(r.outer));
        for (const auto& h : r.holes) s -= std::fabs(polyArea(h));
    }
    return s;
}

// Triangulation plate du résultat (comme applyBoolOp).
static std::vector<Vec2> regionTris(const std::vector<BoolRegion>& regions) {
    std::vector<Vec2> out;
    for (const BoolRegion& r : regions) {
        std::vector<Vec2> pts;
        std::vector<int> tris;
        if (!triangulatePolygonHoles(r.outer, r.holes, pts, tris)) continue;
        for (int t : tris) out.push_back(pts[t]);
    }
    return out;
}

static bool pointInTris(const Vec2& p, const std::vector<Vec2>& tris) {
    for (size_t i = 0; i + 2 < tris.size(); i += 3)
        if (pointInTriangle(p, tris[i], tris[i + 1], tris[i + 2])) return true;
    return false;
}

// Appartenance à la région elle-même (extérieurs − trous), sans trianguler.
static bool pointInRegion(const Vec2& p, const std::vector<BoolRegion>& regions) {
    for (const BoolRegion& r : regions) {
        if (!pointInPolygon(p, r.outer)) continue;
        bool inHole = false;
        for (const auto& h : r.holes)
            if (pointInPolygon(p, h)) { inHole = true; break; }
        if (!inHole) return true;
    }
    return false;
}

// Convex polygon : enveloppe convexe (Andrew, monotone chain) de points
// aléatoires — toujours simple (les rayons aléatoires sur un cercle, même
// triés par angle, peuvent former des polygones auto-intersectants, ce que
// l'application ne produit jamais : ses ensembles sont des polygones simples
// ou des maillages de triangles sans chevauchement).
static std::vector<Vec2> randomConvex(std::mt19937& rng, float cx, float cy, float r) {
    std::uniform_real_distribution<float> pos(-r, r);
    const int n = 5 + (int)(rng() % 8);
    std::vector<Vec2> pts;
    for (int i = 0; i < n; ++i) pts.push_back({cx + pos(rng), cy + pos(rng)});
    std::sort(pts.begin(), pts.end(), [](const Vec2& a, const Vec2& b) {
        return a.x < b.x || (a.x == b.x && a.y < b.y);
    });
    std::vector<Vec2> hull;
    for (const Vec2& p : pts) {  // chaîne inférieure
        while (hull.size() >= 2 &&
               cross(hull[hull.size() - 1] - hull[hull.size() - 2],
                     p - hull[hull.size() - 1]) <= 0.0f)
            hull.pop_back();
        hull.push_back(p);
    }
    const int lower = (int)hull.size();
    for (int i = (int)pts.size() - 2; i >= 0; --i) {  // chaîne supérieure
        const Vec2& p = pts[i];
        while ((int)hull.size() > lower &&
               cross(hull[hull.size() - 1] - hull[hull.size() - 2],
                     p - hull[hull.size() - 1]) <= 0.0f)
            hull.pop_back();
        hull.push_back(p);
    }
    hull.pop_back();  // dernier == premier
    if (hull.size() < 3) return randomConvex(rng, cx, cy, r);
    if (polyArea(hull) < 0.0f) std::reverse(hull.begin(), hull.end());
    return hull;
}

// Polygone simple concave : enveloppe convexe dont certains sommets sont
// tirés vers le centre (l'étoile autour du centre reste simple — jamais
// d'auto-intersection). Représente les formes concaves de l'application.
static std::vector<Vec2> randomConcave(std::mt19937& rng, float cx, float cy, float r) {
    std::vector<Vec2> base = randomConvex(rng, cx, cy, r);
    Vec2 c{0, 0};
    for (const Vec2& p : base) { c.x += p.x; c.y += p.y; }
    c.x /= (float)base.size();
    c.y /= (float)base.size();
    std::uniform_real_distribution<float> f(0.12f, 0.55f);
    for (size_t i = 0; i < base.size(); ++i) {
        if (rng() % 3 == 0) {  // ~1 sommet sur 3 devient un creux
            const float t = f(rng);
            base[i].x = c.x + (base[i].x - c.x) * t;
            base[i].y = c.y + (base[i].y - c.y) * t;
        }
    }
    if (polyArea(base) < 0.0f) std::reverse(base.begin(), base.end());
    return base;
}

static std::vector<Vec2> rectTris(float x0, float y0, float x1, float y1) {
    return {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y0}, {x1, y1}, {x0, y1}};
}

static int g_fail = 0;
static int g_cases = 0;

static float opArea(SetOp op, const std::vector<Vec2>& a, const std::vector<Vec2>& b) {
    std::vector<BoolRegion> r;
    triangleSetBoolean(op, a, b, r);
    return regionArea(r);
}

// Identités exactes entre les 4 opérations (aucune référence externe bruitée) :
//   union + inter = A + B, diff + inter = A, sym + 2·inter = A + B.
static void checkIdentities(SetOp op, const std::vector<Vec2>& a,
                            const std::vector<Vec2>& b, float areaA, float areaB,
                            const char* name) {
    const float u = opArea(SetOp::Union, a, b);
    const float i = opArea(SetOp::Intersection, a, b);
    const float d = opArea(SetOp::Difference, a, b);
    const float s = opArea(SetOp::SymDiff, a, b);
    ++g_cases;
    if (std::fabs(u + i - (areaA + areaB)) > 1e-3f ||
        std::fabs(d + i - areaA) > 1e-3f ||
        std::fabs(s + 2.0f * i - (areaA + areaB)) > 1e-3f) {
        ++g_fail;
        std::printf("  ÉCHEC %s : u=%.4f i=%.4f d=%.4f s=%.4f (A=%.3f B=%.3f)\n",
                    name, u, i, d, s, areaA, areaB);
    }
}

int main() {
    std::mt19937 rng(12345);

    // --- 1) Grille de rectangles aléatoires qui se chevauchent ---
    std::uniform_real_distribution<float> pos(-3.0f, 3.0f);
    std::uniform_real_distribution<float> sz(0.3f, 3.0f);
    for (int iter = 0; iter < 4000; ++iter) {
        const float ax0 = pos(rng), ay0 = pos(rng);
        const float bx0 = pos(rng), by0 = pos(rng);
        const float aw = sz(rng), ah = sz(rng);
        const float bw = sz(rng), bh = sz(rng);
        const std::vector<Vec2> a = rectTris(ax0, ay0, ax0 + aw, ay0 + ah);
        const std::vector<Vec2> b = rectTris(bx0, by0, bx0 + bw, by0 + bh);
        const float areaA = aw * ah, areaB = bw * bh;
        checkIdentities(SetOp::Union, a, b, areaA, areaB, "rectangles");
    }
    std::printf("Rectangles aléatoires : %d cas, %d échecs\n", g_cases, g_fail);
    if (g_fail) return 1;

    // --- 2) Polygones simples aléatoires (convexes et concaves, centres
    // qui se chevauchent) — vérification des aires des 4 opérations. ---
    g_fail = 0;
    g_cases = 0;
    std::uniform_int_distribution<int> shape(0, 1);
    auto genPoly = [&](float cx, float cy) {
        return shape(rng) ? randomConvex(rng, cx, cy, 3.0f)
                          : randomConcave(rng, cx, cy, 3.0f);
    };
    for (int iter = 0; iter < 3000; ++iter) {
        const std::vector<Vec2> ap = genPoly(0.0f, 0.0f);
        const std::vector<Vec2> bp = genPoly(0.4f, 0.3f);
        // Triangulation comme l'application (triangulatePolygon, enroulement
        // cohérent) : chaque face est un polygone, découpé en triangles.
        std::vector<Vec2> a, b;
        auto tri = [](const std::vector<Vec2>& poly, std::vector<Vec2>& out) {
            std::vector<int> local;
            if (!triangulatePolygon(poly, local)) return false;
            for (size_t i = 0; i + 2 < local.size(); i += 3) {
                out.push_back(poly[local[i]]);
                out.push_back(poly[local[i + 1]]);
                out.push_back(poly[local[i + 2]]);
            }
            return true;
        };
        if (!tri(ap, a) || !tri(bp, b)) continue;
        // Écarte les formes mal conditionnées (slivers quasi dégénérés) :
        // l'application réelle dessine des formes raisonnables.
        if (std::fabs(polyArea(ap)) < 0.3f || std::fabs(polyArea(bp)) < 0.3f) continue;
        const float areaA = std::fabs(polyArea(ap));
        const float areaB = std::fabs(polyArea(bp));
        checkIdentities(SetOp::Union, a, b, areaA, areaB, "identités");
    }
    std::printf("Polygones aléatoires : %d cas, %d échecs\n", g_cases, g_fail);

    // --- 3) Vérification grille du résultat réel (exactitude géométrique) ---
    g_fail = 0;
    g_cases = 0;
    setTriangleSetBooleanDebug(false);
    for (int iter = 0; iter < 1500; ++iter) {
        const std::vector<Vec2> ap = genPoly(0.0f, 0.0f);
        const std::vector<Vec2> bp = genPoly(0.4f, 0.3f);
        std::vector<Vec2> a, b;
        auto tri = [](const std::vector<Vec2>& poly, std::vector<Vec2>& out) {
            std::vector<int> local;
            if (!triangulatePolygon(poly, local)) return false;
            for (size_t i = 0; i + 2 < local.size(); i += 3) {
                out.push_back(poly[local[i]]);
                out.push_back(poly[local[i + 1]]);
                out.push_back(poly[local[i + 2]]);
            }
            return true;
        };
        if (!tri(ap, a) || !tri(bp, b)) continue;
        if (std::fabs(polyArea(ap)) < 0.3f || std::fabs(polyArea(bp)) < 0.3f) continue;
        float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
        for (const Vec2& p : ap) {
            minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
        }
        for (const Vec2& p : bp) {
            minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
        }
        for (SetOp op : {SetOp::Union, SetOp::Intersection, SetOp::Difference, SetOp::SymDiff}) {
            std::vector<BoolRegion> r;
            triangleSetBoolean(op, a, b, r);
            // Vérification 1 : l'aire triangulée doit égaler l'aire des
            // régions (extérieurs − trous). Si le pont d'un trou dégénère, le
            // trou est avalé et les aires divergent.
            {
                float regArea = 0.0f, triArea = 0.0f;
                for (const BoolRegion& rr : r) {
                    regArea += std::fabs(polyArea(rr.outer));
                    for (const auto& h : rr.holes) regArea -= std::fabs(polyArea(h));
                }
                const std::vector<Vec2>& rt = regionTris(r);
                for (size_t i = 0; i + 2 < rt.size(); i += 3) {
                    triArea += std::fabs(
                        (rt[i].x * (rt[i + 1].y - rt[i + 2].y) +
                         rt[i + 1].x * (rt[i + 2].y - rt[i].y) +
                         rt[i + 2].x * (rt[i].y - rt[i + 1].y)) *
                        0.5f);
                }
                if (std::fabs(regArea - triArea) > 1e-2f) {
                    std::printf("  ÉCHEC aire tri vs région : op=%d reg=%.4f tri=%.4f "
                                "(iter %d) régionTris=%zu\n",
                                (int)op, regArea, triArea, iter, rt.size());
                    std::printf("    A=[");
                    for (const Vec2& p : ap)
                        std::printf(" (%.9g,%.9g)", (double)p.x, (double)p.y);
                    std::printf(" ]\n    B=[");
                    for (const Vec2& p : bp)
                        std::printf(" (%.9g,%.9g)", (double)p.x, (double)p.y);
                    std::printf(" ]\n");
                    setTriangleSetBooleanDebug(true);
                    std::vector<BoolRegion> dbg;
                    triangleSetBoolean(op, a, b, dbg);
                    std::vector<Vec2> dpts;
                    std::vector<int> dtris;
                    for (const BoolRegion& rr : dbg)
                        triangulatePolygonHoles(rr.outer, rr.holes, dpts, dtris);
                    setTriangleSetBooleanDebug(false);
                }
            }
            // Vérification 2 (grille) : appartenance exacte à la région
            // (extérieurs − trous) et à la triangulation.
            const std::vector<Vec2> res = regionTris(r);
            const int N = 260;
            long badReg = 0, badTri = 0, total = 0;
            for (int i = 0; i < N; ++i) {
                for (int j = 0; j < N; ++j) {
                    const Vec2 c{minX + (maxX - minX) * (i + 0.5f) / N,
                                 minY + (maxY - minY) * (j + 0.5f) / N};
                    const bool ia = pointInTris(c, a);
                    const bool ib = pointInTris(c, b);
                    bool expect = false;
                    switch (op) {
                        case SetOp::Union: expect = ia || ib; break;
                        case SetOp::Intersection: expect = ia && ib; break;
                        case SetOp::Difference: expect = ia && !ib; break;
                        case SetOp::SymDiff: expect = ia != ib; break;
                    }
                    ++total;
                    if (expect != pointInRegion(c, r)) ++badReg;
                    if (expect != pointInTris(c, res)) ++badTri;
                }
            }
            ++g_cases;
            if (badReg > total / 500 || badTri > total / 500) {
                ++g_fail;
                std::printf("  ÉCHEC grille op=%d : région %ld/%ld, tris %ld/%ld "
                            "cellules fausses (iter %d)\n",
                            (int)op, badReg, total, badTri, total, iter);
                std::printf("    A=[");
                for (const Vec2& p : ap) std::printf(" (%.3f,%.3f)", p.x, p.y);
                std::printf(" ]\n    B=[");
                for (const Vec2& p : bp) std::printf(" (%.3f,%.3f)", p.x, p.y);
                std::printf(" ]\n");
                // Re-exécute l'opération en échec avec le débogage détaillé.
                setTriangleSetBooleanDebug(true);
                std::vector<BoolRegion> dbg;
                triangleSetBoolean(op, a, b, dbg);
                setTriangleSetBooleanDebug(false);
                for (size_t ri = 0; ri < dbg.size(); ++ri) {
                    std::printf("    REGION%zu outer=[", ri);
                    for (const Vec2& p : dbg[ri].outer)
                        std::printf(" (%.9g,%.9g)", (double)p.x, (double)p.y);
                    std::printf(" ]\n");
                    for (size_t hi = 0; hi < dbg[ri].holes.size(); ++hi) {
                        std::printf("    REGION%zu trou%zu=[", ri, hi);
                        for (const Vec2& p : dbg[ri].holes[hi])
                            std::printf(" (%.9g,%.9g)", (double)p.x, (double)p.y);
                        std::printf(" ]\n");
                    }
                }
                if (g_fail >= 3) {
                    std::printf("  (arrêt après 3 échecs)\n");
                    return 1;
                }
            }
        }
    }
    std::printf("Grille géométrique : %d cas, %d échecs\n", g_cases, g_fail);
    return g_fail ? 1 : 0;
}
