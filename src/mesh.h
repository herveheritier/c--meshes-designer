#pragma once
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace mesh {

// ---------------------------------------------------------------------------
// Vecteur 2D et opérations de base
// ---------------------------------------------------------------------------
struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

inline Vec2 operator+(const Vec2& a, const Vec2& b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(const Vec2& a, const Vec2& b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(const Vec2& a, float s) { return {a.x * s, a.y * s}; }
inline Vec2 operator/(const Vec2& a, float s) { return {a.x / s, a.y / s}; }
inline float dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }
inline float cross(const Vec2& a, const Vec2& b) { return a.x * b.y - a.y * b.x; }
inline float length(const Vec2& a) { return std::sqrt(dot(a, a)); }
inline float distance(const Vec2& a, const Vec2& b) { return length(a - b); }

// ---------------------------------------------------------------------------
// Couleur RGBA (0..1)
// ---------------------------------------------------------------------------
struct Color {
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
};

inline Color rgba(float r, float g, float b, float a = 1.0f) { return {r, g, b, a}; }

// ---------------------------------------------------------------------------
// Face : polygone défini par une liste d'indices de sommets (ordre anti-horaire)
// Chaque face peut porter une couleur de remplissage (facultative).
// ---------------------------------------------------------------------------
struct Face {
    std::vector<int> verts;
    Color color;                // couleur de remplissage
    bool hasColor = false;      // vrai si une couleur a été posée
};

// ---------------------------------------------------------------------------
// Mesh 2D : sommets + faces polygonales (les arêtes sont dérivées des faces)
// ---------------------------------------------------------------------------
class Mesh2D {
public:
    std::vector<Vec2> vertices;
    std::vector<Face> faces;
    // Nom du plan (spec 2.2 : facultatif, défaut « Plan n » affiché à l'écran).
    std::string name;

    void clear();
    bool empty() const;

    // --- Sommets ---
    int addVertex(const Vec2& p);
    void moveVertex(int index, const Vec2& p);
    // Retire un sommet et ré-indexe les faces (le sommet disparaît des boucles).
    bool removeVertex(int index);
    // Retire plusieurs sommets (les indices sont remappés automatiquement).
    void removeVertices(const std::vector<int>& indices);
    // Fusionne plusieurs sommets en un seul (spec 5.5 / 5.6) : le sommet
    // d'indice le plus petit est conservé (déplacé à `pos`), les autres sont
    // remplacés par lui dans les faces puis retirés. Les faces devenues
    // dégénérées (< 3 sommets ou boucle en double) sont supprimées.
    // Retourne l'index du sommet conservé, ou -1 si l'entrée est invalide.
    int mergeVertices(const std::vector<int>& indices, const Vec2& pos);

    // --- Faces ---
    // Crée un polygone à partir d'indices valides (>= 3 sommets, sans doublon).
    // Retourne l'index de la face ou -1 si le polygone est invalide.
    int addFace(const std::vector<int>& verts);
    // Crée un polygone à partir d'indices valides puis le découpe immédiatement
    // en triangles (ear clipping). Retourne le nombre de faces créées, ou -1 si
    // le polygone est invalide ou dégénéré (aire nulle, auto-sécant…).
    int addTriangulatedFace(const std::vector<int>& verts);
    bool removeFace(int index);
    // Découpe le plan avec le polygone fermé `cut` (boucle simple, sens
    // quelconque, concave autorisée) : les faces partiellement recouvertes sont
    // recoupées (la partie restante conserve la couleur), celles entièrement
    // dans la zone découpée sont supprimées, les autres restent intactes.
    // Retourne false si `cut` ne touche aucune face (rien n'a changé).
    bool cutPolygon(const std::vector<Vec2>& cut);

    // --- Arêtes (dérivées des faces) ---
    using Edge = std::pair<int, int>;
    static Edge normEdge(int a, int b) { return a < b ? Edge{a, b} : Edge{b, a}; }
    // Liste des arêtes uniques de toutes les faces.
    std::vector<Edge> edges() const;
    // Occurrences d'une arête (a,b) : (index de face, position de `a` dans la boucle),
    // `b` suivant immédiatement `a` (dans un sens ou l'autre).
    std::vector<std::pair<int, int>> edgeOccurrences(int a, int b) const;
    // Insère un sommet sur l'arête (a,b) à la fraction t, et met à jour les boucles.
    // Retourne l'index du nouveau sommet, ou -1 si l'arête n'existe pas.
    int insertVertexOnEdge(int a, int b, float t);
    // Dissout l'arête (a,b) : fusionne b dans a dans les faces concernées.
    bool dissolveEdge(int a, int b);
    // Retire les faces qui partagent l'arête (a,b) (règle « segment » de la spec).
    void removeFacesSharingEdge(int a, int b);
    // Extrude une arête : duplique (a,b) décalé de `delta` et crée un quadrilatère.
    void extrudeEdge(int a, int b, const Vec2& delta);

    // --- Analyse ---
    // Vrai si la boucle est une face valide (>= 3 sommets, indices dans les
    // bornes, sans doublon).
    bool validFace(const std::vector<int>& verts) const;
    Vec2 centroid() const;
    // Nombre total de triangles après triangulation (n-2 par face simple).
    int triangleCount() const;
    // Triangulation de toutes les faces : triplets d'indices de sommets globaux.
    void triangulated(std::vector<int>& tris) const;
};

// ---------------------------------------------------------------------------
// Scène : une ou plusieurs « plans » (feuilles de dessin empilées).
// L'ordre des plans est significatif : le plan d'indice le plus élevé recouvre
// les précédents là où ils se chevauchent. Un seul plan est actif à la fois.
// ---------------------------------------------------------------------------
struct Scene {
    std::vector<Mesh2D> planes;   // ordre d'empilement
    int active = 0;               // plan actif (index dans `planes`)

    int count() const { return (int)planes.size(); }
    int countVerts() const {
        int n = 0;
        for (const auto& p : planes) n += (int)p.vertices.size();
        return n;
    }
    int countFaces() const {
        int n = 0;
        for (const auto& p : planes) n += (int)p.faces.size();
        return n;
    }

    Mesh2D& activePlane() {
        if (planes.empty()) planes.emplace_back();
        if (active < 0) active = 0;
        if (active >= (int)planes.size()) active = (int)planes.size() - 1;
        return planes[active];
    }
    const Mesh2D& activePlane() const {
        static const Mesh2D kEmpty;
        if (active < 0 || active >= (int)planes.size()) return kEmpty;
        return planes[active];
    }
    void clear() {
        planes.clear();
        planes.emplace_back();
        active = 0;
    }
};

}  // namespace mesh
