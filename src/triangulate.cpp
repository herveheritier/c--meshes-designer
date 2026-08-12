#include "triangulate.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace mesh {

namespace {

// Aire signée (x2) du triangle (a,b,c).
float triArea2(const Vec2& a, const Vec2& b, const Vec2& c) { return cross(b - a, c - a); }

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
// pour la recherche de ponts. Le partage d'une extrémité n'est pas un
// croisement.
bool segmentsCrossProperly(const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& d) {
    const float d1 = cross(d - c, a - c);
    const float d2 = cross(d - c, b - c);
    const float d3 = cross(b - a, c - a);
    const float d4 = cross(b - a, d - a);
    const float eps = 1e-9f;
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
    float area = polyArea2(pts);
    if (std::fabs(area) < 1e-9f) return false;  // dégénéré
    const bool cw = area < 0.0f;

    std::vector<int> idx;
    idx.reserve(n);
    for (int i = 0; i < n; ++i) idx.push_back(i);
    mergeCoincident(idx, pts);  // doublons d'entrée (ponts) : un seul sommet par position

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
            // Polygone dégénéré ou auto-sécant : repli en éventail.
            tris.clear();
            for (int i = 1; i + 1 < (int)idx.size(); ++i) {
                tris.push_back(idx[0]);
                tris.push_back(idx[i]);
                tris.push_back(idx[i + 1]);
            }
            return false;
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
    tris.push_back(idx[0]);
    tris.push_back(idx[1]);
    tris.push_back(idx[2]);
    return true;
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

bool triangulatePolygonHoles(const std::vector<Vec2>& outer,
                             const std::vector<std::vector<Vec2>>& holes,
                             std::vector<Vec2>& pts, std::vector<int>& tris) {
    pts.clear();
    tris.clear();

    // Boucles CCW.
    std::vector<Vec2> region = toCCW(outer);
    if (region.size() < 3) return false;
    for (const auto& h : holes) {
        // Trou dégénéré, vide ou entièrement en dehors de la boucle : ignoré.
        if (h.size() < 3) continue;
        std::vector<Vec2> hh = toCCW(h);
        if (std::fabs(polyArea2(hh)) < 1e-9f) continue;
        if (!pointInPolygon(hh[0], region)) continue;
        bridgeHole(region, hh);
    }

    // Triangule le polygone (avec les ponts).
    std::vector<int> ltris;
    const bool ok = triangulatePolygon(region, ltris);

    // Points du résultat : on déduplique les positions (les ponts créent des
    // jumeaux) et on renumérote les triangles.
    std::vector<int> remap(region.size(), -1);
    const float eps = 1e-6f;
    for (size_t i = 0; i < region.size(); ++i) {
        for (size_t j = 0; j < pts.size(); ++j) {
            if (distance(region[i], pts[j]) < eps) {
                remap[i] = (int)j;
                break;
            }
        }
        if (remap[i] < 0) {
            remap[i] = (int)pts.size();
            pts.push_back(region[i]);
        }
    }
    for (int t : ltris) tris.push_back(remap[t]);
    return ok;
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

}  // namespace mesh
