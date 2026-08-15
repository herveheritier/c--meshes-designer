#pragma once
#include "camera.h"
#include "io.h"
#include "mesh.h"
#include "renderer.h"
#include "triangulate.h"  // SetOp pour les opérations ensemblistes (5.12)

#include <imgui.h>

#include <functional>
#include <string>
#include <vector>

struct SDL_Window;

namespace mesh {

// --- Modes ---
// Cible d'édition : sommet / segment / triangle (4.4).
enum class SelMode { Vertex, Edge, Face };
// Outils : sélection + formes prédéfinies (4.2) + découpe (polygone soustrait).
enum class Tool { Select, Rectangle, Square, Circle, Triangle, Pentagon, Hexagon, Star, Ring, Crown, Cut, Polygon };
inline bool isShapeTool(Tool t) { return t != Tool::Select && t != Tool::Cut && t != Tool::Polygon; }

// Réticules (9.2) : Off → Simple (croix de visée) → Symmetric (croix pleine
// grandeur) → Mirror (reflets du curseur à travers les axes du monde).
enum class ReticleState { Off, Simple, Symmetric, Mirror };
enum class PreviewMode { Off, Simple, Planes };

// Anneau de manipulation unifié des maillages : cible du ring (sélection du
// plan actif, plan courant, ou scène complète) — même principe que le calque
// (7.7) : une poignée par action. Remplace l'ancien mode « Scène » (8.5).
enum class RingTarget { None, Selection, Plane, Scene };
// Calque d'image de fond (7.7) : manipulation de l'image à la souris.
// Poignées de l'anneau de manipulation unifié autour du curseur.
enum class LayerHandleKind {
    None,
    MoveFree,     // centre : glisser librement le calque
    MoveX,        // flèches horizontales : glisser le calque en X
    MoveY,        // flèches verticales : glisser le calque en Y
    Rotate,       // anneau : pivoter le calque autour de son centre
    ScaleX,       // poignées horizontales : largeur seule
    ScaleY,       // poignées verticales : hauteur seule
    ScaleBoth,    // poignées diagonales : les deux axes, rapport x/y conservé
    MirrorX,      // clic : symétrie horizontale du calque
    MirrorY,      // clic : symétrie verticale du calque
    MirrorBoth,   // clic : symétrie centrale (X et Y) du calque
};

// --- Drag en cours ---
enum class DragKind { None, Move, MoveAll, Box, Shape, LayerHandle, MeshRing, Lasso };

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
    // Anneau (calque 7.7 / maillage) : ancre monde sous le curseur au début
    // de la saisie (pivot des rotations / échelles) et écran de départ.
    Vec2 sceneAnchor{0, 0};
    Vec2 sceneStartScreen{0, 0};
    // Anneau maillage : ancre (pivot) au début de la saisie — l'anneau SUIT
    // la cible pendant les déplacements (les poignées restent autour d'elle).
    Vec2 ringStartAnchor{0, 0};
    // Calque d'image (7.7) : état du calque au début de la saisie (pour
    // annuler proprement et détecter un glisser sans effet).
    Vec2 layerStartCenter{0, 0};
    float layerStartRot = 0.0f, layerStartSx = 1.0f, layerStartSy = 1.0f;
    // Poignée saisie dans l'anneau unifié : type + position locale de la
    // poignée (±hw, ±hh dans le repère de départ) pour ancrer l'échelle.
    LayerHandleKind layerHandleKind = LayerHandleKind::None;
    Vec2 layerHandleLocal{0, 0};
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

    // --- Anneau de manipulation unifié des maillages (sélection / plan /
    // scène) : même principe que le calque (7.7) — une poignée par action.
    // Le bouton « Manipuler » du paquet Scène arme l'anneau (cible courante) ;
    // clic droit ou Échap au canvas désarme.
    RingTarget ringTarget = RingTarget::Selection;
    bool ringArmed = false;
    bool ringAnchored = false;
    Vec2 ringAnchor{0, 0};
    // Sensibilité de l'échelle au glisser : ×exp(0,01/px) (~×2,7 pour 100 px
    // écran) — le facteur est proportionnel aux PIXELS glissés (décalage monde
    // × zoom), pour une réponse identique et efficace quel que soit le zoom.
    static constexpr float kRingScalePerPx = 0.01f;
    // Couleur de fond du canvas (bouton « fond » du groupe Scène).
    Color bgColor = kBgDefault;

