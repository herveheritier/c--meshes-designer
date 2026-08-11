#pragma once
#include "mesh.h"

#include <vector>

namespace mesh {

// ---------------------------------------------------------------------------
// Triangulation et tests géométriques
// ---------------------------------------------------------------------------

// Triangule un polygone simple (concave ou convexe) par ear clipping.
// `pts` : sommets du polygone dans l'ordre (peu importe le sens).
// `tris`: reçoit des triplets d'indices dans `pts` (n-2 triangles).
// Retourne false si le polygone est dégénéré (auto-sécant, aire nulle…) ;
// dans ce cas un repli en éventail est tout de même émis.
bool triangulatePolygon(const std::vector<Vec2>& pts, std::vector<int>& tris);

// Triangule la bande entre deux boucles de sommets ordonnées dans le même
// sens (les polygones réguliers concentriques d'une couronne). Produit
// exactement n+m triangles, sans croisement ni chevauchement, tous orientés
// dans le même sens ; chaque triangle est émis comme un sextuplet
// (ring, idx, ring, idx, ring, idx) où ring ∈ {0, 1} désigne la boucle
// (0 = extérieure, 1 = intérieure) et idx la position dans la boucle.
// N'émet rien si une boucle a moins de 3 sommets.
void triangulateBand(const std::vector<Vec2>& outer, const std::vector<Vec2>& inner,
                     std::vector<int>& tris);


// Vrai si p est à l'intérieur (ou sur le bord) du triangle (a,b,c).
bool pointInTriangle(const Vec2& p, const Vec2& a, const Vec2& b, const Vec2& c);

// Vrai si p est à l'intérieur (ou sur le bord) du polygone simple `poly`.
bool pointInPolygon(const Vec2& p, const std::vector<Vec2>& poly);

// Triangule un polygone simple percé de trous : `outer` est la boucle
// extérieure, `holes` les boucles de trous (sens quelconques, trous entièrement
// dans `outer` et disjoints). Chaque trou est relié à la boucle extérieure par
// un pont (double arête) et l'ensemble est découpé par ear clipping.
// `pts` reçoit les sommets (dédupliqués par position) et `tris` des triplets
// d'indices dans `pts`. Retourne false si la boucle extérieure est dégénérée.
bool triangulatePolygonHoles(const std::vector<Vec2>& outer,
                             const std::vector<std::vector<Vec2>>& holes,
                             std::vector<Vec2>& pts, std::vector<int>& tris);

// Soustraction de polygones : soustrait la boucle simple `cut` (concave
// autorisée) de la boucle simple `subject`. `pts` reçoit les sommets du
// résultat (sommets du sujet puis points ajoutés, dédupliqués par position) et
// `tris` des triplets d'indices dans `pts` couvrant exactement le sujet moins
// la découpe. Retourne true si la découpe a touché le sujet (tris vide =
// sujet entièrement recouvert) et false si elle ne le touche pas (ou si le
// sujet / la découpe est dégénéré).
bool subtractPolygon(const std::vector<Vec2>& subject, const std::vector<Vec2>& cut,
                     std::vector<Vec2>& pts, std::vector<int>& tris);

// Distance de p au segment [a,b].
float pointSegmentDistance(const Vec2& p, const Vec2& a, const Vec2& b);

}  // namespace mesh
