#pragma once
#include "camera.h"
#include "io.h"
#include "mesh.h"
#include "renderer.h"

#include <imgui.h>

#include <string>
#include <vector>

struct SDL_Window;

namespace mesh {

// --- Modes ---
// Cible d'édition : sommet / segment / triangle (4.4).
enum class SelMode { Vertex, Edge, Face };
// Outils : sélection + formes prédéfinies (4.2) + découpe (polygone soustrait).
enum class Tool { Select, Rectangle, Square, Circle, Triangle, Pentagon, Hexagon, Star, Ring, Crown, Cut };
inline bool isShapeTool(Tool t) { return t != Tool::Select && t != Tool::Cut; }

enum class ReticleState { Off, Simple, Symmetric };
enum class PreviewMode { Off, Simple, Planes };

// --- Drag en cours ---
enum class DragKind { None, Move, MoveAll, Box, Shape };

struct Drag {
    DragKind kind = DragKind::None;
    std::vector<int> movingVerts;       // sommets déplacés (Move)
    std::vector<Vec2> startPositions;   // positions initiales (Move)
    // Positions initiales de tous les sommets de tous les plans (MoveAll).
    // allPlaneStarts[i] = sommets du plan i au début du glisser (8.4).
    std::vector<std::vector<Vec2>> allPlaneStarts;
    Vec2 grabWorld{0, 0};               // point de saisie en monde (Move / MoveAll)
    Vec2 startScreen{0, 0};             // pixels, relatif au viewport (Box)
    Vec2 curScreen{0, 0};
    // Tracé de forme : 0 prêt · 1 ancre posée · 2 verrouillé (étoile/anneau)
    int shapeStage = 0;
    Vec2 shapeAnchor{0, 0};             // ancre (centre ou coin) en monde
    Vec2 shapeCur{0, 0};                // position courante du curseur (monde)
    float shapeRadius = 0.0f;           // rayon verrouillé (étoile/anneau)
    float shapeAngle = 0.0f;            // orientation verrouillée
    float shapeInner = 0.0f;            // profondeur étoile / rayon du trou (anneau)
};

// ---------------------------------------------------------------------------
// Application : état global + logique d'édition
// ---------------------------------------------------------------------------
class App {
public:
    // --- Modèle & rendu ---
    Scene scene;                // plusieurs plans empilés
    Camera2D camera;
    Renderer renderer;
    SDL_Window* window = nullptr;

    // --- Sélection & outils ---
    SelMode selMode = SelMode::Vertex;
    Tool tool = Tool::Select;
    std::vector<int> selVerts;
    std::vector<Mesh2D::Edge> selEdges;
    std::vector<int> selFaces;
    int hoverVertex = -1;
    // Segment le plus proche du pointeur (mode sommet) : il s'illumine, car un
    // clic y accroche un nouveau sommet pour former un triangle (4.1).
    Mesh2D::Edge hoverEdge{-1, -1};
    // Distance (pixels écran) de détection des segments (survol + accroche +
    // sélection en mode « segment »). Réglable dans le panneau « Réglages ».
    float edgePickTol = 7.0f;

    // --- Construction de triangle (4.1) : sommets partiels en cours ---
    int triP1 = -1;
    int triP2 = -1;

    // --- Grille & réticule ---
    bool gridOn = true;                 // affichage de la grille
    bool snapOn = true;                 // aimantation (indépendante de l'affichage)
    float gridStep = 1.0f;
    ReticleState reticle = ReticleState::Simple;  // croix de visée par défaut (3.4)

