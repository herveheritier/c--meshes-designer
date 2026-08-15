#include "triangulate.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace mesh {

namespace {

// Aire signée (x2) du triangle (a,b,c).
float triArea2(const Vec2& a, const Vec2& b, const Vec2& c) { return cross(b - a, c - a); }

static bool g_boolDebug = false;  // trace de résolution (débogage temporaire)

// Aire signée (x2) d'un polygone fermé (positif = sens anti-horaire).
float polyArea2(const std::vector<Vec2>& p) {
    float s = 0.0f;
    for (size_t i = 0; i < p.size(); ++i) {
        const Vec2& a = p[i];
        const Vec2& b = p[(i + 1) % p.size()];
        s += a.x * b.y - b.x * a.y;
    }
    return s;
}

// Copie d'un polygone dans l'ordre anti-horaire (sens conservé sinon).
std::vector<Vec2> toCCW(const std::vector<Vec2>& p) {
    if (polyArea2(p) < 0.0f) return std::vector<Vec2>(p.rbegin(), p.rend());
    return p;
}

// Croisement strict des segments [a,b] et [c,d] (points intérieurs) : utile
// pour la recherche de ponts et le contrôle de simplicité des bandes pincées.
// Le partage d'une extrémité n'est pas un croisement. Les produits vectoriels
// sont calculés en double précision : en simple, deux arêtes presque
// parallèles d'une bande en croissant (résultats booléens pincés) donnent des
// signes inversés par pure erreur d'arrondi et font croire à un croisement.
bool segmentsCrossProperly(const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& d) {
    const double d1 = ((double)d.x - c.x) * ((double)a.y - c.y) - ((double)d.y - c.y) * ((double)a.x - c.x);
    const double d2 = ((double)d.x - c.x) * ((double)b.y - c.y) - ((double)d.y - c.y) * ((double)b.x - c.x);
    const double d3 = ((double)b.x - a.x) * ((double)c.y - a.y) - ((double)b.y - a.y) * ((double)c.x - a.x);
    const double d4 = ((double)b.x - a.x) * ((double)d.y - a.y) - ((double)b.y - a.y) * ((double)d.x - a.x);
    const double eps = 1e-12;
    return (d1 < -eps ? -1 : d1 > eps ? 1 : 0) * (d2 < -eps ? -1 : d2 > eps ? 1 : 0) < 0 &&
           (d3 < -eps ? -1 : d3 > eps ? 1 : 0) * (d4 < -eps ? -1 : d4 > eps ? 1 : 0) < 0;
}

// Découpe le polygone `subject` par le demi-plan gauche de l'arête (a,b) :
// on garde les points où cross(b-a, p-a) >= -eps (Sutherland–Hodgman).
std::vector<Vec2> clipByHalfPlane(const std::vector<Vec2>& subject, const Vec2& a,
                                  const Vec2& b) {
    const float eps = 1e-7f;
    std::vector<Vec2> out;
    const int n = (int)subject.size();
    if (n < 3) return out;
    for (int i = 0; i < n; ++i) {
        const Vec2& cur = subject[i];
        const Vec2& nxt = subject[(i + 1) % n];
        const float curSide = cross(b - a, cur - a);
        const float nxtSide = cross(b - a, nxt - a);
        const bool curIn = curSide >= -eps;
        const bool nxtIn = nxtSide >= -eps;
        if (curIn != nxtIn) {
            const float t = curSide / (curSide - nxtSide);
            out.push_back(cur + (nxt - cur) * t);
        }
        if (nxtIn) out.push_back(nxt);
    }
    return out;
}

// Retire les sommets consécutifs confondus (ponts, doublons d'entrée) : un
// sommet coïncidant avec son prédécesseur est simplement retiré, les indices
// suivants étant renumérotés.
void mergeCoincident(std::vector<int>& idx, const std::vector<Vec2>& pts) {
    const float eps = 1e-6f;
    size_t w = 0;
    for (size_t r = 0; r < idx.size(); ++r) {
        if (w > 0 && distance(pts[idx[w - 1]], pts[idx[r]]) < eps) continue;  // jumeau
        idx[w++] = idx[r];
    }
    idx.resize(w);
}

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

// Coupe `poly` le long de la diagonale (i, j) la plus courte (non adjacente,
// intérieure au polygone, sans croiser d'arête) et triangule chaque moitié :
// repli robuste quand aucune oreille n'existe (aiguilles quasi dégénérées,
// boucles qui se touchent presque) — l'éventail classique y produisait des
// triangles faux ou rien du tout. Retourne false si aucun découpage valide.
static bool triangulatePolygonImpl(const std::vector<Vec2>& pts,
                                   const std::vector<int>& idx0,
                                   std::vector<int>& tris, int depth);

// Coupe la boucle le long de la diagonale (i, j) la plus courte (non
// adjacente, strictement intérieure — milieu dans le polygone — et sans
// croiser d'arête) puis triangule chaque moitié : repli robuste quand aucune
// oreille n'existe (aiguilles quasi dégénérées, boucles qui se touchent
// presque) — l'éventail classique y produisait des triangles faux ou rien du
// tout. Retourne false si aucun découpage valide.
static bool triangulateBySplit(const std::vector<Vec2>& pts,
                               const std::vector<int>& idx, std::vector<int>& tris,
                               int depth) {
    const int n = (int)idx.size();
    if (n < 4 || depth > 48) return false;
    std::vector<Vec2> poly;
    poly.reserve(n);
    for (int v : idx) poly.push_back(pts[v]);
    std::vector<std::pair<float, std::pair<int, int>>> cand;
    for (int i = 0; i < n; ++i) {
        for (int j = i + 2; j < n; ++j) {
            if (i == 0 && j == n - 1) continue;  // arête de fermeture
            const Vec2 a = pts[idx[i]];
            const Vec2 b = pts[idx[j]];
            const Vec2 mid{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
            if (!pointInPolygon(mid, poly)) continue;
            bool cross = false;
            for (int k = 0; k < n && !cross; ++k) {
                const int ka = idx[k];
                const int kb = idx[(k + 1) % n];
                if (ka == idx[i] || kb == idx[i] || ka == idx[j] || kb == idx[j])
                    continue;
                if (segmentsCrossProperly(a, b, pts[ka], pts[kb])) cross = true;
            }
            if (cross) continue;
            cand.push_back({dot(a - b, a - b), {i, j}});
        }
    }
    std::sort(cand.begin(), cand.end());
    for (const auto& c : cand) {
        const int i = c.second.first;
        const int j = c.second.second;
        std::vector<int> left, right;
        left.reserve((size_t)(j - i + 1));
        for (int k = i; k <= j; ++k) left.push_back(idx[k]);
        right.reserve((size_t)(n - (j - i) + 1));
        for (int k = j; k < n; ++k) right.push_back(idx[k]);
        for (int k = 0; k <= i; ++k) right.push_back(idx[k]);
        std::vector<int> lTris, rTris;
        if (!triangulatePolygonImpl(pts, left, lTris, depth + 1)) continue;
        if (!triangulatePolygonImpl(pts, right, rTris, depth + 1)) continue;
        tris.insert(tris.end(), lTris.begin(), lTris.end());
        tris.insert(tris.end(), rTris.begin(), rTris.end());
        return true;
    }
    return false;
}

static bool triangulatePolygonImpl(const std::vector<Vec2>& pts,
                                   const std::vector<int>& idx0,
                                   std::vector<int>& tris, int depth) {
    if (depth > 48) return false;
    const int n = (int)idx0.size();
    if (n < 3) return false;
    if (n == 3) {
        if (std::fabs(triArea2(pts[idx0[0]], pts[idx0[1]], pts[idx0[2]])) < 1e-9f)
            return false;
        tris.insert(tris.end(), {idx0[0], idx0[1], idx0[2]});
        return true;
    }
    std::vector<int> idx = idx0;
    mergeCoincident(idx, pts);  // doublons d'entrée (ponts) : un seul sommet par position
    if (idx.size() < 3) return false;
    if (idx.size() == 3) {
        if (std::fabs(triArea2(pts[idx[0]], pts[idx[1]], pts[idx[2]])) < 1e-9f)
            return false;
        tris.insert(tris.end(), {idx[0], idx[1], idx[2]});
        return true;
    }
    const bool cw = polyArea2(pts) < 0.0f;  // orientation globale du polygone
    int guard = 0;
    while (idx.size() > 3) {
        // « Meilleure oreille » : on coupe l'oreille valide dont le triangle
        // a le plus grand ANGLE MINIMAL (le plus proche de l'équilatéral) ;
        // égalité (à une petite tolérance près) → le sommet le plus proche du
        // MILIEU de la boucle courante. Couper la PREMIÈRE oreille valide dans
        // l'ordre de la boucle dégénérait en éventail dès que le polygone
        // était convexe : tous les triangles partageaient le même sommet et
        // les plus éloignés devenaient très allongés. L'angle minimal répartit
        // les sommets entre les triangles ; le départage « milieu de boucle »
        // produit le motif en zigzag optimal sur les polygones réguliers :
        // chaque sommet n'apparaît que dans ~3 triangles, jamais dans tous.
        //
        // NB : maximiser le MINIMUM des trois angles du triangle est le
        // critère standard « best ear » (Delaunay-like). La tolérance d'angle
        // regroupe les oreilles identiques en théorie mais distinctes au bruit
        // flottant près (les cosinus ne sont pas exactement égaux) ; sans
        // elle, l'ordre de parcours re-fabriquait l'éventail. Le « milieu de
        // boucle » est décisif sur les polygones symétriques (octogone
        // régulier…) où tous les angles sont égaux.
        constexpr float kAngTol = 1e-4f;  // radians : regroupe les oreilles ~égales
        const int sz = (int)idx.size();
        int bestK = -1;
        float bestAng = -1.0f;   // meilleur angle minimal (radians)
        float bestMid = 1e9f;    // distance au milieu de la boucle (départage)
        for (size_t k = 0; k < idx.size(); ++k) {
            const int ia = idx[(k + idx.size() - 1) % idx.size()];
            const int ib = idx[k];
            const int ic = idx[(k + 1) % idx.size()];
            const float cr = triArea2(pts[ia], pts[ib], pts[ic]);
            const bool convex = cw ? cr < 1e-7f : cr > 1e-7f;
            if (!convex) continue;
            // Aucun AUTRE sommet ne doit se trouver dans le triangle (oreille).
            // Un sommet coïncidant avec un des trois coins (pont, doublon) ne
            // bloque pas : il est à la même position qu'un coin du triangle.
            bool blocked = false;
            for (int v : idx) {
                if (v == ia || v == ib || v == ic) continue;
                if (distance(pts[v], pts[ia]) < 1e-6f || distance(pts[v], pts[ib]) < 1e-6f ||
                    distance(pts[v], pts[ic]) < 1e-6f)
                    continue;
                if (pointInTriangle(pts[v], pts[ia], pts[ib], pts[ic])) { blocked = true; break; }
            }
            if (blocked) continue;
            // Angle minimal du triangle : acos sur les trois angles (loi des
            // cosinus), borné pour la robustesse. Aire = |cr|/2 (départage).
            const Vec2 ba = pts[ia] - pts[ib];
            const Vec2 cb = pts[ic] - pts[ib];
            const Vec2 ac = pts[ic] - pts[ia];
            const float lab = std::sqrt(dot(ba, ba));
            const float lbc = std::sqrt(dot(cb, cb));
            const float lca = std::sqrt(dot(ac, ac));
            float angMin = 3.14159265358979f;
            auto angleAt = [&](const Vec2& u, const Vec2& v, float lu, float lv) {
                if (lu < 1e-9f || lv < 1e-9f) return 0.0f;
                return std::acos(std::clamp(dot(u, v) / (lu * lv), -1.0f, 1.0f));
            };
            angMin = std::min(angMin, angleAt(pts[ia] - pts[ib], pts[ic] - pts[ib], lab, lbc));
            angMin = std::min(angMin, angleAt(pts[ib] - pts[ia], pts[ic] - pts[ia], lab, lca));
            angMin = std::min(angMin, angleAt(pts[ib] - pts[ic], pts[ia] - pts[ic], lbc, lca));
            // Distance (en indices) du sommet de l'oreille au milieu de la
            // boucle courante : départage qui force le motif en zigzag sur
            // les polygones réguliers (on « pèle » depuis le centre de la
            // boucle au lieu de toujours repartir du même coin).
            const float mid = std::fabs((float)k - (float)(sz - 1) * 0.5f);
            if (bestK < 0 || angMin > bestAng + kAngTol ||
                (angMin >= bestAng - kAngTol && mid < bestMid)) {
                bestAng = angMin;
                bestMid = mid;
                bestK = (int)k;
            }
        }
        if (bestK < 0) {
            // Aiguille dégénérée (triplet de sommets quasi alignés, souvent
            // créé par deux arêtes de frontière presque tangentes) : la
            // pointe, « confondue » avec une arête à la précision des tests
            // de point (pointInTriangle, eps 1e-6 sur les produits
            // vectoriels), bloque toutes les oreilles. On retire le sommet le
            // plus plat (plus petit |cross|) et on reprend la recherche
            // d'oreilles ; le sommet retiré est un pic de mesure nulle.
            const int sz = (int)idx.size();
            int flat = -1;
            float minCross = 1e30f;
            for (int k = 0; k < sz; ++k) {
                const int ia = idx[(k + sz - 1) % sz];
                const int ib = idx[k];
                const int ic = idx[(k + 1) % sz];
                const float cr =
                    std::fabs(triArea2(pts[ia], pts[ib], pts[ic]));
                if (cr < minCross) {
                    minCross = cr;
                    flat = k;
                }
            }
            if (flat >= 0 && minCross < 1e-6f) {
                idx.erase(idx.begin() + flat);
                if (idx.size() > 3) mergeCoincident(idx, pts);
                continue;
            }
            // Vrai échec (polygone auto-sécant ou aiguille résiduelle) :
            // repli par découpage en deux moitiés le long de la diagonale la
            // plus courte — l'éventail classique y produisait des triangles
            // faux ou rien du tout. Les oreilles déjà coupées sont gardées :
            // le découpage ne couvre que le reste de la boucle.
            return triangulateBySplit(pts, idx, tris, depth);
        }
        // Coupe la meilleure oreille.
        const size_t k = (size_t)bestK;
        const int ia = idx[(k + idx.size() - 1) % idx.size()];
        const int ib = idx[k];
        const int ic = idx[(k + 1) % idx.size()];
        tris.push_back(ia);
        tris.push_back(ib);
        tris.push_back(ic);
        idx.erase(idx.begin() + (long)k);
        // Le pont (double arête) peut réunir deux sommets jumeaux : on fond
        // les coïncidents devenus consécutifs.
        if (idx.size() > 3) mergeCoincident(idx, pts);
        if (++guard > n * n) return false;
    }
    // Dernier triangle : un polygone entièrement dégénéré (tous les points
    // alignés — la recherche d'oreille n'a rien trouvé, le retrait d'aiguille
    // a réduit la boucle sans la rendre triangulable) est refusé comme les
    // autres entrées invalides.
    if (std::fabs(triArea2(pts[idx[0]], pts[idx[1]], pts[idx[2]])) < 1e-9f)
        return false;
    tris.push_back(idx[0]);
    tris.push_back(idx[1]);
    tris.push_back(idx[2]);
    return true;
}

// Triangulation d'un polygone simple : enveloppe publique de l'implémentation
// (indices 0..n-1, cas dégénérés rejetés sans rien produire).
bool triangulatePolygon(const std::vector<Vec2>& pts, std::vector<int>& tris) {
    tris.clear();
    const int n = (int)pts.size();
    if (n < 3) return false;
    std::vector<int> idx;
    idx.reserve(n);
    for (int i = 0; i < n; ++i) idx.push_back(i);
    if (!triangulatePolygonImpl(pts, idx, tris, 0)) {
        tris.clear();
        return false;
    }
    return !tris.empty();
}

// Découpe une boucle fermée (dernier point = premier) en boucles simples si un
// sommet apparaît plusieurs fois — deux régions qui se touchent en un sommet
// tracent une figure en 8 qu'il faut séparer pour la triangulation.
void splitRepeatedPoints(std::vector<Vec2> loop,
                         std::vector<std::vector<Vec2>>& out) {
    if (loop.size() >= 2 && distance(loop.front(), loop.back()) < 1e-6f)
        loop.pop_back();
    if (loop.size() < 3) return;
    for (size_t i = 0; i < loop.size(); ++i) {
        for (size_t j = i + 1; j < loop.size(); ++j) {
            if (distance(loop[i], loop[j]) < 1e-6f) {
                std::vector<Vec2> p1(loop.begin() + i, loop.begin() + j + 1);
                std::vector<Vec2> p2;
                p2.reserve(loop.size() - (j - i));
                p2.insert(p2.end(), loop.begin() + j, loop.end());
                p2.insert(p2.end(), loop.begin(), loop.begin() + i + 1);
                splitRepeatedPoints(std::move(p1), out);
                splitRepeatedPoints(std::move(p2), out);
                return;
            }
        }
    }
    if (loop.size() >= 3 && std::fabs(polyArea2(loop)) > 1e-9f)
        out.push_back(std::move(loop));
}

// Insère un trou (boucle CCW) dans le polygone simple CCW `poly` par un pont :
// la paire (sommet du trou, sommet de poly) la plus proche mutuellement
// visible — le segment ne traverse aucune arête ni le trou ni l'extérieur.
// Le trou est déroulé dans `poly` (deux copies de chaque extrémité du pont).
// Retourne false si aucun pont n'a été trouvé.
static bool bridgeHole(std::vector<Vec2>& poly, const std::vector<Vec2>& hole) {
    const int n = (int)poly.size();
    const int m = (int)hole.size();
    if (n < 3 || m < 3) return false;

    // Toutes les paires (sommet de poly, sommet du trou) par distance croissante.
    std::vector<std::pair<float, std::pair<int, int>>> cand;
    cand.reserve((size_t)n * m);
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < m; ++j) {
            const float d = dot(poly[i] - hole[j], poly[i] - hole[j]);
            cand.push_back({d, {i, j}});
        }
    }
    std::sort(cand.begin(), cand.end());

