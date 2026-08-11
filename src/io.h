#pragma once
#include "mesh.h"

#include <string>
#include <vector>

namespace mesh {

// Format d'export des données triangulées.
enum class ExportFormat { OBJ, QB64 };

struct IoResult {
    bool ok = false;
    std::string error;
};

// Format natif texte (sommets + faces, lisible et rééditable).
IoResult saveNative(const Mesh2D& m, const std::string& path);
IoResult loadNative(Mesh2D& m, const std::string& path);

// Export OBJ (sommets 2D, z=0 ; faces 1-based).
IoResult exportOBJ(const Mesh2D& m, const std::string& path);

// Import OBJ : sommets `v x y [z]` (z ignoré), faces `f` avec indices 1-based
// (formes `a`, `a/b`, `a//b`, `a/b/c` supportées ; indices négatifs acceptés).
IoResult loadObj(Mesh2D& m, const std::string& path);

// Export SVG vectoriel d'un plan : un polygone par face, avec la couleur de
// remplissage quand elle existe. Y monde vers le haut → Y SVG inversé.
IoResult exportPlaneSVG(const Mesh2D& m, const std::string& path);

// Export texte simple, pensé pour une lecture aisée depuis QB64 (INPUT #).
IoResult exportQB64(const Mesh2D& m, const std::string& path);

// ---------------------------------------------------------------------------
// Formats de la spec « meshes designer »
// ---------------------------------------------------------------------------

// Scène complète : plusieurs plans + vue (zoom en multiplicateur, centre) + grille.
struct SceneSnapshot {
    Scene scene;                 // plans + plan actif
    float zoomMult = 1.0f;       // 1.0 = 100 %
    float cx = 0.0f;
    float cy = 0.0f;
    bool grid = true;
    float gridStep = 1.0f;
    std::string name;
};

// JSON : format complet, aller-retour exact (plans, points, triangles,
// couleurs, vue, grille). `path` ne doit PAS contenir l'extension (ajoutée ici).
IoResult saveSceneJson(const SceneSnapshot& s, const std::string& pathNoExt);
IoResult loadSceneJson(SceneSnapshot& s, const std::string& pathNoExt);

// Autosave : scène + vue + historiques annuler/rétablir (scènes complètes).
IoResult saveAutoJson(const SceneSnapshot& s, const std::vector<Scene>& undo,
                      const std::vector<Scene>& redo, const std::string& path);
IoResult loadAutoJson(SceneSnapshot& s, std::vector<Scene>& undo,
                      std::vector<Scene>& redo, const std::string& path);

// « meshes » (texte) : une ligne = un plan ; coordonnées `x,y` séparées par
// des points-virgules ; chaque triplet consécutif forme un triangle ;
// coordonnées identiques dédupliquées ; reliquat de 1-2 sommets filtré.
IoResult saveMeshesText(const Scene& s, const std::string& path);
IoResult loadMeshesText(Scene& s, const std::string& path);

// Préférences persistées (palette, opacité, côtés du cercle, emplacements…).
struct PrefsData {
    std::vector<Color> palette;
    float brushOpacity = 0.45f;
    int circleSides = 16;
    float edgePickTol = 7.0f;            // distance de détection des segments (px)
    int mergeRadius = 20;                 // rayon de fusion par déplacement, px (8..64)
    std::vector<std::string> locations;   // emplacements d'enregistrement (20 max)
    int importMode = 0;                   // 0 = remplacer, 1 = fusionner
    bool allColors = false;               // mode « toutes couleurs » conservé (7.6)
    bool snapOn = true;                   // aimantation sur la grille (indépendante de l'affichage)
    std::vector<std::string> versions;    // noms des versions horodatées de l'autosave (10 max)
    bool consoleVisible = false;
    float consoleX = 0.0f, consoleY = 0.0f;
    float consoleW = 520.0f, consoleH = 220.0f;
};

IoResult savePrefsJson(const PrefsData& p, const std::string& path);
IoResult loadPrefsJson(PrefsData& p, const std::string& path);

}  // namespace mesh