    // --- Plans (7) ---
    bool allColors = false;             // mode « toutes couleurs » (7.6), conservé
    bool kiosk = false;                 // mode kiosque / couverture (7.5)
    bool kioskFresh = false;            // 1re frame de kiosque : ignore le clic d'activation
    float kioskX = -1.0f;               // position du pointeur dans le viewport (kiosque)
    std::vector<float> kioskOff;        // décalage par carte (animation cover-flow)
    std::vector<float> kioskVel;        // vitesse du ressort associé (rebond)
    bool dlgDeletePlaneOpen = false;    // confirmation de suppression du plan actif
    // Rectangle du bouton « Kiosque » (frame précédente) : update() n'interprète
    // pas un clic sur cette zone comme une sélection de plan (voile plein écran).
    float kioskBtnMinX = 0.0f, kioskBtnMinY = 0.0f;
    float kioskBtnMaxX = 0.0f, kioskBtnMaxY = 0.0f;

    // --- Formes prédéfinies ---
    int circleSides = 16;               // côtés du cercle/anneau/étoile (molette, mémorisé)
    int crownInnerSides = 8;            // côtés INTÉRIEURS de la couronne (molette+Maj, mémorisé)

    // --- Outil découpe (polygone soustrait au plan actif) ---
    std::vector<Vec2> cutPts;           // sommets du polygone de découpe en cours
    bool isCutArmed() const { return tool == Tool::Cut; }
    bool isCutTracing() const { return tool == Tool::Cut && !cutPts.empty(); }
    void toggleCutTool();               // arme / désarme l'outil découpe (D)
    void applyCut();                    // ferme le polygone et soustrait (Entrée / clic droit)
    void removeLastCutPoint();          // Retour arrière pendant le tracé

    // --- Outil mesure ---
    bool measureActive = false;
    bool measureHasA = false;           // 1er point posé
    bool measureHasB = false;           // 2e point posé (segment complet)
    Vec2 measureA{0, 0};
    Vec2 measureB{0, 0};

    // --- Fusion de points (5.5 / 5.6) ---
    enum class MergeMode { Off, Armed, Locked };
    MergeMode mergeMode = MergeMode::Off;
    int mergeRadius = 20;               // rayon de fusion en pixels écran (8..64, 5.6)
    void toggleMergeMode();             // Off → Armé → Verrouillé → Off (bouton Fusionner)
    // 5.5 : regroupe tous les sommets sélectionnés à leur position moyenne.
    void mergeSelectionToCentroid();
    // 5.6 : fusionne le sommet v avec le plus proche situé à moins du rayon.
    bool tryMergeByDrag(int v);
    // Groupes de sommets superposés (mêmes coordonnées) du plan actif.
    std::vector<std::vector<int>> overlapGroups() const;
    // Groupe superposé le plus proche du curseur (pour le clic sur l'anneau).
    int pickOverlapGroup(const Vec2& world, float tolPx) const;
    // Sommet le plus proche de v à moins de `tolPx` pixels écran (cible de fusion).
    int pickMergeTarget(int v, float tolPx) const;

    // --- Couleurs & pinceau ---
    std::vector<Color> palette;         // palette personnalisable (8 défauts)
    bool brushArmed = false;
    Color brushColor{1.0f, 0.55f, 0.2f, 1.0f};
    float brushOpacity = 0.45f;         // opacité appliquée à chaque peinture

    // --- Presse-papiers interne (5.8) ---
    // Duplique la sélection (Ctrl+D) : copie légèrement décalée, prête à
    // déplacer — les nouveaux éléments deviennent la sélection.
    void duplicateSelection();
    struct ClipData {
        std::vector<Vec2> verts;
        std::vector<Face> faces;
    };
    ClipData clip;
    bool hasClip = false;
    Vec2 clipOffset{0, 0};              // décalage cumulé des collages

    // --- Undo / redo ---
    std::vector<Scene> undoStack, redoStack;
    static constexpr size_t kMaxUndo = 50;

    // --- Fichier & statut ---
    std::string currentFile;
    std::string sceneName;
    bool dirty = false;
    std::string status;
    float statusAge = 0.0f;
    std::string toast;
    float toastAge = 0.0f;

