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

// Vrai si p est à l'intérieur (ou sur le bord) du triangle (a,b,c).
bool pointInTriangle(const Vec2& p, const Vec2& a, const Vec2& b, const Vec2& c);

// Vrai si p est à l'intérieur (ou sur le bord) du polygone simple `poly`.
bool pointInPolygon(const Vec2& p, const std::vector<Vec2>& poly);

// Distance de p au segment [a,b].
float pointSegmentDistance(const Vec2& p, const Vec2& a, const Vec2& b);

}  // namespace mesh
