#include "triangulate.h"

#include <algorithm>

namespace mesh {

namespace {

// Aire signée (x2) du triangle (a,b,c).
float triArea2(const Vec2& a, const Vec2& b, const Vec2& c) { return cross(b - a, c - a); }

}  // namespace

bool pointInTriangle(const Vec2& p, const Vec2& a, const Vec2& b, const Vec2& c) {
    const float eps = 1e-6f;
    const float d1 = triArea2(p, a, b);
    const float d2 = triArea2(p, b, c);
    const float d3 = triArea2(p, c, a);
    const bool hasNeg = (d1 < -eps) || (d2 < -eps) || (d3 < -eps);
    const bool hasPos = (d1 > eps) || (d2 > eps) || (d3 > eps);
    return !(hasNeg && hasPos);
}

bool pointInPolygon(const Vec2& p, const std::vector<Vec2>& poly) {
    const int n = (int)poly.size();
    if (n < 3) return false;
    // Ray casting (à droite) avec gestion des sommets sur la frontière.
    bool inside = false;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        const Vec2& a = poly[i];
        const Vec2& b = poly[j];
        if (pointSegmentDistance(p, a, b) < 1e-7f) return true;
        const bool crosses = ((a.y > p.y) != (b.y > p.y)) &&
                             (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x);
        if (crosses) inside = !inside;
    }
    return inside;
}

float pointSegmentDistance(const Vec2& p, const Vec2& a, const Vec2& b) {
    const Vec2 ab = b - a;
    const float len2 = dot(ab, ab);
    if (len2 < 1e-12f) return distance(p, a);
    const float t = std::clamp(dot(p - a, ab) / len2, 0.0f, 1.0f);
    return distance(p, a + ab * t);
}

// Triangulation « zipper » de la bande entre deux polygones réguliers
// concentriques (couronne) : à chaque pas on avance sur la boucle dont la
// diagonale est la plus courte — aucun croisement, triangles peu allongés, et
// le cas n == m redonne exactement l'anneau. Quand une boucle est épuisée,
// l'éventail de fermeture se fait depuis le sommet O(0) ou I(0) — le
// connecteur courant du zipper — et non depuis le dernier sommet : la couture
// à 0° est ainsi découpée par une vraie diagonale (relier au dernier sommet
// faisait se chevaucher les deux triangles de fermeture).
void triangulateBand(const std::vector<Vec2>& outer, const std::vector<Vec2>& inner,
                     std::vector<int>& tris) {
    tris.clear();
    const int n = (int)outer.size();
    const int m = (int)inner.size();
    if (n < 3 || m < 3) return;
    int i = 0, j = 0;
    while (i < n || j < m) {
        const int ni = (i + 1) % n;
        const int nj = (j + 1) % m;
        if (i >= n) {
            // Boucle extérieure épuisée : éventail sur les sommets intérieurs
            // restants depuis O(0) — le connecteur courant du zipper après le
            // dernier pas est O(0)-I(j).
            tris.insert(tris.end(), {0, 0, 1, nj, 1, j});
            ++j;
        } else if (j >= m) {
            // Boucle intérieure épuisée : éventail sur les sommets extérieurs
            // restants depuis I(0) — le connecteur courant du zipper après le
            // dernier pas est O(i)-I(0).
            tris.insert(tris.end(), {0, i, 0, ni, 1, 0});
            ++i;
        } else {
            const Vec2 d1 = outer[i] - inner[nj];  // diagonale Oi→Ij+1
            const Vec2 d2 = outer[ni] - inner[j];  // diagonale Oi+1→Ij
            if (d1.x * d1.x + d1.y * d1.y < d2.x * d2.x + d2.y * d2.y) {
                tris.insert(tris.end(), {0, i, 1, nj, 1, j});
                ++j;
            } else {
                tris.insert(tris.end(), {0, i, 0, ni, 1, j});
                ++i;
            }
        }
    }
}

bool triangulatePolygon(const std::vector<Vec2>& pts, std::vector<int>& tris) {
    tris.clear();
    const int n = (int)pts.size();
    if (n < 3) return false;
    if (n == 3) {
        // Triangle dégénéré (sommets alignés ou confondus) ?
        if (std::fabs(triArea2(pts[0], pts[1], pts[2])) < 1e-9f) return false;
        tris = {0, 1, 2};
        return true;
    }

    // Orientation globale du polygone.
    float area = 0.0f;
    for (int i = 0; i < n; ++i) {
        const Vec2& a = pts[i];
        const Vec2& b = pts[(i + 1) % n];
        area += a.x * b.y - b.x * a.y;
    }
    if (std::fabs(area) < 1e-9f) return false;  // dégénéré
    const bool cw = area < 0.0f;

    std::vector<int> idx;
    idx.reserve(n);
    for (int i = 0; i < n; ++i) idx.push_back(i);

    int guard = 0;
    while (idx.size() > 3) {
        bool clipped = false;
        for (size_t k = 0; k < idx.size() && !clipped; ++k) {
            const int ia = idx[(k + idx.size() - 1) % idx.size()];
            const int ib = idx[k];
            const int ic = idx[(k + 1) % idx.size()];
            const float cr = triArea2(pts[ia], pts[ib], pts[ic]);
            const bool convex = cw ? cr < 1e-7f : cr > 1e-7f;
            if (!convex) continue;
            // Aucun autre sommet ne doit se trouver dans le triangle (oreille).
            bool blocked = false;
            for (int v : idx) {
                if (v == ia || v == ib || v == ic) continue;
                if (pointInTriangle(pts[v], pts[ia], pts[ib], pts[ic])) { blocked = true; break; }
            }
            if (blocked) continue;
            // Oreille valide : on la coupe.
            tris.push_back(ia);
            tris.push_back(ib);
            tris.push_back(ic);
            idx.erase(idx.begin() + (long)k);
            clipped = true;
        }
        if (!clipped) {
            // Polygone dégénéré ou auto-sécant : repli en éventail.
            tris.clear();
            for (int i = 1; i + 1 < (int)idx.size(); ++i) {
                tris.push_back(idx[0]);
                tris.push_back(idx[i]);
                tris.push_back(idx[i + 1]);
            }
            return false;
        }
        if (++guard > n * n) return false;
    }
    tris.push_back(idx[0]);
    tris.push_back(idx[1]);
    tris.push_back(idx[2]);
    return true;
}

}  // namespace mesh