    // --- Console (14) ---
    std::vector<std::string> consoleLog;
    bool consoleVisible = false;
    ImVec2 consolePos{0, 0};
    ImVec2 consoleSize{520, 220};

    // --- Panneaux flottants ---
    bool paletteOpen = false;
    bool alignOpen = false;
    bool settingsOpen = false;            // panneau « Réglages »

    // --- Prévisualisation & performance ---
    PreviewMode preview = PreviewMode::Off;
    bool showRedraw = false;            // touche F : compteur de redessins
    float fps = 0.0f;                   // lissé
    bool fpsPillGreen = true;           // anti-clignotement (42/48)
    int redraws = 0;
    int redrawAcc = 0;
    float redrawTimer = 0.0f;

    // --- Emplacements d'enregistrement (11.2) ---
    std::vector<std::string> saveLocations;  // 20 max, du plus récent au plus ancien
    // Versions horodatées de l'autosave (noms de fichiers, 10 max).
    std::vector<std::string> versionFiles;
    bool dlgVersionsOpen = false;
    bool restoreVersionFile(const std::string& name);
    // Confirmation de sortie : demandée quand l'utilisateur ferme la fenêtre
    // avec des modifications non enregistrées (dialogue « Quitter sans
    // enregistrer ? »). `quitPending` reste vrai tant que la sortie n'est pas
    // confirmée ; l'application quitte dès que la scène n'est plus modifiée.
    bool dlgQuitOpen = false;
    bool quitPending = false;
    bool dlgSaveOpen = false;
    char dlgSaveName[128] = {0};
    bool dlgImportOpen = false;
    int dlgImportFmt = 0;               // 0 = meshes, 1 = JSON
    char dlgImportPath[1024] = {0};
    bool dlgImportReplace = true;       // dernier mode choisi mémorisé
    bool dlgResetOpen = false;
    bool dlgHelpOpen = false;
    bool dlgRotateOpen = false;           // rotation précise (saisie d'angle)
    float rotateDeg = 90.0f;              // angle par défaut du dialogue
    bool dlgScaleOpen = false;            // mise à l'échelle précise (saisie d'un facteur)
    float scaleFactor = 2.0f;             // facteur par défaut du dialogue
    bool dlgPngOpen = false;              // export d'image (PNG)
    char dlgPngPath[1024] = {0};
    bool dlgSvgOpen = false;              // export SVG du plan actif
    char dlgSvgPath[1024] = {0};
    bool dlgRenameOpen = false;           // renommer le plan actif
    char dlgRenameName[64] = {0};

    // --- Export d'image (PNG depuis la prévisualisation) ---
    bool exportPngRequested = false;
    std::string exportPngPath;

    // --- Viewport ---
    ImVec2 viewportPos{0, 0};
    ImVec2 viewportSize{0, 0};
    bool viewportHovered = false;
    bool cameraFramed = false;
    float rotDeg = 0.0f;                // angle de rotation cumulé, affiché « rot X° » au HUD

    // --- Cycle de vie ---
    void init();
    void shutdown();
    void newDocument();

    // --- Frame ---
    void update(float dt);
    void drawScene();
    // Capture le viewport et écrit le PNG demandé (exportPngRequested).
    void exportPngIfRequested();

