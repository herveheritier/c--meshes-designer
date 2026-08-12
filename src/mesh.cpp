#include "mesh.h"
#include "triangulate.h"

#include <algorithm>

namespace mesh {

void Mesh2D::clear() {
    vertices.clear();
    faces.clear();
}

bool Mesh2D::empty() const { return vertices.empty() && faces.empty(); }

// ---------------------------------------------------------------------------
// Sommets
// ---------------------------------------------------------------------------
int Mesh2D::addVertex(const Vec2& p) {
    vertices.push_back(p);
    return (int)vertices.size() - 1;
}

void Mesh2D::moveVertex(int index, const Vec2& p) {
    if (index >= 0 && index < (int)vertices.size()) vertices[index] = p;
}

bool Mesh2D::removeVertex(int index) {
    if (index < 0 || index >= (int)vertices.size()) return false;

    // 1. Retirer l'indice de toutes les boucles de faces.
    for (Face& f : faces) {
        for (int i = 0; i < (int)f.verts.size();) {
            if (f.verts[i] == index)
                f.verts.erase(f.verts.begin() + i);
            else
                ++i;
        }
    }
    // 2. Supprimer les faces devenues dégénérées (< 3 sommets).
    faces.erase(std::remove_if(faces.begin(), faces.end(),
                               [](const Face& f) { return f.verts.size() < 3; }),
                faces.end());
    // 3. Remapper les indices restants.
    for (Face& f : faces)
        for (int& v : f.verts)
            if (v > index) --v;
    // 4. Retirer le sommet.
    vertices.erase(vertices.begin() + index);
    return true;
}

void Mesh2D::removeVertices(const std::vector<int>& indices) {
    // Tri décroissant + suppression (les indices se remappent tout seuls).
    std::vector<int> sorted = indices;
    std::sort(sorted.begin(), sorted.end(), std::greater<int>());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    for (int i : sorted) removeVertex(i);
}

int Mesh2D::mergeVertices(const std::vector<int>& indices, const Vec2& pos) {
    // Valide les entrées et dédoublonne.
    std::vector<int> uniq;
    for (int i : indices) {
        if (i < 0 || i >= (int)vertices.size()) return -1;
        if (std::find(uniq.begin(), uniq.end(), i) == uniq.end()) uniq.push_back(i);
    }
    if (uniq.empty()) return -1;
    if (uniq.size() < 2) return uniq[0];  // rien à fusionner

    // Le sommet conservé est le plus petit indice, déplacé à `pos`.
    const int keep = *std::min_element(uniq.begin(), uniq.end());
    vertices[keep] = pos;

    // Les autres sommets fusionnés sont remplacés par `keep` dans les faces.
    std::vector<int> removed;
    for (int i : uniq)
        if (i != keep) removed.push_back(i);
    std::sort(removed.begin(), removed.end(), std::greater<int>());  // pour la suppression finale

    for (Face& f : faces) {
        for (int& v : f.verts) {
            if (std::find(uniq.begin(), uniq.end(), v) != uniq.end()) v = keep;
        }
    }

    // Supprime les faces devenues dégénérées (< 3 sommets ou boucle en double).
    faces.erase(std::remove_if(faces.begin(), faces.end(),
                               [](const Face& f) {
                                   if ((int)f.verts.size() < 3) return true;
                                   std::vector<int> u = f.verts;
                                   std::sort(u.begin(), u.end());
                                   return std::adjacent_find(u.begin(), u.end()) != u.end();
                               }),
                faces.end());

    // Retire les sommets fusionnés (ordre décroissant : réindexation sûre,
    // `keep` étant le plus petit indice n'est jamais remappé).
    for (int i : removed) removeVertex(i);
    return keep;
}

// ---------------------------------------------------------------------------
// Faces
// ---------------------------------------------------------------------------
bool Mesh2D::validFace(const std::vector<int>& verts) const {
    if ((int)verts.size() < 3) return false;
    for (int v : verts)
        if (v < 0 || v >= (int)vertices.size()) return false;
    // Pas de doublon dans la boucle (un polygone simple).
    std::vector<int> uniq = verts;
    std::sort(uniq.begin(), uniq.end());
    return std::adjacent_find(uniq.begin(), uniq.end()) == uniq.end();
}

int Mesh2D::addFace(const std::vector<int>& verts) {
    if (!validFace(verts)) return -1;
    Face f;
    f.verts = verts;
    faces.push_back(std::move(f));
    return (int)faces.size() - 1;
}

int Mesh2D::addTriangulatedFace(const std::vector<int>& verts) {
    if (!validFace(verts)) return -1;

    // Points du polygone puis découpe en triangles (ear clipping).
    std::vector<Vec2> pts;
    pts.reserve(verts.size());
    for (int v : verts) pts.push_back(vertices[v]);
    std::vector<int> local;
    if (!triangulatePolygon(pts, local) || local.empty()) return -1;  // dégénéré

    // `local` contient des triplets d'indices dans `pts` : les traduire en
    // indices de sommets globaux et créer une face par triangle.
    int created = 0;
    for (size_t i = 0; i + 2 < local.size(); i += 3) {
        Face f;
        f.verts = {verts[local[i]], verts[local[i + 1]], verts[local[i + 2]]};
        faces.push_back(std::move(f));
        ++created;
    }
    return created;
}

bool Mesh2D::removeFace(int index) {
    if (index < 0 || index >= (int)faces.size()) return false;
    faces.erase(faces.begin() + index);
    return true;
}

std::vector<int> Mesh2D::shiftFaces(const std::vector<int>& sel, int dir) {
    std::vector<int> out;
    // Pas de retour précoce sur les petits maillages : le résultat doit TOUJOURS
    // refléter le masque de sélection (un plan à une seule face renvoie les
    // indices intacts, la sélection ne doit pas se vider).
    if (dir == 0) return out;
    // Masque de sélection courant : les indices suivent les faces lors des
    // échanges (mêmes positions que le vecteur `faces`, donc mêmes swaps).
    std::vector<bool> mask(faces.size(), false);
    for (int s : sel)
        if (s >= 0 && (size_t)s < faces.size()) mask[(size_t)s] = true;
    // Échange manuel des bits du masque : std::swap ne s'applique pas aux
    // proxies de std::vector<bool>.
    auto swapBits = [&](size_t a, size_t b) {
        const bool t = mask[a];
        mask[a] = mask[b];
        mask[b] = t;
    };
    if (dir > 0) {
        // Vers l'avant : chaque face sélectionnée échange avec la face non
        // sélectionnée qui la suit (parcours du haut vers le bas).
        for (int i = (int)faces.size() - 2; i >= 0; --i)
            if (mask[(size_t)i] && !mask[(size_t)i + 1]) {
                std::swap(faces[(size_t)i], faces[(size_t)i + 1]);
                swapBits((size_t)i, (size_t)i + 1);
            }
    } else {
        // Vers l'arrière : chaque face sélectionnée échange avec la face non
        // sélectionnée qui la précède (parcours du bas vers le haut).
        for (int i = 1; i < (int)faces.size(); ++i)
            if (mask[(size_t)i] && !mask[(size_t)i - 1]) {
                std::swap(faces[(size_t)i], faces[(size_t)i - 1]);
                swapBits((size_t)i, (size_t)i - 1);
            }
    }
    for (int i = 0; i < (int)mask.size(); ++i)
        if (mask[(size_t)i]) out.push_back(i);
    return out;
}

bool Mesh2D::cutPolygon(const std::vector<Vec2>& cut) {
    if (cut.size() < 3) return false;
    if (faces.empty()) return false;

    // Les sommets actuels sont conservés (indices stables) ; les points
    // produits par le recoupage sont ajoutés à la suite, dédupliqués par
    // position pour recoller les arêtes partagées entre faces.
    std::vector<Vec2> newVerts = vertices;
    std::vector<Face> newFaces;
    newFaces.reserve(faces.size());
    bool changed = false;

    const auto findOrAdd = [&](const Vec2& p) -> int {
        for (size_t i = 0; i < newVerts.size(); ++i)
            if (distance(newVerts[i], p) < 1e-4f) return (int)i;
        newVerts.push_back(p);
        return (int)newVerts.size() - 1;
    };

    for (int fi = 0; fi < (int)faces.size(); ++fi) {
        const Face& f = faces[(size_t)fi];
        if ((int)f.verts.size() < 3) continue;
        std::vector<Vec2> facePts;
        facePts.reserve(f.verts.size());
        for (int v : f.verts) facePts.push_back(vertices[v]);

        std::vector<Vec2> outPts;
        std::vector<int> tris;
        if (!subtractPolygon(facePts, cut, outPts, tris)) {
            // Face intacte : conservée telle quelle.
            newFaces.push_back(f);
            continue;
        }
        changed = true;

        // Les triangles du résultat remplacent la face (couleur conservée).
        for (size_t i = 0; i + 2 < tris.size(); i += 3) {
            const Vec2& a = outPts[tris[i]];
            const Vec2& b = outPts[tris[i + 1]];
            const Vec2& c = outPts[tris[i + 2]];
            // Triangles dégénérés (bords partagés, ponts) : écartés.
            if (std::fabs(cross(b - a, c - a)) < 1e-7f) continue;
            Face nf;
            nf.verts = {findOrAdd(a), findOrAdd(b), findOrAdd(c)};
            nf.color = f.color;
            nf.hasColor = f.hasColor;
            newFaces.push_back(std::move(nf));
        }
    }

    if (!changed) return false;
    vertices.swap(newVerts);
    faces.swap(newFaces);
    return true;
}

// ---------------------------------------------------------------------------
// Arêtes
// ---------------------------------------------------------------------------
std::vector<Mesh2D::Edge> Mesh2D::edges() const {
    std::vector<Edge> out;
    for (const Face& f : faces) {
        const int n = (int)f.verts.size();
        for (int i = 0; i < n; ++i)
            out.push_back(normEdge(f.verts[i], f.verts[(i + 1) % n]));
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::vector<std::pair<int, int>> Mesh2D::edgeOccurrences(int a, int b) const {
    std::vector<std::pair<int, int>> occ;
    for (int fi = 0; fi < (int)faces.size(); ++fi) {
        const Face& f = faces[fi];
        const int n = (int)f.verts.size();
        for (int i = 0; i < n; ++i) {
            const int va = f.verts[i];
            const int vb = f.verts[(i + 1) % n];
            if (va == a && vb == b) occ.emplace_back(fi, i);       // a suivi de b
            else if (vb == a && va == b) occ.emplace_back(fi, (i + 1) % n);  // b suivi de a → position de a
        }
    }
    return occ;
}

int Mesh2D::insertVertexOnEdge(int a, int b, float t) {
    if (a < 0 || b < 0 || a >= (int)vertices.size() || b >= (int)vertices.size()) return -1;
    const auto occ = edgeOccurrences(a, b);
    if (occ.empty()) return -1;

    t = std::clamp(t, 0.01f, 0.99f);
    const Vec2 p = vertices[a] + (vertices[b] - vertices[a]) * t;
    const int newIndex = addVertex(p);

    // Insérer le nouveau sommet juste après `a` dans chaque boucle concernée.
    for (const auto& [fi, posA] : occ)
        faces[fi].verts.insert(faces[fi].verts.begin() + posA + 1, newIndex);
    return newIndex;
}

bool Mesh2D::dissolveEdge(int a, int b) {
    const auto occ = edgeOccurrences(a, b);
    if (occ.empty()) return false;

    // Retirer `b` des boucles : l'arête (a,b) s'effondre sur `a`.
    for (const auto& [fi, posA] : occ) {
        Face& f = faces[fi];
        if ((int)f.verts.size() <= 3) continue;  // ne pas dégénérer un triangle
        const int posB = (posA + 1) % (int)f.verts.size();
        if (f.verts[posB] == b) f.verts.erase(f.verts.begin() + posB);
    }
    faces.erase(std::remove_if(faces.begin(), faces.end(),
                               [](const Face& f) { return f.verts.size() < 3; }),
                faces.end());

    // Retirer le sommet b s'il n'est plus référencé.
    bool used = false;
    for (const Face& f : faces)
        for (int v : f.verts)
            if (v == b) { used = true; break; }
    if (!used) removeVertex(b);
    return true;
}

void Mesh2D::removeFacesSharingEdge(int a, int b) {
    for (int i = (int)faces.size() - 1; i >= 0; --i) {
        const Face& f = faces[i];
        bool shares = false;
        const int n = (int)f.verts.size();
        for (int j = 0; j < n && !shares; ++j) {
            const int va = f.verts[j];
            const int vb = f.verts[(j + 1) % n];
            if ((va == a && vb == b) || (va == b && vb == a)) shares = true;
        }
        if (shares) faces.erase(faces.begin() + i);
    }
}

void Mesh2D::extrudeEdge(int a, int b, const Vec2& delta) {
    if (a < 0 || b < 0 || a >= (int)vertices.size() || b >= (int)vertices.size()) return;
    const int a2 = addVertex(vertices[a] + delta);
    const int b2 = addVertex(vertices[b] + delta);
    // Orientation du quad : l'intérieur est du côté de `delta` (enroulement CCW).
    const Vec2 e = vertices[b] - vertices[a];
    if (cross(e, delta) >= 0.0f)
        addFace({a, b, b2, a2});
    else
        addFace({b, a, a2, b2});
}

// ---------------------------------------------------------------------------
// Analyse
// ---------------------------------------------------------------------------
Vec2 Mesh2D::centroid() const {
    Vec2 c;
    if (vertices.empty()) return c;
    for (const Vec2& v : vertices) { c.x += v.x; c.y += v.y; }
    c.x /= (float)vertices.size();
    c.y /= (float)vertices.size();
    return c;
}

int Mesh2D::triangleCount() const {
    int n = 0;
    for (const Face& f : faces)
        if ((int)f.verts.size() >= 3) n += (int)f.verts.size() - 2;
    return n;
}

void Mesh2D::triangulated(std::vector<int>& tris) const {
    tris.clear();
    std::vector<int> local;
    for (const Face& f : faces) {
        if ((int)f.verts.size() < 3) continue;
        std::vector<Vec2> pts;
        pts.reserve(f.verts.size());
        for (int v : f.verts) pts.push_back(vertices[v]);
        triangulatePolygon(pts, local);
        for (int i : local) tris.push_back(f.verts[i]);
    }
}

}  // namespace mesh