    for (const auto& c : cand) {
        const Vec2 P = poly[c.second.first];   // sommet de poly
        const Vec2 M = hole[c.second.second];  // sommet du trou
        // Le milieu du pont doit être dans `poly` (jamais à travers un trou ni
        // un pont déjà posé) et le segment ne doit traverser aucune arête.
        const Vec2 mid{(P.x + M.x) * 0.5f, (P.y + M.y) * 0.5f};
        if (!pointInPolygon(mid, poly)) continue;
        bool cross = false;
        for (int i = 0; i < n && !cross; ++i)
            if (segmentsCrossProperly(P, M, poly[i], poly[(i + 1) % n])) cross = true;
        for (int j = 0; j < m && !cross; ++j)
            if (segmentsCrossProperly(P, M, hole[j], hole[(j + 1) % m])) cross = true;
        if (cross) continue;

        // Bâtit la chaîne : …O_i, M, hole parcouru EN SENS HORAIRE, M, O_i,
        // O_{i+1}… — le pont est une double arête O_i→M puis M→O_i (O_i
        // apparaît deux fois). Le sens horaire soustrait l'aire du trou de
        // celle de la boucle extérieure (le polygone combiné est simple).
        std::vector<Vec2> chain;
        chain.reserve(m + 1);
        chain.push_back(M);
        for (int k = 1; k < m; ++k) chain.push_back(hole[(c.second.second - k + m) % m]);
        chain.push_back(M);

        std::vector<Vec2> merged;
        merged.reserve(poly.size() + chain.size() + 1);
        for (int i = 0; i <= c.second.first; ++i) merged.push_back(poly[i]);
        merged.insert(merged.end(), chain.begin(), chain.end());
        for (int i = c.second.first; i < n; ++i) merged.push_back(poly[i]);
        poly = std::move(merged);
        return true;
    }
    return false;
}

