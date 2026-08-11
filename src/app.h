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
// Outils : sélection + formes prédéfinies (4.2).
enum class Tool { Select, Rectangle, Square, Circle, Triangle, Pentagon, Hexagon, Star, Ring };
inline bool isShapeTool(Tool t) { return t != Tool::Select; }

enum class ReticleState { Off, Simple, Symmetric };
enum class PreviewMode { Off, Simple, Planes };

// --- Drag en cours ---
enum class DragKind { None, Move, Box, Shape };

struct Drag {
    DragKind kind = DragKind::None;
    std::vector<int> movingVerts;       // sommets déplacés (Move)
    std::vector<Vec2> startPositions;   // positions initiales (Move)
    Vec2 grabWorld{0, 0};               // point de saisie en monde (Move)
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
    bool gridOn = true;                 // affichage + aimantation
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
    int circleSides = 16;               // côtés du cercle/anneau (molette, mémorisé)

    // --- Couleurs & pinceau ---
    std::vector<Color> palette;         // palette personnalisable (8 défauts)
    bool brushArmed = false;
    Color brushColor{1.0f, 0.55f, 0.2f, 1.0f};
    float brushOpacity = 0.45f;         // opacité appliquée à chaque peinture

    // --- Presse-papiers interne (5.8) ---
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
    bool shapesOpen = false;
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
    bool dlgSaveOpen = false;
    char dlgSaveName[128] = {0};
    bool dlgImportOpen = false;
    int dlgImportFmt = 0;               // 0 = meshes, 1 = JSON
    char dlgImportPath[1024] = {0};
    bool dlgImportReplace = true;       // dernier mode choisi mémorisé
    bool dlgResetOpen = false;
    bool dlgHelpOpen = false;

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

    // --- Commandes (undoables) ---
    void pushUndo();
    void undo();
    void redo();
    void deleteSelection();
    void createFaceFromSelection();
    void insertVertexAt(const Mesh2D::Edge& e, const Vec2& world);
    void resetScene();
    void selectAll();
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
    bool exportMeshesTo(const std::string& path);
    void openImportDialog(int fmt, const std::string& path = "");

    // --- Divers ---
    void setStatus(const std::string& msg);
    void setToast(const std::string& msg, float secs = 3.0f);
    void logMsg(const std::string& msg);
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

    // --- Picking ---
    int pickVertex(const Vec2& world, float tolPx) const;
    Mesh2D::Edge pickEdge(const Vec2& world, float tolPx) const;

private:
    Drag drag_;
    float autosaveTimer_ = 0.0f;
    bool rotUndoPushed_ = false;
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
    Vec2 snappedPoint(const Vec2& w) const;
    Vec2 snapDelta(Vec2 d) const;
    void drawGrid();
    void drawMeshGeometry();
    void drawDragPreview();
    void drawShapeOutline();
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

    // Sélection clic droit
    bool pickNearestOnly(const Vec2& world);
    void addEntityToSelection(const Vec2& world);
};

}  // namespace mesh