    // --- Commandes (undoables) ---
    void pushUndo();
    void undo();
    void redo();
    void deleteSelection();
    void createFaceFromSelection();
    // Miroir de la sélection autour du premier point sélectionné (ancre).
    void mirrorSelectionX();
    void mirrorSelectionY();
    // Mise à l'échelle de la sélection (facteur > 0, ≠ 1), pivot = centre.
    void scaleSelectionExact(float factor);
    // Cadrage de la vue sur la sélection courante (zoom automatique).
    void frameSelection();
    // Copie complète du plan actif, insérée juste au-dessus et sélectionnée.
    void duplicatePlane();
    // Renomme le plan actif (spec 2.2) : nom vide = repli « Plan n ».
    void renameActivePlane(const std::string& name);
    // Bascule de l'outil mesure (2 clics = distance affichée au HUD).
    void toggleMeasure();
    // Aire totale (somme des aires des faces) du plan actif, en unités monde.
    float activePlaneArea() const;
    void insertVertexAt(const Mesh2D::Edge& e, const Vec2& world);
    void resetScene();
    void selectAll();
    // Inverse la sélection du plan actif (Ctrl+I) : les éléments non
    // sélectionnés deviennent sélectionnés et réciproquement.
    void invertSelection();
    void cycleTarget();
    void cycleReticle();
    void cyclePreview();
    void exitPreview();
    void startShapeTool(Tool t);

    // --- Gestion des plans (7) ---
    void nextPlane();
    void prevPlane();
    void planeUp();
    void planeDown();
    void addPlane(bool after);
    void deletePlane();
    void setActivePlane(int i);
    void toggleKiosk();
    // Plan mis en avant par la position du pointeur (kiosque, 7.5).
    int kioskTarget() const;

    // --- Alignement / répartition / rotation ---
    void alignX();
    void alignY();
    void distributeX();
    void distributeY();
    void rotateSelectionAround(const Vec2& pivot, float deg);
    // Rotation précise (saisie d'un angle) : pivot = centre de la sélection.
    void rotateSelectionExact(float deg);
    // Déplacement au clavier de la sélection (flèches) : dx/dy en unités monde.
    // Une salve de flèches rapprochées forme une seule étape annulable.
    void nudgeSelection(float dx, float dy);
    // Rotation de TOUS les plans autour du pivot (8.3, AltGr + molette).
    void rotateAllPlanesAround(const Vec2& pivot, float deg);

    // --- Presse-papiers ---
    void copySelection();
    void cutSelection();
    void pasteClipboard();

    // --- Peinture ---
    int pickFace(const Vec2& world) const;
    void paintFace(int fi);
    void setBrushColor(const Color& c);

    // --- Fichiers ---
    bool saveToLocation(const std::string& name);
    bool importJson(const std::string& path, bool replace);
    bool importMeshes(const std::string& path, bool replace);
    bool importObj(const std::string& path, bool replace);
    bool exportMeshesTo(const std::string& path);
    bool exportSvgTo(const std::string& path);
    void openImportDialog(int fmt, const std::string& path = "");

    // --- Divers ---
    void setStatus(const std::string& msg);
    void setToast(const std::string& msg, float secs = 3.0f);
    void logMsg(const std::string& msg);
    // Statut « Couronne : N ext. / M int. — molette : extérieurs, Maj+molette :
    // intérieurs » (partagé par le canvas, le bouton et le menu des formes).
    void statusCrown();
    // Aide prospective au survol (spec 13) : rafraîchit le toast avec le geste
    // possible sous le curseur, sans rafraîchissement continu.
    void updateHoverHelp(const Vec2& mouseWorld);
    void onEscape();
    void cancelShapeTrace();
    std::vector<int> selectionVertices() const;
    // Bascule la sélection courante en cible « sommet » (aligner/répartir).
    void toVertexSelection();
    void frameView();
    void clearSelection();
    void cancelDrag();
    std::string prefsDir() const;
    void savePrefsFile();
    void loadPrefsFile();
    void saveAutoFile();
    void loadAutoFile();