// Décompose « boucle − trou » en bandes simples quand le trou TOUCHE la boucle
// (résultat pincé des opérations booléennes : aux points de croisement des
// deux ensembles, le trou et la boucle du résultat partagent des sommets). Un
// pont de longueur nulle rendrait le polygone fusionné dégénéré ; à la place,
// chaque paire de sommets partagés consécutifs (ordre CCW de la boucle)
// découpe une bande bordée par l'arc de boucle (CCW) et l'arc de trou (CW)
// correspondant : des polygones simples, triangulés séparément. Les bandes ne
// se touchent qu'aux points partagés (mesure nulle) : leur réunion recouvre
// exactement la zone. `holeCW` : le trou dans son sens HORAIRE (tel que tracé
// par les opérations booléennes). Retourne false si le trou ne touche pas la
// boucle ou si la décomposition échoue (repli sur le pont classique).
static bool decomposePinchedRegion(const std::vector<Vec2>& outer,
                                   const std::vector<Vec2>& holeCW,
                                   std::vector<std::vector<Vec2>>& bands) {
    const int n = (int)outer.size();
    const int m = (int)holeCW.size();
    if (n < 3 || m < 3) return false;
    const float eps = 1e-4f;
    // Sommets partagés : (indice sur la boucle, indice dans le trou).
    std::vector<std::pair<int, int>> shared;
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < m; ++j) {
            if (distance(outer[i], holeCW[j]) < eps) {
                bool dup = false;
                for (const auto& s : shared)
                    if (distance(outer[s.first], outer[i]) < 1e-6f) { dup = true; break; }
                if (!dup) shared.push_back({i, j});
            }
        }
    }
    if (shared.size() < 2) return false;  // 0 ou 1 point partagé : non décomposable
    // Ordre CCW sur la boucle.
    std::sort(shared.begin(), shared.end(),
              [](const std::pair<int, int>& a, const std::pair<int, int>& b) {
                  return a.first < b.first;
              });
    bands.clear();
    for (size_t k = 0; k < shared.size(); ++k) {
        const auto& sA = shared[k];
        const auto& sB = shared[(k + 1) % shared.size()];
        // Arc de boucle CCW de sA vers sB (sB inclus — le sommet partagé est
        // la couture entre l'arc de boucle et l'arc de trou).
        std::vector<Vec2> band;
        for (int i = sA.first;; i = (i + 1) % n) {
            band.push_back(outer[i]);
            if (i == sB.first) break;
        }
        // Arc de trou CW de sB vers sA (le sommet sA est exclu : la bande se
        // referme par la dernière arête du trou). Contrairement au cas simple
        // (deux points partagés isolés), l'arc peut traverser D'AUTRES sommets
        // partagés (trous qui se touchent entre eux et avec la boucle) : on
        // les traverse, la bande reste simple tant qu'ils ne sont pas sur
        // l'arc de boucle de la bande elle-même.
        int guard = 0;
        for (int j = (sB.second + 1) % m;; j = (j + 1) % m) {
            if (++guard > m) return false;  // sécurité : boucle sans fin
            if (distance(holeCW[j], holeCW[sA.second]) < 1e-6f) break;
            band.push_back(holeCW[j]);
        }
        // Bande trop petite (arcs vides ou dégénérés) : rien à découper.
        if (band.size() < 3) continue;
        // Bande d'aire nulle : l'arc de boucle et l'arc de trou coïncident
        // (arête partagée parcourue dans les deux sens) — rien à découper.
        if (polyArea2(band) < 1e-9f) continue;
        // La bande ne doit pas être (une partie de) le trou : l'arc de boucle
        // ne doit pas coïncider avec une chaîne d'arêtes du trou. Deux sommets
        // partagés ADJACENTS sur la boucle dont l'arête (a,b) est AUSSI une
        // arête du trou réduisent la bande au trou lui-même (à exclure, pas à
        // découper) ; si l'arête de boucle n'est pas une arête du trou, la
        // bande est un vrai coin de région. Le test est STRUCTUREL (les
        // arêtes de l'arc de boucle sont-elles dans le trou ?) — un test de
        // centre est trop fragile : le centre d'aire d'une grande bande en
        // croissant qui enveloppe le trou tombe dans le trou.
        {
            const int az = (int)(sB.first - sA.first + n) % n;
            bool sharedChain = az > 0;
            for (int k = 0; k < az && sharedChain; ++k) {
                const Vec2& eA = outer[(sA.first + k) % n];
                const Vec2& eB = outer[(sA.first + k + 1) % n];
                bool inHole = false;
                for (int j = 0; j < m && !inHole; ++j) {
                    const Vec2& hA = holeCW[j];
                    const Vec2& hB = holeCW[(j + 1) % m];
                    if ((distance(eA, hA) < 1e-6f && distance(eB, hB) < 1e-6f) ||
                        (distance(eA, hB) < 1e-6f && distance(eB, hA) < 1e-6f))
                        inHole = true;
                }
                if (!inHole) sharedChain = false;
            }
            if (sharedChain) continue;
        }
        // Bande simple : aucun sommet en double, aucune arête qui se croise.
        bool simple = true;
        const int bz = (int)band.size();
        for (int i = 0; i < bz && simple; ++i)
            for (int j = i + 1; j < bz && simple; ++j)
                if (distance(band[i], band[j]) < 1e-6f) simple = false;
        for (int i = 0; i < bz && simple; ++i) {
            for (int j = i + 1; j < bz; ++j) {
                if ((i + 1) % bz == j || (j + 1) % bz == i) continue;  // arêtes voisines
                if (segmentsCrossProperly(band[i], band[(i + 1) % bz], band[j],
                                          band[(j + 1) % bz])) {
                    simple = false;
                    break;
                }
            }
        }
        if (!simple) return false;  // bande non simple : repli sur le pont classique
        bands.push_back(std::move(band));
    }
    return !bands.empty();
}