    // --- Calque d'image de fond (7.7) ---
    // Mode unifié armé depuis le popup « Calque » : un anneau de poignées
    // apparaît autour du curseur (déplacement, rotation, échelle, symétrie).
    // Clic gauche sur le canvas ancre l'anneau ; clic droit ou Échap désarme.
    // L'état du calque (chemin, transformée, opacité) vit dans scene.image et
    // est persisté dans le JSON de scène.
    bool layerArmed = false;
    // Position monde où l'anneau est ancré (après le clic) ; tant que
    // layerAnchored est faux, l'anneau suit le curseur.
    Vec2 layerAnchor{0, 0};
    bool layerAnchored = false;
    // Poignée survolée (pour mise en évidence + infobulle).
    LayerHandleKind layerHover = LayerHandleKind::None;
    // Texture GL de l'image décodée (0 = aucune) ; la synchronisation avec
    // scene.image.path (chargement / déchargement) se fait dans drawScene.
    unsigned imageTex = 0;
    std::string imageLoadedPath;
    // Dialogue « Charger une image… » (popup Calque).
    bool dlgLayerOpen = false;
    char dlgLayerPath[1024] = {0};

    // --- Sélection au lasso (5.9) ---
    // Armé depuis le bouton « Lasso » du paquet Sélection : le canvas sert
    // alors à tracer librement un polygone (en pixels écran) autour des
    // éléments à sélectionner ; relâcher applique la sélection (Maj = ajoute),
    // clic droit ou Échap désarme.
    bool lassoArmed = false;
    std::vector<Vec2> lassoPts;  // tracé en cours (coordonnées écran, échantillonné)
    void toggleLasso();          // arme / désarme la sélection au lasso
    void applyLassoSelection();  // sélectionne les éléments dans le polygone
    // 7.8 : une seule étape annulable par salve de molette sur le bouton
    // « Opacité du plan actif » (réarmée quand la molette s'arrête).
    bool opacUndoPushed_ = false;
    // Molette sur le bouton « Calque » : une seule étape annulable par salve.
    bool layerOpacUndoPushed_ = false;

    // --- Pipette de couleur (6.5) ---
    // Armée depuis le paquet Outils : un clic gauche sur le canvas prélève la
    // couleur affichée à cet endroit (faces, calque d'image, fond…) et la pose
    // comme couleur de pinceau. L'échantillonnage se fait dans drawScene — la
    // scène vient d'être dessinée, l'interface pas encore (on lit donc ce qui
    // est réellement affiché au canvas). Clic droit ou Échap désarme.
    bool pipetteArmed = false;
    bool pipettePending_ = false;   // prélèvement demandé (position en attente)
    Vec2 pipettePos{0, 0};          // position du clic (viewport, pixels logiques)
    void togglePipette();           // arme / désarme la pipette
    // Sélectionne les éléments du plan actif dont le point de référence
    // (sommet : position, segment : milieu, triangle : centre) satisfait
    // `inside` — partagé entre rectangle (5.1) et lasso (5.10).
    void collectSelectionInside(const std::function<bool(const Vec2&)>& inside);

    // --- Opérations ensemblistes (5.12) ---
    // Deux ensembles de triangles A et B, chacun mémorisé depuis la sélection
    // courante (cible triangle) du plan actif au moment de la capture. Un
    // ensemble invalide (plan changé, géométrie modifiée) est oublié : les
    // opérations (union, intersection, différence, symétrique) se font entre
    // les deux ensembles du MÊME plan actif.
    std::vector<int> boolSetA, boolSetB;    // faces mémorisées
    int boolSetAPlane = -1, boolSetBPlane = -1;  // plan d'origine de chaque ensemble
    void memorizeBoolSet(int which);        // 0 = A, 1 = B : capture la sélection courante
    void clearBoolSets();
    // Applique l'opération ensembliste entre les deux ensembles mémorisés : le
    // résultat (découpé aux arêtes de l'autre ensemble) devient la sélection
    // triangle du plan actif — dans la zone de A∪B, seule la géométrie du
    // résultat est conservée (le reste du plan est intact).
    void applyBoolOp(SetOp op);
    // Vrai si l'ensemble (0 = A, 1 = B) est mémorisé et toujours valide.
    bool boolSetValid(int which) const;
    // Nombre de faces mémorisées de l'ensemble (0 = A, 1 = B).
    size_t boolSetCount(int which) const { return which == 0 ? boolSetA.size() : boolSetB.size(); }

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
    // Distance (pixels écran) de détection des sommets (survol + sélection en
    // mode « sommet »). Réglable dans le panneau « Réglages ».
    float vertexPickTol = 8.0f;

