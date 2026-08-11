#pragma once
#include "svgparse.h"

#include <imgui.h>

#include <string>
#include <unordered_map>

namespace mesh::ui {

// ---------------------------------------------------------------------------
// Icônes SVG du dossier assets/ : chargées au premier accès (par nom de
// fichier, sans extension), parsées une seule fois puis rendues
// vectoriellement dans ImDrawList.
// ---------------------------------------------------------------------------

class IconSet {
public:
    static IconSet& instance();
    // Géométrie parsée de l'icône (mise en cache), nullptr si absente/invalide.
    const svg::Icon* parsed(const char* name);

private:
    bool load();
    std::unordered_map<std::string, std::string> map_;      // contenu brut
    std::unordered_map<std::string, svg::Icon> parsed_;      // géométrie en cache
    bool tried_ = false;  // premier essai de chargement déjà effectué
};

// Dessine une icône parsée dans le carré [pos, pos+size]. `color` remplace
// « currentColor ». Retourne false si la géométrie est vide.
bool drawSvgIcon(ImDrawList* dl, const svg::Icon& icon, const ImVec2& pos,
                 float size, ImU32 color);

// Icône par nom (via IconSet), centrée dans le carré [pos, pos+size].
// Retourne false si introuvable.
bool drawSvgIconNamed(ImDrawList* dl, const char* name, const ImVec2& pos,
                      float size, ImU32 color);

}  // namespace mesh::ui