bool triangulatePolygonHoles(const std::vector<Vec2>& outer,
                             const std::vector<std::vector<Vec2>>& holes,
                             std::vector<Vec2>& pts, std::vector<int>& tris) {
    pts.clear();
    tris.clear();
    const std::vector<Vec2> region = toCCW(outer);
    if (region.size() < 3) return false;

    // Appartenance à la région (extérieur − trous) : sert à écarter, après
    // découpage, les morceaux qui viennent de l'intérieur d'un trou (les
    // splices de trous pincés laissent des boucles de trou dans la liste).

    // Ordre de traitement des trous : parcours en profondeur depuis la boucle
    // extérieure dans le graphe des sommets partagés (trous qui se touchent
    // entre eux et avec la boucle — résultats pincés des booléens). Un trou
    // qui touche la boucle (ou un trou qui touche un tel trou…) doit être
    // découpé (bandes) ou inséré (splice) AVANT qu'un trou strictement
    // intérieur ne soit ponté : le pont insère le trou dans la boucle et
    // corromprait les arcs de boucle que le trou pincé utilise pour ses
    // bandes (arc passant à travers la boucle du trou ponté → bande non
    // simple → repli splice → polygone auto-touchant multiple → oreilles
    // fausses qui recouvrent les trous).
    const float shEps = 1e-4f;
    const int H = (int)holes.size();
    std::vector<char> sharesOuter(H, 0), vis(H, 0);
    std::vector<std::vector<int>> graph(H);
    for (int i = 0; i < H; ++i) {
        for (const Vec2& a : holes[i]) {
            for (const Vec2& b : region)
                if (distance(a, b) < shEps) { sharesOuter[i] = 1; break; }
            if (sharesOuter[i]) break;
        }
    }
    for (int i = 0; i < H; ++i) {
        for (int j = i + 1; j < H; ++j) {
            bool touch = false;
            for (const Vec2& a : holes[i]) {
                for (const Vec2& b : holes[j])
                    if (distance(a, b) < shEps) { touch = true; break; }
                if (touch) break;
            }
            if (touch) {
                graph[i].push_back(j);
                graph[j].push_back(i);
            }
        }
    }
    std::vector<int> order;
    order.reserve(H);
    for (int start = 0; start < H; ++start) {
        if (!sharesOuter[start] || vis[start]) continue;
        std::vector<int> stack{start};
        vis[start] = 1;
        while (!stack.empty()) {
            const int cur = stack.back();
            stack.pop_back();
            order.push_back(cur);
            for (int nb : graph[cur])
                if (!vis[nb]) {
                    vis[nb] = 1;
                    stack.push_back(nb);
                }
        }
    }
    for (int i = 0; i < H; ++i)
        if (!vis[i]) order.push_back(i);  // composantes strictement intérieures

    // Polygones simples à trianguler : au départ la boucle extérieure ; les
    // trous strictement intérieurs y sont pontés (bridgeHole), un trou qui
    // touche la boucle la découpe en bandes simples (décomposition pincée), un
    // trou qui ne la touche qu'en un sommet y est inséré directement (pont de
    // longueur nulle) — la boucle fusionnée est alors auto-touchante en ce
    // point mais reste simple, l'oreille en fait le tour.
    std::vector<std::vector<Vec2>> polys;
    polys.push_back(region);
    for (int hi : order) {
        const auto& h = holes[hi];
        // Trou dégénéré, vide : ignoré.
        if (h.size() < 3) continue;
        std::vector<Vec2> hh = toCCW(h);
        if (std::fabs(polyArea2(hh)) < 1e-9f) continue;
        // Le trou n'appartient à un polygone QUE si son intérieur y est : un
        // sommet du trou posé sur la frontière (trou pincé partageant un
        // sommet avec la boucle) ne suffit pas — sinon le trou serait soustrait
        // de plusieurs bandes à la fois. Le centre du trou (moyenne des
        // sommets) est strictement dans le polygone contenant son intérieur.
        Vec2 hc{0.0f, 0.0f};
        for (const Vec2& v : hh) {
            hc.x += v.x;
            hc.y += v.y;
        }
        hc.x /= (float)hh.size();
        hc.y /= (float)hh.size();
        std::vector<std::vector<Vec2>> next;
        for (const auto& poly : polys) {
            if (!pointInPolygon(hc, poly)) {
                next.push_back(poly);
                continue;
            }
            std::vector<std::vector<Vec2>> bands;
            if (decomposePinchedRegion(poly, h, bands)) {
                for (auto& b : bands) next.push_back(std::move(b));
                continue;
            }
            // Sommet partagé : insertion directe dans la boucle (pont de
            // longueur nulle). Le trou est déroulé en sens HORAIRE depuis le
            // sommet partagé puis on revient au même sommet : comme le pont
            // classique, le sens horaire soustrait l'aire du trou de celle de
            // la boucle. La boucle fusionnée se touche en ce sommet, mais
            // l'oreille (triangulatePolygon) gère les doublons de pont.
            const float eps = 1e-4f;
            int pi = -1, hj = -1;
            for (int i = 0; i < (int)poly.size() && pi < 0; ++i)
                for (int j = 0; j < (int)hh.size(); ++j)
                    if (distance(poly[i], hh[j]) < eps) { pi = i; hj = j; break; }
            if (pi >= 0) {
                std::vector<Vec2> merged;
                merged.reserve(poly.size() + hh.size());
                for (int i = 0; i <= pi; ++i) merged.push_back(poly[i]);
                for (int k = 1; k < (int)hh.size(); ++k)
                    merged.push_back(hh[(hj - k + (int)hh.size()) % hh.size()]);
                merged.push_back(poly[pi]);
                for (int i = pi + 1; i < (int)poly.size(); ++i)
                    merged.push_back(poly[i]);
                next.push_back(std::move(merged));
                continue;
            }
            // Trou strictement intérieur : pont classique.
            std::vector<Vec2> merged = poly;
            if (!bridgeHole(merged, hh)) {
                next.push_back(poly);  // pont introuvable : on garde tel quel
                continue;
            }
            next.push_back(std::move(merged));
        }
        polys.swap(next);
    }

    // Triangule chaque polygone simple ; points dédupliqués par position.
    bool any = false;
    const float eps = 1e-6f;
    for (const auto& poly : polys) {
        std::vector<int> ltris;
        if (!triangulatePolygon(poly, ltris)) continue;
        std::vector<int> remap(poly.size(), -1);
        for (size_t i = 0; i < poly.size(); ++i) {
            for (size_t j = 0; j < pts.size(); ++j) {
                if (distance(poly[i], pts[j]) < eps) { remap[i] = (int)j; break; }
            }
            if (remap[i] < 0) {
                remap[i] = (int)pts.size();
                pts.push_back(poly[i]);
            }
        }
        for (int t : ltris) tris.push_back(remap[t]);
        any = true;
    }
    return any;
}