    // --- Clic cyclique dans la pile de faces superposées (5.13) ---
    // Quand plusieurs faces se chevauchent sous le curseur, chaque clic au
    // même endroit sélectionne la face suivante en dessous. La position écran
    // du dernier clic (et la face alors choisie) décide de la suite de la
    // descente ; un clic ailleurs ou sur une zone sans face réinitialise.
    int lastFaceClick_ = -1;
    Vec2 lastFaceClickScreen_{-1e9f, -1e9f};

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
    bool wireframe = false;             // mode filaire (7.6) : arêtes seules, sans
                                        // remplissage — 2e état du bouton d'affichage
                                        // (exclusifs : cycleFillMode n'en active
                                        // qu'un à la fois ; un prefs.json édité à
                                        // la main avec les deux à vrai se guérit
                                        // au clic suivant)
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
    // Enchaînement des découpes : tant que l'outil reste armé, chaque découpe
    // appliquée s'ajoute à la MÊME étape annulable — l'historique n'est poussé
    // qu'à la 1re découpe de la chaîne. Passe à faux dès que la chaîne se
    // termine (désarmement, changement d'outil, annulation, scène remplacée…).
    bool cutChainUndo_ = false;
    bool isCutArmed() const { return tool == Tool::Cut; }
    bool isCutTracing() const { return tool == Tool::Cut && !cutPts.empty(); }
    void toggleCutTool();               // arme / désarme l'outil découpe (D)
    void applyCut();                    // ferme le polygone et soustrait (Entrée / clic droit)
    void removeLastCutPoint();          // Retour arrière pendant le tracé

    // --- Outil polygone (trace → triangulation automatique dans le plan actif) ---
    std::vector<Vec2> polyPts;          // sommets du polygone en cours de tracé
    bool isPolygonArmed() const { return tool == Tool::Polygon; }
    bool isPolygonTracing() const { return tool == Tool::Polygon && !polyPts.empty(); }
    void togglePolygonTool();           // arme / désarme l'outil polygone (U)
    void applyPolygon();                // ferme le polygone et le triangule dans le plan
    void removeLastPolygonPoint();      // Retour arrière pendant le tracé

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

    // --- Barre d'outils (3.2) ---
    // Un bit par paquet de boutons (bit i = paquet i affiché) : chaque paquet
    // possède un bouton dédié (chevron ▼/►) qui l'ouvre / le ferme. L'état est
    // mémorisé dans prefs.json (champ « toolbarPacks ») et rappelé au
    // démarrage. Tous les paquets sont ouverts par défaut.
    uint32_t toolbarPacks = 0xFFFFFFFFu;

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
    // Sélection chaînée (5.11) : sélectionne tous les éléments du plan actif
    // liés à la sélection courante par des chaînes d'adjacence — triangles par
    // au moins un sommet partagé, segments par un sommet partagé, sommets par
    // un segment. Ctrl ou Maj enfoncés : ajoute à la sélection, sinon remplace.
    void selectLinked();
    void cycleTarget();
    void cycleReticle();
    void cyclePreview();
    // Cycle le mode d'affichage des plans (7.6) : normal → toutes couleurs →
    // filaire → normal.
    void cycleFillMode();
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

    // --- Anneau de manipulation unifié des maillages ---
    // Arme / désarme l'anneau pour la cible courante (re-clic = désarmer).
    void toggleRingMode();
    // Choisit la cible (boutons radio Sélection / Plan / Scène) et arme
    // l'anneau ; None = désarmer.
    void setRingTarget(RingTarget t);
    // Nom lisible de la cible courante (sélection / plan courant / scène).
    const char* ringTargetName() const;
    // Centre (boîte englobante) de la cible du ring (sélection / plan / scène) ;
    // false si la cible ne contient aucun sommet.
    bool ringTargetCenter(Vec2& out) const;
    // Ancre l'anneau au centre de la cible courante ; si la cible est vide,
    // l'anneau reste non ancré (il suit le curseur jusqu'au premier clic).
    void recenterRing();
    // Saisie souris de l'anneau maillage : clic gauche = début (poignée,
    // symétrie instantanée ou bande de rotation), glisser = appliquer,
    // relâchement = fin (une étape annulable, retirée si aucun changement).
    bool isMeshRingDragging() const { return drag_.kind == DragKind::MeshRing; }
    void beginMeshRingDrag(const Vec2& world, const Vec2& screen);
    void applyMeshRingDrag(const Vec2& world);
    void endMeshRingDrag();
    // Symétrie instantanée de la cible autour de l'anneau (miroir X, Y, X/Y).
    void applyMeshRingMirror(LayerHandleKind kind);
    // Mémorise les sommets de départ de la cible (false si rien à manipuler).
    bool snapshotMeshRingTarget();
    // Applique `fn` à chaque sommet de la cible courante (positions restaurées).
    template <typename Fn>
    void eachRingVertex(Fn fn) {
        switch (ringTarget) {
            case RingTarget::Scene:
                for (Mesh2D& p : scene.planes)
                    for (Vec2& v : p.vertices) fn(v);
                break;
            case RingTarget::Plane:
                for (Vec2& v : scene.activePlane().vertices) fn(v);
                break;
            case RingTarget::Selection:
                for (int v : drag_.movingVerts)
                    if (v >= 0 && (size_t)v < scene.activePlane().vertices.size())
                        fn(scene.activePlane().vertices[v]);
                break;
            default: break;
        }
    }