    // Accesseurs pour le HUD (rectangle de sélection).
    bool isBoxDragging() const { return drag_.kind == DragKind::Box; }
    ImVec2 boxStart() const { return {drag_.startScreen.x, drag_.startScreen.y}; }
    ImVec2 boxCur() const { return {drag_.curScreen.x, drag_.curScreen.y}; }
    size_t selectionCount() const;
    float zoomMult() const { return camera.zoom / 40.0f; }
    Vec2 viewportVec2() const { return {viewportSize.x, viewportSize.y}; }
    bool isShapeTracing() const { return drag_.kind == DragKind::Shape; }
    bool isShapeArmed() const { return isShapeTool(tool); }
    // Phase « intérieurs » de la couronne : la molette règle les côtés
    // intérieurs une fois le rayon verrouillé (2e clic posé) ; avant, elle
    // règle les extérieurs. Partagé par le canvas, la barre d'outils et le
    // menu des formes (4.2).
    bool crownInnerPhase() const { return isShapeTracing() && drag_.shapeStage >= 2; }
    // Ancre du tracé de forme en cours (monde) — pour le badge de compteurs de
    // la couronne affiché près de la forme pendant le tracé (4.2).
    Vec2 shapeAnchor() const { return drag_.shapeAnchor; }

    // --- Picking ---
    int pickVertex(const Vec2& world, float tolPx) const;
    Mesh2D::Edge pickEdge(const Vec2& world, float tolPx) const;

private:
    Drag drag_;
    std::string lastHoverHelpKey_;   // dernière aide au survol affichée (spec 13)
    float autosaveTimer_ = 0.0f;
    std::string versionTimestamp() const;   // « AAAA-MM-JJ_HHMMSS » pour les versions
    void pruneVersions();                   // conserve 10 versions max, supprime les fichiers
    // Scène à l'origine de la dernière version horodatée : une nouvelle version
    // n'est créée que si la géométrie a changé (pas de churn à chaque autosave).
    Scene lastVersionedScene_;
    bool rotUndoPushed_ = false;
    unsigned int nudgeTimeMs_ = 0;  // SDL_GetTicks() de la dernière flèche (salve = 1 undo)
    float savedZoom_ = -1.0f;   // état de la dernière sauvegarde automatique
    float savedCx_ = 0.0f;
    float savedCy_ = 0.0f;
    bool savedGrid_ = true;
    float savedGridStep_ = 1.0f;

    void handleSelectClick(const Vec2& world, const Vec2& screen);
    void handleSelectRelease(const Vec2& screen);
    void beginMoveDrag(const Vec2& world);
    void endMoveDrag(const Vec2& world);
    void applyMove(const Vec2& world);
    // Déplacement de tous les plans ensemble (8.4, AltGr + clic droit + glisser).
    void beginMoveAllDrag(const Vec2& world);
    void endMoveAllDrag(const Vec2& world);
    void applyMoveAll(const Vec2& world);
    Vec2 snappedPoint(const Vec2& w) const;
    Vec2 snapDelta(Vec2 d) const;
    void drawGrid();
    void drawMeshGeometry();
    void drawMeasureVisual();
    void drawDragPreview();
    void drawCutPreview();
    void drawShapeOutline();
    void drawMergeVisuals();
    void drawCircleLines(const Vec2& c, float radPx, const Color& col, int segs);
    void drawPlane(const Mesh2D& p, bool isActive);
    void drawPreviewGeometry();
    void dashedPairs(const std::vector<Mesh2D::Edge>& edges, const Mesh2D& p,
                     float dashPx, float gapPx, std::vector<Vec2>& out) const;

    // Construction de triangles & formes
    void addTriangleBuild(const Vec2& world);
    void advanceShapeClick(const Vec2& world);
    void completeShape();
    void addFan(const Vec2& center, float radius, float angle, int sides);
    void addRimPolygon(const Vec2& center, float radius, float angle, int sides);
    void addQuad(const Vec2& p0, const Vec2& p1);
    void addStar(const Vec2& center, float radius, float angle, float depth, int points);
    void addRing(const Vec2& center, float radius, float angle, float hole, int sides);
    void addCrown(const Vec2& center, float radius, float angle, float hole,
                  int outerSides, int innerSides, float innerAngle);

    // Sélection clic droit
    bool pickNearestOnly(const Vec2& world);
    void addEntityToSelection(const Vec2& world);
};

}  // namespace mesh