// Segment de frontière du résultat : dirigé (a→b) pour que le résultat soit à
// gauche ; `curve` rappelle la courbe d'origine (0 = sujet, 1 = découpe) pour
// résoudre les points de contact.
struct BoundSeg {
    Vec2 a, b;
    int curve = 0;
    bool used = false;
};



// Intersection de deux segments (extrémités comprises) ; le point est écrit
// dans `out`. Retourne false si les segments sont parallèles.
bool segmentIntersect(const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& d, Vec2& out) {
    const Vec2 ab = b - a;
    const Vec2 cd = d - c;
    const float denom = cross(ab, cd);
    if (std::fabs(denom) < 1e-12f) return false;
    const Vec2 ac = c - a;
    const float t = cross(ac, cd) / denom;
    const float u = cross(ac, ab) / denom;
    const float eps = 1e-6f;
    if (t < -eps || t > 1.0f + eps || u < -eps || u > 1.0f + eps) return false;
    out = a + ab * std::clamp(t, 0.0f, 1.0f);
    return true;
}

// Découpe l'arête (a,b) aux intersections avec les arêtes des polygones
// `others` et aux sommets de ces polygones posés dessus (t-jonctions).
// Retourne les points de subdivision triés le long du segment, dédupliqués.
std::vector<Vec2> splitEdge(const Vec2& a, const Vec2& b,
                            const std::vector<std::vector<Vec2>>& others) {
    const Vec2 ab = b - a;
    const float len2 = dot(ab, ab);
    std::vector<std::pair<float, Vec2>> hits;
    hits.push_back({0.0f, a});
    hits.push_back({1.0f, b});
    const float eps = 1e-7f;
    for (const auto& poly : others) {
        const int n = (int)poly.size();
        for (int i = 0; i < n; ++i) {
            const Vec2& c = poly[i];
            const Vec2& d = poly[(i + 1) % n];
            Vec2 x;
            if (segmentIntersect(a, b, c, d, x)) {
                const float t = std::clamp(dot(x - a, ab) / len2, 0.0f, 1.0f);
                hits.push_back({t, x});
            }
            if (pointSegmentDistance(c, a, b) < eps) {
                const float t = dot(c - a, ab) / len2;
                if (t > 1e-6f && t < 1.0f - 1e-6f) hits.push_back({t, c});
            }
            if (pointSegmentDistance(d, a, b) < eps) {
                const float t = dot(d - a, ab) / len2;
                if (t > 1e-6f && t < 1.0f - 1e-6f) hits.push_back({t, d});
            }
        }
    }
    std::sort(hits.begin(), hits.end(),
              [](const std::pair<float, Vec2>& x, const std::pair<float, Vec2>& y) {
                  return x.first < y.first;
              });
    std::vector<Vec2> out;
    for (const auto& h : hits) {
        if (out.empty() || distance(out.back(), h.second) > 1e-6f) out.push_back(h.second);
    }
    return out;
}

