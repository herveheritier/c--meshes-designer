#pragma once
#include <string>
#include <vector>

namespace mesh::svg {

// ---------------------------------------------------------------------------
// Analyse d'un fichier SVG réduit au sous-ensemble utilisé par le dossier
// assets/ : éléments svg/line/rect/circle/polyline/polygon/path, attributs
// viewBox / fill / stroke / stroke-width, et commandes de tracé
// M/m L/l H/h V/v C/c S/s Q/q T/t A/a Z/z.
//
// Les coordonnées produites sont exprimées dans l'espace du viewBox (le
// rendu applique la transformation vers l'écran). `currentColor` n'est pas
// résolu ici : il est remplacé par la couleur de dessin au moment du rendu.
// ---------------------------------------------------------------------------

struct Pt {
    float x = 0.0f;
    float y = 0.0f;
};

// Contour à dessiner (polyline ouverte ou fermée).
struct Stroke {
    std::vector<Pt> pts;
    bool closed = false;
};

// Remplissages élémentaires (tous convexes dans les icônes du projet).
struct FillCircle {
    Pt c;
    float r = 0.0f;
};
struct FillRect {
    Pt min, max;
    float rounding = 0.0f;
};
struct FillPoly {
    std::vector<Pt> pts;
};

struct Icon {
    float vbMinX = 0.0f, vbMinY = 0.0f, vbW = 16.0f, vbH = 16.0f;
    float strokeWidth = 1.5f;
    std::vector<Stroke> strokes;
    std::vector<FillCircle> fillCircles;
    std::vector<FillRect> fillRects;
    std::vector<FillPoly> fillPolys;
    bool ok = false;  // faux si le document est invalide ou absent de viewBox
};

Icon parseSvg(const std::string& svg);

}  // namespace mesh::svg
