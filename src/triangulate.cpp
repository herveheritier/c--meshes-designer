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