bool subtractPolygon(const std::vector<Vec2>& subject, const std::vector<Vec2>& cut,
                     std::vector<Vec2>& pts, std::vector<int>& tris) {
    pts.clear();
    tris.clear();
    if (subject.size() < 3 || cut.size() < 3) return false;

    std::vector<Vec2> subj = toCCW(subject);
    std::vector<Vec2> cutCCW = toCCW(cut);
    if (std::fabs(polyArea2(subj)) < 1e-9f || std::fabs(polyArea2(cutCCW)) < 1e-9f) return false;

    // Parties de la découpe (triangles) : on soustrait leur réunion.
    std::vector<std::vector<Vec2>> cutParts;
    {
        std::vector<int> ct;
        if (!triangulatePolygon(cutCCW, ct)) return false;
        cutParts.reserve(ct.size() / 3);
        for (size_t i = 0; i + 2 < ct.size(); i += 3)
            cutParts.push_back({cutCCW[ct[i]], cutCCW[ct[i + 1]], cutCCW[ct[i + 2]]});
    }

    // Zones de la découpe à l'intérieur du sujet (composantes disjointes).
    std::vector<std::vector<Vec2>> overlaps;
    for (const auto& part : cutParts) {
        std::vector<Vec2> clipped = part;
        for (int e = 0; e < (int)subj.size() && clipped.size() >= 3; ++e)
            clipped = clipByHalfPlane(clipped, subj[e], subj[(e + 1) % subj.size()]);
        if (clipped.size() >= 3 && std::fabs(polyArea2(clipped)) > 1e-9f)
            overlaps.push_back(toCCW(clipped));
    }
    if (overlaps.empty()) return false;  // la découpe ne touche pas le sujet

    // Surface totale recouverte : si elle égale celle du sujet, il disparaît.
    float cutArea = 0.0f;
    for (const auto& o : overlaps) cutArea += std::fabs(polyArea2(o)) * 0.5f;
    const float subjArea = std::fabs(polyArea2(subj)) * 0.5f;
    if (cutArea >= subjArea - 1e-4f * std::max(1.0f, subjArea)) return true;  // recouvert

    // --- Frontière du résultat : arêtes du sujet HORS des zones + arêtes des
    // zones DANS le sujet, dirigées pour que le résultat soit à gauche. ---
    std::vector<BoundSeg> segs;
    bool droppedS = false;  // une partie d'une arête du sujet a été éliminée
    for (int i = 0; i < (int)subj.size(); ++i) {
        const std::vector<Vec2> sp =
            splitEdge(subj[i], subj[(i + 1) % subj.size()], overlaps);
        for (size_t k = 0; k + 1 < sp.size(); ++k) {
            const Vec2 mid{(sp[k].x + sp[k + 1].x) * 0.5f, (sp[k].y + sp[k + 1].y) * 0.5f};
            bool in = false;
            for (const auto& o : overlaps)
                if (pointInPolygon(mid, o)) { in = true; break; }
            if (in) { droppedS = true; continue; }
            segs.push_back({sp[k], sp[k + 1], 0, false});
        }
    }
    bool keptO = false;  // une arête de la découpe pénètre dans le sujet
    for (const auto& o : overlaps) {
        for (int i = 0; i < (int)o.size(); ++i) {
            const std::vector<Vec2> sp = splitEdge(o[i], o[(i + 1) % o.size()], {subj});
            for (size_t k = 0; k + 1 < sp.size(); ++k) {
                const Vec2 mid{(sp[k].x + sp[k + 1].x) * 0.5f, (sp[k].y + sp[k + 1].y) * 0.5f};
                if (!pointInPolygon(mid, subj)) continue;  // hors du sujet
                keptO = true;
                // Sens horaire : le résultat (extérieur de la zone) reste à gauche.
                segs.push_back({sp[k + 1], sp[k], 1, false});
            }
        }
    }
    if (!droppedS && !keptO) return false;  // simple contact sans surface retirée
    if (segs.empty()) return true;          // entièrement recouvert : résultat vide

    // --- Annulation des arêtes internes ---
    // Les zones de recouvrement sont issues des triangles de la découpe : deux
    // zones voisines partagent une arête (diagonale) qui apparaît donc DEUX
    // fois dans `segs`, en sens opposés. Ces arêtes internes ne font pas
    // partie de la frontière du résultat et doivent être retirées — sinon le
    // tracé des boucles se perd aux nœuds ambigus et une découpe de coin qui
    // déborde du sujet renvoyait un résultat vide.
    //
    // NB : la tolérance est volontairement plus stricte que celle du tracé de
    // boucles (1e-6 ici, 1e-4 là-bas) : une diagonale partagée réutilise
    // exactement les mêmes valeurs flottantes, donc 1e-6 suffit et évite
    // d'annuler à tort deux arêtes de frontière voisines mais distinctes.
    {
        const float seps = 1e-6f;
        std::vector<bool> drop(segs.size(), false);
        for (size_t i = 0; i < segs.size(); ++i) {
            if (drop[i]) continue;
            for (size_t j = i + 1; j < segs.size(); ++j) {
                if (drop[j]) continue;
                if (distance(segs[i].a, segs[j].b) < seps &&
                    distance(segs[i].b, segs[j].a) < seps) {
                    drop[i] = drop[j] = true;  // même arête, sens opposés : interne
                    break;
                }
            }
        }
        std::vector<BoundSeg> kept;
        kept.reserve(segs.size());
        for (size_t i = 0; i < segs.size(); ++i)
            if (!drop[i]) kept.push_back(segs[i]);
        segs.swap(kept);
    }
    if (segs.empty()) return true;  // frontière entièrement doublée : dégénéré → couvert

    // --- Tracé des boucles : à chaque nœud on continue sur la même courbe
    // quand c'est possible (point de contact), sinon on bascule (croisement). ---
    const float eps = 1e-4f;
    std::vector<std::vector<Vec2>> loops;
    for (size_t s0 = 0; s0 < segs.size(); ++s0) {
        if (segs[s0].used) continue;
        std::vector<Vec2> loop;
        std::vector<int> usedHere;
        int idx = (int)s0;
        int curve = segs[s0].curve;
        const Vec2 startPt = segs[s0].a;
        bool closed = false;
        const int guardMax = (int)segs.size() * 2 + 8;
        for (int guard = 0; guard < guardMax; ++guard) {
            if (segs[idx].used) break;  // chemin déjà parcouru : brisé
            segs[idx].used = true;
            usedHere.push_back(idx);
            loop.push_back(segs[idx].a);
            const Vec2 to = segs[idx].b;
            if (distance(to, startPt) < eps) { closed = true; break; }
            int same = -1, other = -1;
            for (size_t j = 0; j < segs.size(); ++j) {
                if (segs[j].used || distance(segs[j].a, to) >= eps) continue;
                if (segs[j].curve == curve) same = (int)j;
                else other = (int)j;
            }
            if (same >= 0) idx = same;
            else if (other >= 0) idx = other;
            else break;  // impasse (dégénéré)
            curve = segs[idx].curve;
        }
        if (!closed) {
            for (int i : usedHere) segs[i].used = false;  // ne rien perdre
            continue;
        }
        std::vector<Vec2> cl;
        for (const Vec2& p : loop)
            if (cl.empty() || distance(cl.back(), p) > 1e-6f) cl.push_back(p);
        if (cl.size() >= 3 && std::fabs(polyArea2(cl)) > 1e-9f) loops.push_back(std::move(cl));
    }

    // --- Groupement : boucles anti-horaires = extérieurs, horaires = trous. ---
    std::vector<std::vector<Vec2>> outers;
    std::vector<std::vector<std::vector<Vec2>>> holeGroups;
    for (const auto& lp : loops) {
        if (polyArea2(lp) > 0.0f) {
            outers.push_back(lp);
            holeGroups.emplace_back();
        }
    }
    if (outers.empty()) return true;  // uniquement des boucles horaires : vide
    for (const auto& lp : loops) {
        if (polyArea2(lp) > 0.0f) continue;  // un extérieur
        const Vec2 p = lp[0];
        for (size_t i = 0; i < outers.size(); ++i) {
            if (pointInPolygon(p, outers[i])) {
                holeGroups[i].push_back(lp);
                break;
            }
        }
    }

    // --- Triangulation de chaque composante (extérieur + ses trous). ---
    for (size_t i = 0; i < outers.size(); ++i) {
        std::vector<Vec2> oPts;
        std::vector<int> oTris;
        if (!triangulatePolygonHoles(outers[i], holeGroups[i], oPts, oTris)) continue;
        const int base = (int)pts.size();
        pts.insert(pts.end(), oPts.begin(), oPts.end());
        for (int t : oTris) tris.push_back(base + t);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Opérations ensemblistes (booléennes) PAR FRONTIÈRE : A et B sont traités
// comme des régions polygonales (réunion de leurs triangles). On construit la
// frontière du résultat à partir des frontières de A et B (découpées aux
// intersections, classées de part et d'autre, arêtes coïncidentes départagées
// par un test latéral), on trace les boucles fermées (extérieurs + trous) et
// la triangulation est laissée à l'appelant. Le résultat est minimal : pas de
// coutures internes, pas de fragments le long des diagonales internes des
// ensembles, et les faces d'un ensemble entièrement recouvert (ex. B dans une
// différence A−B) disparaissent naturellement.
// ---------------------------------------------------------------------------

namespace {

// Clé d'une arête orientée, quantifiée sur une grille fine : une arête
// partagée par deux faces réutilise exactement les mêmes coordonnées de
// sommets, donc l'annulation des paires opposées est exacte.
struct EdgeKey {
    int64_t ax, ay, bx, by;
    bool operator==(const EdgeKey& o) const {
        return ax == o.ax && ay == o.ay && bx == o.bx && by == o.by;
    }
};
struct EdgeKeyHash {
    size_t operator()(const EdgeKey& k) const {
        uint64_t h = (uint64_t)k.ax * 0x9E3779B97F4A7C15ull;
        h ^= (uint64_t)k.ay * 0xBF58476D1CE4E5B9ull;
        h ^= (uint64_t)k.bx * 0x94D049BB133111EBull;
        h ^= (uint64_t)k.by * 0x8DAEB9D1A1E2F3C5ull;
        h ^= h >> 33;
        return (size_t)h;
    }
};

inline EdgeKey edgeKey(const Vec2& a, const Vec2& b) {
    auto q = [](float v) { return (int64_t)std::llround((double)v * 1e6); };
    return {q(a.x), q(a.y), q(b.x), q(b.y)};
}

// Arêtes de frontière de la réunion des triangles d'un ensemble : chaque
// arête est orientée pour que l'intérieur de l'ensemble soit à gauche
// (l'orientation est déduite de l'aire signée de chaque triangle, insensible
// au sens de la boucle des faces), puis les paires opposées (arêtes internes
// partagées par deux faces) sont annulées. `curve` : 0 = ensemble A, 1 = B.
void extractBoundary(const std::vector<Vec2>& tris, int curve,
                     std::vector<BoundSeg>& out) {
    out.clear();
    struct Entry {
        std::array<Vec2, 2> seg;  // points originaux (non quantifiés)
        int count = 0;            // occurrences du sens direct
    };
    std::unordered_map<EdgeKey, Entry, EdgeKeyHash> edges;
    for (size_t t = 0; t + 2 < tris.size(); t += 3) {
        const Vec2& p0 = tris[t];
        const Vec2& p1 = tris[t + 1];
        const Vec2& p2 = tris[t + 2];
        if (std::fabs(cross(p1 - p0, p2 - p0)) < 1e-12f) continue;  // dégénéré
        const bool ccw = cross(p1 - p0, p2 - p0) > 0.0f;
        // Arêtes orientées intérieur-à-gauche (l'ordre CCW donne cet effet).
        const std::array<Vec2, 6> dir = ccw ? std::array<Vec2, 6>{p0, p1, p1, p2, p2, p0}
                                            : std::array<Vec2, 6>{p0, p2, p2, p1, p1, p0};
        for (int k = 0; k < 6; k += 2) {
            const EdgeKey key = edgeKey(dir[k], dir[k + 1]);
            const EdgeKey rev = edgeKey(dir[k + 1], dir[k]);
            auto it = edges.find(rev);
            if (it != edges.end() && it->second.count > 0) {
                if (--it->second.count == 0) edges.erase(it);
            } else {
                Entry& e = edges[key];
                if (e.count == 0) e.seg = {dir[k], dir[k + 1]};
                ++e.count;
            }
        }
    }
    for (const auto& kv : edges)
        if (kv.second.count > 0)  // une seule copie par arête de frontière
            out.push_back({kv.second.seg[0], kv.second.seg[1], curve, false});
}

// Découpe le segment (a,b) aux intersections avec les segments `others`
// (croisements propres et extrémités posées dessus, t-jonctions). Points de
// subdivision triés le long du segment, dédupliqués. Le segment lui-même est
// toujours présent, même dégénéré.
std::vector<Vec2> splitSegBySegs(const Vec2& a, const Vec2& b,
                                 const std::vector<BoundSeg>& others) {
    std::vector<Vec2> out{a, b};
    const Vec2 ab = b - a;
    const float len2 = dot(ab, ab);
    if (len2 < 1e-20f) return out;
    std::vector<std::pair<float, Vec2>> hits;
    hits.push_back({0.0f, a});
    hits.push_back({1.0f, b});
    const float eps = 1e-7f;
    const float minX = std::min(a.x, b.x) - eps, maxX = std::max(a.x, b.x) + eps;
    const float minY = std::min(a.y, b.y) - eps, maxY = std::max(a.y, b.y) + eps;
    for (const auto& o : others) {
        // Préfiltre boîtes englobantes : un croisement exige des boîtes qui
        // se touchent — évite le test complet segment×segment à distance.
        if (std::max(o.a.x, o.b.x) < minX || std::min(o.a.x, o.b.x) > maxX ||
            std::max(o.a.y, o.b.y) < minY || std::min(o.a.y, o.b.y) > maxY)
            continue;
        Vec2 x;
        if (segmentIntersect(a, b, o.a, o.b, x)) {
            const float t = std::clamp(dot(x - a, ab) / len2, 0.0f, 1.0f);
            hits.push_back({t, x});
        }
        for (const Vec2& c : {o.a, o.b}) {
            if (pointSegmentDistance(c, a, b) < eps) {
                const float t = dot(c - a, ab) / len2;
                if (t > 1e-6f && t < 1.0f - 1e-6f) hits.push_back({t, c});
            }
        }
    }
    std::sort(hits.begin(), hits.end(),
              [](const std::pair<float, Vec2>& x, const std::pair<float, Vec2>& y) {
                  return x.first < y.first;
              });
    out.clear();
    for (const auto& h : hits)
        if (out.empty() || distance(out.back(), h.second) > 1e-6f) out.push_back(h.second);
    return out;
}

// Découpe chaque segment de `in` aux intersections avec les segments de
// `others`, et écrit les sous-segments (même courbe) dans `out`.
void splitBoundaryBy(const std::vector<BoundSeg>& in,
                     const std::vector<BoundSeg>& others,
                     std::vector<BoundSeg>& out) {
    out.clear();
    out.reserve(in.size() * 2);
    for (const auto& s : in) {
        const std::vector<Vec2> sp = splitSegBySegs(s.a, s.b, others);
        for (size_t k = 0; k + 1 < sp.size(); ++k)
            if (distance(sp[k], sp[k + 1]) > 1e-7f)
                out.push_back({sp[k], sp[k + 1], s.curve, false});
    }
}

// Vrai si le point est dans la réunion des triangles `tris` (liste plate) —
// frontière incluse (une arête coïncidente est « dedans »).
bool pointInTris(const Vec2& p, const std::vector<Vec2>& tris) {
    for (size_t i = 0; i + 2 < tris.size(); i += 3)
        if (pointInTriangle(p, tris[i], tris[i + 1], tris[i + 2])) return true;
    return false;
}

// Appartenance à l'autre ensemble de part et d'autre du segment, à une petite
// distance perpendiculaire : départage les arêtes coïncidentes, que le test
// du centre (posé sur la frontière de l'autre ensemble) ne sait pas classer.
struct Sides {
    bool l = false, r = false;
};

Sides segmentSides(const BoundSeg& s, const std::vector<Vec2>& tris) {
    const Vec2 d = s.b - s.a;
    const float len = length(d);
    if (len < 1e-12f) return {};
    const Vec2 n{-d.y / len, d.x / len};  // normale gauche unitaire
    const float off = std::max(1e-4f * len, 1e-6f);
    const Vec2 mid{(s.a.x + s.b.x) * 0.5f, (s.a.y + s.b.y) * 0.5f};
    return {pointInTris(mid + n * off, tris), pointInTris(mid - n * off, tris)};
}

// Morceau de frontière dont le centre est dans l'AUTRE ensemble (ou sur sa
// frontière) : gardé par l'opération ? `curveA` : morceau de A (sinon de B),
// `sd` : appartenance à l'autre ensemble des côtés gauche / droite.
bool keepPiece(SetOp op, bool curveA, const Sides& sd) {
    if (curveA) {
        switch (op) {
            case SetOp::Union:        return !sd.r;  // le côté extérieur à A hors de B
            case SetOp::Intersection: return sd.l;   // l'intérieur de A dans B
            case SetOp::Difference:   return !sd.l;  // l'intérieur de A hors de B
            case SetOp::SymDiff:      return sd.l == sd.r;
        }
    } else {
        switch (op) {
            case SetOp::Union:        return !sd.r;
            case SetOp::Intersection: return sd.l;
            case SetOp::Difference:   return sd.r;   // B dans A : gardé en sens inverse
            case SetOp::SymDiff:      return sd.l == sd.r;
        }
    }
    return false;
}

// Le morceau gardé doit-il être inversé (l'intérieur du résultat est à droite) ?
bool reversePiece(SetOp op, bool curveA, const Sides& sd) {
    if (curveA) return op == SetOp::SymDiff && sd.l && sd.r;
    return op == SetOp::Difference || (op == SetOp::SymDiff && sd.l && sd.r);
}

// Applique l'opération : conserve les morceaux de frontière du résultat, avec
// leur direction définitive. Un morceau strictement hors de l'autre ensemble
// suit une règle simple (pas de test latéral) ; seul le cas « centre dans
// l'autre ensemble ou sur sa frontière » demande les côtés.
void selectBoundary(SetOp op, const std::vector<BoundSeg>& splitA,
                    const std::vector<BoundSeg>& splitB,
                    const std::vector<Vec2>& a, const std::vector<Vec2>& b,
                    std::vector<BoundSeg>& out) {
    out.clear();
    out.reserve(splitA.size() + splitB.size());
    for (const auto& s : splitA) {
        const Vec2 mid{(s.a.x + s.b.x) * 0.5f, (s.a.y + s.b.y) * 0.5f};
        if (g_boolDebug)
            std::printf("    [selA] (%.4f,%.4f)->(%.4f,%.4f) dansB=%d\n", s.a.x,
                        s.a.y, s.b.x, s.b.y, (int)pointInTris(mid, b));
        if (!pointInTris(mid, b)) {
            if (op != SetOp::Intersection) out.push_back(s);
            continue;
        }
        const Sides sd = segmentSides(s, b);
        if (g_boolDebug)
            std::printf("    [selA] côtés l=%d r=%d keep=%d\n", (int)sd.l,
                        (int)sd.r, (int)keepPiece(op, true, sd));
        if (keepPiece(op, true, sd)) {
            BoundSeg k = s;
            if (reversePiece(op, true, sd)) std::swap(k.a, k.b);
            out.push_back(k);
        }
    }
    for (const auto& s : splitB) {
        const Vec2 mid{(s.a.x + s.b.x) * 0.5f, (s.a.y + s.b.y) * 0.5f};
        if (!pointInTris(mid, a)) {
            if (op == SetOp::Union || op == SetOp::SymDiff) out.push_back(s);
            continue;
        }
        const Sides sd = segmentSides(s, a);
        if (keepPiece(op, false, sd)) {
            BoundSeg k = s;
            if (reversePiece(op, false, sd)) std::swap(k.a, k.b);
            out.push_back(k);
        }
    }
}

// Retire les doublons de la frontière du résultat : deux morceaux coïncidents
// en sens opposés s'annulent (frontière interne), deux morceaux identiques
// n'en gardent qu'un (arêtes coïncidentes de A et B conservées des deux côtés).
void dedupeBoundary(std::vector<BoundSeg>& segs) {
    const float eps = 1e-6f;
    std::vector<bool> drop(segs.size(), false);
    for (size_t i = 0; i < segs.size(); ++i) {
        if (drop[i]) continue;
        for (size_t j = i + 1; j < segs.size(); ++j) {
            if (drop[j]) continue;
            const bool same = distance(segs[i].a, segs[j].a) < eps &&
                              distance(segs[i].b, segs[j].b) < eps;
            const bool opp = distance(segs[i].a, segs[j].b) < eps &&
                             distance(segs[i].b, segs[j].a) < eps;
            if (same) {
                drop[j] = true;
            } else if (opp) {
                drop[i] = drop[j] = true;
                break;
            }
        }
    }
    std::vector<BoundSeg> kept;
    kept.reserve(segs.size());
    for (size_t i = 0; i < segs.size(); ++i)
        if (!drop[i]) kept.push_back(segs[i]);
    segs.swap(kept);
}

std::vector<std::vector<Vec2>> traceLoops(std::vector<BoundSeg>& segs) {
    std::vector<std::vector<Vec2>> loops;
    const float eps = 1e-4f;
    for (size_t s0 = 0; s0 < segs.size(); ++s0) {
        if (segs[s0].used) continue;
        std::vector<Vec2> loop;
        std::vector<int> usedHere;
        int idx = (int)s0;
        int curve = segs[s0].curve;
        const Vec2 startPt = segs[s0].a;
        bool closed = false;
        const int guardMax = (int)segs.size() * 2 + 8;
        for (int guard = 0; guard < guardMax; ++guard) {
            if (segs[idx].used) break;  // chemin déjà parcouru : brisé
            segs[idx].used = true;
            usedHere.push_back(idx);
            loop.push_back(segs[idx].a);
            const Vec2 to = segs[idx].b;
            if (distance(to, startPt) < eps) { closed = true; break; }
            int same = -1, other = -1;
            for (size_t j = 0; j < segs.size(); ++j) {
                if (segs[j].used || distance(segs[j].a, to) >= eps) continue;
                if (segs[j].curve == curve) same = (int)j;
                else other = (int)j;
            }
            if (same >= 0) idx = same;
            else if (other >= 0) idx = other;
            else break;  // impasse (dégénéré)
            curve = segs[idx].curve;
        }
        if (!closed) {
            for (int i : usedHere) segs[i].used = false;  // ne rien perdre
            continue;
        }
        std::vector<Vec2> cl;
        for (const Vec2& p : loop)
            if (cl.empty() || distance(cl.back(), p) > 1e-6f) cl.push_back(p);
        if (cl.size() >= 3) splitRepeatedPoints(std::move(cl), loops);
    }
    return loops;
}

}  // namespace

bool triangleSetBooleanDebug() { return g_boolDebug; }
void setTriangleSetBooleanDebug(bool on) { g_boolDebug = on; }

void triangleSetBoolean(SetOp op, const std::vector<Vec2>& a,
                        const std::vector<Vec2>& b, std::vector<BoolRegion>& out) {
    out.clear();
    if (a.size() < 3 || b.size() < 3) return;

    // Frontières des deux régions (arêtes internes annulées).
    std::vector<BoundSeg> bndA, bndB;
    extractBoundary(a, 0, bndA);
    extractBoundary(b, 1, bndB);
    if (bndA.empty() || bndB.empty()) return;

    // Découpe aux intersections et t-jonctions, puis sélection de la
    // frontière du résultat.
    std::vector<BoundSeg> splitA, splitB;
    splitBoundaryBy(bndA, bndB, splitA);
    splitBoundaryBy(bndB, bndA, splitB);

    std::vector<BoundSeg> kept;
    selectBoundary(op, splitA, splitB, a, b, kept);
    if (g_boolDebug) {
        std::printf("[bool] op=%d splitA=%zu splitB=%zu keptAvantDedup=%zu\n",
                    (int)op, splitA.size(), splitB.size(), kept.size());
        for (size_t i = 0; i < splitA.size(); ++i)
            std::printf("  A%zu (%.4f,%.4f)->(%.4f,%.4f) milieuDansB=%d\n", i,
                        splitA[i].a.x, splitA[i].a.y, splitA[i].b.x, splitA[i].b.y,
                        (int)pointInTris(
                            {(splitA[i].a.x + splitA[i].b.x) * 0.5f,
                             (splitA[i].a.y + splitA[i].b.y) * 0.5f},
                            b));
        for (size_t i = 0; i < splitB.size(); ++i)
            std::printf("  B%zu (%.4f,%.4f)->(%.4f,%.4f) milieurDansA=%d\n", i,
                        splitB[i].a.x, splitB[i].a.y, splitB[i].b.x, splitB[i].b.y,
                        (int)pointInTris(
                            {(splitB[i].a.x + splitB[i].b.x) * 0.5f,
                             (splitB[i].a.y + splitB[i].b.y) * 0.5f},
                            a));
        for (size_t i = 0; i < kept.size(); ++i)
            std::printf("  garde%zu curve=%d (%.4f,%.4f)->(%.4f,%.4f)\n", i,
                        kept[i].curve, kept[i].a.x, kept[i].a.y, kept[i].b.x,
                        kept[i].b.y);
    }
    if (kept.empty()) return;
    dedupeBoundary(kept);
    if (kept.empty()) return;

    if (g_boolDebug) {
        std::printf("[bool] op=%d keptApresDedup=%zu\n", (int)op, kept.size());
        for (size_t i = 0; i < kept.size(); ++i)
            std::printf("  seg%zu curve=%d (%.4f,%.4f)->(%.4f,%.4f)\n", i,
                        kept[i].curve, kept[i].a.x, kept[i].a.y, kept[i].b.x,
                        kept[i].b.y);
    }

    // Boucles fermées du résultat, regroupées en composantes (extérieur + ses
    // trous) : une composante = un CCW (extérieur) et ses CW (trous).
    const std::vector<std::vector<Vec2>> loops = traceLoops(kept);
    if (g_boolDebug) {
        std::printf("[bool] boucles=%zu\n", loops.size());
        for (size_t i = 0; i < loops.size(); ++i) {
            std::printf("  boucle%zu aire=%.4f n=%zu :", i, polyArea2(loops[i]),
                        loops[i].size());
            for (const Vec2& p : loops[i])
                std::printf(" (%.4f,%.4f)", p.x, p.y);
            std::printf("\n");
        }
    }
    if (loops.empty()) return;
    std::vector<std::vector<Vec2>> outers;
    std::vector<std::vector<std::vector<Vec2>>> holeGroups;
    for (const auto& lp : loops) {
        if (polyArea2(lp) > 0.0f) {
            outers.push_back(lp);
            holeGroups.emplace_back();
        }
    }
    for (const auto& lp : loops) {
        if (polyArea2(lp) > 0.0f) continue;  // un extérieur
        const Vec2 p = lp[0];
        for (size_t i = 0; i < outers.size(); ++i) {
            if (pointInPolygon(p, outers[i])) {
                holeGroups[i].push_back(lp);
                break;
            }
        }
    }
    for (size_t i = 0; i < outers.size(); ++i) {
        BoolRegion r;
        r.outer = outers[i];
        r.holes = holeGroups[i];
        out.push_back(std::move(r));
    }
}

}  // namespace mesh