    // --- Calque d'image (7.7) ---
    void toggleLayerMode();              // arme / désarme le mode calque unifié
    bool isLayerDragging() const;        // saisie du calque en cours
    void beginLayerDrag(const Vec2& world, const Vec2& screen);
    void applyLayerDrag(const Vec2& world, const Vec2& screen);
    void endLayerDrag();
    // Poignées monde de l'anneau autour du point donné (rendu + détection au
    // clic) : une poignée par action, sur le cercle de 40 px écran.
    void ringHandlePositions(const Vec2& c, std::vector<LayerHandleKind>& kinds,
                             std::vector<Vec2>& worldPos) const;
    // Charge le fichier image (PNG/JPEG) : décode, crée la texture, dimensionne
    // le calque à ~la moitié de la vue. Échec → statut + false (calque intact).
    bool loadImageLayer(const std::string& path);
    // Retire le calque (une étape annulable).
    void removeImageLayer();
    // Symétrie instantanée du calque (miroir X, Y ou les deux).
    void applyLayerSymmetry(LayerHandleKind kind);
    // Réajuste la taille du calque à ~la moitié de la vue (une étape annulable).
    void fitLayerToView();



    // --- Presse-papiers ---
    void copySelection();
    void cutSelection();
    void pasteClipboard();

    // --- Peinture ---
    // Face la plus haute sous `world` (celle dessinée en dernier, qui
    // recouvre les autres). Les autres usages de sélection (clic cyclique
    // 5.13) passent par pickFaces, qui renvoie TOUTE la pile.
    int pickFace(const Vec2& world) const;
    // Toutes les faces contenant `world`, de la plus haute (dessus, dessinée
    // en dernier) à la plus basse. Le clic cyclique (5.13) descend dans cette
    // pile : chaque clic au même endroit sélectionne la face suivante en
    // dessous — indispensable pour les ensembles qui se chevauchent (5.12).
    std::vector<int> pickFaces(const Vec2& world) const;
    // Clic cyclique (5.13) : la face à sélectionner sous `world` — la plus
    // haute au premier clic, puis chaque clic au même endroit écran descend
    // d'un cran dans la pile (retour en haut après la plus basse). Met à jour
    // lastFaceClick_ / lastFaceClickScreen_ ; renvoie -1 hors de toute face.
    int cyclePickFace(const Vec2& world, const Vec2& screen);
    void paintFace(int fi);
    // Peint plusieurs faces d'un coup (6.2 : toutes les faces sélectionnées
    // sont peintes, sinon seule la face cliquée l'est).
    void paintFaces(const std::vector<int>& faces);
    void setBrushColor(const Color& c);

    // --- Ordre z des faces (devant / derrière) ---
    // Les faces sélectionnées (cible « triangle ») avancent (]) ou reculent
    // ([) d'un cran dans l'ordre de dessin du plan : une face avancée est
    // dessinée par-dessus celles qui la recouvraient (et se sélectionne en
    // premier). Chaque déplacement est une étape annulable.
    void faceForward();
    void faceBackward();

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
    void drawPolygonPreview();
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
    // Ctrl+clic droit : ajoute l'entité sous le curseur sans déplacer. `screen`
    // sert au clic cyclique des faces superposées (5.13), comme le clic gauche.
    void addEntityToSelection(const Vec2& world, const Vec2& screen);
};

}  // namespace mesh
