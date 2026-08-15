#include "app.h"
#include "pngexport.h"
#include "triangulate.h"

// Décodeur PNG/JPEG (calque d'image de fond, 7.7) — implémentation dans
// src/stb_image_impl.cpp.
#include "stb_image.h"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>

namespace mesh {

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kBasePxPerUnit = 40.0f;   // zoom ×1 = 40 px/unité
constexpr float kMinZoomPx = 4.0f;        // ×0.1
constexpr float kMaxZoomPx = 400.0f;      // ×10

// Couleurs de la scène (le fond du canvas est pilotable : bgColor, 8.5).
const Color kGridMinor{1.0f, 1.0f, 1.0f, 0.055f};
const Color kGridMajor{1.0f, 1.0f, 1.0f, 0.13f};
const Color kAxisX{0.92f, 0.30f, 0.30f, 0.75f};
const Color kAxisY{0.25f, 0.85f, 0.40f, 0.75f};
const Color kFaceFill{0.28f, 0.55f, 0.95f, 0.20f};
const Color kFaceSel{0.98f, 0.60f, 0.15f, 0.40f};
const Color kEdge{0.78f, 0.82f, 0.90f, 0.85f};
const Color kEdgeDim{0.55f, 0.60f, 0.72f, 0.42f};   // plans inactifs (contexte)
const Color kEdgeSel{1.00f, 0.68f, 0.15f, 1.00f};
const Color kEdgeHover{0.35f, 0.95f, 1.00f, 1.00f};   // segment survolé (base de triangle)
const Color kEdgeHoverHalo{0.35f, 0.95f, 1.00f, 0.35f};
const Color kVert{0.66f, 0.72f, 0.82f, 0.95f};
const Color kVertDim{0.50f, 0.56f, 0.66f, 0.55f};   // points atténués des plans inactifs
const Color kVertHover{1.0f, 1.0f, 1.0f, 1.0f};
const Color kVertSel{1.00f, 0.80f, 0.20f, 1.0f};
const Color kPreview{0.40f, 0.95f, 1.00f, 0.95f};
const Color kPreviewFill{0.40f, 0.95f, 1.00f, 0.16f};   // remplissage translucide de l'aperçu
const Color kMergeRing{1.00f, 0.55f, 0.15f, 0.95f};       // anneau orange des points superposés (5.5)
const Color kMergeRadius{0.45f, 0.92f, 0.50f, 0.55f};     // rayon de fusion par déplacement (5.6)
const Color kMergeRadiusFill{0.45f, 0.92f, 0.50f, 0.09f};
const Color kMergeTarget{0.45f, 0.95f, 0.55f, 1.0f};      // cible située dans le rayon de fusion

const char* toolName(Tool t) {
    switch (t) {
        case Tool::Rectangle: return "rectangle";
        case Tool::Square: return "carré";
        case Tool::Circle: return "cercle";
        case Tool::Triangle: return "triangle";
        case Tool::Pentagon: return "pentagone";
        case Tool::Hexagon: return "hexagone";
        case Tool::Star: return "étoile";
        case Tool::Ring: return "anneau";
        case Tool::Crown: return "couronne";
        case Tool::Cut: return "découpe";
        case Tool::Polygon: return "polygone";
        default: return "?";
    }
}

bool nearMultiple(float x, float m) {
    const float r = std::fmod(std::fabs(x), m);
    return r < m * 0.02f || m - r < m * 0.02f;
}

// AltGr = touche Alt droite sur la plupart des claviers (spec 8.3). Selon les
// claviers / systèmes, AltGr est délivré comme Ctrl+Alt simultanés (Windows) ou
// comme la touche Menu (certains portables Linux) : on accepte toutes les formes
// pour rester fiable en WLM.
bool altGrDown() {
    const ImGuiIO& io = ImGui::GetIO();
    return ImGui::IsKeyDown(ImGuiKey_RightAlt) || ImGui::IsKeyDown(ImGuiKey_Menu) ||
           (io.KeyAlt && io.KeyCtrl);
}

std::vector<Color> defaultPalette() {
    return {
        rgba(1.00f, 0.35f, 0.35f),  // rouge
        rgba(1.00f, 0.65f, 0.20f),  // orange
        rgba(1.00f, 0.85f, 0.25f),  // jaune
        rgba(0.40f, 0.85f, 0.35f),  // vert
        rgba(0.30f, 0.80f, 0.85f),  // cyan
        rgba(0.40f, 0.55f, 1.00f),  // bleu
        rgba(0.75f, 0.45f, 0.95f),  // violet
        rgba(0.92f, 0.95f, 1.00f),  // blanc cassé
    };
}

}  // namespace

// ---------------------------------------------------------------------------
// Cycle de vie
// ---------------------------------------------------------------------------
void App::init() {
    if (!renderer.init()) {
        std::fprintf(stderr, "Échec d'initialisation du rendu OpenGL.\n");
    }
    palette = defaultPalette();
    loadPrefsFile();
    loadAutoFile();
    if (scene.countVerts() == 0 && undoStack.empty()) {
        // Scène de démonstration au premier lancement : plusieurs plans. Le
        // calque mémorisé (7.9) survit à la démo (scene.clear() le retirerait).
        const ImageLayer rememberedImage = scene.image;
        scene.clear();

        // Plan 1 : rectangle + étoile.
        Mesh2D& p1 = scene.planes[0];
        const int r0 = p1.addVertex({-1.00f, -0.60f});
        const int r1 = p1.addVertex({ 1.00f, -0.60f});
        const int r2 = p1.addVertex({ 1.00f,  0.60f});
        const int r3 = p1.addVertex({-1.00f,  0.60f});
        p1.addTriangulatedFace({r0, r1, r2, r3});
        std::vector<int> star;
        for (int i = 0; i < 10; ++i) {
            const float ang = (float)i * kPi / 5.0f - kPi / 2.0f;
            const float rad = (i % 2 == 0) ? 0.90f : 0.42f;
            star.push_back(p1.addVertex({2.6f + std::cos(ang) * rad, std::sin(ang) * rad}));
        }
        p1.addTriangulatedFace(star);

        // Plan 2 : triangles colorés (illustre la peinture).
        scene.planes.emplace_back();
        Mesh2D& p2 = scene.planes[1];
        auto paintFrom = [&](int before, const Color& c) {
            for (int i = before; i < (int)p2.faces.size(); ++i) {
                Face& f = p2.faces[i];
                f.color = c;
                f.hasColor = true;
            }
        };
        const int b0 = (int)p2.faces.size();
        const int t0 = p2.addVertex({-2.4f, -1.4f});
        const int t1 = p2.addVertex({-1.2f, -1.4f});
        const int t2 = p2.addVertex({-1.8f, -0.2f});
        if (p2.addTriangulatedFace({t0, t1, t2}) > 0)
            paintFrom(b0, {0.20f, 0.65f, 0.90f, 0.45f});
        const int b1 = (int)p2.faces.size();
        const int q0 = p2.addVertex({-2.4f, 0.8f});
        const int q1 = p2.addVertex({-1.2f, 0.8f});
        const int q2 = p2.addVertex({-1.2f, 1.8f});
        const int q3 = p2.addVertex({-2.4f, 1.8f});
        if (p2.addTriangulatedFace({q0, q1, q2, q3}) > 0)
            paintFrom(b1, {0.95f, 0.45f, 0.30f, 0.45f});
        // Le calque mémorisé (7.9) est restauré sur la scène de démonstration.
        scene.image = rememberedImage;
    }
    setStatus("Bienvenue ! Clic gauche : poser un point (3 clics = un triangle). "
              "Clic droit : déplacer la sélection. Molette : zoom.");
    logMsg("Meshes Designer démarré");
}

void App::shutdown() {
    saveAutoFile();
    savePrefsFile();
    if (imageTex) {
        renderer.destroyTexture(imageTex);
        imageTex = 0;
    }
    renderer.shutdown();
}

void App::newDocument() {
    scene.clear();
    clearSelection();
    undoStack.clear();
    redoStack.clear();
    cutChainUndo_ = false;  // l'historique est effacé : plus de chaîne de découpes
    currentFile.clear();
    sceneName.clear();
    triP1 = triP2 = -1;
    dirty = false;
    ringArmed = false;
    ringAnchored = false;
    layerArmed = false;
    layerAnchored = false;
    lassoArmed = false;
    lassoPts.clear();
    pipetteArmed = false;
    pipettePending_ = false;
    bgColor = kBgDefault;
    clearBoolSets();  // les ensembles A/B (5.12) sont liés à la scène
    camera.reset();
    cameraFramed = false;
    rotDeg = 0.0f;
    setStatus("Nouveau document");
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------
void App::update(float dt) {
    statusAge = std::max(0.0f, statusAge - dt);
    toastAge = std::max(0.0f, toastAge - dt);
    ImGuiIO& io = ImGui::GetIO();

    // Performance : images par seconde + redessins.
    if (dt > 1e-5f) {
        const float inst = 1.0f / dt;
        fps = (fps <= 0.0f) ? inst : fps * 0.92f + inst * 0.08f;
        if (fps < 42.0f) fpsPillGreen = false;
        else if (fps > 48.0f) fpsPillGreen = true;
    }
    redrawAcc++;
    redrawTimer += dt;
    if (redrawTimer >= 1.0f) {
        redraws = redrawAcc;
        redrawAcc = 0;
        redrawTimer = 0.0f;
    }

    // Persistance automatique (seulement si quelque chose a changé).
    autosaveTimer_ += dt;
    if (autosaveTimer_ >= 5.0f) {
        autosaveTimer_ = 0.0f;
        if (dirty || camera.zoom != savedZoom_ || camera.cx != savedCx_ ||
            camera.cy != savedCy_ || gridOn != savedGrid_ || gridStep != savedGridStep_)
            saveAutoFile();
    }

    const Vec2 vp = viewportVec2();

    // Premier cadrage automatique dès que la taille du viewport est connue.
    if (!cameraFramed && vp.x > 10.0f && vp.y > 10.0f) frameView();

    // Mode kiosque (7.5) : choix du plan en « couverture », aucune édition.
    if (kiosk) {
        if (viewportHovered) kioskX = io.MousePos.x - viewportPos.x;

        // Navigation clavier : les flèches gauche/droite parcourent les cartes
        // (le pointeur reste la méthode principale ; la position est bornée).
        {
            const float step = viewportSize.x / (float)std::max(scene.count() - 1, 1);
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
                kioskX = std::clamp(kioskX - step, 0.0f, viewportSize.x);
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
                kioskX = std::clamp(kioskX + step, 0.0f, viewportSize.x);
        }

        // Décalages par carte pilotés par un ressort légèrement sous-amorti :
        // le défilement glisse en douceur et rebondit à l'arrivée (cover-flow).
        {
            const int n = scene.count();
            const int t = kioskTarget();
            if ((int)kioskOff.size() != n || (int)kioskVel.size() != n) {
                kioskOff.resize((size_t)n);
                kioskVel.resize((size_t)n);
                for (int i = 0; i < n; ++i) {
                    kioskOff[i] = (float)(i - t);
                    kioskVel[i] = 0.0f;
                }
            } else {
                const float h = std::min(dt, 1.0f / 30.0f);  // stabilité du ressort
                constexpr float kStiff = 150.0f;
                constexpr float kDamp = 17.0f;
                for (int i = 0; i < n; ++i) {
                    const float target = (float)(i - t);
                    kioskVel[i] += (target - kioskOff[i]) * kStiff * h -
                                   kioskVel[i] * kDamp * h;
                    kioskOff[i] += kioskVel[i] * h;
                }
            }
        }

        // Toast vivant : renseigne sur le plan mis en avant au survol des cartes.
        if (!scene.planes.empty()) {
            const int t = kioskTarget();
            const Mesh2D& p = scene.planes[t];
            char msg[256];
            const std::string plabel =
                p.name.empty() ? std::string() : " « " + p.name + " »";
            std::snprintf(msg, sizeof(msg),
                          "Plan %d/%d%s%s — %d point(s), %d triangle(s) — "
                          "clic gauche : choisir ce plan · ←/→ : naviguer · "
                          "Échap / clic droit : sortir",
                          t + 1, scene.count(), t == scene.active ? " (actif)" : "",
                          plabel.c_str(), (int)p.vertices.size(), p.triangleCount());
            setToast(msg, 3.0f);
        }

        if (kioskFresh) {
            // 1re frame : ignore le clic qui vient d'activer le mode
            // (évite qu'un double-clic sur le bouton K sélectionne un plan).
            kioskFresh = false;
        } else if (io.MouseClicked[0] && viewportHovered) {
            // Clic sur le bouton « Kiosque » (rectangle de la frame précédente) :
            // laissé au bouton du voile, qui gère sa propre sortie.
            const bool onBtn = io.MousePos.x >= kioskBtnMinX &&
                               io.MousePos.x <= kioskBtnMaxX &&
                               io.MousePos.y >= kioskBtnMinY &&
                               io.MousePos.y <= kioskBtnMaxY;
            if (!onBtn) {
                const int t = kioskTarget();
                kiosk = false;
                setActivePlane(t);
                setStatus("Kiosque : plan " + std::to_string(t + 1) + " sélectionné");
                logMsg("Plan sélectionné au kiosque : n° " + std::to_string(t + 1));
            }
        } else if (io.MouseClicked[1]) {
            // Clic droit : sortie sans changement, où que soit le pointeur.
            kiosk = false;
            setStatus("Kiosque : aucun changement");
        }
        return;
    }

    const Vec2 mouseScreen{io.MousePos.x - viewportPos.x, io.MousePos.y - viewportPos.y};
    const Vec2 mouseWorld = camera.screenToWorld(mouseScreen, vp);

    // --- Mode prévisualisation : navigation seule, aucune édition ---
    // Le bouton « Aperçu » est une fenêtre flottante à part : un clic dessus ne
    // passe pas par le viewport (viewportHovered est faux), donc tout clic
    // gauche arrivé ici est bien une sortie directe de la prévisualisation.
    if (preview != PreviewMode::Off) {
        if (viewportHovered) {
            if (io.MouseWheel != 0.0f)
                camera.zoomAt(std::pow(1.1f, io.MouseWheel), mouseScreen, vp);
            if (io.MouseDown[2]) camera.pan(io.MouseDelta.x, io.MouseDelta.y, vp);
            if (io.MouseClicked[0]) exitPreview();  // clic gauche : sortie directe
        }
        return;
    }

    // --- Molette : rotation globale (AltGr), côtés du cercle/anneau/couronne,
    // rotation de la sélection, ou zoom ---
    if (io.MouseWheel != 0.0f && viewportHovered) {
        if (altGrDown() && drag_.kind == DragKind::None) {
            // 8.3 : AltGr + molette fait pivoter TOUS les plans autour du
            // curseur (5° par cran, cumulatif). Le pivot = position courante
            // du curseur dans le repère : s'il bouge, le pivot suit.
            if (!rotUndoPushed_) {
                pushUndo();
                rotUndoPushed_ = true;
            }
            rotateAllPlanesAround(mouseWorld, 5.0f * io.MouseWheel);
        } else if (isShapeTool(tool) && (tool == Tool::Circle || tool == Tool::Ring ||
                                         tool == Tool::Star || tool == Tool::Crown)) {
            // Couronne : la molette suit la phase du tracé — tant que le rayon
            // n'est pas verrouillé (avant le 2e clic) elle règle les côtés
            // EXTÉRIEURS, après le 2e clic les côtés INTÉRIEURS. Maj+molette
            // force l'autre jeu de côtés (utile avant le 1er clic, en barre
            // d'outils ou dans le menu des formes).
            if (tool == Tool::Crown) {
                const bool innerPhase = crownInnerPhase();
                const bool adjustInner = io.KeyShift != innerPhase;
                if (adjustInner)
                    crownInnerSides =
                        std::clamp(crownInnerSides + (int)std::lround(io.MouseWheel), 3, 64);
                else
                    circleSides =
                        std::clamp(circleSides + (int)std::lround(io.MouseWheel), 3, 64);
                statusCrown();
            } else {
                circleSides = std::clamp(circleSides + (int)std::lround(io.MouseWheel), 3, 64);
            }
        } else if (!ringArmed && drag_.kind == DragKind::None &&
                   tool == Tool::Select && selectionVertices().size() >= 2) {
            if (!rotUndoPushed_) {
                pushUndo();
                rotUndoPushed_ = true;
            }
            rotateSelectionAround(mouseWorld, 5.0f * io.MouseWheel);
        } else if (drag_.kind == DragKind::None) {
            camera.zoomAt(std::pow(1.1f, io.MouseWheel), mouseScreen, vp);
            camera.zoom = std::clamp(camera.zoom, kMinZoomPx, kMaxZoomPx);
        }
    }
    if (io.MouseWheel == 0.0f) rotUndoPushed_ = false;
    if (io.MouseReleased[0] || io.MouseReleased[1] || io.MouseReleased[2]) rotUndoPushed_ = false;

    // --- Pan : clic du milieu + glisser ---
    if (io.MouseDown[2] && drag_.kind == DragKind::None && viewportHovered) {
        camera.pan(io.MouseDelta.x, io.MouseDelta.y, vp);
        return;
    }

    // Hors du viewport : ne rien faire sauf si un drag est en cours.
    if (!viewportHovered && drag_.kind == DragKind::None) return;

    // --- Anneau de manipulation unifié des maillages (sélection / plan /
    // scène) : l'anneau armé monopolise le canvas — clic gauche ancre puis
    // saisit une poignée (symétries : clic instantané), clic droit désarme.
    // Molette (zoom) et clic du milieu (pan) restent des navigations de vue.
    if (ringArmed) {
        if (isMeshRingDragging()) {
            if (io.MouseDown[0]) {
                applyMeshRingDrag(mouseWorld);
            } else {
                endMeshRingDrag();
            }
        } else if (drag_.kind == DragKind::None) {
            if (io.MouseClicked[0]) {
                beginMeshRingDrag(mouseWorld, mouseScreen);
            } else if (io.MouseClicked[1]) {
                toggleRingMode();  // clic droit : désarmer
            }
        }
        return;
    }

    // --- Calque d'image (7.7) : mode unifié — un anneau de poignées apparaît
    // autour du curseur. Clic gauche ancre l'anneau ; les poignées permettent
    // déplacement, rotation, échelle et symétrie. Clic droit ou Échap désarme.
    if (layerArmed) {
        if (isLayerDragging()) {
            if (io.MouseDown[0]) {
                applyLayerDrag(mouseWorld, mouseScreen);
            } else {
                endLayerDrag();
            }
        } else if (drag_.kind == DragKind::None) {
            if (io.MouseClicked[0]) {
                beginLayerDrag(mouseWorld, mouseScreen);
            } else if (io.MouseClicked[1]) {
                toggleLayerMode();  // clic droit : désarmer
            }
        }
        return;
    }

    // --- Sélection au lasso (5.9) : tracer librement autour des éléments à
    // sélectionner. Le mode armé monopolise le canvas ; clic droit ou Échap
    // désarme. Maj au relâchement = ajoute à la sélection, sinon remplace.
    if (lassoArmed) {
        if (drag_.kind == DragKind::Lasso) {
            if (io.MouseDown[0]) {
                // Échantillonnage du tracé : un point tous les ~6 px écran.
                if (lassoPts.empty() || distance(lassoPts.back(), mouseScreen) >= 6.0f)
                    lassoPts.push_back(mouseScreen);
            } else {
                applyLassoSelection();
                drag_.kind = DragKind::None;
                lassoPts.clear();
            }
        } else if (drag_.kind == DragKind::None) {
            if (io.MouseClicked[0]) {
                drag_.kind = DragKind::Lasso;
                lassoPts.clear();
                lassoPts.push_back(mouseScreen);
            } else if (io.MouseClicked[1]) {
                toggleLasso();  // clic droit : désarmer
            }
        }
        return;
    }

    // --- Pipette (6.5) : un clic gauche sur le canvas demande le prélèvement
    // (l'échantillonnage a lieu dans drawScene, quand la scène est dessinée et
    // l'interface pas encore). Le mode armé monopolise le canvas.
    if (pipetteArmed) {
        if (io.MouseClicked[0] && viewportHovered) {
            pipettePending_ = true;
            pipettePos = mouseScreen;
        } else if (io.MouseClicked[1]) {
            togglePipette();  // clic droit : désarmer
        }
        return;
    }

    // Survol (mode sommets) : sommet le plus proche d'abord, sinon le segment
    // le plus proche (il s'illumine : un clic y accrochera un nouveau sommet
    // pour former un triangle). Pendant un clic (drag Box), le segment de la
    // frame du clic est conservé : c'est lui que le relâchement utilisera.
    hoverVertex = -1;
    if (tool == Tool::Select && selMode == SelMode::Vertex) {
        if (drag_.kind == DragKind::None) {
            hoverVertex = pickVertex(mouseWorld, vertexPickTol);
            hoverEdge = (hoverVertex >= 0) ? Mesh2D::Edge{-1, -1}
                                           : pickEdge(mouseWorld, edgePickTol);
        }
    } else {
        hoverEdge = {-1, -1};
    }
    // Aide prospective : le toast décrit le geste possible sous le curseur et
    // guide la phase en cours d'une construction (spec 13).
    updateHoverHelp(mouseWorld);

    // Mise à jour du drag en cours.
    if (drag_.kind != DragKind::None) {
        switch (drag_.kind) {
            case DragKind::Move:
                if (io.MouseDown[1]) {
                    applyMove(mouseWorld);
                } else {
                    endMoveDrag(mouseWorld);
                    drag_.kind = DragKind::None;
                }
                break;
            case DragKind::MoveAll:
                if (io.MouseDown[1]) {
                    applyMoveAll(mouseWorld);
                } else {
                    endMoveAllDrag(mouseWorld);
                    drag_.kind = DragKind::None;
                }
                break;
            case DragKind::Box: drag_.curScreen = mouseScreen; break;
            case DragKind::Shape:
                drag_.shapeCur = snappedPoint(mouseWorld);
                if (drag_.shapeStage == 2) {
                    const float d = length(drag_.shapeCur - drag_.shapeAnchor);
                    const float r = std::max(drag_.shapeRadius, 1e-4f);
                    drag_.shapeInner = std::clamp(d / r, 0.1f, 0.9f);
                }
                break;
            default: break;
        }
    }

    // --- Clic droit : sélection / déplacement de la sélection ---
    if (io.MouseClicked[1]) {
        if (tool == Tool::Cut) {
            applyCut();  // clic droit : ferme la découpe et l'applique
            return;
        }
        if (tool == Tool::Polygon) {
            applyPolygon();  // clic droit : ferme le polygone et triangule
            return;
        }
        if (measureActive) {
            toggleMeasure();  // clic droit : désarme l'outil mesure
            return;
        }
        if (drag_.kind == DragKind::Shape) {
            cancelShapeTrace();  // annule le tracé en cours (4.2)
        } else if (altGrDown()) {
            // 8.4 : AltGr + clic droit arme le déplacement de TOUS les plans
            // d'un même décalage (la vue ne bouge pas).
            if (drag_.kind == DragKind::None) beginMoveAllDrag(mouseWorld);
        } else if (io.KeyCtrl) {
            addEntityToSelection(mouseWorld, mouseScreen);  // ajoute sans déplacer, jamais de doublon
        } else if (drag_.kind == DragKind::None) {
            // Mode sommet : le bouton droit sélectionne le sommet le plus
            // proche. Sans touche clavier, ce sommet devient le seul
            // sélectionné et il est immédiatement saisissable (grab) ; avec
            // Maj, il est ajouté/retiré de la sélection sans saisie.
            // Mode segment : même principe — le segment le plus proche devient
            // le seul sélectionné et se déplace aussitôt.
            // Mode triangle : idem — le triangle sous le curseur devient le
            // seul sélectionné et se déplace aussitôt.
            bool handled = false;
            if (selMode == SelMode::Vertex) {
                const int v = pickVertex(mouseWorld, vertexPickTol);
                if (v >= 0) {
                    handled = true;
                    if (io.KeyShift) {
                        const auto it = std::find(selVerts.begin(), selVerts.end(), v);
                        if (it != selVerts.end()) selVerts.erase(it);
                        else selVerts.push_back(v);
                    } else {
                        clearSelection();
                        selVerts.push_back(v);
                        beginMoveDrag(mouseWorld);
                    }
                }
            } else if (selMode == SelMode::Edge) {
                const Mesh2D::Edge e = pickEdge(mouseWorld, edgePickTol);
                if (e.first >= 0) {
                    handled = true;
                    if (io.KeyShift) {
                        const auto it = std::find(selEdges.begin(), selEdges.end(), e);
                        if (it != selEdges.end()) selEdges.erase(it);
                        else selEdges.push_back(e);
                    } else {
                        clearSelection();
                        selEdges.push_back(e);
                        beginMoveDrag(mouseWorld);
                    }
                }
            } else if (selMode == SelMode::Face) {
                // 5.13 : même clic cyclique qu'à gauche — chaque clic au même
                // endroit descend d'un cran dans la pile de faces superposées.
                const int fi = cyclePickFace(mouseWorld, mouseScreen);
                if (fi >= 0) {
                    handled = true;
                    if (io.KeyShift) {
                        const auto it = std::find(selFaces.begin(), selFaces.end(), fi);
                        if (it != selFaces.end()) selFaces.erase(it);
                        else selFaces.push_back(fi);
                    } else {
                        clearSelection();
                        selFaces.push_back(fi);
                        beginMoveDrag(mouseWorld);
                    }
                }
            }
            if (!handled) {
                if (!selectionVertices().empty()) {
                    beginMoveDrag(mouseWorld);
                } else if (pickNearestOnly(mouseWorld)) {
                    beginMoveDrag(mouseWorld);
                } else {
                    clearSelection();
                }
            }
        }
    }

    // --- Clic gauche ---
    if (io.MouseClicked[0]) {
        if (tool == Tool::Cut) {
            // Sommet du polygone de découpe ; un clic près du 1er point ferme
            // et découpe (équivalent au clic droit / Entrée).
            const Vec2 w = snappedPoint(mouseWorld);
            const float closeTol = 14.0f / std::max(camera.zoom, 1e-3f);
            if (cutPts.size() >= 3 && distance(w, cutPts.front()) < closeTol) {
                applyCut();
            } else {
                cutPts.push_back(w);
                setStatus("Découpe : " + std::to_string(cutPts.size()) +
                          " point(s) — clic droit ou Entrée : découper · Retour "
                          "arrière : retirer le dernier point");
            }
            return;
        }
        if (tool == Tool::Polygon) {
            // Sommet du polygone ; un clic près du 1er point ferme et triangule
            // (équivalent au clic droit / Entrée).
            const Vec2 w = snappedPoint(mouseWorld);
            const float closeTol = 14.0f / std::max(camera.zoom, 1e-3f);
            if (polyPts.size() >= 3 && distance(w, polyPts.front()) < closeTol) {
                applyPolygon();
            } else {
                polyPts.push_back(w);
                setStatus("Polygone : " + std::to_string(polyPts.size()) +
                          " point(s) — clic droit ou Entrée : valider · Retour "
                          "arrière : retirer le dernier point");
            }
            return;
        }
        // Outil mesure : les clics posent le 1er puis le 2e point (outil
        // Sélection uniquement — une forme armée garde la priorité).
        if (measureActive && tool == Tool::Select && drag_.kind == DragKind::None) {
            const Vec2 w = snappedPoint(mouseWorld);
            if (!measureHasA) {
                measureA = w;
                measureB = measureA;
                measureHasA = true;
                measureHasB = false;
                setStatus("Mesure : 1er point posé — 2e clic : distance · "
                          "clic droit ou Échap pour désarmer");
            } else {
                measureB = w;
                measureHasA = false;
                measureHasB = true;
                char buf[64];
                std::snprintf(buf, sizeof(buf), "Distance : %.2f unités",
                              distance(measureA, measureB));
                setStatus(buf);
                logMsg(buf);
            }
            return;
        }
        if (isShapeTool(tool)) {
            if (drag_.kind == DragKind::None) {
                // 1er clic : pose l'ancre, la forme suit la souris jusqu'au 2e clic.
                drag_.kind = DragKind::Shape;
                drag_.shapeStage = 1;
                drag_.shapeAnchor = snappedPoint(mouseWorld);
                drag_.shapeCur = drag_.shapeAnchor;
                setStatus(tool == Tool::Crown
                              ? "1er clic posé — déplacez la souris (molette : côtés "
                                "extérieurs), 2e clic verrouille le rayon"
                              : "1er clic posé — déplacez la souris, puis validez au 2e clic");
            } else if (drag_.kind == DragKind::Shape) {
                advanceShapeClick(mouseWorld);
            }
        } else if (drag_.kind == DragKind::None) {
            if (brushArmed) {
                // 6.2 : avec des triangles sélectionnés (cible « triangle »),
                // tous sont peints d'un coup ; sans sélection, seul le
                // triangle cliqué l'est.
                if (!selFaces.empty()) {
                    pushUndo();
                    paintFaces(selFaces);
                    return;
                }
                const int fi = pickFace(mouseWorld);
                if (fi >= 0) {
                    pushUndo();
                    paintFace(fi);
                    return;
                }
            }
            handleSelectClick(mouseWorld, mouseScreen);
        }
    }

    // Fin du lasso (clic gauche sans déplacement = construction).
    if (io.MouseReleased[0] && drag_.kind == DragKind::Box) {
        handleSelectRelease(mouseScreen);
        drag_.kind = DragKind::None;
    }
}

void App::drawScene() {
    const float vw = viewportSize.x;
    const float vh = viewportSize.y;
    if (vw < 1.0f || vh < 1.0f) return;

    const float halfW = vw / (2.0f * camera.zoom);
    const float halfH = vh / (2.0f * camera.zoom);
    renderer.setProjection(camera.cx - halfW, camera.cx + halfW, camera.cy - halfH,
                           camera.cy + halfH);
    renderer.clear(bgColor);

    // Calque d'image (7.7) : synchronise la texture avec scene.image.path
    // (chargement / déchargement quand le chemin change — chargement d'une
    // scène, annulation, réinitialisation, démarrage avec calque mémorisé…).
    if (scene.image.path != imageLoadedPath) {
        if (imageTex) {
            renderer.destroyTexture(imageTex);
            imageTex = 0;
        }
        if (!scene.image.path.empty()) {
            // La transformée (position, rotation, échelle, opacité, visibilité)
            // vient de l'état restauré (scène chargée, préférences 7.9, undo) :
            // loadImageLayer ne décode que la texture — on la réapplique après.
            const ImageLayer saved = scene.image;
            if (!loadImageLayer(scene.image.path)) {
                // Échec (fichier introuvable, format…) : le calque est retiré,
                // le statut a été posé par loadImageLayer.
                scene.image = ImageLayer{};
            } else {
                scene.image.center = saved.center;
                scene.image.rotation = saved.rotation;
                scene.image.scaleX = saved.scaleX;
                scene.image.scaleY = saved.scaleY;
                scene.image.opacity = saved.opacity;
                scene.image.visible = saved.visible;
            }
        }
        imageLoadedPath = scene.image.path;
    }

    // Dessin du calque derrière la grille et les plans (seulement si la
    // texture a bien été chargée pour le chemin courant).
    if (imageTex && scene.image.visible && scene.image.path == imageLoadedPath &&
        scene.image.w > 0 && scene.image.h > 0) {
        const float hw = scene.image.w * scene.image.scaleX * 0.5f;
        const float hh = scene.image.h * scene.image.scaleY * 0.5f;
        const float cs = std::cos(scene.image.rotation);
        const float sn = std::sin(scene.image.rotation);
        const Vec2 c = scene.image.center;
        // Les 4 coins (±hw, ±hh) tournés de `rotation` autour du centre.
        const Vec2 p0{c.x - cs * hw + sn * hh, c.y - sn * hw - cs * hh};
        const Vec2 p1{c.x + cs * hw + sn * hh, c.y + sn * hw - cs * hh};
        const Vec2 p2{c.x + cs * hw - sn * hh, c.y + sn * hw + cs * hh};
        const Vec2 p3{c.x - cs * hw - sn * hh, c.y - sn * hw + cs * hh};
        renderer.drawTexturedQuad(imageTex, p0, p1, p2, p3,
                                  rgba(1.0f, 1.0f, 1.0f, scene.image.opacity));
    }

    // Kiosque : la scène d'édition reste visible DERRIÈRE le voile plein écran
    // dessiné par l'interface (ui.cpp). Aucune édition n'est possible (update()
    // court-circuite le mode) ; le voile apporte la teinte ardoise et l'ombre
    // des cartes se détache sur la scène assombrie.
    if (preview == PreviewMode::Off && gridOn) drawGrid();

    if (preview == PreviewMode::Planes || preview == PreviewMode::Simple) {
        drawPreviewGeometry();
    } else {
        drawMeshGeometry();
        drawMergeVisuals();
        drawMeasureVisual();
        drawDragPreview();
    }
    // Pipette (6.5) : le prélèvement demandé pendant update() est honoré ici —
    // la scène vient d'être dessinée et l'interface pas encore, on lit donc le
    // pixel réellement affiché au canvas. Échelle écran → framebuffer (HiDPI).
    if (pipettePending_) {
        pipettePending_ = false;
        int fbW = 0, fbH = 0, winW = 0, winH = 0;
        if (window) {
            SDL_GL_GetDrawableSize(window, &fbW, &fbH);
            SDL_GetWindowSize(window, &winW, &winH);
        }
        const float fx = winW > 0 ? (float)fbW / (float)winW : 1.0f;
        const float fy = winH > 0 ? (float)fbH / (float)winH : 1.0f;
        Color c;
        if (renderer.readPixel((int)(pipettePos.x * fx), (int)(pipettePos.y * fy), c)) {
            setBrushColor(c);  // pose la couleur ET arme le pinceau
            char hex[8];
            std::snprintf(hex, sizeof(hex), "#%02X%02X%02X", (int)(c.r * 255.0f),
                          (int)(c.g * 255.0f), (int)(c.b * 255.0f));
            setStatus("Pipette : " + std::string(hex) + " (" +
                      std::to_string((int)(c.r * 255.0f)) + "," +
                      std::to_string((int)(c.g * 255.0f)) + "," +
                      std::to_string((int)(c.b * 255.0f)) + ") — pinceau armé");
            logMsg("Pipette : " + std::string(hex));
        } else {
            setStatus("Pipette : clic hors du canvas");
        }
        pipetteArmed = false;
    }

    // Export d'image : la demande (posée par l'interface) est honorée dès que
    // la scène est dessinée — avant que l'interface ne soit rendue par-dessus.
    exportPngIfRequested();
}

void App::exportPngIfRequested() {
    if (!exportPngRequested) return;
    exportPngRequested = false;
    if (exportPngPath.empty()) {
        setStatus("Export PNG : nom de fichier vide");
        return;
    }
    const int w = renderer.viewportW();
    const int h = renderer.viewportH();
    if (w <= 0 || h <= 0) {
        setStatus("Export PNG : viewport vide");
        return;
    }
    const std::vector<unsigned char> px = renderer.readPixelsRGBA();
    if (px.empty()) {
        setStatus("Export PNG : lecture des pixels impossible");
        return;
    }
    if (writePng(exportPngPath, w, h, px.data())) {
        setStatus("Image PNG exportée : " + exportPngPath);
        logMsg("Image PNG exportée : " + exportPngPath + " (" +
               std::to_string(w) + "×" + std::to_string(h) + ")");
    } else {
        setStatus("Échec de l'export PNG : " + exportPngPath);
        logMsg("Échec de l'export PNG : " + exportPngPath);
    }
}

// Géométrie seule (prévisualisation) : pas de points de contrôle ni de sélection.
void App::drawPreviewGeometry() {
    const int n = scene.count();
    const int active = scene.active;

    // Aperçu simple (9.3.1) : plan actif rempli, autres en contours estompés.
    if (preview == PreviewMode::Simple) {
        for (int i = 0; i < n; ++i) {
            if (i == active) {
                drawPlane(scene.planes[i], true);
            } else {
                const Mesh2D& p = scene.planes[i];
                std::vector<Vec2> segs;
                const auto es = p.edges();
                segs.reserve(es.size() * 2);
                for (const auto& e : es) {
                    segs.push_back(p.vertices[e.first]);
                    segs.push_back(p.vertices[e.second]);
                }
                renderer.drawLines(segs, kEdgeDim);
            }
        }
        return;
    }

    // « Plans » (9.3.2) : tous les plans remplis, dans l'ordre d'empilement
    // (le plan d'indice le plus élevé recouvre les précédents). Aucun contour
    // n'est tracé : la vue de composition montre uniquement les surfaces, sans
    // les arêtes internes des triangles ni le périmètre des plans.
    for (int i = 0; i < n; ++i) {
        const Mesh2D& p = scene.planes[i];
        for (const Face& f : p.faces) {
            if ((int)f.verts.size() < 3) continue;
            std::vector<Vec2> pts;
            pts.reserve(f.verts.size());
            for (int v : f.verts) pts.push_back(p.vertices[v]);
            std::vector<int> local;
            triangulatePolygon(pts, local);
            std::vector<Vec2> triPts;
            triPts.reserve(local.size());
            for (int idx : local) triPts.push_back(pts[idx]);
            Color c = f.hasColor ? f.color : kFaceFill;
            c.a *= p.opacity;  // opacité du plan (7.8)
            renderer.drawTriangles(triPts, c);
        }
    }
}

// ---------------------------------------------------------------------------
// Sélection & picking
// ---------------------------------------------------------------------------
void App::clearSelection() {
    selVerts.clear();
    selEdges.clear();
    selFaces.clear();
    hoverVertex = -1;
    hoverEdge = {-1, -1};
    // Note : le clic cyclique (5.13) n'est PAS réinitialisé ici — clearSelection
    // est appelé après cyclePickFace dans le clic gauche, ce qui casserait la
    // descente dans la pile. Le cycle se réinitialise dans cyclePickFace
    // (clic hors de toute face) et au changement de plan / outil.
}

int App::pickVertex(const Vec2& world, float tolPx) const {
    const float tol = tolPx / camera.zoom;
    int best = -1;
    float bestD = tol;
    for (int i = 0; i < (int)scene.activePlane().vertices.size(); ++i) {
        const float d = distance(world, scene.activePlane().vertices[i]);
        if (d <= bestD) {
            bestD = d;
            best = i;
        }
    }
    return best;
}

Mesh2D::Edge App::pickEdge(const Vec2& world, float tolPx) const {
    const float tol = tolPx / camera.zoom;
    Mesh2D::Edge best{-1, -1};
    float bestD = tol;
    for (const auto& e : scene.activePlane().edges()) {
        const float d = pointSegmentDistance(world, scene.activePlane().vertices[e.first],
                                             scene.activePlane().vertices[e.second]);
        if (d <= bestD) {
            bestD = d;
            best = e;
        }
    }
    return best;
}

int App::pickFace(const Vec2& world) const {
    for (int fi = (int)scene.activePlane().faces.size() - 1; fi >= 0; --fi) {
        const Face& f = scene.activePlane().faces[fi];
        std::vector<Vec2> pts;
        pts.reserve(f.verts.size());
        for (int v : f.verts) pts.push_back(scene.activePlane().vertices[v]);
        if (pointInPolygon(world, pts)) return fi;
    }
    return -1;
}

std::vector<int> App::pickFaces(const Vec2& world) const {
    std::vector<int> out;
    const Mesh2D& m = scene.activePlane();
    for (int fi = (int)m.faces.size() - 1; fi >= 0; --fi) {
        const Face& f = m.faces[fi];
        std::vector<Vec2> pts;
        pts.reserve(f.verts.size());
        for (int v : f.verts) pts.push_back(m.vertices[v]);
        if (pointInPolygon(world, pts)) out.push_back(fi);
    }
    return out;
}

int App::cyclePickFace(const Vec2& world, const Vec2& screen) {
    const std::vector<int> stack = pickFaces(world);
    if (stack.empty()) {
        lastFaceClick_ = -1;
        lastFaceClickScreen_ = {-1e9f, -1e9f};
        return -1;
    }
    // Clic au même endroit écran que le précédent ET sur une face de la même
    // pile : on descend d'un cran (la face juste en dessous de la précédente) ;
    // au-delà de la plus basse, on revient à la plus haute. Un clic ailleurs
    // (ou une pile différente) repart de la face du dessus.
    if (lastFaceClick_ >= 0 &&
        distance(screen, lastFaceClickScreen_) < 6.0f) {
        for (size_t i = 0; i < stack.size(); ++i) {
            if (stack[i] == lastFaceClick_) {
                const int next = stack[(i + 1) % stack.size()];
                lastFaceClick_ = next;
                lastFaceClickScreen_ = screen;
                return next;
            }
        }
    }
    lastFaceClick_ = stack[0];
    lastFaceClickScreen_ = screen;
    return stack[0];
}

std::vector<int> App::selectionVertices() const {
    std::vector<int> out;
    auto push = [&](int v) {
        if (std::find(out.begin(), out.end(), v) == out.end()) out.push_back(v);
    };
    switch (selMode) {
        case SelMode::Vertex:
            out = selVerts;
            break;
        case SelMode::Edge:
            for (const auto& e : selEdges) {
                push(e.first);
                push(e.second);
            }
            break;
        case SelMode::Face:
            for (int fi : selFaces)
                for (int v : scene.activePlane().faces[fi].verts) push(v);
            break;
    }
    return out;
}

size_t App::selectionCount() const {
    return selVerts.size() + selEdges.size() + selFaces.size();
}

void App::toVertexSelection() {
    if (selMode != SelMode::Vertex) {
        selMode = SelMode::Vertex;
        selVerts = selectionVertices();
        selEdges.clear();
        selFaces.clear();
    }
}

// ---------------------------------------------------------------------------
// Interaction de l'outil Sélection
// ---------------------------------------------------------------------------
void App::handleSelectClick(const Vec2& world, const Vec2& screen) {
    ImGuiIO& io = ImGui::GetIO();
    if (selMode == SelMode::Vertex) {
        // 5.5 : un clic sur l'anneau orange sélectionne TOUS les points
        // superposés (le bouton Fusionner pourra ensuite les regrouper).
        const int g = pickOverlapGroup(world, 12.0f);
        if (g >= 0) {
            const auto groups = overlapGroups();
            if (!io.KeyShift) clearSelection();
            for (int v : groups[g]) selVerts.push_back(v);
            setStatus("N points superposés sélectionnés — " +
                      std::to_string(groups[g].size()) +
                      " · « Fusionner » les regroupe en un seul point");
            return;
        }
        const int v = pickVertex(world, vertexPickTol);
        if (v >= 0) {
            if (io.KeyShift) {
                const auto it = std::find(selVerts.begin(), selVerts.end(), v);
                if (it != selVerts.end()) selVerts.erase(it);
                else selVerts.push_back(v);
            } else {
                clearSelection();
                selVerts.push_back(v);
            }
            return;
        }
        if (!io.KeyShift) clearSelection();
        // Le segment illuminé au survol est la base du prochain triangle : on
        // le mémorise au moment du clic (le relâchement s'y accroche).
        hoverEdge = pickEdge(world, edgePickTol);
        drag_.kind = DragKind::Box;
        drag_.startScreen = screen;
        drag_.curScreen = screen;
    } else if (selMode == SelMode::Edge) {
        const Mesh2D::Edge e = pickEdge(world, edgePickTol);
        if (e.first >= 0) {
            const auto it = std::find(selEdges.begin(), selEdges.end(), e);
            if (io.KeyShift) {
                if (it != selEdges.end()) selEdges.erase(it);
                else selEdges.push_back(e);
            } else {
                clearSelection();
                selEdges.push_back(e);
            }
            return;
        }
        if (!io.KeyShift) clearSelection();
        drag_.kind = DragKind::Box;
        drag_.startScreen = screen;
        drag_.curScreen = screen;
    } else {  // Face (triangle)
        // 5.13 : clic cyclique — quand plusieurs faces se chevauchent au même
        // endroit, chaque clic sélectionne la face suivante en dessous (et
        // l'ensemble des faces empilées est signalé à l'utilisateur).
        const std::vector<int> stack = pickFaces(world);
        const int fi = cyclePickFace(world, screen);
        if (fi >= 0) {
            const auto it = std::find(selFaces.begin(), selFaces.end(), fi);
            if (io.KeyShift) {
                if (it != selFaces.end()) selFaces.erase(it);
                else selFaces.push_back(fi);
            } else {
                clearSelection();
                selFaces.push_back(fi);
            }
            if (stack.size() > 1) {
                const int rank = (int)(std::find(stack.begin(), stack.end(), fi) -
                                       stack.begin()) +
                                 1;  // 1 = la plus haute
                setStatus(std::to_string(stack.size()) +
                          " faces superposées ici — face " + std::to_string(rank) + "/" +
                          std::to_string(stack.size()) +
                          " (de haut en bas) · re-clic au même endroit pour la "
                          "suivante");
            }
            return;
        }
        if (!io.KeyShift) clearSelection();
        drag_.kind = DragKind::Box;
        drag_.startScreen = screen;
        drag_.curScreen = screen;
    }
}

void App::beginMoveDrag(const Vec2& world) {
    const std::vector<int> verts = selectionVertices();
    if (verts.empty()) return;
    pushUndo();
    drag_.kind = DragKind::Move;
    drag_.movingVerts = verts;
    drag_.startPositions.clear();
    drag_.startPositions.reserve(verts.size());
    for (int v : verts) drag_.startPositions.push_back(scene.activePlane().vertices[v]);
    drag_.grabWorld = world;
}

void App::beginMoveAllDrag(const Vec2& world) {
    pushUndo();
    drag_.kind = DragKind::MoveAll;
    drag_.allPlaneStarts.clear();
    drag_.allPlaneStarts.reserve(scene.planes.size());
    for (const Mesh2D& p : scene.planes)
        drag_.allPlaneStarts.push_back(p.vertices);
    drag_.grabWorld = world;
    setStatus("Déplacement de tous les plans ensemble — AltGr+clic droit + glisser");
}

void App::applyMoveAll(const Vec2& world) {
    Vec2 delta = world - drag_.grabWorld;
    if (snapOn) delta = snapDelta(delta);
    for (size_t i = 0; i < scene.planes.size() && i < drag_.allPlaneStarts.size(); ++i) {
        const std::vector<Vec2>& start = drag_.allPlaneStarts[i];
        Mesh2D& p = scene.planes[i];
        const size_t n = std::min(p.vertices.size(), start.size());
        for (size_t j = 0; j < n; ++j) p.vertices[j] = start[j] + delta;
    }
}

void App::endMoveAllDrag(const Vec2& world) {
    (void)world;  // aucun picking au relâchement : on ne change pas la sélection
    bool moved = false;
    for (size_t i = 0; i < scene.planes.size() && i < drag_.allPlaneStarts.size(); ++i) {
        const std::vector<Vec2>& start = drag_.allPlaneStarts[i];
        const Mesh2D& p = scene.planes[i];
        const size_t n = std::min(p.vertices.size(), start.size());
        for (size_t j = 0; j < n; ++j) {
            if (distance(p.vertices[j], start[j]) > 1e-6f) {
                moved = true;
                break;
            }
        }
        if (moved) break;
    }
    if (!moved) {
        // AltGr + clic droit simple : aucun déplacement, on annule l'entrée.
        undoStack.pop_back();
    } else {
        setStatus("Tous les plans déplacés d'un même décalage (AltGr+clic droit)");
        logMsg("Tous les plans déplacés d'un même décalage");
    }
}

void App::endMoveDrag(const Vec2& world) {
    bool moved = false;
    const size_t n = scene.activePlane().vertices.size();
    for (size_t i = 0; i < drag_.movingVerts.size(); ++i) {
        const int v = drag_.movingVerts[i];
        if (v < 0 || (size_t)v >= n) continue;
        if (distance(scene.activePlane().vertices[v], drag_.startPositions[i]) > 1e-6f) {
            moved = true;
            break;
        }
    }
    if (!moved) {
        // Clic droit simple : sélectionne l'entité la plus proche et
        // désélectionne le reste (ou efface si zone vide).
        undoStack.pop_back();
        pickNearestOnly(world);
        return;
    }

    // 5.6 : en mode fusion par déplacement (armé ou verrouillé), relâcher le
    // point unique sélectionné près d'un autre point les fusionne en un seul.
    if (mergeMode != MergeMode::Off && drag_.movingVerts.size() == 1) {
        if (tryMergeByDrag(drag_.movingVerts[0])) return;
    }
    setStatus(std::to_string(drag_.movingVerts.size()) + " sommet(s) déplacé(s)");
}

void App::applyMove(const Vec2& world) {
    Vec2 delta = world - drag_.grabWorld;
    if (snapOn) delta = snapDelta(delta);
    const size_t n = scene.activePlane().vertices.size();
    for (size_t i = 0; i < drag_.movingVerts.size(); ++i) {
        const int v = drag_.movingVerts[i];
        if (v < 0 || (size_t)v >= n) continue;  // le maillage a pu changer pendant le drag
        scene.activePlane().vertices[v] = drag_.startPositions[i] + delta;
    }
}

// ---------------------------------------------------------------------------
// Fusion de points (5.5 / 5.6)
// ---------------------------------------------------------------------------
// Groupes de sommets du plan actif occupant la même position (mêmes
// coordonnées, tolérance 1e-4 pour les flottants). Chaque groupe contient les
// indices de tous les sommets superposés (≥ 2 membres).
std::vector<std::vector<int>> App::overlapGroups() const {
    const Mesh2D& m = scene.activePlane();
    const int n = (int)m.vertices.size();
    std::vector<bool> used((size_t)n, false);
    std::vector<std::vector<int>> out;
    constexpr float kEps = 1e-4f;
    for (int i = 0; i < n; ++i) {
        if (used[i]) continue;
        std::vector<int> g = {i};
        used[i] = true;
        for (int j = i + 1; j < n; ++j) {
            if (used[j]) continue;
            if (std::fabs(m.vertices[j].x - m.vertices[i].x) < kEps &&
                std::fabs(m.vertices[j].y - m.vertices[i].y) < kEps) {
                g.push_back(j);
                used[j] = true;
            }
        }
        if (g.size() >= 2) out.push_back(std::move(g));
    }
    return out;
}

int App::pickOverlapGroup(const Vec2& world, float tolPx) const {
    const auto groups = overlapGroups();
    const float tol = tolPx / camera.zoom;
    int best = -1;
    float bestD = tol;
    for (size_t gi = 0; gi < groups.size(); ++gi) {
        const Vec2 p = scene.activePlane().vertices[groups[gi][0]];
        const float d = distance(world, p);
        if (d <= bestD) {
            bestD = d;
            best = (int)gi;
        }
    }
    return best;
}

// Sommet le plus proche de `v` à moins de `tolPx` pixels écran (indépendant du
// zoom). Utilisé par la fusion par déplacement (cible de fusion) et par le
// rendu (surbrillance de la cible dans le rayon).
int App::pickMergeTarget(int v, float tolPx) const {
    const Mesh2D& m = scene.activePlane();
    if (v < 0 || (size_t)v >= m.vertices.size()) return -1;
    const Vec2 p = m.vertices[v];
    const float tol = tolPx / camera.zoom;
    int best = -1;
    float bestD = tol;
    for (int i = 0; i < (int)m.vertices.size(); ++i) {
        if (i == v) continue;
        const float d = distance(p, m.vertices[i]);
        if (d <= bestD) {
            bestD = d;
            best = i;
        }
    }
    return best;
}

void App::toggleMergeMode() {
    if (mergeMode == MergeMode::Locked) {
        mergeMode = MergeMode::Off;
        setStatus("Fusion par déplacement désarmée");
        return;
    }
    if (mergeMode == MergeMode::Armed) {
        mergeMode = MergeMode::Locked;
        setStatus("Fusion par déplacement verrouillée (cadenas) — "
                  "les fusions s'enchaînent jusqu'à désarmement");
        return;
    }
    if (selMode == SelMode::Vertex && selVerts.size() == 1) {
        mergeMode = MergeMode::Armed;
        setStatus("Fusion par déplacement armée — glissez le point sélectionné "
                  "près d'un autre (rayon " + std::to_string(mergeRadius) + " px)");
    } else if (selMode == SelMode::Vertex && selVerts.size() >= 2) {
        mergeSelectionToCentroid();  // 5.5 : regrouper la sélection courante
    }
}

void App::mergeSelectionToCentroid() {
    if (selMode != SelMode::Vertex || selVerts.size() < 2) return;
    const std::vector<int> verts = selVerts;
    const Mesh2D& m = scene.activePlane();
    Vec2 c;
    for (int v : verts) c = c + m.vertices[v];
    c = c / (float)verts.size();
    pushUndo();
    const int keep = scene.activePlane().mergeVertices(verts, c);
    if (keep < 0) {
        undoStack.pop_back();
        return;
    }
    clearSelection();
    selVerts.push_back(keep);
    dirty = true;
    setStatus(std::to_string(verts.size()) + " points superposés fusionnés "
              "en un seul (position moyenne)");
    logMsg("Fusion de " + std::to_string(verts.size()) + " points superposés");
}

bool App::tryMergeByDrag(int v) {
    const int target = pickMergeTarget(v, (float)mergeRadius);
    if (target < 0) return false;
    const Mesh2D& m = scene.activePlane();
    const Vec2 pos = (m.vertices[v] + m.vertices[target]) * 0.5f;
    const int keep = scene.activePlane().mergeVertices({v, target}, pos);
    if (keep < 0) return false;
    clearSelection();
    selVerts.push_back(keep);  // point fusionné sélectionné (enchaînement 5.6)
    if (mergeMode == MergeMode::Armed) {
        // Mode simple : une fusion réussie désarme (il faut ré-armer).
        mergeMode = MergeMode::Off;
    }
    dirty = true;
    setStatus(std::string("Points fusionnés par déplacement") +
              (mergeMode == MergeMode::Locked
                   ? " — mode verrouillé, glissez à nouveau pour continuer"
                   : ""));
    logMsg("Fusion par déplacement : " + std::to_string(v) + " et " +
           std::to_string(target));
    return true;
}

void App::handleSelectRelease(const Vec2& screen) {
    const Vec2 world = camera.screenToWorld(screen, viewportVec2());
    if (distance(drag_.startScreen, screen) < 4.0f) {
        // Simple clic sur une zone vide : construction de triangle (mode sommet).
        if (selMode == SelMode::Vertex) addTriangleBuild(world);
        return;
    }

    // Rectangle de sélection (lasso) : sélectionne les sommets, ne déplace jamais.
    const ImVec2 p0(std::min(drag_.startScreen.x, screen.x),
                    std::min(drag_.startScreen.y, screen.y));
    const ImVec2 p1(std::max(drag_.startScreen.x, screen.x),
                    std::max(drag_.startScreen.y, screen.y));
    ImGuiIO& io = ImGui::GetIO();

    auto inBox = [&](const Vec2& w) {
        const Vec2 s = camera.worldToScreen(w, viewportVec2());
        return s.x >= p0.x && s.x <= p1.x && s.y >= p0.y && s.y <= p1.y;
    };

    if (!io.KeyShift) clearSelection();
    collectSelectionInside(inBox);
    setStatus("Sélection rectangulaire (" + std::to_string(selectionCount()) + " élément(s))");
}

// ---------------------------------------------------------------------------
// Sélection au lasso (5.9) : tracé libre + sélection des éléments contenus
// ---------------------------------------------------------------------------
void App::toggleLasso() {
    if (lassoArmed) {
        lassoArmed = false;
        lassoPts.clear();
        if (drag_.kind == DragKind::Lasso) drag_.kind = DragKind::None;
        setStatus("Sélection au lasso désarmée");
        return;
    }
    // Le lasso monopolise le canvas : l'anneau de manipulation, l'outil calque
    // et les modes transitoires se désarment, l'outil revient à la sélection.
    ringArmed = false;
    ringAnchored = false;
    layerArmed = false;
    layerAnchored = false;
    brushArmed = false;
    measureActive = false;
    mergeMode = MergeMode::Off;
    cutChainUndo_ = false;  // une découpe en chaîne s'achève avec le mode
    cutPts.clear();
    polyPts.clear();
    triP1 = triP2 = -1;
    pipetteArmed = false;
    pipettePending_ = false;
    if (drag_.kind != DragKind::None) drag_.kind = DragKind::None;
    if (tool != Tool::Select) {
        tool = Tool::Select;
        setStatus("Lasso : l'outil revient à la sélection");
    }
    lassoArmed = true;
    setStatus("Sélection au lasso armée — clic gauche + glisser au canvas : "
              "encercler les éléments à sélectionner (Maj au relâchement : "
              "ajouter) · clic droit ou Échap : désarmer");
}

void App::applyLassoSelection() {
    if (lassoPts.size() < 3) {
        setStatus("Lasso : tracé trop court");
        return;
    }
    // Le tracé est en pixels écran : on le projette dans le monde (le polygone
    // suit la vue telle qu'elle est au relâchement).
    std::vector<Vec2> poly;
    poly.reserve(lassoPts.size());
    for (const Vec2& s : lassoPts) poly.push_back(camera.screenToWorld(s, viewportVec2()));

    // Même critère que la sélection rectangulaire : sommet par sa position,
    // segment par son milieu, triangle par son centre (plan actif uniquement).
    const ImGuiIO& io = ImGui::GetIO();
    if (!io.KeyShift) clearSelection();
    auto inside = [&](const Vec2& w) { return pointInPolygon(w, poly); };
    collectSelectionInside(inside);
    setStatus("Sélection au lasso (" + std::to_string(selectionCount()) + " élément(s))");
}

// ---------------------------------------------------------------------------
// Pipette de couleur (6.5) : prélever la couleur affichée au canvas
// ---------------------------------------------------------------------------
void App::togglePipette() {
    if (pipetteArmed) {
        pipetteArmed = false;
        pipettePending_ = false;
        setStatus("Pipette désarmée");
        return;
    }
    // Comme les autres modes canvas, la pipette désarme le reste.
    ringArmed = false;
    ringAnchored = false;
    layerArmed = false;
    layerAnchored = false;
    lassoArmed = false;
    lassoPts.clear();
    brushArmed = false;
    measureActive = false;
    mergeMode = MergeMode::Off;
    cutChainUndo_ = false;  // une découpe en chaîne s'achève avec le mode
    cutPts.clear();
    polyPts.clear();
    triP1 = triP2 = -1;
    if (drag_.kind != DragKind::None) drag_.kind = DragKind::None;
    if (tool != Tool::Select) tool = Tool::Select;
    pipetteArmed = true;
    setStatus("Pipette armée — clic gauche sur le canvas : prélever la couleur "
              "affichée (faces, image, fond…) · clic droit ou Échap : désarmer");
}

void App::collectSelectionInside(const std::function<bool(const Vec2&)>& inside) {
    if (selMode == SelMode::Vertex) {
        for (int i = 0; i < (int)scene.activePlane().vertices.size(); ++i)
            if (inside(scene.activePlane().vertices[i])) selVerts.push_back(i);
    } else if (selMode == SelMode::Edge) {
        for (const auto& e : scene.activePlane().edges()) {
            const Vec2 mid =
                (scene.activePlane().vertices[e.first] +
                 scene.activePlane().vertices[e.second]) * 0.5f;
            if (inside(mid)) selEdges.push_back(e);
        }
    } else {
        for (int fi = 0; fi < (int)scene.activePlane().faces.size(); ++fi) {
            Vec2 c;
            for (int v : scene.activePlane().faces[fi].verts)
                c = c + scene.activePlane().vertices[v];
            c = c / (float)scene.activePlane().faces[fi].verts.size();
            if (inside(c)) selFaces.push_back(fi);
        }
    }
}

bool App::pickNearestOnly(const Vec2& world) {
    if (selMode == SelMode::Vertex) {
        const int v = pickVertex(world, vertexPickTol);
        if (v < 0) return false;
        clearSelection();
        selVerts.push_back(v);
        return true;
    }
    if (selMode == SelMode::Edge) {
        const Mesh2D::Edge e = pickEdge(world, edgePickTol);
        if (e.first < 0) return false;
        clearSelection();
        selEdges.push_back(e);
        return true;
    }
    const int fi = pickFace(world);
    if (fi < 0) return false;
    clearSelection();
    selFaces.push_back(fi);
    return true;
}

void App::addEntityToSelection(const Vec2& world, const Vec2& screen) {
    if (selMode == SelMode::Vertex) {
        const int v = pickVertex(world, vertexPickTol);
        if (v < 0) return;
        if (std::find(selVerts.begin(), selVerts.end(), v) == selVerts.end())
            selVerts.push_back(v);
    } else if (selMode == SelMode::Edge) {
        const Mesh2D::Edge e = pickEdge(world, edgePickTol);
        if (e.first < 0) return;
        if (std::find(selEdges.begin(), selEdges.end(), e) == selEdges.end())
            selEdges.push_back(e);
    } else {
        // 5.13 : le clic cyclique s'applique aussi à Ctrl+clic droit — chaque
        // clic au même endroit ajoute la face suivante en dessous.
        const int fi = cyclePickFace(world, screen);
        if (fi < 0) return;
        if (std::find(selFaces.begin(), selFaces.end(), fi) == selFaces.end())
            selFaces.push_back(fi);
    }
}

// ---------------------------------------------------------------------------
// Construction de triangle (4.1)
// ---------------------------------------------------------------------------
void App::addTriangleBuild(const Vec2& world) {
    const Vec2 w = snappedPoint(world);

    // Clic près d'un segment existant : triangle complet accroché au segment.
    // Le segment illuminé au survol (hoverEdge) est celui qui sert de base ;
    // s'il est absent (état atypique), on reprend le picking au point aimanté.
    Mesh2D::Edge e = hoverEdge;
    if (e.first < 0) e = pickEdge(w, edgePickTol);
    if (e.first >= 0) {
        // L'aimantation peut poser le sommet exactement sur la ligne du
        // segment (grille grossière) : le triangle serait dégénéré (aire
        // nulle) — on refuse alors l'accroche et on bascule en pose libre.
        const Vec2 pa = scene.activePlane().vertices[e.first];
        const Vec2 pb = scene.activePlane().vertices[e.second];
        if (std::fabs(cross(pb - pa, w - pa)) < 1e-4f) e = {-1, -1};
    }
    if (e.first >= 0) {
        pushUndo();
        const int nv = scene.activePlane().addVertex(w);
        scene.activePlane().addFace({e.first, e.second, nv});
        triP1 = triP2 = -1;
        setStatus("Triangle créé à partir d'un segment");
        logMsg("Triangle créé à partir d'un segment");
        return;
    }

    pushUndo();
    const int nv = scene.activePlane().addVertex(w);
    if (triP1 < 0) {
        triP1 = nv;
        setStatus("1er sommet posé — clic pour le 2e sommet (le 3e clic ferme le triangle)");
    } else if (triP2 < 0) {
        triP2 = nv;
        setStatus("2e sommet posé — le 3e clic ferme le triangle");
    } else {
        scene.activePlane().addFace({triP1, triP2, nv});
        triP1 = triP2 = -1;
        setStatus("Triangle fermé");
        logMsg("Triangle créé");
    }
}

// ---------------------------------------------------------------------------
// Formes prédéfinies (4.2)
// ---------------------------------------------------------------------------
void App::startShapeTool(Tool t) {
    tool = t;
    cutChainUndo_ = false;  // une découpe en chaîne s'achève avec la forme
    cutPts.clear();   // une forme remplace la découpe en cours
    polyPts.clear();
    drag_.kind = DragKind::None;
    drag_.shapeStage = 0;
    const bool threeClicks = t == Tool::Star || t == Tool::Ring || t == Tool::Crown;
    setStatus("Forme « " + std::string(toolName(t)) + " » armée — 1er clic : ancre, puis "
              "déplacez la souris, " + std::string(threeClicks ? "3e" : "2e") +
              " clic : valider");
    logMsg("Forme « " + std::string(toolName(t)) + " » armée");
}

void App::toggleCutTool() {
    cutChainUndo_ = false;  // chaque armement démarre une nouvelle étape annulable
    if (tool == Tool::Cut) {
        tool = Tool::Select;
        cutPts.clear();
        setStatus("Outil découpe désarmé");
    } else {
        if (isShapeTool(tool)) cancelShapeTrace();  // la forme en cours cède la place
        tool = Tool::Cut;
        cutPts.clear();
        setStatus("Découpe armée — clics gauches : sommets du polygone · clic droit ou "
                  "Entrée : découper · l'outil reste armé après chaque découpe (une "
                  "étape annulable par chaîne) · Retour arrière : dernier point · "
                  "Échap : annuler (D)");
        logMsg("Outil découpe armé");
    }
}

void App::applyCut() {
    if (tool != Tool::Cut) return;
    if (cutPts.size() < 3) {
        setStatus("Découpe : il faut au moins 3 points");
        return;
    }
    // Calcule le résultat sur une copie : on ne pousse un undo que si la
    // découpe touche réellement des faces.
    Mesh2D copy = scene.activePlane();
    if (!copy.cutPolygon(cutPts)) {
        setStatus("La découpe ne touche aucune face du plan actif — tracez une "
                  "autre découpe ou Échap / D pour terminer");
        cutPts.clear();
        return;  // l'outil reste armé : on peut retracer dans la même chaîne
    }
    // Enchaînement : la 1re découpe de la salve pousse l'historique ; tant que
    // l'outil reste armé, les suivantes partagent la même étape annulable.
    if (!cutChainUndo_) {
        pushUndo();
        cutChainUndo_ = true;
    }
    scene.activePlane() = std::move(copy);
    const int nv = (int)scene.activePlane().vertices.size();
    // Le résultat d'une découpe est entièrement triangulé : on affiche le
    // nombre de TRIANGLES (les faces non touchées y comptent aussi, via leur
    // propre triangulation — même vocabulaire que le HUD / le kiosque).
    const int nt = scene.activePlane().triangleCount();
    cutPts.clear();
    const std::string msg = "Découpe appliquée — " + std::to_string(nv) + " sommets, " +
                            std::to_string(nt) +
                            " triangles — l'outil reste armé : tracez une autre découpe · "
                            "Échap ou D : terminer (une seule étape annulable)";
    setStatus(msg);
    logMsg(msg);
}

void App::removeLastCutPoint() {
    if (tool != Tool::Cut || cutPts.empty()) return;
    cutPts.pop_back();
    setStatus(cutPts.empty()
                  ? "Découpe : plus aucun point — Échap désarme l'outil"
                  : "Découpe : " + std::to_string(cutPts.size()) +
                        " point(s) — Retour arrière : retirer le dernier");
}

// --- Outil Polygone : tracer un polygone libre et le trianguler ---
void App::togglePolygonTool() {
    cutChainUndo_ = false;  // une découpe en chaîne s'achève avec le changement d'outil
    if (tool == Tool::Polygon) {
        tool = Tool::Select;
        polyPts.clear();
        setStatus("Outil polygone désarmé");
    } else {
        if (isShapeTool(tool)) cancelShapeTrace();
        tool = Tool::Polygon;
        polyPts.clear();
        setStatus("Polygone armé — clics gauches : sommets du polygone · clic droit ou "
                  "Entrée : valider et trianguler · Retour arrière : dernier point · "
                  "Échap : annuler (U)");
        logMsg("Outil polygone armé");
    }
}

void App::applyPolygon() {
    if (tool != Tool::Polygon) return;
    if (polyPts.size() < 3) {
        setStatus("Polygone : il faut au moins 3 points");
        return;
    }
    pushUndo();
    Mesh2D& m = scene.activePlane();
    // Ajoute les sommets, puis triangule le polygone via addTriangulatedFace.
    std::vector<int> verts;
    verts.reserve(polyPts.size());
    for (const Vec2& p : polyPts) verts.push_back(m.addVertex(p));
    if (m.addTriangulatedFace(verts) <= 0) {
        undoStack.pop_back();
        setStatus("Polygone dégénéré — impossible de trianguler");
        polyPts.clear();
        tool = Tool::Select;
        return;
    }
    const int nv = (int)m.vertices.size();
    const int nf = (int)m.faces.size();
    polyPts.clear();
    tool = Tool::Select;
    const std::string msg = "Polygone triangulé — " + std::to_string(nv) + " sommets, " +
                            std::to_string(nf) + " faces";
    setStatus(msg);
    logMsg(msg);
    dirty = true;
}

void App::removeLastPolygonPoint() {
    if (tool != Tool::Polygon || polyPts.empty()) return;
    polyPts.pop_back();
    setStatus(polyPts.empty()
                  ? "Polygone : plus aucun point — Échap désarme l'outil"
                  : "Polygone : " + std::to_string(polyPts.size()) +
                        " point(s) — Retour arrière : retirer le dernier");
}

void App::advanceShapeClick(const Vec2& world) {
    if (drag_.shapeStage == 1) {
        drag_.shapeCur = snappedPoint(world);
        if (tool == Tool::Star || tool == Tool::Ring || tool == Tool::Crown) {
            const Vec2 d = drag_.shapeCur - drag_.shapeAnchor;
            drag_.shapeRadius = length(d);
            drag_.shapeAngle = std::atan2(d.y, d.x);
            drag_.shapeInner = 0.5f;
            drag_.shapeStage = 2;
            if (tool == Tool::Star)
                setStatus("Étoile : 2e clic verrouille rayon et orientation — "
                          "déplacez pour la profondeur, 3e clic valide");
            else if (tool == Tool::Crown)
                setStatus("Couronne : 2e clic verrouille — distance : taille du trou, "
                          "angle du curseur : orientation intérieure · 3e clic valide "
                          "(molette : intérieurs, Maj+molette : extérieurs)");
            else
                setStatus("Anneau : 2e clic verrouille — déplacez pour la taille du trou, "
                          "3e clic valide (molette : côtés)");
        } else {
            completeShape();
            drag_.kind = DragKind::None;
            drag_.shapeStage = 0;
            if (tool == Tool::Circle) tool = Tool::Select;  // le cercle se désarme après création
        }
    } else if (drag_.shapeStage == 2) {
        drag_.shapeCur = snappedPoint(world);
        completeShape();
        drag_.kind = DragKind::None;
        drag_.shapeStage = 0;
        if (tool == Tool::Circle) tool = Tool::Select;
    }
}

void App::cancelShapeTrace() {
    if (drag_.kind == DragKind::Shape) {
        drag_.kind = DragKind::None;
        drag_.shapeStage = 0;
        setStatus("Tracé annulé (Échap quitte le mode)");
    }
}

void App::completeShape() {
    const Vec2& a = drag_.shapeAnchor;
    const Vec2& c = drag_.shapeCur;
    const Vec2 d = c - a;
    // Anneau, couronne et étoile : le 2e clic VERROUILLE le rayon et
    // l'orientation ; en phase 3, le curseur ne règle plus que la taille du trou
    // (anneau/couronne) ou la profondeur (étoile) — le rayon créé doit rester
    // celui verrouillé.
    const bool locked = drag_.shapeStage >= 2 &&
                        (tool == Tool::Ring || tool == Tool::Crown || tool == Tool::Star);
    const float rad = locked ? drag_.shapeRadius : length(d);
    const float ang = locked ? drag_.shapeAngle : std::atan2(d.y, d.x);

    if (rad < 1e-4f) {
        setStatus("Forme dégénérée, ignorée");
        return;
    }
    pushUndo();
    switch (tool) {
        case Tool::Rectangle: {
            if (std::fabs(c.x - a.x) < 1e-4f || std::fabs(c.y - a.y) < 1e-4f) {
                undoStack.pop_back();
                setStatus("Rectangle dégénéré, ignoré");
                return;
            }
            const Vec2 p0{std::min(a.x, c.x), std::min(a.y, c.y)};
            const Vec2 p1{std::max(a.x, c.x), std::max(a.y, c.y)};
            addQuad(p0, p1);
            break;
        }
        case Tool::Square: {
            const float s = std::max(std::fabs(d.x), std::fabs(d.y));
            if (s < 1e-4f) {
                undoStack.pop_back();
                setStatus("Carré dégénéré, ignoré");
                return;
            }
            const Vec2 p0{a.x, a.y};
            const Vec2 p1{a.x + std::copysign(s, d.x), a.y + std::copysign(s, d.y)};
            addQuad(p0, p1);
            break;
        }
        case Tool::Circle: addFan(a, rad, ang, circleSides); break;
        case Tool::Triangle: addRimPolygon(a, rad, ang, 3); break;
        case Tool::Pentagon: addFan(a, rad, ang, 5); break;
        case Tool::Hexagon: addFan(a, rad, ang, 6); break;
        case Tool::Star: addStar(a, rad, ang, drag_.shapeInner, circleSides); break;
        case Tool::Ring: addRing(a, rad, ang, drag_.shapeInner, circleSides); break;
        case Tool::Crown: {
            // La forme intérieure s'oriente comme la forme extérieure l'a été au
            // 2e clic : l'angle du curseur autour de l'ancre règle sa rotation
            // (la distance du curseur règle la taille du trou).
            const float innerAng = std::atan2(d.y, d.x);
            addCrown(a, rad, ang, drag_.shapeInner, circleSides, crownInnerSides,
                     innerAng);
            break;
        }
        default: break;
    }
    setStatus("Forme « " + std::string(toolName(tool)) + " » créée");
    logMsg("Forme « " + std::string(toolName(tool)) + " » créée");
}

void App::addFan(const Vec2& center, float radius, float angle, int sides) {
    const int ci = scene.activePlane().addVertex(center);
    std::vector<int> rim;
    rim.reserve(sides);
    for (int i = 0; i < sides; ++i) {
        const float a = angle + (float)i * 2.0f * kPi / (float)sides;
        rim.push_back(scene.activePlane().addVertex({center.x + std::cos(a) * radius,
                                      center.y + std::sin(a) * radius}));
    }
    for (int i = 0; i < sides; ++i)
        scene.activePlane().addFace({ci, rim[i], rim[(i + 1) % sides]});
}

void App::addRimPolygon(const Vec2& center, float radius, float angle, int sides) {
    std::vector<int> rim;
    rim.reserve(sides);
    for (int i = 0; i < sides; ++i) {
        const float a = angle + (float)i * 2.0f * kPi / (float)sides;
        rim.push_back(scene.activePlane().addVertex({center.x + std::cos(a) * radius,
                                      center.y + std::sin(a) * radius}));
    }
    scene.activePlane().addTriangulatedFace(rim);
}

void App::addQuad(const Vec2& p0, const Vec2& p1) {
    const int i0 = scene.activePlane().addVertex(p0);
    const int i1 = scene.activePlane().addVertex({p1.x, p0.y});
    const int i2 = scene.activePlane().addVertex(p1);
    const int i3 = scene.activePlane().addVertex({p0.x, p1.y});
    scene.activePlane().addTriangulatedFace({i0, i1, i2, i3});
}

void App::addStar(const Vec2& center, float radius, float angle, float depth, int points) {
    const int ci = scene.activePlane().addVertex(center);
    const int n = points * 2;
    std::vector<int> ring;
    ring.reserve(n);
    for (int i = 0; i < n; ++i) {
        const float a = angle + (float)i * kPi / (float)points;
        const float r = (i % 2 == 0) ? radius : radius * depth;
        ring.push_back(scene.activePlane().addVertex({center.x + std::cos(a) * r,
                                       center.y + std::sin(a) * r}));
    }
    for (int i = 0; i < n; ++i)
        scene.activePlane().addFace({ci, ring[i], ring[(i + 1) % n]});
}

void App::addRing(const Vec2& center, float radius, float angle, float hole, int sides) {
    std::vector<int> outer, inner;
    outer.reserve(sides);
    inner.reserve(sides);
    for (int i = 0; i < sides; ++i) {
        const float a = angle + (float)i * 2.0f * kPi / (float)sides;
        outer.push_back(scene.activePlane().addVertex({center.x + std::cos(a) * radius,
                                        center.y + std::sin(a) * radius}));
        inner.push_back(scene.activePlane().addVertex({center.x + std::cos(a) * hole * radius,
                                        center.y + std::sin(a) * hole * radius}));
    }
    for (int i = 0; i < sides; ++i) {
        const int j = (i + 1) % sides;
        scene.activePlane().addFace({outer[i], outer[j], inner[j]});
        scene.activePlane().addFace({outer[i], inner[j], inner[i]});
    }
}

// --- Couronne (4.2) : anneau dont les côtés intérieurs et extérieurs sont
// indépendants. La bande entre les deux polygones réguliers (n et m sommets)
// est triangulée par « zipper » sans croisement ni chevauchement (voir
// triangulateBand dans triangulate.cpp) ; chaque triangle est émis comme un
// sextuplet (boucle, index) : 0 = extérieur, 1 = intérieur.
void App::addCrown(const Vec2& center, float radius, float angle, float hole,
                   int outerSides, int innerSides, float innerAngle) {
    // innerAngle : angle de départ du polygone INTÉRIEUR, indépendant de celui
    // de la forme extérieure (orientée au 2e clic) — le curseur règle cette
    // rotation pendant la phase 2 du tracé.
    outerSides = std::max(outerSides, 3);
    innerSides = std::max(innerSides, 3);
    std::vector<Vec2> oPts, iPts;
    std::vector<int> outer, inner;
    oPts.reserve(outerSides);
    iPts.reserve(innerSides);
    outer.reserve(outerSides);
    inner.reserve(innerSides);
    for (int i = 0; i < outerSides; ++i) {
        const float a = angle + (float)i * 2.0f * kPi / (float)outerSides;
        const Vec2 p{center.x + std::cos(a) * radius, center.y + std::sin(a) * radius};
        oPts.push_back(p);
        outer.push_back(scene.activePlane().addVertex(p));
    }
    for (int i = 0; i < innerSides; ++i) {
        const float a = innerAngle + (float)i * 2.0f * kPi / (float)innerSides;
        const Vec2 p{center.x + std::cos(a) * hole * radius,
                     center.y + std::sin(a) * hole * radius};
        iPts.push_back(p);
        inner.push_back(scene.activePlane().addVertex(p));
    }
    std::vector<int> band;
    triangulateBand(oPts, iPts, band);
    auto get = [&](int ring, int idx) -> int {
        return ring == 0 ? outer[idx] : inner[idx];
    };
    for (size_t k = 0; k + 5 < band.size(); k += 6)
        scene.activePlane().addFace({get(band[k], band[k + 1]), get(band[k + 2], band[k + 3]),
                                     get(band[k + 4], band[k + 5])});
}

// ---------------------------------------------------------------------------
// Alignement, répartition, rotation (5.3 / 5.4)
// ---------------------------------------------------------------------------
void App::alignX() {
    if (selVerts.size() < 2) return;
    pushUndo();
    const float x = scene.activePlane().vertices[selVerts[0]].x;
    for (size_t i = 1; i < selVerts.size(); ++i) scene.activePlane().vertices[selVerts[i]].x = x;
    setStatus("Aligner X (" + std::to_string(selVerts.size()) + " points)");
}

void App::alignY() {
    if (selVerts.size() < 2) return;
    pushUndo();
    const float y = scene.activePlane().vertices[selVerts[0]].y;
    for (size_t i = 1; i < selVerts.size(); ++i) scene.activePlane().vertices[selVerts[i]].y = y;
    setStatus("Aligner Y (" + std::to_string(selVerts.size()) + " points)");
}

void App::distributeX() {
    if (selVerts.size() < 3) return;
    pushUndo();
    std::vector<int> sorted = selVerts;
    std::sort(sorted.begin(), sorted.end(),
              [&](int a, int b) { return scene.activePlane().vertices[a].x < scene.activePlane().vertices[b].x; });
    const float x0 = scene.activePlane().vertices[sorted.front()].x;
    const float x1 = scene.activePlane().vertices[sorted.back()].x;
    const float step = (x1 - x0) / (float)(sorted.size() - 1);
    for (size_t i = 1; i + 1 < sorted.size(); ++i)
        scene.activePlane().vertices[sorted[i]].x = x0 + step * (float)i;
    setStatus("Répartir X (" + std::to_string(sorted.size()) + " points)");
}

void App::distributeY() {
    if (selVerts.size() < 3) return;
    pushUndo();
    std::vector<int> sorted = selVerts;
    std::sort(sorted.begin(), sorted.end(),
              [&](int a, int b) { return scene.activePlane().vertices[a].y < scene.activePlane().vertices[b].y; });
    const float y0 = scene.activePlane().vertices[sorted.front()].y;
    const float y1 = scene.activePlane().vertices[sorted.back()].y;
    const float step = (y1 - y0) / (float)(sorted.size() - 1);
    for (size_t i = 1; i + 1 < sorted.size(); ++i)
        scene.activePlane().vertices[sorted[i]].y = y0 + step * (float)i;
    setStatus("Répartir Y (" + std::to_string(sorted.size()) + " points)");
}

// Miroir de la sélection autour du premier point sélectionné (l'ancre), comme
// pour Aligner X/Y : le point d'ancre reste en place, les autres se reflètent.
void App::mirrorSelectionX() {
    toVertexSelection();
    if (selVerts.size() < 2) {
        setStatus("Miroir X : sélectionnez au moins 2 sommets (M)");
        return;
    }
    pushUndo();
    const float ax = scene.activePlane().vertices[selVerts[0]].x;
    for (size_t i = 1; i < selVerts.size(); ++i) {
        Vec2& p = scene.activePlane().vertices[selVerts[i]];
        p.x = 2.0f * ax - p.x;
    }
    setStatus("Miroir X (" + std::to_string(selVerts.size()) + " points)");
    logMsg("Miroir X : " + std::to_string(selVerts.size()) + " points");
}

void App::mirrorSelectionY() {
    toVertexSelection();
    if (selVerts.size() < 2) {
        setStatus("Miroir Y : sélectionnez au moins 2 sommets (Maj+M)");
        return;
    }
    pushUndo();
    const float ay = scene.activePlane().vertices[selVerts[0]].y;
    for (size_t i = 1; i < selVerts.size(); ++i) {
        Vec2& p = scene.activePlane().vertices[selVerts[i]];
        p.y = 2.0f * ay - p.y;
    }
    setStatus("Miroir Y (" + std::to_string(selVerts.size()) + " points)");
    logMsg("Miroir Y : " + std::to_string(selVerts.size()) + " points");
}

// Mise à l'échelle de la sélection : facteur appliqué depuis le centre de la
// sélection (comme la rotation précise).
void App::scaleSelectionExact(float factor) {
    const std::vector<int> verts = selectionVertices();
    if (verts.size() < 2) {
        setStatus("Mise à l'échelle : sélectionnez au moins 2 sommets");
        return;
    }
    if (factor <= 0.0f || std::fabs(factor - 1.0f) < 1e-4f) {
        setStatus("Mise à l'échelle : facteur invalide (positif et différent de 1)");
        return;
    }
    pushUndo();
    Vec2 c{0.0f, 0.0f};
    for (int v : verts) c = c + scene.activePlane().vertices[v];
    c = c / (float)verts.size();
    for (int v : verts) {
        Vec2& p = scene.activePlane().vertices[v];
        p = c + (p - c) * factor;
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "Sélection mise à l'échelle ×%.2f (pivot : centre)", factor);
    setStatus(buf);
    std::snprintf(buf, sizeof(buf), "Mise à l'échelle ×%.2f", factor);
    logMsg(buf);
}

void App::rotateSelectionAround(const Vec2& pivot, float deg) {
    rotDeg += deg;  // angle cumulé affiché au HUD (rot X°)
    const float rad = deg * kPi / 180.0f;
    const float cs = std::cos(rad);
    const float sn = std::sin(rad);
    for (int v : selectionVertices()) {
        const Vec2 p = scene.activePlane().vertices[v] - pivot;
        scene.activePlane().vertices[v] = {pivot.x + p.x * cs - p.y * sn, pivot.y + p.x * sn + p.y * cs};
    }
    setStatus("Rotation de " + std::to_string((int)deg) + "° autour du curseur");
}

void App::rotateSelectionExact(float deg) {
    const std::vector<int> verts = selectionVertices();
    if (verts.size() < 2) {
        setStatus("Rotation précise : sélectionnez au moins 2 sommets");
        return;
    }
    pushUndo();
    Vec2 c{0.0f, 0.0f};
    for (int v : verts) c = c + scene.activePlane().vertices[v];
    c = c / (float)verts.size();
    rotateSelectionAround(c, deg);
    char buf[64];
    std::snprintf(buf, sizeof(buf),
                  "Rotation précise de %.1f° autour du centre de la sélection", deg);
    setStatus(buf);
    std::snprintf(buf, sizeof(buf), "Rotation précise de %.1f°", deg);
    logMsg(buf);
}

void App::nudgeSelection(float dx, float dy) {
    const std::vector<int> verts = selectionVertices();
    if (verts.empty()) {
        setStatus("Déplacement au clavier : sélectionnez d'abord des éléments");
        return;
    }
    // Une salve de flèches (~350 ms entre deux presses) constitue une seule
    // étape annulable : on ne pousse l'historique qu'à la première flèche.
    const unsigned int now = SDL_GetTicks();
    if ((int)(now - nudgeTimeMs_) > 350) pushUndo();
    nudgeTimeMs_ = now;
    const size_t n = scene.activePlane().vertices.size();
    for (int v : verts) {
        if (v < 0 || (size_t)v >= n) continue;
        scene.activePlane().vertices[v].x += dx;
        scene.activePlane().vertices[v].y += dy;
    }
    dirty = true;
    const char* dir = dx != 0.0f ? (dx > 0.0f ? "droite" : "gauche")
                                 : (dy > 0.0f ? "haut" : "bas");
    setStatus(std::to_string(verts.size()) + " sommet(s) déplacé(s) — " +
              std::string(dir));
}

void App::rotateAllPlanesAround(const Vec2& pivot, float deg) {
    rotDeg += deg;  // angle cumulé affiché au HUD (rot X°), remis à zéro par Ctrl+0
    const float rad = deg * kPi / 180.0f;
    const float cs = std::cos(rad);
    const float sn = std::sin(rad);
    for (Mesh2D& p : scene.planes) {
        for (Vec2& v : p.vertices) {
            const Vec2 d = v - pivot;
            v = {pivot.x + d.x * cs - d.y * sn, pivot.y + d.x * sn + d.y * cs};
        }
    }
    setStatus("Rotation de tous les plans de " + std::to_string((int)deg) + "° autour du curseur (AltGr+molette)");
}

// ---------------------------------------------------------------------------
// Anneau de manipulation unifié des maillages (sélection / plan / scène) :
// même principe que le calque (7.7) — une seule poignée par action. La cible
// « scène » remplace l'ancien mode Scène (8.5) : une seule façon de manipuler
// chaque chose (l'AltGr+molette de rotation globale reste un geste de vue).
// ---------------------------------------------------------------------------
void App::toggleRingMode() {
    if (ringArmed) {
        if (isMeshRingDragging()) endMeshRingDrag();
        ringArmed = false;
        ringAnchored = false;
        setStatus("Manipulation désarmée");
        return;
    }
    // L'anneau monopolise le canvas : l'outil calque, le lasso, la pipette et
    // les modes transitoires se désarment, l'outil revient à la sélection.
    if (layerArmed) toggleLayerMode();
    if (lassoArmed) toggleLasso();
    if (pipetteArmed) togglePipette();
    if (drag_.kind == DragKind::Shape) cancelShapeTrace();
    else if (drag_.kind != DragKind::None) drag_.kind = DragKind::None;
    brushArmed = false;
    measureActive = false;
    mergeMode = MergeMode::Off;
    cutChainUndo_ = false;  // une découpe en chaîne s'achève avec le mode
    cutPts.clear();
    polyPts.clear();
    triP1 = triP2 = -1;
    if (tool != Tool::Select) {
        tool = Tool::Select;
        setStatus("Manipulation : les outils d'édition sont désarmés");
    }
    ringArmed = true;
    recenterRing();  // l'anneau s'arme au centre de la cible, pas sous le curseur
    setStatus("Manipulation armée — anneau au centre de la cible « " +
              std::string(ringTargetName()) +
              " » : déplacement (centre/flèches), rotation (anneau), échelle "
              "(carreaux/losange), symétries (pastilles rouges, clic) · clic "
              "ailleurs pour déplacer l'anneau · clic droit ou Échap : désarmer");
}

void App::setRingTarget(RingTarget t) {
    if (t == RingTarget::None) {  // re-clic sur la cible active : désarmer
        if (ringArmed) toggleRingMode();
        return;
    }
    ringTarget = t;
    recenterRing();  // l'anneau se place au centre de la nouvelle cible
    if (ringArmed)
        setStatus("Manipulation : cible « " + std::string(ringTargetName()) + " »");
    else
        toggleRingMode();  // arme l'anneau pour la cible choisie
}

const char* App::ringTargetName() const {
    switch (ringTarget) {
        case RingTarget::Plane: return "plan courant";
        case RingTarget::Scene: return "scène";
        default:                return "sélection";
    }
}

bool App::ringTargetCenter(Vec2& out) const {
    // Centre de la boîte englobante de la cible — même convention que le
    // cadrage / la rotation précise (« centre de la sélection »).
    bool first = true;
    Vec2 minv{0.0f, 0.0f}, maxv{0.0f, 0.0f};
    auto add = [&](const Vec2& p) {
        if (first) {
            minv = maxv = p;
            first = false;
        } else {
            minv.x = std::min(minv.x, p.x);
            minv.y = std::min(minv.y, p.y);
            maxv.x = std::max(maxv.x, p.x);
            maxv.y = std::max(maxv.y, p.y);
        }
    };
    if (ringTarget == RingTarget::Selection) {
        const std::vector<int> verts = selectionVertices();
        const Mesh2D& m = scene.activePlane();
        for (int v : verts)
            if (v >= 0 && (size_t)v < m.vertices.size()) add(m.vertices[v]);
    } else if (ringTarget == RingTarget::Plane) {
        for (const Vec2& p : scene.activePlane().vertices) add(p);
    } else {
        for (const Mesh2D& p : scene.planes)
            for (const Vec2& v : p.vertices) add(v);
    }
    if (first) return false;
    out = (minv + maxv) * 0.5f;
    return true;
}

void App::recenterRing() {
    Vec2 c;
    if (ringTargetCenter(c)) {
        ringAnchor = c;
        ringAnchored = true;
    } else {
        // Cible vide (ex. sélection vide) : l'anneau suit le curseur jusqu'au
        // premier clic gauche (qui l'ancre), comme auparavant.
        ringAnchored = false;
    }
}

bool App::snapshotMeshRingTarget() {
    // Toujours TOUS les plans (indexé par plan) : la restauration et la
    // détection de changement restent uniformes quel que soit le plan actif.
    drag_.allPlaneStarts.clear();
    drag_.movingVerts.clear();
    for (const Mesh2D& p : scene.planes) drag_.allPlaneStarts.push_back(p.vertices);
    if (ringTarget == RingTarget::Selection) {
        drag_.movingVerts = selectionVertices();  // sommets touchés par le ring
        return !drag_.movingVerts.empty();
    }
    if (ringTarget == RingTarget::Plane)
        return !scene.activePlane().vertices.empty();
    size_t n = 0;  // Scène : au moins un sommet dans la scène
    for (const auto& v : drag_.allPlaneStarts) n += v.size();
    return n > 0;
}

void App::beginMeshRingDrag(const Vec2& world, const Vec2& screen) {
    if (!ringArmed) return;
    // Cible vide (anneau non ancré : il suivait le curseur) : premier clic
    // gauche — l'anneau s'ancre ici.
    if (!ringAnchored) {
        ringAnchor = world;
        ringAnchored = true;
    }
    // Détecter la poignée survolée (une seule par action).
    std::vector<LayerHandleKind> kinds;
    std::vector<Vec2> wpos;
    ringHandlePositions(ringAnchor, kinds, wpos);
    const Vec2 vps = viewportVec2();
    float best = 16.0f;
    int bestIdx = -1;
    for (int i = 0; i < (int)wpos.size(); ++i) {
        const float d = distance(camera.worldToScreen(wpos[i], vps), screen);
        if (d < best) { best = d; bestIdx = i; }
    }
    if (bestIdx >= 0) {
        const LayerHandleKind hk = kinds[bestIdx];
        // Symétries : actions instantanées — le clic applique (pas de glisser).
        if (hk == LayerHandleKind::MirrorX || hk == LayerHandleKind::MirrorY ||
            hk == LayerHandleKind::MirrorBoth) {
            applyMeshRingMirror(hk);
            return;
        }
        if (!snapshotMeshRingTarget()) {
            setStatus("Manipulation : rien à manipuler — " +
                      std::string(ringTargetName()) + " vide");
            return;
        }
        pushUndo();
        drag_.kind = DragKind::MeshRing;
        drag_.layerHandleKind = hk;
        drag_.sceneAnchor = world;
        drag_.sceneStartScreen = screen;
        drag_.ringStartAnchor = ringAnchor;  // l'anneau suit la cible pendant le déplacement
        setStatus("Manipulation : glissez pour modifier");
        return;
    }
    // Aucune poignée : la bande annulaire de l'anneau de rotation (40-68 px)
    // pivote la cible autour du point d'ancrage ; ailleurs, re-ancrer.
    const Vec2 cs = camera.worldToScreen(ringAnchor, vps);
    const float dc = distance(screen, cs);
    if (dc > 40.0f && dc < 68.0f) {
        if (!snapshotMeshRingTarget()) {
            setStatus("Manipulation : rien à manipuler — " +
                      std::string(ringTargetName()) + " vide");
            return;
        }
        pushUndo();
        drag_.kind = DragKind::MeshRing;
        drag_.layerHandleKind = LayerHandleKind::Rotate;
        drag_.sceneAnchor = world;
        drag_.sceneStartScreen = screen;
        setStatus("Manipulation : pivotez (glissez autour de l'anneau)");
        return;
    }
    ringAnchor = world;
    setStatus("Manipulation : anneau ré-ancré — utilisez les poignées ou "
              "re-cliquez ailleurs");
}

void App::applyMeshRingDrag(const Vec2& world) {
    if (drag_.kind != DragKind::MeshRing) return;
    // On restaure d'abord les positions de départ, puis on applique la
    // transformation courante : l'état reste déterministe à chaque frame
    // (aucune dérive d'arrondi, relâcher puis re-saisir repart de zéro).
    for (size_t i = 0; i < scene.planes.size() && i < drag_.allPlaneStarts.size(); ++i) {
        const std::vector<Vec2>& start = drag_.allPlaneStarts[i];
        Mesh2D& p = scene.planes[i];
        const size_t n = std::min(p.vertices.size(), start.size());
        for (size_t j = 0; j < n; ++j) p.vertices[j] = start[j];
    }
    const Vec2 pivot = ringAnchor;
    switch (drag_.layerHandleKind) {
        case LayerHandleKind::MoveFree: {
            Vec2 delta = world - drag_.sceneAnchor;
            if (snapOn) delta = snapDelta(delta);
            eachRingVertex([&](Vec2& v) { v = v + delta; });
            // L'anneau suit la cible : les poignées restent autour d'elle
            // (le pivot des rotations / échelles suit le déplacement).
            ringAnchor = drag_.ringStartAnchor + delta;
            break;
        }
        case LayerHandleKind::MoveX: {
            const float dx = world.x - drag_.sceneAnchor.x;
            eachRingVertex([&](Vec2& v) { v.x += dx; });
            ringAnchor = drag_.ringStartAnchor + Vec2{dx, 0.0f};
            break;
        }
        case LayerHandleKind::MoveY: {
            const float dy = world.y - drag_.sceneAnchor.y;
            eachRingVertex([&](Vec2& v) { v.y += dy; });
            ringAnchor = drag_.ringStartAnchor + Vec2{0.0f, dy};
            break;
        }
        case LayerHandleKind::Rotate: {
            const Vec2 d0 = drag_.sceneAnchor - pivot;
            const Vec2 d1 = world - pivot;
            const float a0 = std::atan2(d0.y, d0.x);
            const float a1 = std::atan2(d1.y, d1.x);
            const float rad = a1 - a0;
            const float cs = std::cos(rad);
            const float sn = std::sin(rad);
            eachRingVertex([&](Vec2& v) {
                const Vec2 d = v - pivot;
                v = {pivot.x + d.x * cs - d.y * sn, pivot.y + d.x * sn + d.y * cs};
            });
            break;
        }
        case LayerHandleKind::ScaleX: {
            // Rétrécissement GÉNÉREUX piloté par la distance du curseur au
            // CENTRE de l'anneau (le seuil de rétrécissement est au centre,
            // pas au point de saisie) : en glissant vers l'intérieur, le module
            // décroît proportionnellement jusqu'à quasi zéro au centre ; vers
            // l'extérieur, la réponse exponentielle en pixels (indépendante du
            // zoom). Pas de saut à la saisie : f = 1 au point de saisie.
            const float dg = drag_.sceneAnchor.x - pivot.x;  // signée, au point de saisie
            const float dw = world.x - pivot.x;              // signée, au curseur
            float fx;
            if (dw * dg <= 0.0f)
                fx = 1e-4f;  // au centre ou de l'autre côté : module quasi nul
            else {
                const float r = dw / dg;  // 1 au point de saisie, → 0 vers le centre
                fx = r >= 1.0f ? std::exp((dw - dg) * camera.zoom * kRingScalePerPx)
                               : std::max(r, 1e-4f);
            }
            eachRingVertex([&](Vec2& v) { v.x = pivot.x + (v.x - pivot.x) * fx; });
            break;
        }
        case LayerHandleKind::ScaleY: {
            const float dg = drag_.sceneAnchor.y - pivot.y;
            const float dw = world.y - pivot.y;
            float fy;
            if (dw * dg <= 0.0f)
                fy = 1e-4f;
            else {
                const float r = dw / dg;
                fy = r >= 1.0f ? std::exp((dw - dg) * camera.zoom * kRingScalePerPx)
                               : std::max(r, 1e-4f);
            }
            eachRingVertex([&](Vec2& v) { v.y = pivot.y + (v.y - pivot.y) * fy; });
            break;
        }
        case LayerHandleKind::ScaleBoth: {
            // Distance RADIALE SIGNÉE le long de la direction d'ouverture du
            // point de saisie : positive vers l'extérieur (agrandir), négative
            // vers l'intérieur (rétrécir) — la distance seule est toujours ≥ 0
            // et ne permettrait que d'agrandir (même convention que les
            // échelles X et Y : glisser vers le centre réduit).
            const Vec2 dir0 = drag_.sceneAnchor - pivot;
            const float r0 = std::max(length(dir0), 1e-4f);
            const Vec2 dir = dir0 / r0;
            const float sd = dot(world - drag_.sceneAnchor, dir);
            const float f = std::exp(sd * camera.zoom * kRingScalePerPx);
            eachRingVertex([&](Vec2& v) {
                const Vec2 d = v - pivot;
                v = {pivot.x + d.x * f, pivot.y + d.y * f};
            });
            break;
        }
        default: break;
    }
}

void App::endMeshRingDrag() {
    if (drag_.kind != DragKind::MeshRing) return;
    const LayerHandleKind k = drag_.layerHandleKind;
    bool changed = false;
    for (size_t i = 0; i < scene.planes.size() && i < drag_.allPlaneStarts.size(); ++i) {
        const std::vector<Vec2>& start = drag_.allPlaneStarts[i];
        const Mesh2D& p = scene.planes[i];
        const size_t n = std::min(p.vertices.size(), start.size());
        for (size_t j = 0; j < n; ++j) {
            if (distance(p.vertices[j], start[j]) > 1e-5f) {
                changed = true;
                break;
            }
        }
        if (changed) break;
    }
    drag_.kind = DragKind::None;
    drag_.layerHandleKind = LayerHandleKind::None;
    if (!changed) {
        if (!undoStack.empty()) undoStack.pop_back();  // clic sans glisser : pas d'étape
        setStatus("Manipulation : aucun changement");
        return;
    }
    const char* what =
        (k == LayerHandleKind::MoveFree || k == LayerHandleKind::MoveX ||
         k == LayerHandleKind::MoveY)   ? "Déplacé"
        : (k == LayerHandleKind::Rotate) ? "Pivoté"
                                         : "Redimensionné";
    setStatus(std::string(what) + " (« " + ringTargetName() + " »)");
    logMsg(std::string(what) + " : " + ringTargetName());
}

void App::applyMeshRingMirror(LayerHandleKind kind) {
    if (!snapshotMeshRingTarget()) {
        setStatus("Manipulation : rien à manipuler — " +
                  std::string(ringTargetName()) + " vide");
        return;
    }
    // Aucune transformation en cours : les positions courantes sont déjà les
    // positions de départ — la symétrie s'applique directement.
    pushUndo();
    const Vec2 pivot = ringAnchor;
    switch (kind) {
        case LayerHandleKind::MirrorX:
            eachRingVertex([&](Vec2& v) { v.x = 2.0f * pivot.x - v.x; });
            setStatus("Manipulation : miroir X (symétrie horizontale)");
            logMsg("Miroir X : " + std::string(ringTargetName()));
            break;
        case LayerHandleKind::MirrorY:
            eachRingVertex([&](Vec2& v) { v.y = 2.0f * pivot.y - v.y; });
            setStatus("Manipulation : miroir Y (symétrie verticale)");
            logMsg("Miroir Y : " + std::string(ringTargetName()));
            break;
        case LayerHandleKind::MirrorBoth:
            eachRingVertex([&](Vec2& v) {
                const float nx = 2.0f * pivot.x - v.x;
                const float ny = 2.0f * pivot.y - v.y;
                v = {nx, ny};
            });
            setStatus("Manipulation : miroir X et Y (symétrie centrale)");
            logMsg("Miroir X/Y : " + std::string(ringTargetName()));
            break;
        default: return;
    }
}

// ---------------------------------------------------------------------------
// Calque d'image de fond (7.7) : déplacer / pivoter / redimensionner l'image
// ---------------------------------------------------------------------------
void App::toggleLayerMode() {
    if (layerArmed) {
        if (isLayerDragging()) endLayerDrag();
        layerArmed = false;
        layerAnchored = false;
        layerHover = LayerHandleKind::None;
        setStatus("Manipulation du calque désarmée");
        return;
    }
    if (scene.image.path.empty()) {
        setStatus("Aucun calque : chargez d'abord une image (bouton Calque)");
        return;
    }
    if (isMeshRingDragging()) endMeshRingDrag();
    else if (ringArmed) {
        ringArmed = false;
        ringAnchored = false;
    }
    else if (drag_.kind == DragKind::Shape) cancelShapeTrace();
    else if (drag_.kind != DragKind::None) drag_.kind = DragKind::None;
    brushArmed = false;
    measureActive = false;
    mergeMode = MergeMode::Off;
    cutChainUndo_ = false;  // une découpe en chaîne s'achève avec le mode
    cutPts.clear();
    polyPts.clear();
    triP1 = triP2 = -1;
    lassoArmed = false;
    lassoPts.clear();
    pipetteArmed = false;
    pipettePending_ = false;
    if (tool != Tool::Select) {
        tool = Tool::Select;
        setStatus("Calque : les outils d'édition sont désarmés");
    }
    layerArmed = true;
    layerAnchored = false;
    layerHover = LayerHandleKind::None;
    setStatus("Calque : déplacez le curseur, cliquez pour ancrer l'anneau — "
              "une poignée par action : déplacement (centre/flèches), "
              "rotation (anneau), échelle (carreaux/losange), symétries "
              "(pastilles rouges, clic) · clic droit ou Échap : désarmer");
}

bool App::isLayerDragging() const {
    return drag_.kind == DragKind::LayerHandle;
}

void App::beginLayerDrag(const Vec2& world, const Vec2& screen) {
    if (!layerArmed) return;
    // Si l'anneau n'est pas encore ancré, le clic l'ancre à la position monde
    // (l'anneau suivait le curseur, qui se trouve donc sur la poignée centrale
    // « déplacement libre » au moment du clic).
    if (!layerAnchored) {
        layerAnchor = world;
        layerAnchored = true;
    }
    // Détecter la poignée survolée (une seule par action).
    std::vector<LayerHandleKind> kinds;
    std::vector<Vec2> wpos;
    ringHandlePositions(layerAnchor, kinds, wpos);
    const Vec2 vps = viewportVec2();
    float best = 16.0f;
    int bestIdx = -1;
    for (int i = 0; i < (int)wpos.size(); ++i) {
        const float d = distance(camera.worldToScreen(wpos[i], vps), screen);
        if (d < best) { best = d; bestIdx = i; }
    }
    if (bestIdx >= 0) {
        const LayerHandleKind hk = kinds[bestIdx];
        // Symétries : actions instantanées — le clic applique (pas de glisser).
        if (hk == LayerHandleKind::MirrorX || hk == LayerHandleKind::MirrorY ||
            hk == LayerHandleKind::MirrorBoth) {
            applyLayerSymmetry(hk);
            return;
        }
        // Poignée d'action : démarrer le drag.
        pushUndo();
        drag_.layerStartCenter = scene.image.center;
        drag_.layerStartRot = scene.image.rotation;
        drag_.layerStartSx = scene.image.scaleX;
        drag_.layerStartSy = scene.image.scaleY;
        drag_.kind = DragKind::LayerHandle;
        drag_.layerHandleKind = hk;
        drag_.sceneAnchor = world;
        drag_.sceneStartScreen = screen;
        setStatus("Calque : glissez pour modifier");
        return;
    }
    // Aucune poignée : la bande annulaire de l'anneau de rotation (40-68 px)
    // pivote le calque autour de son centre ; ailleurs, le clic ré-ancre.
    const Vec2 cs = camera.worldToScreen(layerAnchor, vps);
    const float dc = distance(screen, cs);
    if (dc > 40.0f && dc < 68.0f) {
        pushUndo();
        drag_.layerStartCenter = scene.image.center;
        drag_.layerStartRot = scene.image.rotation;
        drag_.layerStartSx = scene.image.scaleX;
        drag_.layerStartSy = scene.image.scaleY;
        drag_.kind = DragKind::LayerHandle;
        drag_.layerHandleKind = LayerHandleKind::Rotate;
        drag_.sceneAnchor = world;
        drag_.sceneStartScreen = screen;
        setStatus("Calque : pivotez (glissez autour de l'anneau)");
        return;
    }
    // Clic dans le vide : ré-ancrer l'anneau ici.
    layerAnchor = world;
    setStatus("Calque : anneau ré-ancré — utilisez les poignées ou "
              "re-cliquez ailleurs");
}

void App::applyLayerDrag(const Vec2& world, const Vec2& /*screen*/) {
    if (drag_.kind != DragKind::LayerHandle) return;
    ImageLayer& il = scene.image;
    switch (drag_.layerHandleKind) {
        case LayerHandleKind::MoveFree: {
            const Vec2 delta = world - drag_.sceneAnchor;
            // L'anneau suit le calque : l'ancre garde son décalage constant
            // par rapport au centre (le pivot des rotations / échelles suit).
            const Vec2 off = layerAnchor - il.center;
            il.center = drag_.layerStartCenter + delta;
            layerAnchor = il.center + off;
            break;
        }
        case LayerHandleKind::MoveX: {
            const Vec2 delta = world - drag_.sceneAnchor;
            const Vec2 off = layerAnchor - il.center;
            il.center = drag_.layerStartCenter + Vec2{delta.x, 0.0f};
            layerAnchor = il.center + off;
            break;
        }
        case LayerHandleKind::MoveY: {
            const Vec2 delta = world - drag_.sceneAnchor;
            const Vec2 off = layerAnchor - il.center;
            il.center = drag_.layerStartCenter + Vec2{0.0f, delta.y};
            layerAnchor = il.center + off;
            break;
        }
        case LayerHandleKind::Rotate: {
            const Vec2 d0 = drag_.sceneAnchor - drag_.layerStartCenter;
            const Vec2 d1 = world - il.center;
            float a0 = std::atan2(d0.y, d0.x);
            float a1 = std::atan2(d1.y, d1.x);
            il.rotation = drag_.layerStartRot + (a1 - a0);
            break;
        }
        case LayerHandleKind::ScaleX: {
            // Échelle horizontale depuis l'ancre de l'anneau. Le SIGNE de
            // l'échelle (symétrie éventuelle) est préservé : seul le module
            // varie — après un miroir, glisser la poignée agrandit ou réduit
            // le calque sans le retourner (actions non inversées).
            const float cs = std::cos(il.rotation);
            const float sn = std::sin(il.rotation);
            const Vec2 u{cs, sn};
            // Pixels glissés (décalage monde × zoom) : réponse identique quel
            // que soit le zoom (même convention que l'anneau des maillages).
            const float dx = dot(world - drag_.sceneAnchor, u) * camera.zoom;
            const float hw0 = std::fabs(il.w * drag_.layerStartSx) * 0.5f;
            const float sign = drag_.layerStartSx >= 0.0f ? 1.0f : -1.0f;
            const float newHw = std::max(hw0 + dx * 0.5f, 1e-4f);
            il.scaleX = sign * std::clamp(2.0f * newHw / (float)il.w, 1e-4f, 1e5f);
            break;
        }
        case LayerHandleKind::ScaleY: {
            const float cs = std::cos(il.rotation);
            const float sn = std::sin(il.rotation);
            const Vec2 v{-sn, cs};
            // Pixels glissés (× zoom) : réponse identique quel que soit le zoom.
            const float dy = dot(world - drag_.sceneAnchor, v) * camera.zoom;
            const float hh0 = std::fabs(il.h * drag_.layerStartSy) * 0.5f;
            const float sign = drag_.layerStartSy >= 0.0f ? 1.0f : -1.0f;
            const float newHh = std::max(hh0 + dy * 0.5f, 1e-4f);
            il.scaleY = sign * std::clamp(2.0f * newHh / (float)il.h, 1e-4f, 1e5f);
            break;
        }
        case LayerHandleKind::ScaleBoth: {
            const float cs = std::cos(il.rotation);
            const float sn = std::sin(il.rotation);
            const Vec2 u{cs, sn}, v{-sn, cs};
            // Pixels glissés (× zoom) : réponse identique quel que soit le zoom.
            const float dx = dot(world - drag_.sceneAnchor, u) * camera.zoom;
            const float dy = dot(world - drag_.sceneAnchor, v) * camera.zoom;
            // Modules des demi-largeurs/hauteurs + signes préservés (miroirs).
            const float hw0 = std::fabs(il.w * drag_.layerStartSx) * 0.5f;
            const float hh0 = std::fabs(il.h * drag_.layerStartSy) * 0.5f;
            const float sxSign = drag_.layerStartSx >= 0.0f ? 1.0f : -1.0f;
            const float sySign = drag_.layerStartSy >= 0.0f ? 1.0f : -1.0f;
            const float k = std::max(1.0f + (dx + dy) * 0.25f / std::max(hw0 + hh0, 1e-4f), 0.01f);
            const float newHw = std::max(hw0 * k, 1e-4f);
            const float newHh = std::max(hh0 * k, 1e-4f);
            il.scaleX = sxSign * std::clamp(2.0f * newHw / (float)il.w, 1e-4f, 1e5f);
            il.scaleY = sySign * std::clamp(2.0f * newHh / (float)il.h, 1e-4f, 1e5f);
            break;
        }
        default: break;
    }
}

void App::endLayerDrag() {
    if (drag_.kind != DragKind::LayerHandle) return;
    const LayerHandleKind k = drag_.layerHandleKind;
    const ImageLayer& il = scene.image;
    const bool changed = distance(il.center, drag_.layerStartCenter) > 1e-5f ||
                         std::fabs(il.rotation - drag_.layerStartRot) > 1e-5f ||
                         std::fabs(il.scaleX - drag_.layerStartSx) > 1e-5f ||
                         std::fabs(il.scaleY - drag_.layerStartSy) > 1e-5f;
    drag_.kind = DragKind::None;
    drag_.layerHandleKind = LayerHandleKind::None;
    if (!changed) {
        if (!undoStack.empty()) undoStack.pop_back();
        setStatus("Calque : aucun changement");
        return;
    }
    const char* what =
        (k == LayerHandleKind::MoveFree || k == LayerHandleKind::MoveX ||
         k == LayerHandleKind::MoveY)   ? "Calque déplacé"
        : (k == LayerHandleKind::Rotate) ? "Calque pivoté"
        : (k == LayerHandleKind::ScaleX || k == LayerHandleKind::ScaleY ||
           k == LayerHandleKind::ScaleBoth) ? "Calque redimensionné"
        : "Calque modifié";
    setStatus(std::string(what) + " (calque d'image)");
    logMsg(std::string(what));
}

bool App::loadImageLayer(const std::string& path) {
    if (path.empty()) return false;
    int w = 0, h = 0, ch = 0;
    unsigned char* px = stbi_load(path.c_str(), &w, &h, &ch, 4);  // force RGBA
    if (!px) {
        setStatus("Calque : impossible de charger « " + path + " »");
        logMsg("Calque : échec du chargement de « " + path + " »");
        return false;
    }
    if (w <= 0 || h <= 0 || w > 16384 || h > 16384) {
        stbi_image_free(px);
        setStatus("Calque : dimensions d'image invalides");
        return false;
    }
    // stb lit les lignes de haut en bas : on les renverse pour que l'image
    // soit à l'endroit en coordonnées monde (Y vers le haut, UV bas = ligne 0).
    std::vector<unsigned char> flipped((size_t)w * (size_t)h * 4u);
    for (int y = 0; y < h; ++y) {
        std::memcpy(flipped.data() + (size_t)(h - 1 - y) * (size_t)w * 4u,
                    px + (size_t)y * (size_t)w * 4u, (size_t)w * 4u);
    }
    stbi_image_free(px);
    const unsigned tex = renderer.createTexture(w, h, flipped.data());
    if (!tex) {
        setStatus("Calque : échec de la création de la texture");
        return false;
    }
    if (imageTex) renderer.destroyTexture(imageTex);
    imageTex = tex;
    scene.image.path = path;
    scene.image.w = w;
    scene.image.h = h;
    // Taille par défaut : l'image occupe ~la moitié de la vue (contenue).
    const float vwWorld = viewportSize.x / camera.zoom;
    const float vhWorld = viewportSize.y / camera.zoom;
    const float s = std::min(vwWorld * 0.5f / (float)w, vhWorld * 0.5f / (float)h);
    scene.image.scaleX = scene.image.scaleY = std::max(s, 1e-4f);
    scene.image.center = {camera.cx, camera.cy};
    scene.image.rotation = 0.0f;
    scene.image.opacity = 1.0f;
    scene.image.visible = true;
    dirty = true;
    setStatus("Calque chargé : " + path + " (" + std::to_string(w) + "×" +
              std::to_string(h) + ")");
    logMsg("Calque d'image chargé : " + path);
    return true;
}

void App::removeImageLayer() {
    if (scene.image.path.empty()) return;
    pushUndo();
    scene.image = ImageLayer{};
    dirty = true;
    layerArmed = false;
    layerAnchored = false;
    // La texture est détruite par la synchronisation de drawScene.
    setStatus("Calque d'image retiré");
    logMsg("Calque d'image retiré");
}

void App::applyLayerSymmetry(LayerHandleKind kind) {
    if (scene.image.path.empty()) return;
    pushUndo();
    switch (kind) {
        case LayerHandleKind::MirrorX:
            scene.image.scaleX = -scene.image.scaleX;
            setStatus("Calque : symétrie horizontale (miroir X)");
            logMsg("Calque : symétrie horizontale");
            break;
        case LayerHandleKind::MirrorY:
            scene.image.scaleY = -scene.image.scaleY;
            setStatus("Calque : symétrie verticale (miroir Y)");
            logMsg("Calque : symétrie verticale");
            break;
        case LayerHandleKind::MirrorBoth:
            scene.image.scaleX = -scene.image.scaleX;
            scene.image.scaleY = -scene.image.scaleY;
            setStatus("Calque : symétrie centrale (miroir X/Y)");
            logMsg("Calque : symétrie centrale");
            break;
        default: break;
    }
    dirty = true;
}

void App::fitLayerToView() {
    if (scene.image.path.empty() || scene.image.w <= 0 || scene.image.h <= 0) return;
    pushUndo();
    const float vwWorld = viewportSize.x / camera.zoom;
    const float vhWorld = viewportSize.y / camera.zoom;
    const float s = std::min(vwWorld * 0.5f / (float)scene.image.w,
                             vhWorld * 0.5f / (float)scene.image.h);
    scene.image.scaleX = scene.image.scaleY = std::max(s, 1e-4f);
    scene.image.center = {camera.cx, camera.cy};
    dirty = true;
    setStatus("Calque ajusté à la vue");
}

void App::ringHandlePositions(const Vec2& c, std::vector<LayerHandleKind>& kinds,
                              std::vector<Vec2>& worldPos) const {
    kinds.clear();
    worldPos.clear();
    // Une SEULE poignée par action, sur le cercle de 40 px écran (converti en
    // monde) : échelle X (E), échelle uniforme (NE), échelle Y (N), miroir Y
    // (NW), déplacement X (W), miroir X/Y (SW), déplacement Y (S), miroir X
    // (SE). Le centre (déplacement libre) est une poignée ; la rotation est la
    // bande annulaire de l'anneau lui-même, gérée par begin*Drag.
    const float r = 40.0f / std::max(camera.zoom, 1e-3f);
    struct Slot { float deg; LayerHandleKind kind; };
    static const Slot slots[] = {
        {0.0f,   LayerHandleKind::ScaleX},
        {45.0f,  LayerHandleKind::ScaleBoth},
        {90.0f,  LayerHandleKind::ScaleY},
        {135.0f, LayerHandleKind::MirrorY},
        {180.0f, LayerHandleKind::MoveX},
        {225.0f, LayerHandleKind::MirrorBoth},
        {270.0f, LayerHandleKind::MoveY},
        {315.0f, LayerHandleKind::MirrorX},
    };
    kinds.push_back(LayerHandleKind::MoveFree);  worldPos.push_back(c);
    for (const Slot& s : slots) {
        const float rad = s.deg * kPi / 180.0f;
        kinds.push_back(s.kind);
        worldPos.push_back({c.x + std::cos(rad) * r, c.y + std::sin(rad) * r});
    }
}


// ---------------------------------------------------------------------------
// Presse-papiers interne (5.8)
// ---------------------------------------------------------------------------
void App::copySelection() {
    const std::vector<int> verts = selectionVertices();
    if (verts.empty()) return;
    clip.verts.clear();
    clip.faces.clear();
    std::vector<int> map(scene.activePlane().vertices.size(), -1);
    for (int i = 0; i < (int)verts.size(); ++i) {
        map[verts[i]] = i;
        clip.verts.push_back(scene.activePlane().vertices[verts[i]]);
    }
    for (const Face& f : scene.activePlane().faces) {
        bool all = true;
        for (int v : f.verts)
            if (map[v] < 0) {
                all = false;
                break;
            }
        if (all) {
            Face nf;
            nf.color = f.color;
            nf.hasColor = f.hasColor;
            for (int v : f.verts) nf.verts.push_back(map[v]);
            clip.faces.push_back(nf);
        }
    }
    hasClip = true;
    clipOffset = {0, 0};
    setStatus(std::to_string(verts.size()) + " sommet(s), " +
              std::to_string(clip.faces.size()) + " face(s) copié(s)");
    logMsg("Copié : " + std::to_string(verts.size()) + " sommet(s)");
}

void App::cutSelection() {
    copySelection();
    deleteSelection();
}

void App::duplicateSelection() {
    if (selectionCount() == 0) {
        setStatus("Dupliquer : sélection vide");
        return;
    }
    // Copie puis colle : le collage décale d'un demi-pas de grille et rend la
    // copie sélectionnée — prête à être déplacée (Ctrl+D pour redoubler). On
    // restaure l'offset cumulé pour que les Ctrl+V suivants restent prévisibles.
    const Vec2 savedOffset = clipOffset;
    copySelection();
    clipOffset = savedOffset;
    pasteClipboard();
    clipOffset = savedOffset;
    setStatus("Sélection dupliquée et décalée — glissez pour la déplacer · "
              "Ctrl+D pour redoubler");
    logMsg("Sélection dupliquée");
}

void App::pasteClipboard() {
    if (!hasClip) return;
    pushUndo();
    clipOffset.x += gridStep * 0.5f;
    clipOffset.y += gridStep * 0.5f;
    std::vector<int> map(clip.verts.size());
    selVerts.clear();
    selEdges.clear();
    selFaces.clear();
    selMode = SelMode::Vertex;
    for (size_t i = 0; i < clip.verts.size(); ++i) {
        map[i] = scene.activePlane().addVertex(clip.verts[i] + clipOffset);
        selVerts.push_back(map[i]);
    }
    for (const Face& f : clip.faces) {
        Face nf;
        nf.color = f.color;
        nf.hasColor = f.hasColor;
        for (int v : f.verts) nf.verts.push_back(map[v]);
        scene.activePlane().faces.push_back(nf);
    }
    setStatus("Collé (" + std::to_string(clip.faces.size()) + " face(s)) — Ctrl+V pour recoller");
    logMsg("Collé (" + std::to_string(clip.faces.size()) + " face(s))");
}

// ---------------------------------------------------------------------------
// Peinture (6)
// ---------------------------------------------------------------------------
void App::setBrushColor(const Color& c) {
    brushColor = c;
    brushArmed = true;
    setStatus("Pinceau armé — clic gauche : peindre les triangles sélectionnés, "
              "ou le triangle survolé si rien n'est sélectionné");
}

void App::paintFace(int fi) {
    Face& f = scene.activePlane().faces[fi];
    f.color = brushColor;
    f.color.a = brushOpacity;
    f.hasColor = true;
    setStatus("Triangle peint");
    logMsg("Triangle peint");
}

void App::paintFaces(const std::vector<int>& faces) {
    Mesh2D& m = scene.activePlane();
    int painted = 0;
    for (int fi : faces) {
        if (fi < 0 || (size_t)fi >= m.faces.size()) continue;
        Face& f = m.faces[fi];
        f.color = brushColor;
        f.color.a = brushOpacity;
        f.hasColor = true;
        ++painted;
    }
    const std::string msg =
        std::to_string(painted) + (painted > 1 ? " triangles peints" : " triangle peint");
    setStatus(msg);
    logMsg(msg);
}

// Ordre z des faces (devant / derrière) : les faces sélectionnées avancent
// (]) ou reculent ([) d'un cran dans l'ordre de dessin du plan. Le vecteur
// des faces EST l'ordre z : dessinées dans l'ordre (les dernières recouvrent)
// et choisies de la dernière à la première (le dessus d'abord).
void App::faceForward() {
    if (selMode != SelMode::Face || selFaces.empty()) {
        setStatus("Ordre z : sélectionnez d'abord des triangles (cible « triangle »)");
        return;
    }
    pushUndo();
    std::vector<int> before = selFaces;
    std::sort(before.begin(), before.end());
    selFaces = scene.activePlane().shiftFaces(selFaces, +1);
    if (selFaces == before) {  // déjà au premier plan : pas d'étape d'annulation vide
        if (!undoStack.empty()) undoStack.pop_back();
        setStatus("Faces déjà au premier plan (])");
        return;
    }
    setStatus("Faces mises à l'avant (]) — ordre z du plan");
    logMsg("Faces mises à l'avant (ordre z)");
}

void App::faceBackward() {
    if (selMode != SelMode::Face || selFaces.empty()) {
        setStatus("Ordre z : sélectionnez d'abord des triangles (cible « triangle »)");
        return;
    }
    pushUndo();
    std::vector<int> before = selFaces;
    std::sort(before.begin(), before.end());
    selFaces = scene.activePlane().shiftFaces(selFaces, -1);
    if (selFaces == before) {  // déjà au dernier plan : pas d'étape d'annulation vide
        if (!undoStack.empty()) undoStack.pop_back();
        setStatus("Faces déjà au dernier plan ([)");
        return;
    }
    setStatus("Faces mises à l'arrière ([) — ordre z du plan");
    logMsg("Faces mises à l'arrière (ordre z)");
}

// ---------------------------------------------------------------------------
// Cibles, réticule, prévisualisation
// ---------------------------------------------------------------------------
void App::cycleTarget() {
    selMode = (SelMode)(((int)selMode + 1) % 3);
    lastFaceClick_ = -1;  // les faces changent de sens selon la cible
    lastFaceClickScreen_ = {-1e9f, -1e9f};
    clearSelection();
    setStatus(selMode == SelMode::Vertex   ? "Cible : sommet"
              : selMode == SelMode::Edge   ? "Cible : segment"
                                           : "Cible : triangle");
}

void App::cycleReticle() {
    reticle = (ReticleState)(((int)reticle + 1) % 4);
    setStatus(reticle == ReticleState::Off         ? "Réticule désactivé"
              : reticle == ReticleState::Simple    ? "Réticule simple"
              : reticle == ReticleState::Symmetric ? "Réticule symétrique (croix pleine grandeur)"
                                                   : "Réticule miroir (reflets du curseur à "
                                                     "travers les axes du monde)");
}

void App::cyclePreview() {
    preview = (PreviewMode)(((int)preview + 1) % 3);
    if (preview == PreviewMode::Off) {
        setStatus("Retour à l'édition");
    } else {
        logMsg(preview == PreviewMode::Simple ? "Prévisualisation — aperçu simple"
                                              : "Prévisualisation — tous les plans");
        setStatus("Prévisualisation — Échap, clic gauche ou Ctrl+S pour sortir");
    }
}

// Cycle le mode d'affichage des plans (7.6) : normal (seul le plan actif est
// rempli) → toutes couleurs (tous les plans remplis) → filaire (arêtes seules,
// aucun remplissage) → normal.
void App::cycleFillMode() {
    if (!allColors && !wireframe) {
        allColors = true;
        setStatus("Toutes couleurs : remplir tous les plans pendant l'édition (7.6)");
    } else if (allColors) {
        allColors = false;
        wireframe = true;
        setStatus("Mode filaire : arêtes seules, sans remplissage (7.6)");
    } else {
        wireframe = false;
        setStatus("Rendu normal : seul le plan actif est rempli (7.6)");
    }
}

void App::toggleMeasure() {
    measureActive = !measureActive;
    if (measureActive) {
        measureHasA = measureHasB = false;
        setStatus("Outil mesure armé — clic gauche : 1er point, 2e clic : distance "
                  "(Ctrl+M pour désarmer)");
        logMsg("Outil mesure armé");
    } else {
        setStatus("Outil mesure désarmé");
        logMsg("Outil mesure désarmé");
    }
}

float App::activePlaneArea() const {
    const Mesh2D& m = scene.activePlane();
    float area = 0.0f;
    for (const Face& f : m.faces) {
        if ((int)f.verts.size() < 3) continue;
        float s = 0.0f;
        for (size_t i = 0; i < f.verts.size(); ++i) {
            const Vec2& a = m.vertices[f.verts[i]];
            const Vec2& b = m.vertices[f.verts[(i + 1) % f.verts.size()]];
            s += a.x * b.y - b.x * a.y;
        }
        area += std::fabs(s) * 0.5f;
    }
    return area;
}

void App::exitPreview() {
    if (preview != PreviewMode::Off) {
        preview = PreviewMode::Off;
        setStatus("Retour à l'édition");
    }
}

// ---------------------------------------------------------------------------
// Gestion des plans (7)
// ---------------------------------------------------------------------------
void App::setActivePlane(int i) {
    if (scene.planes.empty()) scene.planes.emplace_back();
    if (i < 0 || i >= scene.count()) return;
    if (scene.active == i) return;
    scene.active = i;
    lastFaceClick_ = -1;  // la pile de faces appartient au plan actif
    lastFaceClickScreen_ = {-1e9f, -1e9f};
    clearSelection();
    triP1 = triP2 = -1;
    const std::string label =
        scene.planes[i].name.empty()
            ? std::string()
            : " « " + scene.planes[i].name + " »";
    setStatus("Plan " + std::to_string(i + 1) + " / " +
              std::to_string(scene.count()) + label);
}

void App::nextPlane() {
    if (scene.count() < 2) return;
    setActivePlane((scene.active + 1) % scene.count());
}

void App::prevPlane() {
    if (scene.count() < 2) return;
    setActivePlane((scene.active - 1 + scene.count()) % scene.count());
}

void App::planeUp() {
    if (scene.active >= scene.count() - 1) return;
    pushUndo();
    std::swap(scene.planes[scene.active], scene.planes[scene.active + 1]);
    ++scene.active;
    clearSelection();
    setStatus("Plan monté d'un rang (indice " + std::to_string(scene.active + 1) + ")");
    logMsg("Plan monté d'un rang dans l'empilement");
}

void App::planeDown() {
    if (scene.active <= 0) return;
    pushUndo();
    std::swap(scene.planes[scene.active], scene.planes[scene.active - 1]);
    --scene.active;
    clearSelection();
    setStatus("Plan descendu d'un rang (indice " + std::to_string(scene.active + 1) + ")");
    logMsg("Plan descendu d'un rang dans l'empilement");
}

void App::addPlane(bool after) {
    pushUndo();
    const int at = std::clamp(scene.active + (after ? 1 : 0), 0, scene.count());
    scene.planes.insert(scene.planes.begin() + at, Mesh2D());
    scene.active = at;
    clearSelection();
    triP1 = triP2 = -1;
    setStatus("Plan vide ajouté " + std::string(after ? "après" : "avant") +
              " (n° " + std::to_string(at + 1) + "/" + std::to_string(scene.count()) + ")");
    logMsg("Plan vide ajouté " + std::string(after ? "après" : "avant") + " le plan courant");
}

void App::duplicatePlane() {
    if (scene.planes.empty()) return;
    pushUndo();
    const int at = scene.active + 1;
    scene.planes.insert(scene.planes.begin() + at, scene.planes[scene.active]);
    scene.planes[at].name.clear();  // la copie reprend le nom par défaut « Plan n »
    scene.active = at;
    clearSelection();
    triP1 = triP2 = -1;
    setStatus("Plan dupliqué (n° " + std::to_string(at + 1) + "/" +
              std::to_string(scene.count()) + ") — Ctrl+Z pour annuler");
    logMsg("Plan dupliqué (n° " + std::to_string(at + 1) + ")");
}

void App::renameActivePlane(const std::string& name) {
    if (scene.planes.empty()) return;
    // Nom vide = repli sur le nom par défaut « Plan n » (spec 2.2).
    std::string clean = name;
    // Pas de retour à la ligne ni de séparateur : le nom est affiché tel quel
    // dans le HUD, le kiosque et les dialogues.
    while (!clean.empty() && (clean.back() == '\n' || clean.back() == '\r'))
        clean.pop_back();
    if (clean == scene.planes[scene.active].name) return;  // inchangé
    pushUndo();
    scene.planes[scene.active].name = clean;
    dlgRenameOpen = false;
    setStatus("Plan renommé : « " +
              (clean.empty()
                   ? "Plan " + std::to_string(scene.active + 1)
                   : clean) +
              " »");
    logMsg("Plan n° " + std::to_string(scene.active + 1) + " renommé");
}

void App::deletePlane() {
    if (scene.count() <= 1) return;
    pushUndo();
    scene.planes.erase(scene.planes.begin() + scene.active);
    if (scene.active >= scene.count()) scene.active = scene.count() - 1;
    clearSelection();
    triP1 = triP2 = -1;
    clearBoolSets();  // 5.12 : les indices de faces ont changé
    dlgDeletePlaneOpen = false;
    setStatus("Plan supprimé (" + std::to_string(scene.count()) + " plan(s) restant(s))");
    logMsg("Plan supprimé — Ctrl+Z pour annuler");
}

void App::toggleKiosk() {
    if (scene.count() < 2) {
        setStatus("Kiosque : au moins 2 plans requis");
        return;
    }
    kiosk = !kiosk;
    if (kiosk) {
        const ImGuiIO& io = ImGui::GetIO();
        kioskX = io.MousePos.x - viewportPos.x;
        kioskFresh = true;
        // Les modes qui monopolisent le canvas se désarment en entrant au kiosque.
        lassoArmed = false;
        lassoPts.clear();
        pipetteArmed = false;
        pipettePending_ = false;
        setStatus("Kiosque — déplacez la souris ou utilisez ←/→ : le plan en avant "
                  "est pré-sélectionné ; clic gauche : choisir ; Échap ou clic droit : sortir");
    } else {
        setStatus("Retour à l'édition");
    }
    logMsg(kiosk ? "Kiosque activé (couverture des plans)" : "Kiosque désactivé");
}

int App::kioskTarget() const {
    const int n = scene.count();
    if (n <= 1) return scene.active;
    const float u = std::clamp(kioskX / std::max(viewportSize.x, 1.0f), 0.0f, 1.0f);
    return std::clamp((int)std::lround(u * (float)(n - 1)), 0, n - 1);
}

// ---------------------------------------------------------------------------
// Commandes
// ---------------------------------------------------------------------------
void App::pushUndo() {
    undoStack.push_back(scene);
    if (undoStack.size() > kMaxUndo) undoStack.erase(undoStack.begin());
    redoStack.clear();
    dirty = true;
    // Filet de sécurité de l'enchaînement des découpes : toute nouvelle étape
    // annulable (autre action, annulation…) clôt une chaîne de découpes en
    // cours — applyCut la rouvre après son propre pushUndo (1re découpe).
    cutChainUndo_ = false;
    // Les ensembles A/B (5.12) référencent des faces par indice : toute
    // édition du maillage les invalide (indices décalés, géométrie changée).
    clearBoolSets();
}

void App::undo() {
    if (undoStack.empty()) return;
    redoStack.push_back(scene);
    scene = undoStack.back();
    undoStack.pop_back();
    if (redoStack.size() > kMaxUndo) redoStack.erase(redoStack.begin());
    cutChainUndo_ = false;  // la scène annulée n'a plus de chaîne de découpes ouverte
    clearSelection();
    triP1 = triP2 = -1;
    clearBoolSets();  // les indices de faces ont changé : les ensembles sont oubliés
    dirty = true;
    setStatus("Annulation (Ctrl+Z)");
}

void App::redo() {
    if (redoStack.empty()) return;
    undoStack.push_back(scene);
    scene = redoStack.back();
    redoStack.pop_back();
    cutChainUndo_ = false;  // la scène rétablie n'a plus de chaîne de découpes ouverte
    clearSelection();
    triP1 = triP2 = -1;
    clearBoolSets();  // les indices de faces ont changé : les ensembles sont oubliés
    dirty = true;
    setStatus("Rétablissement (Ctrl+Y)");
}

void App::deleteSelection() {
    if (selMode == SelMode::Vertex && !selVerts.empty()) {
        pushUndo();
        const int n = (int)selVerts.size();
        scene.activePlane().removeVertices(selVerts);
        selVerts.clear();
        triP1 = triP2 = -1;
        setStatus(std::to_string(n) + " sommet(s) supprimé(s)");
        logMsg(std::to_string(n) + " sommet(s) supprimé(s)");
    } else if (selMode == SelMode::Edge && !selEdges.empty()) {
        pushUndo();
        for (const auto& e : selEdges) scene.activePlane().removeFacesSharingEdge(e.first, e.second);
        selEdges.clear();
        setStatus("Segment(s) supprimé(s)");
        logMsg("Segment(s) supprimé(s)");
    } else if (selMode == SelMode::Face && !selFaces.empty()) {
        pushUndo();
        std::vector<int> sorted = selFaces;
        std::sort(sorted.begin(), sorted.end(), std::greater<int>());
        for (int fi : sorted) scene.activePlane().removeFace(fi);
        selFaces.clear();
        setStatus("Triangle(s) supprimé(s)");
        logMsg("Triangle(s) supprimé(s)");
    }
}

void App::createFaceFromSelection() {
    if (selVerts.size() < 3) {
        setStatus("Sélectionnez au moins 3 sommets (mode sommets)");
        return;
    }
    pushUndo();
    const int n = scene.activePlane().addTriangulatedFace(selVerts);
    if (n > 0) setStatus("Face créée (" + std::to_string(n) + " triangle(s))");
    else setStatus("Polygone invalide (sommets en double ?)");
}

void App::insertVertexAt(const Mesh2D::Edge& e, const Vec2& world) {
    const Vec2 a = scene.activePlane().vertices[e.first];
    const Vec2 b = scene.activePlane().vertices[e.second];
    const Vec2 ab = b - a;
    const float len2 = dot(ab, ab);
    float t = 0.5f;
    if (len2 > 1e-12f) t = std::clamp(dot(world - a, ab) / len2, 0.01f, 0.99f);
    const int nv = scene.activePlane().insertVertexOnEdge(e.first, e.second, t);
    if (nv >= 0) {
        selVerts.assign(1, nv);
        selEdges.clear();
        selMode = SelMode::Vertex;
    }
}

void App::selectAll() {
    if (selMode == SelMode::Vertex) {
        selVerts.clear();
        for (int i = 0; i < (int)scene.activePlane().vertices.size(); ++i) selVerts.push_back(i);
    } else if (selMode == SelMode::Edge) {
        selEdges = scene.activePlane().edges();
    } else {
        selFaces.clear();
        for (int i = 0; i < (int)scene.activePlane().faces.size(); ++i) selFaces.push_back(i);
    }
    setStatus("Tout sélectionner (" + std::to_string(selectionCount()) + " élément(s))");
}

void App::invertSelection() {
    if (selMode == SelMode::Vertex) {
        std::vector<int> inv;
        for (int i = 0; i < (int)scene.activePlane().vertices.size(); ++i)
            if (std::find(selVerts.begin(), selVerts.end(), i) == selVerts.end())
                inv.push_back(i);
        selVerts = std::move(inv);
    } else if (selMode == SelMode::Edge) {
        const auto all = scene.activePlane().edges();
        std::vector<Mesh2D::Edge> inv;
        for (const auto& e : all)
            if (std::find(selEdges.begin(), selEdges.end(), e) == selEdges.end())
                inv.push_back(e);
        selEdges = std::move(inv);
    } else {
        std::vector<int> inv;
        for (int i = 0; i < (int)scene.activePlane().faces.size(); ++i)
            if (std::find(selFaces.begin(), selFaces.end(), i) == selFaces.end())
                inv.push_back(i);
        selFaces = std::move(inv);
    }
    setStatus("Sélection inversée (" + std::to_string(selectionCount()) + " élément(s))");
    logMsg("Sélection inversée (" + std::to_string(selectionCount()) + " élément(s))");
}

// Sélection chaînée (5.11) : à partir de la sélection courante du plan actif,
// sélectionne TOUS les éléments qui lui sont liés par des chaînes d'adjacence —
// triangles liés par au moins un sommet partagé, segments par un sommet partagé,
// sommets par un segment. Le lien se propage de proche en proche (composantes
// connexes du graphe d'adjacence). Ctrl ou Maj enfoncés : les éléments chaînés
// s'AJOUTENT à la sélection ; sinon la sélection est remplacée.
void App::selectLinked() {
    const ImGuiIO& io = ImGui::GetIO();
    const bool add = io.KeyCtrl || io.KeyShift;
    const Mesh2D& m = scene.activePlane();
    std::vector<int> stack, comp;

    if (selMode == SelMode::Face) {
        if (selFaces.empty()) {
            setStatus("Sélection chaînée : sélectionnez d'abord un triangle "
                      "(cible triangle)");
            return;
        }
        // Sommet → faces qui le contiennent ; deux faces sont liées si elles
        // partagent au moins un sommet.
        std::vector<std::vector<int>> vfaces(m.vertices.size());
        for (int fi = 0; fi < (int)m.faces.size(); ++fi)
            for (int v : m.faces[fi].verts)
                if (v >= 0 && (size_t)v < vfaces.size()) vfaces[v].push_back(fi);
        std::vector<char> seen(m.faces.size(), 0);
        for (int s : selFaces) {
            if (s < 0 || (size_t)s >= m.faces.size() || seen[s]) continue;
            seen[s] = 1;
            stack.push_back(s);
            while (!stack.empty()) {
                const int fi = stack.back();
                stack.pop_back();
                comp.push_back(fi);
                for (int v : m.faces[fi].verts)
                    for (int fj : vfaces[v])
                        if (!seen[fj]) {
                            seen[fj] = 1;
                            stack.push_back(fj);
                        }
            }
        }
        if (add) {
            for (int fi : comp)
                if (std::find(selFaces.begin(), selFaces.end(), fi) == selFaces.end())
                    selFaces.push_back(fi);
        } else {
            selFaces = comp;
        }
        setStatus("Sélection chaînée : " + std::to_string(selFaces.size()) +
                  " triangle(s) sélectionné(s)");
        logMsg("Sélection chaînée : " + std::to_string(selFaces.size()) + " triangle(s)");
        return;
    }

    if (selMode == SelMode::Edge) {
        if (selEdges.empty()) {
            setStatus("Sélection chaînée : sélectionnez d'abord un segment "
                      "(cible segment)");
            return;
        }
        // Sommet → arêtes qui le contiennent ; deux segments sont liés s'ils
        // partagent au moins un sommet.
        const std::vector<Mesh2D::Edge> all = m.edges();
        if (all.empty()) {
            // Sélection périmée (faces supprimées depuis) : rien à chaîner.
            setStatus("Sélection chaînée : sélectionnez d'abord un segment (cible segment)");
            return;
        }
        std::vector<std::vector<int>> vedges(m.vertices.size());
        for (int ei = 0; ei < (int)all.size(); ++ei) {
            vedges[all[ei].first].push_back(ei);
            vedges[all[ei].second].push_back(ei);
        }
        std::vector<char> seen(all.size(), 0);
        std::vector<Mesh2D::Edge> ecomp;
        for (const Mesh2D::Edge& s : selEdges) {
            // Indice de l'arête sélectionnée dans la liste unique du plan.
            int si = -1;
            for (int ei = 0; ei < (int)all.size(); ++ei)
                if (all[ei] == s) {
                    si = ei;
                    break;
                }
            if (si < 0 || seen[si]) continue;
            seen[si] = 1;
            stack.push_back(si);
            while (!stack.empty()) {
                const int ei = stack.back();
                stack.pop_back();
                ecomp.push_back(all[ei]);
                const int va = all[ei].first;
                const int vb = all[ei].second;
                for (int ej : vedges[va])
                    if (!seen[ej]) {
                        seen[ej] = 1;
                        stack.push_back(ej);
                    }
                for (int ej : vedges[vb])
                    if (!seen[ej]) {
                        seen[ej] = 1;
                        stack.push_back(ej);
                    }
            }
        }
        if (add) {
            for (const auto& e : ecomp)
                if (std::find(selEdges.begin(), selEdges.end(), e) == selEdges.end())
                    selEdges.push_back(e);
        } else {
            selEdges = ecomp;
        }
        setStatus("Sélection chaînée : " + std::to_string(selEdges.size()) +
                  " segment(s) sélectionné(s)");
        logMsg("Sélection chaînée : " + std::to_string(selEdges.size()) + " segment(s)");
        return;
    }

    // Sommets : chaîne par les segments — deux sommets sont liés s'ils sont
    // reliés par une arête du maillage.
    if (selVerts.empty()) {
        setStatus("Sélection chaînée : sélectionnez d'abord un sommet");
        return;
    }
    const std::vector<Mesh2D::Edge> all = m.edges();
    std::vector<std::vector<int>> vadj(m.vertices.size());
    for (const auto& e : all) {
        vadj[e.first].push_back(e.second);
        vadj[e.second].push_back(e.first);
    }
    std::vector<char> seen(m.vertices.size(), 0);
    for (int s : selVerts) {
        if (s < 0 || (size_t)s >= m.vertices.size() || seen[s]) continue;
        seen[s] = 1;
        stack.push_back(s);
        while (!stack.empty()) {
            const int v = stack.back();
            stack.pop_back();
            comp.push_back(v);
            for (int u : vadj[v])
                if (!seen[u]) {
                    seen[u] = 1;
                    stack.push_back(u);
                }
        }
    }
    if (add) {
        for (int v : comp)
            if (std::find(selVerts.begin(), selVerts.end(), v) == selVerts.end())
                selVerts.push_back(v);
    } else {
        selVerts = comp;
    }
    setStatus("Sélection chaînée : " + std::to_string(selVerts.size()) +
              " sommet(s) sélectionné(s)");
    logMsg("Sélection chaînée : " + std::to_string(selVerts.size()) + " sommet(s)");
}

// ---------------------------------------------------------------------------
// Opérations ensemblistes (5.12) : deux ensembles de triangles mémorisés, une
// opération entre eux (union / intersection / différence / symétrique).
// ---------------------------------------------------------------------------

void App::clearBoolSets() {
    boolSetA.clear();
    boolSetB.clear();
    boolSetAPlane = boolSetBPlane = -1;
}

bool App::boolSetValid(int which) const {
    const std::vector<int>& set = which == 0 ? boolSetA : boolSetB;
    const int plane = which == 0 ? boolSetAPlane : boolSetBPlane;
    if (set.empty() || plane != scene.active) return false;
    const Mesh2D& m = scene.activePlane();
    for (int fi : set)
        if (fi < 0 || (size_t)fi >= m.faces.size()) return false;
    return true;
}

void App::memorizeBoolSet(int which) {
    if (selMode != SelMode::Face) {
        setStatus("Opérations ensemblistes : passez à la cible « triangle » puis "
                  "sélectionnez les faces d'un ensemble");
        return;
    }
    if (selFaces.empty()) {
        setStatus("Opérations ensemblistes : sélectionnez d'abord des triangles "
                  "(cible « triangle »)");
        return;
    }
    const std::string name = which == 0 ? "A" : "B";
    if (which == 0) {
        boolSetA = selFaces;
        boolSetAPlane = scene.active;
    } else {
        boolSetB = selFaces;
        boolSetBPlane = scene.active;
    }
    setStatus("Ensemble " + name + " mémorisé : " + std::to_string(selFaces.size()) +
              " triangle(s) — sélectionnez l'autre ensemble puis choisissez "
              "l'opération (union / intersection / différence / symétrique)");
    logMsg("Ensemble " + name + " mémorisé : " + std::to_string(selFaces.size()) +
           " triangle(s)");
}

void App::applyBoolOp(SetOp op) {
    if (!boolSetValid(0) || !boolSetValid(1)) {
        setStatus("Opérations ensemblistes : mémorisez d'abord les ensembles A et B "
                  "(cible triangle, même plan actif)");
        return;
    }
    Mesh2D& m = scene.activePlane();

    // Triangles des ensembles : chaque face (éventuellement polygonale) est
    // triangulée, avec pour chaque triangle la face dont il provient.
    std::vector<Vec2> triA, triB;
    std::vector<int> faceOfA, faceOfB;
    auto collect = [&](const std::vector<int>& faces, std::vector<Vec2>& tris,
                       std::vector<int>& faceOf) {
        for (int fi : faces) {
            const Face& f = m.faces[fi];
            if ((int)f.verts.size() < 3) continue;
            std::vector<Vec2> pts;
            pts.reserve(f.verts.size());
            for (int v : f.verts) pts.push_back(m.vertices[v]);
            std::vector<int> local;
            if (!triangulatePolygon(pts, local)) continue;
            for (size_t i = 0; i + 2 < local.size(); i += 3) {
                tris.push_back(pts[local[i]]);
                tris.push_back(pts[local[i + 1]]);
                tris.push_back(pts[local[i + 2]]);
                faceOf.push_back(fi);
            }
        }
    };
    collect(boolSetA, triA, faceOfA);
    collect(boolSetB, triB, faceOfB);
    if (triA.size() < 3 || triB.size() < 3) {
        setStatus("Opérations ensemblistes : un des ensembles est vide");
        return;
    }
    // Résolution PAR FRONTIÈRE : A et B sont traités comme des régions
    // polygonales (réunion de leurs triangles), le résultat est un ensemble de
    // composantes (extérieur + trous). La triangulation finale est faite UNE
    // fois ici — elle est minimale, sans coutures internes ni fragments le
    // long des diagonales internes des ensembles.
    std::vector<BoolRegion> regions;
    triangleSetBoolean(op, triA, triB, regions);
    if (regions.empty()) {
        setStatus("Opérations ensemblistes : résultat vide — les deux ensembles "
                  "n'ont pas de zone commune pour cette opération");
        return;
    }

    // Triangles monde du résultat, validés avant de toucher à la géométrie.
    std::vector<Vec2> resTris;
    for (const BoolRegion& r : regions) {
        std::vector<Vec2> pts;
        std::vector<int> tris;
        if (!triangulatePolygonHoles(r.outer, r.holes, pts, tris)) continue;
        resTris.reserve(resTris.size() + tris.size());
        for (int t : tris) resTris.push_back(pts[t]);
    }
    if (resTris.size() < 3) {
        setStatus("Opérations ensemblistes : résultat vide — les deux ensembles "
                  "n'ont pas de zone commune pour cette opération");
        return;
    }

    // Faces de la zone A∪B : remplacées par la géométrie du résultat — dans la
    // zone des deux ensembles, seule la géométrie du résultat reste (les
    // restes, ex. la partie de B hors du résultat d'une différence, sont
    // retirés du plan). Le reste du plan, qui n'appartient à aucun des deux
    // ensembles, reste intact. La zone est calculée AVANT pushUndo : celui-ci
    // oublie les ensembles mémorisés (5.12) — sans cela la zone serait vide et
    // l'opération ne remplacerait rien.
    std::vector<char> inZone(m.faces.size(), 0);
    for (int fi : boolSetA) inZone[fi] = 1;
    for (int fi : boolSetB) inZone[fi] = 1;
    // Couleur des faces sources : classification du centre de chaque triangle
    // du résultat (recherche dans A puis dans B).
    const Face* fallback = !boolSetA.empty() ? &m.faces[boolSetA[0]] : nullptr;

    pushUndo();

    std::vector<Vec2> newVerts = m.vertices;
    std::vector<Face> newFaces;
    newFaces.reserve(m.faces.size());
    const auto findOrAdd = [&](const Vec2& p) -> int {
        for (size_t i = 0; i < newVerts.size(); ++i)
            if (distance(newVerts[i], p) < 1e-4f) return (int)i;
        newVerts.push_back(p);
        return (int)newVerts.size() - 1;
    };
    auto faceAt = [&](const Vec2& p) -> const Face* {
        for (size_t i = 0; i + 2 < triA.size(); i += 3)
            if (pointInTriangle(p, triA[i], triA[i + 1], triA[i + 2]))
                return &m.faces[faceOfA[i / 3]];
        for (size_t i = 0; i + 2 < triB.size(); i += 3)
            if (pointInTriangle(p, triB[i], triB[i + 1], triB[i + 2]))
                return &m.faces[faceOfB[i / 3]];
        return nullptr;
    };

    std::vector<int> newSel;
    newSel.reserve(resTris.size() / 3);
    bool inserted = false;
    for (int fi = 0; fi < (int)m.faces.size(); ++fi) {
        if (!inZone[fi]) {
            newFaces.push_back(m.faces[fi]);
            continue;
        }
        if (inserted) continue;  // les faces de la zone sont remplacées
        inserted = true;
        // Les triangles du résultat prennent la place de la zone (couleurs
        // conservées), la sélection reste une sélection de triangles.
        for (size_t i = 0; i + 2 < resTris.size(); i += 3) {
            const Vec2& p0 = resTris[i];
            const Vec2& p1 = resTris[i + 1];
            const Vec2& p2 = resTris[i + 2];
            const Vec2 c{(p0.x + p1.x + p2.x) / 3.0f, (p0.y + p1.y + p2.y) / 3.0f};
            const Face* src = faceAt(c);
            if (!src) src = fallback;
            Face nf;
            nf.verts = {findOrAdd(p0), findOrAdd(p1), findOrAdd(p2)};
            nf.color = src ? src->color : Color{};
            nf.hasColor = src ? src->hasColor : false;
            newFaces.push_back(std::move(nf));
            newSel.push_back((int)newFaces.size() - 1);
        }
    }
    m.vertices.swap(newVerts);
    m.faces.swap(newFaces);

    // Nettoyage : les sommets des faces remplacées (zone A∪B) que le résultat
    // ne réutilise pas sont retirés du plan — aucun sommet isolé ne doit
    // subsister après l'opération. La sélection (indices de faces) reste
    // valide : aucune face n'est touchée, seuls les indices de sommets sont
    // remappés. Un triangle en cours de construction (4.1) référence d'anciens
    // indices : il est abandonné.
    {
        std::vector<char> used(m.vertices.size(), 0);
        for (const Face& f : m.faces)
            for (int v : f.verts)
                if (v >= 0 && (size_t)v < used.size()) used[v] = 1;
        std::vector<int> orphans;
        for (size_t i = 0; i < used.size(); ++i)
            if (!used[i]) orphans.push_back((int)i);
        if (!orphans.empty()) {
            m.removeVertices(orphans);
            triP1 = triP2 = -1;
        }
    }

    selMode = SelMode::Face;
    clearSelection();
    selFaces = newSel;

    const char* opName = op == SetOp::Union
                             ? "Union"
                             : op == SetOp::Intersection
                                   ? "Intersection"
                                   : op == SetOp::Difference ? "Différence"
                                                              : "Différence symétrique";
    setStatus("Opération ensembliste « " + std::string(opName) + " » : " +
              std::to_string(newSel.size()) +
              " triangle(s) — seule la géométrie du résultat est conservée");
    logMsg("Opération ensembliste « " + std::string(opName) + " » : " +
           std::to_string(newSel.size()) + " triangle(s)");
    clearBoolSets();  // les faces sources ont été remplacées : ensembles périmés
}

void App::resetScene() {
    scene.clear();
    clearSelection();
    undoStack.clear();
    redoStack.clear();
    cutChainUndo_ = false;  // l'historique est effacé : plus de chaîne de découpes
    currentFile.clear();
    sceneName.clear();
    triP1 = triP2 = -1;
    dirty = false;
    ringArmed = false;        // l'anneau de manipulation (sélection/plan/scène)
    ringAnchored = false;
    layerArmed = false;
    layerAnchored = false;      // 7.7 : le calque (retiré avec la scène) aussi
    lassoArmed = false;               // 5.9 : le lasso se désarme aussi
    lassoPts.clear();
    pipetteArmed = false;             // 6.5 : la pipette se désarme aussi
    pipettePending_ = false;
    bgColor = kBgDefault;             // fond par défaut (ardoise)
    clearBoolSets();                  // 5.12 : les ensembles A/B sont liés à la scène
    camera.reset();
    cameraFramed = false;
    rotDeg = 0.0f;
    dlgResetOpen = false;
    setStatus("Scène réinitialisée (historique effacé)");
    logMsg("Scène réinitialisée — historique d'annulation effacé");
}

void App::onEscape() {
    if (kiosk) {
        kiosk = false;
        setStatus("Kiosque : aucun changement");
        return;
    }
    // Calque d'image (7.7) : Échap annule la saisie en cours puis désarme.
    if (layerArmed || isLayerDragging()) {
        if (isLayerDragging()) {
            scene.image.center = drag_.layerStartCenter;
            scene.image.rotation = drag_.layerStartRot;
            scene.image.scaleX = drag_.layerStartSx;
            scene.image.scaleY = drag_.layerStartSy;
            if (!undoStack.empty()) undoStack.pop_back();
            drag_.kind = DragKind::None;
        }
        layerArmed = false;
        layerAnchored = false;
        setStatus("Manipulation du calque désarmée");
        return;
    }
    // Sélection au lasso (5.9) : Échap annule le tracé en cours et désarme.
    if (lassoArmed) {
        lassoPts.clear();
        if (drag_.kind == DragKind::Lasso) drag_.kind = DragKind::None;
        lassoArmed = false;
        setStatus("Sélection au lasso désarmée");
        return;
    }
    // Pipette (6.5) : Échap annule le prélèvement en attente et désarme.
    if (pipetteArmed) {
        pipetteArmed = false;
        pipettePending_ = false;
        setStatus("Pipette désarmée");
        return;
    }
    // Anneau de manipulation (sélection / plan / scène) : Échap annule la
    // saisie en cours (restauration des positions de départ) puis désarme.
    if (ringArmed || isMeshRingDragging()) {
        if (isMeshRingDragging()) {
            for (size_t i = 0; i < scene.planes.size() && i < drag_.allPlaneStarts.size(); ++i) {
                const std::vector<Vec2>& start = drag_.allPlaneStarts[i];
                const size_t n = std::min(scene.planes[i].vertices.size(), start.size());
                for (size_t j = 0; j < n; ++j) scene.planes[i].vertices[j] = start[j];
            }
            if (!undoStack.empty()) undoStack.pop_back();
            drag_.kind = DragKind::None;
        }
        ringArmed = false;
        ringAnchored = false;
        setStatus("Manipulation désarmée");
        return;
    }
    if (drag_.kind == DragKind::Move) {
        const size_t n = scene.activePlane().vertices.size();
        for (size_t i = 0; i < drag_.movingVerts.size(); ++i) {
            const int v = drag_.movingVerts[i];
            if (v >= 0 && (size_t)v < n) scene.activePlane().vertices[v] = drag_.startPositions[i];
        }
        if (!undoStack.empty()) undoStack.pop_back();
        drag_.kind = DragKind::None;
        setStatus("Déplacement annulé");
        return;
    }
    if (drag_.kind == DragKind::MoveAll) {
        for (size_t i = 0; i < scene.planes.size() && i < drag_.allPlaneStarts.size(); ++i) {
            const std::vector<Vec2>& start = drag_.allPlaneStarts[i];
            const size_t n = std::min(scene.planes[i].vertices.size(), start.size());
            for (size_t j = 0; j < n; ++j) scene.planes[i].vertices[j] = start[j];
        }
        if (!undoStack.empty()) undoStack.pop_back();
        drag_.kind = DragKind::None;
        setStatus("Déplacement de tous les plans annulé");
        return;
    }
    if (drag_.kind == DragKind::Shape) {
        drag_.kind = DragKind::None;
        drag_.shapeStage = 0;
        setStatus("Forme désarmée");
        return;
    }
    if (tool == Tool::Cut) {
        if (!cutPts.empty()) {
            cutPts.clear();
            setStatus("Découpe annulée — Échap désarme l'outil");
        } else {
            cutChainUndo_ = false;  // fin de la chaîne de découpes
            tool = Tool::Select;
            setStatus("Outil découpe désarmé");
        }
        return;
    }
    if (tool == Tool::Polygon) {
        if (!polyPts.empty()) {
            polyPts.clear();
            setStatus("Polygone annulé — Échap désarme l'outil");
        } else {
            tool = Tool::Select;
            setStatus("Outil polygone désarmé");
        }
        return;
    }
    if (isShapeTool(tool)) {
        tool = Tool::Select;
        setStatus("Forme désarmée");
        return;
    }
    if (triP1 >= 0) {
        triP1 = triP2 = -1;
        setStatus("Construction de triangle annulée");
        return;
    }
    if (brushArmed) {
        brushArmed = false;
        setStatus("Pinceau désarmé");
        return;
    }
    if (mergeMode != MergeMode::Off) {
        mergeMode = MergeMode::Off;
        setStatus("Fusion par déplacement désarmée");
        return;
    }
    if (measureActive) {
        toggleMeasure();  // Échap désarme aussi l'outil mesure
        return;
    }
    if (preview != PreviewMode::Off) exitPreview();
}

// ---------------------------------------------------------------------------
// Aimantation
// ---------------------------------------------------------------------------
Vec2 App::snappedPoint(const Vec2& w) const {
    if (!snapOn) return w;
    return {std::round(w.x / gridStep) * gridStep, std::round(w.y / gridStep) * gridStep};
}

Vec2 App::snapDelta(Vec2 d) const {
    d.x = std::round(d.x / gridStep) * gridStep;
    d.y = std::round(d.y / gridStep) * gridStep;
    return d;
}

// ---------------------------------------------------------------------------
// Fichiers (spec : JSON, meshes, préférences, autosave)
// ---------------------------------------------------------------------------
bool App::saveToLocation(const std::string& name) {
    if (name.empty()) {
        setStatus("Nom d'emplacement vide");
        return false;
    }
    SceneSnapshot snap;
    snap.scene = scene;
    snap.zoomMult = zoomMult();
    snap.cx = camera.cx;
    snap.cy = camera.cy;
    snap.grid = gridOn;
    snap.gridStep = gridStep;
    snap.name = name;
    const IoResult r = saveSceneJson(snap, name);
    if (!r.ok) {
        setStatus(r.error);
        logMsg("Erreur d'enregistrement : " + r.error);
        return false;
    }
    sceneName = name;
    dirty = false;
    saveLocations.erase(std::remove(saveLocations.begin(), saveLocations.end(), name),
                        saveLocations.end());
    saveLocations.insert(saveLocations.begin(), name);
    if (saveLocations.size() > 20) saveLocations.resize(20);
    savePrefsFile();
    dlgSaveOpen = false;
    setStatus("Enregistré : " + name + ".json");
    logMsg("Scène enregistrée : " + name + ".json");
    return true;
}

void App::openImportDialog(int fmt, const std::string& path) {
    dlgImportFmt = fmt;
    std::snprintf(dlgImportPath, sizeof(dlgImportPath), "%s", path.c_str());
    dlgImportOpen = true;
}

bool App::importJson(const std::string& path, bool replace) {
    std::string p = path;
    const std::string ext = ".json";
    if (p.size() > ext.size() && p.compare(p.size() - ext.size(), ext.size(), ext) == 0)
        p = p.substr(0, p.size() - ext.size());
    SceneSnapshot snap;
    const IoResult r = loadSceneJson(snap, p);
    if (!r.ok) {
        setStatus(r.error);
        logMsg("Import JSON échoué : " + r.error);
        return false;
    }
    if (replace) {
        scene = std::move(snap.scene);
        camera.zoom = std::clamp(snap.zoomMult * kBasePxPerUnit, kMinZoomPx, kMaxZoomPx);
        camera.cx = snap.cx;
        camera.cy = snap.cy;
        gridOn = snap.grid;
        gridStep = snap.gridStep;
        sceneName = snap.name;
        clearSelection();
        triP1 = triP2 = -1;
        clearBoolSets();  // 5.12 : les ensembles A/B ne survivent pas à un import
        undoStack.clear();
        redoStack.clear();
        cutChainUndo_ = false;  // la scène est remplacée : plus de chaîne de découpes
        dirty = false;
        cameraFramed = false;
        setStatus("Scène remplacée depuis « " + path + " »");
    } else {
        pushUndo();
        const int n0 = (int)snap.scene.planes.size();
        for (Mesh2D& p : snap.scene.planes) scene.planes.push_back(std::move(p));
        clearBoolSets();  // 5.12 : les indices de faces ont changé
        dirty = true;
        setStatus("Scène fusionnée avec « " + path + " » (" + std::to_string(n0) +
                  " plan(s) ajouté(s))");
    }
    logMsg("Import JSON : " + path + (replace ? " (remplacer)" : " (fusionner)"));
    dlgImportOpen = false;
    return true;
}

bool App::importMeshes(const std::string& path, bool replace) {
    Scene loaded;
    const IoResult r = loadMeshesText(loaded, path);
    if (!r.ok) {
        setStatus(r.error);
        logMsg("Import meshes échoué : " + r.error);
        return false;
    }
    if (replace) {
        scene = std::move(loaded);
        clearSelection();
        triP1 = triP2 = -1;
        undoStack.clear();
        redoStack.clear();
        cutChainUndo_ = false;  // la scène est remplacée : plus de chaîne de découpes
        dirty = false;
        cameraFramed = false;
        setStatus("Scène remplacée depuis « " + path + " »");
    } else {
        pushUndo();
        const int n0 = (int)loaded.planes.size();
        for (Mesh2D& p : loaded.planes) scene.planes.push_back(std::move(p));
        dirty = true;
        setStatus("Scène fusionnée avec « " + path + " » (" + std::to_string(n0) +
                  " plan(s) ajouté(s))");
    }
    logMsg("Import meshes : " + path + (replace ? " (remplacer)" : " (fusionner)"));
    dlgImportOpen = false;
    return true;
}

bool App::exportMeshesTo(const std::string& path) {
    const IoResult r = saveMeshesText(scene, path);
    if (!r.ok) {
        setStatus(r.error);
        return false;
    }
    setStatus("Export meshes : " + path);
    logMsg("Export meshes : " + path);
    return true;
}

bool App::exportSvgTo(const std::string& path) {
    if (path.empty()) {
        setStatus("Export SVG : nom de fichier vide");
        return false;
    }
    const IoResult r = exportPlaneSVG(scene.activePlane(), path);
    if (!r.ok) {
        setStatus(r.error);
        logMsg("Erreur d'export SVG : " + r.error);
        return false;
    }
    setStatus("Plan exporté en SVG : " + path);
    logMsg("Export SVG du plan actif : " + path);
    dlgSvgOpen = false;
    return true;
}

bool App::importObj(const std::string& path, bool replace) {
    Mesh2D m;
    const IoResult r = loadObj(m, path);
    if (!r.ok) {
        setStatus(r.error);
        logMsg("Import OBJ échoué : " + r.error);
        return false;
    }
    if (replace) {
        scene.clear();
        scene.planes[0] = std::move(m);
        clearSelection();
        triP1 = triP2 = -1;
        undoStack.clear();
        redoStack.clear();
        cutChainUndo_ = false;  // la scène est remplacée : plus de chaîne de découpes
        dirty = false;
        cameraFramed = false;
        setStatus("Scène remplacée depuis « " + path + " » (" +
                  std::to_string(scene.planes[0].faces.size()) + " face(s))");
    } else {
        pushUndo();
        // Fusion dans le plan actif : sommets ajoutés, faces ré-indexées.
        const int base = (int)scene.activePlane().vertices.size();
        for (const Vec2& v : m.vertices) scene.activePlane().addVertex(v);
        int added = 0;
        for (const Face& f : m.faces) {
            if ((int)f.verts.size() < 3) continue;
            std::vector<int> loop;
            loop.reserve(f.verts.size());
            for (int iv : f.verts) loop.push_back(base + iv);
            if (scene.activePlane().addFace(loop) >= 0) ++added;
        }
        dirty = true;
        setStatus("OBJ fusionné avec le plan actif (" + std::to_string(added) +
                  " face(s) ajoutée(s))");
    }
    logMsg("Import OBJ : " + path + (replace ? " (remplacer)" : " (fusionner)"));
    dlgImportOpen = false;
    return true;
}

// ---------------------------------------------------------------------------
// Préférences & autosave
// ---------------------------------------------------------------------------
std::string App::prefsDir() const {
    char* base = SDL_GetPrefPath("meshes-designer", "v1");
    std::string dir = base ? base : "./";
    SDL_free(base);
    return dir;
}

void App::savePrefsFile() {
    PrefsData p;
    p.palette = palette;
    p.brushOpacity = brushOpacity;
    p.circleSides = circleSides;
    p.crownInnerSides = crownInnerSides;
    p.edgePickTol = edgePickTol;
    p.vertexPickTol = vertexPickTol;
    p.mergeRadius = mergeRadius;
    p.locations = saveLocations;
    p.importMode = dlgImportReplace ? 0 : 1;
    p.allColors = allColors;
    p.wireframe = wireframe;
    p.snapOn = snapOn;
    p.bgColor = bgColor;
    p.image = scene.image;   // calque mémorisé (7.9) : chemin + transformée
    p.versions = versionFiles;
    p.consoleVisible = consoleVisible;
    p.consoleX = consolePos.x;
    p.consoleY = consolePos.y;
    p.consoleW = consoleSize.x;
    p.consoleH = consoleSize.y;
    p.toolbarPacks = toolbarPacks;   // paquets de la barre d'outils ouverts (3.2)
    savePrefsJson(p, prefsDir() + "prefs.json");
}

void App::loadPrefsFile() {
    PrefsData p;
    p.palette = palette;
    p.brushOpacity = brushOpacity;
    p.circleSides = circleSides;
    p.crownInnerSides = crownInnerSides;
    p.edgePickTol = edgePickTol;
    p.vertexPickTol = vertexPickTol;
    p.locations = saveLocations;
    p.bgColor = bgColor;
    const IoResult r = loadPrefsJson(p, prefsDir() + "prefs.json");
    if (!r.ok) return;
    if (!p.palette.empty()) palette = p.palette;
    brushOpacity = p.brushOpacity;
    circleSides = p.circleSides;
    crownInnerSides = std::clamp(p.crownInnerSides, 3, 64);
    edgePickTol = std::clamp(p.edgePickTol, 2.0f, 150.0f);
    vertexPickTol = std::clamp(p.vertexPickTol, 2.0f, 150.0f);
    mergeRadius = std::clamp(p.mergeRadius, 8, 64);
    saveLocations = p.locations;
    dlgImportReplace = (p.importMode == 0);
    consoleVisible = p.consoleVisible;
    consolePos = {p.consoleX, p.consoleY};
    consoleSize = {p.consoleW, p.consoleH};
    allColors = p.allColors;
    wireframe = p.wireframe;
    snapOn = p.snapOn;
    // Couleur de fond du canvas (bouton « fond » du groupe Scène) mémorisée.
    bgColor = p.bgColor;
    bgColor.a = 1.0f;
    // Calque mémorisé (7.9) : rappelé si les préférences en portent un (un
    // autosave plus récent, chargé après, le remplace s'il a le sien).
    if (!p.image.path.empty()) scene.image = p.image;
    // Paquets de la barre d'outils (3.2) : l'état ouvert/fermé revient tel
    // quel (un fichier sans le champ garde tous les paquets ouverts).
    toolbarPacks = p.toolbarPacks;
    versionFiles = p.versions;
    if (versionFiles.size() > 10) versionFiles.resize(10);
}

// Égalité géométrique (sommets + faces + couleurs) entre deux plans puis deux
// scènes — utilisée pour ne créer une version horodatée que si l'état a changé.
namespace {
bool sameGeometry(const Mesh2D& a, const Mesh2D& b) {
    if (a.vertices.size() != b.vertices.size() || a.faces.size() != b.faces.size())
        return false;
    for (size_t i = 0; i < a.vertices.size(); ++i)
        if (a.vertices[i].x != b.vertices[i].x || a.vertices[i].y != b.vertices[i].y)
            return false;
    for (size_t i = 0; i < a.faces.size(); ++i) {
        const Face& fa = a.faces[i];
        const Face& fb = b.faces[i];
        if (fa.verts != fb.verts || fa.hasColor != fb.hasColor) return false;
        if (fa.hasColor &&
            (fa.color.r != fb.color.r || fa.color.g != fb.color.g ||
             fa.color.b != fb.color.b || fa.color.a != fb.color.a))
            return false;
    }
    return true;
}
bool sameScene(const Scene& a, const Scene& b) {
    if (a.planes.size() != b.planes.size()) return false;
    for (size_t i = 0; i < a.planes.size(); ++i)
        if (!sameGeometry(a.planes[i], b.planes[i])) return false;
    return true;
}
}  // namespace

std::string App::versionTimestamp() const {
    const std::time_t now = std::time(nullptr);
    std::tm tmv;
#ifdef _WIN32
    localtime_s(&tmv, &now);
#else
    localtime_r(&now, &tmv);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tmv);
    return buf;
}

void App::pruneVersions() {
    while (versionFiles.size() > 10) {
        const std::string old = versionFiles.back();
        versionFiles.pop_back();
        std::remove((prefsDir() + old).c_str());
    }
}

void App::saveAutoFile() {
    SceneSnapshot snap;
    snap.scene = scene;
    snap.zoomMult = zoomMult();
    snap.cx = camera.cx;
    snap.cy = camera.cy;
    snap.grid = gridOn;
    snap.gridStep = gridStep;
    snap.name = sceneName;
    saveAutoJson(snap, undoStack, redoStack, prefsDir() + "autosave.json");
    // Version horodatée de l'autosave (10 conservées, du plus récent au plus
    // ancien) : permet de revenir à un état antérieur. Une nouvelle version
    // n'est créée que si la géométrie a changé depuis la précédente, et les
    // préférences ne sont réécrites que dans ce cas (pas de churn à 5 s).
    if (versionFiles.empty() || !sameScene(lastVersionedScene_, scene)) {
        lastVersionedScene_ = scene;
        const std::string name = "autosave-" + versionTimestamp() + ".json";
        std::ifstream src(prefsDir() + "autosave.json", std::ios::binary);
        if (src) {
            std::ofstream dst(prefsDir() + name, std::ios::binary | std::ios::trunc);
            if (dst) {
                dst << src.rdbuf();
                versionFiles.erase(std::remove(versionFiles.begin(), versionFiles.end(), name),
                                   versionFiles.end());
                versionFiles.insert(versionFiles.begin(), name);
                pruneVersions();
                savePrefsFile();
            }
        }
    }
    savedZoom_ = camera.zoom;
    savedCx_ = camera.cx;
    savedCy_ = camera.cy;
    savedGrid_ = gridOn;
    savedGridStep_ = gridStep;
}

bool App::restoreVersionFile(const std::string& name) {
    SceneSnapshot snap;
    std::vector<Scene> undo, redo;
    const IoResult r = loadAutoJson(snap, undo, redo, prefsDir() + name);
    if (!r.ok) {
        setStatus("Restauration impossible : " + r.error);
        logMsg("Restauration échouée : " + name);
        return false;
    }
    const Scene prev = scene;  // l'état courant, restauré par Ctrl+Z ensuite
    scene = std::move(snap.scene);
    camera.zoom = std::clamp(snap.zoomMult * kBasePxPerUnit, kMinZoomPx, kMaxZoomPx);
    camera.cx = snap.cx;
    camera.cy = snap.cy;
    gridOn = snap.grid;
    gridStep = snap.gridStep;
    sceneName = snap.name;
    // L'état d'avant la restauration reste annulable (Ctrl+Z) : il est ajouté
    // À LA FIN de l'historique chargé avec la version (le plus récent).
    if (undo.size() >= kMaxUndo) undo.resize(kMaxUndo - 1);
    if (redo.size() > kMaxUndo) redo.resize(kMaxUndo);
    undo.push_back(prev);
    undoStack = std::move(undo);
    redoStack = std::move(redo);
    clearSelection();
    triP1 = triP2 = -1;
    dirty = false;
    lastVersionedScene_ = scene;
    dlgVersionsOpen = false;
    setStatus("Version restaurée : " + name + " — Ctrl+Z pour revenir");
    logMsg("Version restaurée : " + name);
    return true;
}

void App::loadAutoFile() {
    SceneSnapshot snap;
    std::vector<Scene> undo, redo;
    const IoResult r = loadAutoJson(snap, undo, redo, prefsDir() + "autosave.json");
    if (!r.ok) return;
    scene = std::move(snap.scene);
    camera.zoom = std::clamp(snap.zoomMult * kBasePxPerUnit, kMinZoomPx, kMaxZoomPx);
    camera.cx = snap.cx;
    camera.cy = snap.cy;
    gridOn = snap.grid;
    gridStep = snap.gridStep;
    sceneName = snap.name;
    if (undo.size() > kMaxUndo) undo.resize(kMaxUndo);
    if (redo.size() > kMaxUndo) redo.resize(kMaxUndo);
    undoStack = std::move(undo);
    redoStack = std::move(redo);
    lastVersionedScene_ = scene;
    logMsg("Session précédente restaurée");
}

// ---------------------------------------------------------------------------
// Divers
// ---------------------------------------------------------------------------
void App::setStatus(const std::string& msg) {
    status = msg;
    statusAge = 3.5f;
    setToast(msg, 3.0f);
}

void App::statusCrown() {
    // Le libellé suit la phase du tracé : tant que le rayon n'est pas verrouillé
    // (avant le 2e clic), la molette règle les côtés extérieurs, puis les
    // intérieurs après le 2e clic (Maj+molette force l'autre jeu de côtés).
    const bool innerPhase = crownInnerPhase();
    setStatus("Couronne : " + std::to_string(circleSides) + " ext. / " +
              std::to_string(crownInnerSides) + " int. — molette : " +
              (innerPhase ? "intérieurs, Maj+molette : extérieurs"
                          : "extérieurs, Maj+molette : intérieurs"));
}

void App::setToast(const std::string& msg, float secs) {
    toast = msg;
    toastAge = secs;
}

// ---------------------------------------------------------------------------
// Aide prospective au survol (spec 13)
// ---------------------------------------------------------------------------
// Le toast décrit le geste possible sous le curseur : sommet, arête de
// triangle, segment ou triangle (modes segment / triangle), pinceau armé,
// zone vide. Pendant une construction (forme 4.2 ou triangle 4.1), il guide la
// phase suivante et reste affiché. Il n'est rafraîchi que lorsque le message
// change ou que le toast a expiré, pour éviter un rafraîchissement continu.
void App::updateHoverHelp(const Vec2& mouseWorld) {
    // Outil découpe : guide la phase du tracé.
    if (tool == Tool::Cut) {
        setToast(cutPts.empty()
                     ? "Clic gauche : 1er sommet du polygone de découpe"
                     : "Clic gauche : sommet suivant (re-clic près du 1er point : fermer "
                       "et découper) · clic droit / Entrée : découper · Retour arrière : "
                       "dernier point",
                 0.5f);
        return;
    }
    // Outil polygone : guide la phase du tracé.
    if (tool == Tool::Polygon) {
        setToast(polyPts.empty()
                     ? "Clic gauche : 1er sommet du polygone"
                     : "Clic gauche : sommet suivant (re-clic près du 1er point : fermer "
                       "et trianguler) · clic droit / Entrée : valider · Retour arrière : "
                       "dernier point",
                 0.5f);
        return;
    }
    // Construction d'une forme prédéfinie (4.2) : guide la phase suivante.
    if (drag_.kind == DragKind::Shape) {
        const char* msg = nullptr;
        if (drag_.shapeStage == 1) {
            msg = (tool == Tool::Crown)
                      ? "1er clic posé — déplacez la souris (molette : côtés extérieurs), "
                        "2e clic verrouille le rayon"
                      : "1er clic posé — déplacez la souris, puis validez au 2e clic";
        } else if (drag_.shapeStage >= 2) {
            msg = (tool == Tool::Star)
                      ? "Étoile : 2e clic verrouille rayon et orientation — "
                        "déplacez pour la profondeur, 3e clic valide"
                      : (tool == Tool::Crown)
                            ? "Couronne : 2e clic verrouille — distance : taille du trou, "
                              "angle du curseur : orientation intérieure · 3e clic valide "
                              "(molette : intérieurs, Maj+molette : extérieurs)"
                            : "Anneau : 2e clic verrouille — déplacez pour la taille du trou, "
                              "3e clic valide (molette : côtés)";
        }
        if (msg) {
            setToast(msg, 0.5f);  // réaffiché tant que le tracé dure
        }
        return;
    }
    // Construction de triangle (4.1) : rappelle la phase suivante.
    if (triP1 >= 0 || triP2 >= 0) {
        setToast(triP2 < 0
                     ? "Cliquez pour poser le 2e sommet — le 3e clic ferme le triangle"
                     : "Cliquez pour poser le 3e sommet — le triangle se ferme",
                 0.5f);
        return;
    }
    if (drag_.kind != DragKind::None) return;  // déplacement / lasso : pas d'aide
    if (tool != Tool::Select) return;          // forme armée, tracé pas commencé

    std::string msg;
    // Pinceau armé : la peinture a priorité sur le survol — avec des
    // triangles sélectionnés, tous sont peints ; sinon seul le survolé l'est.
    const int face = brushArmed ? pickFace(mouseWorld) : -1;
    if (brushArmed && !selFaces.empty()) {
        msg = "Clic gauche pour peindre les " + std::to_string(selFaces.size()) +
              " triangles sélectionnés";
    } else if (face >= 0) {
        msg = "Clic gauche pour peindre ce triangle…";
    } else if (selMode == SelMode::Vertex) {
        if (hoverVertex >= 0) {
            msg = "Sélectionner ce sommet — clic droit pour le déplacer";
        } else if (hoverEdge.first >= 0) {
            msg = "Clic gauche pour créer un nouveau triangle à partir de ce segment";
        } else {
            msg = "Zone vide — clic gauche : sélection ou poser un sommet · "
                  "clic droit : déplacer la sélection";
        }
    } else if (selMode == SelMode::Edge) {
        const Mesh2D::Edge e = pickEdge(mouseWorld, edgePickTol);
        msg = (e.first >= 0)
                  ? "Sélectionner ce segment — clic droit pour le déplacer"
                  : "Zone vide — clic gauche : rectangle de sélection · "
                    "clic droit : déplacer la sélection";
    } else {  // Face
        const std::vector<int> stack = pickFaces(mouseWorld);
        if (!stack.empty()) {
            msg = "Sélectionner ce triangle" +
                  (stack.size() > 1
                       ? " — " + std::to_string(stack.size()) +
                             " faces superposées ici (re-clic : la suivante "
                             "en dessous)"
                       : " — clic droit pour le déplacer");
        } else {
            msg = "Zone vide — clic gauche : rectangle de sélection · "
                  "clic droit : déplacer la sélection";
        }
    }

    if (msg != lastHoverHelpKey_ || toastAge <= 0.0f) {
        lastHoverHelpKey_ = msg;
        setToast(msg);
    }
}

void App::logMsg(const std::string& msg) {
    const std::time_t now = std::time(nullptr);
    std::tm tmv;
#ifdef _WIN32
    localtime_s(&tmv, &now);
#else
    localtime_r(&now, &tmv);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "[%H:%M:%S]", &tmv);
    consoleLog.push_back(std::string(buf) + " " + msg);
    if (consoleLog.size() > 300) consoleLog.erase(consoleLog.begin());
}

void App::frameSelection() {
    // Cadrage sur la sélection courante (tous les modes), repli sur la scène.
    const std::vector<int> verts = selectionVertices();
    const Mesh2D& m = scene.activePlane();
    bool first = true;
    Vec2 minv{0.0f, 0.0f}, maxv{0.0f, 0.0f};
    for (int v : verts) {
        if (v < 0 || (size_t)v >= m.vertices.size()) continue;
        const Vec2& p = m.vertices[v];
        if (first) {
            minv = maxv = p;
            first = false;
        } else {
            minv.x = std::min(minv.x, p.x);
            minv.y = std::min(minv.y, p.y);
            maxv.x = std::max(maxv.x, p.x);
            maxv.y = std::max(maxv.y, p.y);
        }
    }
    if (first) {
        frameView();
        return;
    }
    const Vec2 center = (minv + maxv) * 0.5f;
    const float extent = std::max(maxv.x - minv.x, maxv.y - minv.y) * 0.5f + 0.5f;
    camera.frame(center, extent, viewportVec2());
    cameraFramed = true;
}

void App::frameView() {
    // Cadrage sur la scène entière (tous les plans).
    if (scene.countVerts() == 0) {
        camera.reset();
        cameraFramed = false;
        return;
    }
    cameraFramed = true;
    bool first = true;
    Vec2 minv{0.0f, 0.0f}, maxv{0.0f, 0.0f};
    for (const auto& p : scene.planes) {
        for (const Vec2& v : p.vertices) {
            if (first) {
                minv = maxv = v;
                first = false;
            } else {
                minv.x = std::min(minv.x, v.x);
                minv.y = std::min(minv.y, v.y);
                maxv.x = std::max(maxv.x, v.x);
                maxv.y = std::max(maxv.y, v.y);
            }
        }
    }
    const Vec2 center = (minv + maxv) * 0.5f;
    const float extent = std::max(maxv.x - minv.x, maxv.y - minv.y) * 0.5f + 0.5f;
    camera.frame(center, extent, viewportVec2());
}

void App::cancelDrag() {
    drag_.kind = DragKind::None;
}

// ---------------------------------------------------------------------------
// Rendu de la scène
// ---------------------------------------------------------------------------
void App::drawGrid() {
    const float zoom = camera.zoom;
    const float vw = viewportSize.x, vh = viewportSize.y;
    const float l = camera.cx - vw / (2.0f * zoom);
    const float r = camera.cx + vw / (2.0f * zoom);
    const float b = camera.cy - vh / (2.0f * zoom);
    const float t = camera.cy + vh / (2.0f * zoom);

    float minor = gridStep;
    while (minor * zoom < 14.0f) minor *= 5.0f;
    while (minor * zoom > 70.0f) minor /= 5.0f;
    if (minor < gridStep) minor = gridStep;
    const float major = minor * 5.0f;

    std::vector<Vec2> minorSegs, majorSegs, xAxisSegs, yAxisSegs;

    auto addVLine = [&](std::vector<Vec2>& out, float x) {
        out.push_back({x, b});
        out.push_back({x, t});
    };
    auto addHLine = [&](std::vector<Vec2>& out, float y) {
        out.push_back({l, y});
        out.push_back({r, y});
    };

    const int ix0 = (int)std::ceil(l / minor), ix1 = (int)std::floor(r / minor);
    if (ix1 - ix0 <= 4000) {
        for (int ix = ix0; ix <= ix1; ++ix) {
            const float x = ix * minor;
            if (std::fabs(x) < minor * 0.02f) addVLine(yAxisSegs, x);
            else if (nearMultiple(x, major)) addVLine(majorSegs, x);
            else addVLine(minorSegs, x);
        }
    } else {
        const int mx0 = (int)std::ceil(l / major), mx1 = (int)std::floor(r / major);
        for (int mx = mx0; mx <= mx1; ++mx) {
            const float x = mx * major;
            if (std::fabs(x) < major * 0.02f) addVLine(yAxisSegs, x);
            else addVLine(majorSegs, x);
        }
    }
    const int iy0 = (int)std::ceil(b / minor), iy1 = (int)std::floor(t / minor);
    if (iy1 - iy0 <= 4000) {
        for (int iy = iy0; iy <= iy1; ++iy) {
            const float y = iy * minor;
            if (std::fabs(y) < minor * 0.02f) addHLine(xAxisSegs, y);
            else if (nearMultiple(y, major)) addHLine(majorSegs, y);
            else addHLine(minorSegs, y);
        }
    } else {
        const int my0 = (int)std::ceil(b / major), my1 = (int)std::floor(t / major);
        for (int my = my0; my <= my1; ++my) {
            const float y = my * major;
            if (std::fabs(y) < major * 0.02f) addHLine(xAxisSegs, y);
            else addHLine(majorSegs, y);
        }
    }

    renderer.drawLines(minorSegs, kGridMinor);
    renderer.drawLines(majorSegs, kGridMajor);
    renderer.drawLines(xAxisSegs, kAxisX);
    renderer.drawLines(yAxisSegs, kAxisY);
}

void App::drawMeshGeometry() {
    const int n = scene.count();
    const int active = scene.active;

    // Plans de contexte (inactifs) d'abord : estompés, lignes en pointillés,
    // points atténués (2.2).
    for (int i = 0; i < n; ++i)
        if (i != active) drawPlane(scene.planes[i], false);

    // Plan actif par-dessus (rendu d'édition complet).
    if (active >= 0 && active < n) drawPlane(scene.planes[active], true);
    if (active < 0 || active >= n) return;

    const Mesh2D& m = scene.planes[active];

    // Sélection des faces (mode triangle).
    for (int fi = 0; fi < (int)m.faces.size(); ++fi) {
        if (std::find(selFaces.begin(), selFaces.end(), fi) == selFaces.end()) continue;
        const Face& f = m.faces[fi];
        if ((int)f.verts.size() < 3) continue;
        std::vector<Vec2> pts;
        pts.reserve(f.verts.size());
        for (int v : f.verts) pts.push_back(m.vertices[v]);
        std::vector<int> local;
        triangulatePolygon(pts, local);
        std::vector<Vec2> triPts;
        triPts.reserve(local.size());
        for (int idx : local) triPts.push_back(pts[idx]);
        renderer.drawTriangles(triPts, kFaceSel);
    }

    // Arêtes sélectionnées.
    std::vector<Vec2> selSegs;
    for (const auto& e : selEdges) {
        selSegs.push_back(m.vertices[e.first]);
        selSegs.push_back(m.vertices[e.second]);
    }
    renderer.drawLines(selSegs, kEdgeSel);

    // Segment survolé (mode sommet) : il s'illumine — un clic y accrochera un
    // nouveau sommet pour former un triangle. Halo translucide + trait vif.
    // Masqué dès que le glisser dépasse le seuil du simple clic : c'est alors
    // une sélection au lasso, aucun triangle ne sera accroché au relâchement.
    const bool hoverEdgeVisible =
        hoverEdge.first >= 0 && hoverEdge.second >= 0 &&
        (drag_.kind != DragKind::Box ||
         distance(drag_.startScreen, drag_.curScreen) < 4.0f);
    if (hoverEdgeVisible) {
        const Vec2 ha = m.vertices[hoverEdge.first];
        const Vec2 hb = m.vertices[hoverEdge.second];
        const Vec2 hd = hb - ha;
        const float hl = length(hd);
        if (hl > 1e-6f) {
            const Vec2 hn{-hd.y / hl, hd.x / hl};
            const float off = 1.0f / camera.zoom;  // ~2 px apparents de chaque côté
            renderer.drawLines({ha + hn * off, hb + hn * off}, kEdgeHoverHalo);
            renderer.drawLines({ha - hn * off, hb - hn * off}, kEdgeHoverHalo);
        }
        renderer.drawLines({ha, hb}, kEdgeHover);
    }

    // Sommets du plan actif.
    std::vector<Vec2> norm, sel, hover;
    for (int i = 0; i < (int)m.vertices.size(); ++i) {
        if (std::find(selVerts.begin(), selVerts.end(), i) != selVerts.end())
            sel.push_back(m.vertices[i]);
        else if (i == hoverVertex ||
                 (hoverEdgeVisible && (i == hoverEdge.first || i == hoverEdge.second)))
            hover.push_back(m.vertices[i]);
        else
            norm.push_back(m.vertices[i]);
    }
    renderer.drawPoints(norm, 5.0f, kVert);
    renderer.drawPoints(hover, 7.0f, kVertHover);
    renderer.drawPoints(sel, 7.5f, kVertSel);
}

// Rendu d'un plan : remplissage (actif, ou tous si « toutes couleurs »),
// arêtes pleines (actif) ou en pointillés (inactif), points atténués (inactif).
// En mode filaire (7.6), aucun remplissage : seules les arêtes restent, pour
// toutes les faces de tous les plans — la structure reste lisible à travers.
void App::drawPlane(const Mesh2D& p, bool isActive) {
    // Faces.
    if (!wireframe && (isActive || allColors)) {
        for (const Face& f : p.faces) {
            if ((int)f.verts.size() < 3) continue;
            std::vector<Vec2> pts;
            pts.reserve(f.verts.size());
            for (int v : f.verts) pts.push_back(p.vertices[v]);
            std::vector<int> local;
            triangulatePolygon(pts, local);
            std::vector<Vec2> triPts;
            triPts.reserve(local.size());
            for (int idx : local) triPts.push_back(pts[idx]);
            Color base = f.hasColor ? f.color : kFaceFill;
            if (!isActive) base.a *= 0.75f;  // plans inactifs : remplis mais atténués
            base.a *= p.opacity;  // opacité du plan (7.8)
            renderer.drawTriangles(triPts, base);
        }
    }

    // Arêtes : pleines pour le plan actif, en pointillés sinon.
    const auto es = p.edges();
    if (isActive) {
        std::vector<Vec2> segs;
        segs.reserve(es.size() * 2);
        for (const auto& e : es) {
            segs.push_back(p.vertices[e.first]);
            segs.push_back(p.vertices[e.second]);
        }
        renderer.drawLines(segs, kEdge);
    } else {
        std::vector<Vec2> segs;
        dashedPairs(es, p, 5.0f, 4.0f, segs);
        renderer.drawLines(segs, kEdgeDim);
    }

    // Points des plans inactifs (atténués) ; ceux du plan actif sont dessinés
    // par drawMeshGeometry (avec l'état de sélection).
    if (!isActive && !p.vertices.empty())
        renderer.drawPoints(p.vertices, 3.0f, kVertDim);
}

// Découpe des arêtes en pointillés (longueurs en pixels d'écran, indépendantes
// du zoom) pour le rendu des plans de contexte.
void App::dashedPairs(const std::vector<Mesh2D::Edge>& edges, const Mesh2D& p,
                      float dashPx, float gapPx, std::vector<Vec2>& out) const {
    const float dash = dashPx / camera.zoom;
    const float gap = gapPx / camera.zoom;
    for (const auto& e : edges) {
        const Vec2 a = p.vertices[e.first];
        const Vec2 b = p.vertices[e.second];
        const Vec2 d = b - a;
        const float len = length(d);
        if (len < 1e-6f) continue;
        const Vec2 dir = d / len;
        float t = 0.0f;
        while (t < len) {
            const float t2 = std::min(t + dash, len);
            out.push_back(a + dir * t);
            out.push_back(a + dir * t2);
            t = t2 + gap;
        }
    }
}

// Rendu de l'outil mesure : segment entre les deux points posés (ou aperçu
// depuis le 1er point vers le curseur), points aux extrémités. La distance est
// affichée au HUD (ui.cpp).
void App::drawMeasureVisual() {
    if (!measureActive) return;
    // Segment posé (2 points) ou aperçu depuis le 1er point vers le curseur.
    const bool complete = measureHasB && !measureHasA;
    const ImGuiIO& io = ImGui::GetIO();
    const Vec2 ms{io.MousePos.x - viewportPos.x, io.MousePos.y - viewportPos.y};
    const Vec2 mw = camera.screenToWorld(ms, viewportVec2());
    const Vec2 cur = snappedPoint(mw);
    const Vec2 end = complete ? measureB : cur;
    std::vector<Vec2> segs = {measureA, end};
    renderer.drawLines(segs, kPreview);
    renderer.drawPoints({measureA, end}, 7.0f, kPreview);
}

// Rendu des aides à la fusion : anneau orange autour des points superposés
// (5.5) et, quand la fusion par déplacement est armée/verrouillée, le rayon de
// fusion autour du point unique sélectionné avec la cible surlignée (5.6).
void App::drawMergeVisuals() {
    if (kiosk) return;  // le voile plein écran recouvre le canvas
    if (tool != Tool::Select || selMode != SelMode::Vertex) return;
    const Mesh2D& m = scene.activePlane();

    // 5.5 : anneau orange à chaque position où plusieurs points coïncident.
    const auto groups = overlapGroups();
    for (const auto& g : groups) {
        const Vec2 p = m.vertices[g[0]];
        drawCircleLines(p, 9.0f, kMergeRing, 24);
    }

    // 5.6 : rayon de fusion autour du point unique sélectionné.
    if (mergeMode == MergeMode::Off || selVerts.size() != 1) return;
    const int v = selVerts[0];
    if (v < 0 || (size_t)v >= m.vertices.size()) return;
    const Vec2 c = m.vertices[v];
    const float r = mergeRadius / camera.zoom;
    {
        // Disque translucide : la « zone d'atterrissage » du point à relâcher.
        constexpr int kSegs = 32;
        std::vector<Vec2> fan;
        fan.reserve((size_t)kSegs * 3);
        for (int i = 0; i < kSegs; ++i) {
            const float a0 = (float)i * 2.0f * kPi / (float)kSegs;
            const float a1 = (float)(i + 1) * 2.0f * kPi / (float)kSegs;
            fan.push_back(c);
            fan.push_back({c.x + std::cos(a0) * r, c.y + std::sin(a0) * r});
            fan.push_back({c.x + std::cos(a1) * r, c.y + std::sin(a1) * r});
        }
        renderer.drawTriangles(fan, kMergeRadiusFill);
        drawCircleLines(c, (float)mergeRadius, kMergeRadius, kSegs);
    }
    // Cible dans le rayon : surlignée pour annoncer la fusion au relâchement.
    const int target = pickMergeTarget(v, (float)mergeRadius);
    if (target >= 0) renderer.drawPoints({m.vertices[target]}, 9.0f, kMergeTarget);
}

void App::drawCircleLines(const Vec2& c, float radPx, const Color& col, int segs) {
    const float r = radPx / camera.zoom;
    std::vector<Vec2> pairs;
    pairs.reserve((size_t)segs * 2);
    for (int i = 0; i < segs; ++i) {
        const float a0 = (float)i * 2.0f * kPi / (float)segs;
        const float a1 = (float)(i + 1) * 2.0f * kPi / (float)segs;
        pairs.push_back({c.x + std::cos(a0) * r, c.y + std::sin(a0) * r});
        pairs.push_back({c.x + std::cos(a1) * r, c.y + std::sin(a1) * r});
    }
    renderer.drawLines(pairs, col);
}

void App::drawDragPreview() {
    if (tool == Tool::Cut && !cutPts.empty()) {
        drawCutPreview();
        return;
    }
    if (tool == Tool::Polygon && !polyPts.empty()) {
        drawPolygonPreview();
        return;
    }
    if (drag_.kind == DragKind::Shape) {
        drawShapeOutline();
        return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    const Vec2 ms{io.MousePos.x - viewportPos.x, io.MousePos.y - viewportPos.y};
    const Vec2 mw = camera.screenToWorld(ms, viewportVec2());

    // Aperçu de la construction de triangle (points partiels).
    if (triP1 >= 0 && (size_t)triP1 < scene.activePlane().vertices.size() && drag_.kind == DragKind::None) {
        const Vec2 cur = snappedPoint(mw);
        std::vector<Vec2> segs;
        segs.push_back(scene.activePlane().vertices[triP1]);
        segs.push_back(cur);
        if (triP2 >= 0 && (size_t)triP2 < scene.activePlane().vertices.size()) {
            segs.push_back(scene.activePlane().vertices[triP2]);
            segs.push_back(cur);
        }
        renderer.drawLines(segs, kPreview);
        renderer.drawPoints({cur}, 6.0f, kPreview);
    }

    // Tracé de la sélection au lasso (5.9) : contour cyan + remplissage
    // translucide, comme l'aperçu de l'outil découpe.
    if (drag_.kind == DragKind::Lasso && lassoPts.size() >= 2) {
        std::vector<Vec2> poly = lassoPts;  // écran → monde (projection actuelle)
        for (Vec2& p : poly) p = camera.screenToWorld(p, viewportVec2());
        std::vector<Vec2> segs;
        segs.reserve(poly.size() * 2 + 2);
        for (size_t i = 0; i + 1 < poly.size(); ++i) {
            segs.push_back(poly[i]);
            segs.push_back(poly[i + 1]);
        }
        if (lassoPts.size() >= 3) {
            segs.push_back(poly.back());
            segs.push_back(poly.front());
            std::vector<int> tris;
            if (triangulatePolygon(poly, tris)) {
                std::vector<Vec2> fill;
                fill.reserve(tris.size());
                for (int k : tris) fill.push_back(poly[k]);
                renderer.drawTriangles(fill, kPreviewFill);
            }
        }
        renderer.drawLines(segs, kPreview);
    }
}

// Aperçu de l'outil découpe : le polygone en cours (contour cyan + remplissage
// translucide), la ligne de fermeture vers le curseur et les points posés.
void App::drawCutPreview() {
    const ImGuiIO& io = ImGui::GetIO();
    const Vec2 ms{io.MousePos.x - viewportPos.x, io.MousePos.y - viewportPos.y};
    const Vec2 mw = camera.screenToWorld(ms, viewportVec2());
    const Vec2 cur = snappedPoint(mw);

    std::vector<Vec2> poly = cutPts;
    poly.push_back(cur);

    // Contour : segments consécutifs + fermeture vers le 1er point.
    std::vector<Vec2> segs;
    segs.reserve(poly.size() * 2 + 2);
    for (size_t i = 0; i + 1 < poly.size(); ++i) {
        segs.push_back(poly[i]);
        segs.push_back(poly[i + 1]);
    }
    if (cutPts.size() >= 2) {
        segs.push_back(poly.back());
        segs.push_back(poly.front());
    }
    renderer.drawLines(segs, kPreview);

    // Remplissage translucide du polygone (quand il est fermable).
    if (poly.size() >= 3) {
        std::vector<int> tris;
        if (triangulatePolygon(poly, tris)) {
            std::vector<Vec2> fill;
            fill.reserve(tris.size());
            for (int k : tris) fill.push_back(poly[k]);
            renderer.drawTriangles(fill, kPreviewFill);
        }
    }
    renderer.drawPoints(cutPts, 6.0f, kPreview);
    renderer.drawPoints({cur}, 6.0f, kVertHover);
}

// Aperçu de l'outil polygone : identique à drawCutPreview mais avec polyPts.
void App::drawPolygonPreview() {
    const ImGuiIO& io = ImGui::GetIO();
    const Vec2 ms{io.MousePos.x - viewportPos.x, io.MousePos.y - viewportPos.y};
    const Vec2 mw = camera.screenToWorld(ms, viewportVec2());
    const Vec2 cur = snappedPoint(mw);

    std::vector<Vec2> poly = polyPts;
    poly.push_back(cur);

    // Contour : segments consécutifs + fermeture vers le 1er point.
    std::vector<Vec2> segs;
    segs.reserve(poly.size() * 2 + 2);
    for (size_t i = 0; i + 1 < poly.size(); ++i) {
        segs.push_back(poly[i]);
        segs.push_back(poly[i + 1]);
    }
    if (polyPts.size() >= 2) {
        segs.push_back(poly.back());
        segs.push_back(poly.front());
    }
    // Vert pour le polygone (distinct du cyan de la découpe).
    const Color kPolyPreview{0.30f, 0.95f, 0.45f, 0.95f};
    const Color kPolyPreviewFill{0.30f, 0.95f, 0.45f, 0.16f};
    renderer.drawLines(segs, kPolyPreview);

    // Remplissage translucide du polygone (quand il est fermable).
    if (poly.size() >= 3) {
        std::vector<int> tris;
        if (triangulatePolygon(poly, tris)) {
            std::vector<Vec2> fill;
            fill.reserve(tris.size());
            for (int k : tris) fill.push_back(poly[k]);
            renderer.drawTriangles(fill, kPolyPreviewFill);
        }
    }
    renderer.drawPoints(polyPts, 6.0f, kPolyPreview);
    renderer.drawPoints({cur}, 6.0f, kVertHover);
}

// Aperçu du tracé de forme : la forme est VISIBLE (remplissage cyan
// translucide) avec son contour (périmètre, et pourtour du trou pour l'anneau)
// — sans les arêtes internes (rayons de l'éventail, parois du trou).
void App::drawShapeOutline() {
    const Vec2& a = drag_.shapeAnchor;
    const Vec2& c = drag_.shapeCur;
    const Vec2 d = c - a;
    // Comme dans completeShape : une fois verrouillé (2e clic), le rayon et
    // l'orientation de l'anneau/couronne/étoile ne bougent plus avec le curseur.
    const bool locked = drag_.shapeStage >= 2 &&
                        (tool == Tool::Ring || tool == Tool::Crown || tool == Tool::Star);
    const float rad = locked ? drag_.shapeRadius : length(d);
    const float ang = locked ? drag_.shapeAngle : std::atan2(d.y, d.x);
    std::vector<Vec2> poly, fill, segs;

    auto rimPts = [&](int n, float r, float baseAng, std::vector<Vec2>& out) {
        out.clear();
        for (int i = 0; i <= n; ++i) {
            const float aa = baseAng + (float)i * 2.0f * kPi / (float)n;
            out.push_back({a.x + std::cos(aa) * r, a.y + std::sin(aa) * r});
        }
    };
    // Remplissage d'un polygone régulier en éventail depuis le centre.
    auto fanFill = [&](int n, float r, float baseAng) {
        fill.clear();
        for (int i = 0; i < n; ++i) {
            const float a0 = baseAng + (float)i * 2.0f * kPi / (float)n;
            const float a1 = baseAng + (float)(i + 1) * 2.0f * kPi / (float)n;
            fill.push_back(a);
            fill.push_back({a.x + std::cos(a0) * r, a.y + std::sin(a0) * r});
            fill.push_back({a.x + std::cos(a1) * r, a.y + std::sin(a1) * r});
        }
    };

    // drawLines attend des PAIRES de sommets (GL_LINES) : un polygone fermé
    // (P0…Pn avec Pn = P0) est déroulé en segments consécutifs avant le tracé,
    // sinon seul un segment sur deux serait dessiné (contour incomplet).
    auto drawPolygon = [&](const std::vector<Vec2>& p) {
        segs.clear();
        if (p.size() < 2) return;
        segs.reserve((p.size() - 1) * 2);
        for (size_t i = 0; i + 1 < p.size(); ++i) {
            segs.push_back(p[i]);
            segs.push_back(p[i + 1]);
        }
        renderer.drawLines(segs, kPreview);
    };

    switch (tool) {
        case Tool::Rectangle: {
            const Vec2 p0{std::min(a.x, c.x), std::min(a.y, c.y)};
            const Vec2 p1{std::max(a.x, c.x), std::max(a.y, c.y)};
            poly = {p0, {p1.x, p0.y}, p1, {p0.x, p1.y}, p0};
            fill = {p0, {p1.x, p0.y}, p1, p0, p1, {p0.x, p1.y}};  // 2 triangles
            break;
        }
        case Tool::Square: {
            const float s = std::max(std::fabs(d.x), std::fabs(d.y));
            const Vec2 p0{a.x, a.y};
            const Vec2 p1{a.x + std::copysign(s, d.x), a.y + std::copysign(s, d.y)};
            const Vec2 p2{a.x + std::copysign(s, d.x), a.y};
            const Vec2 p3{a.x, a.y + std::copysign(s, d.y)};
            poly = {p0, p2, p1, p3, p0};
            fill = {p0, p2, p1, p0, p1, p3};
            break;
        }
        case Tool::Circle:
            rimPts(circleSides, rad, ang, poly);
            fanFill(circleSides, rad, ang);
            break;
        case Tool::Triangle: {
            // Un seul triangle, sans point central (addRimPolygon).
            std::vector<Vec2> pts;
            for (int i = 0; i < 3; ++i) {
                const float aa = ang + (float)i * 2.0f * kPi / 3.0f;
                pts.push_back({a.x + std::cos(aa) * rad, a.y + std::sin(aa) * rad});
            }
            poly = {pts[0], pts[1], pts[2], pts[0]};
            fill = {pts[0], pts[1], pts[2]};
            break;
        }
        case Tool::Pentagon:
            rimPts(5, rad, ang, poly);
            fanFill(5, rad, ang);
            break;
        case Tool::Hexagon:
            rimPts(6, rad, ang, poly);
            fanFill(6, rad, ang);
            break;
        case Tool::Star: {
            const int n = circleSides;
            const float depth = drag_.shapeStage >= 2 ? drag_.shapeInner : 0.5f;
            poly.clear();
            fill.clear();
            for (int i = 0; i <= n * 2; ++i) {
                const float aa = ang + (float)i * kPi / (float)n;
                const float r = (i % 2 == 0) ? rad : rad * depth;
                poly.push_back({a.x + std::cos(aa) * r, a.y + std::sin(aa) * r});
            }
            for (int i = 0; i < n * 2; ++i) {
                const float aa = ang + (float)i * kPi / (float)n;
                const float ab = ang + (float)(i + 1) * kPi / (float)n;
                const float ra = (i % 2 == 0) ? rad : rad * depth;
                const float rb = ((i + 1) % 2 == 0) ? rad : rad * depth;
                fill.push_back(a);
                fill.push_back({a.x + std::cos(aa) * ra, a.y + std::sin(aa) * ra});
                fill.push_back({a.x + std::cos(ab) * rb, a.y + std::sin(ab) * rb});
            }
            break;
        }
        case Tool::Ring: {
            const float hole = drag_.shapeStage >= 2 ? drag_.shapeInner : 0.5f;
            // Remplissage de l'anneau (quads jante → trou), sans les parois.
            fill.clear();
            for (int i = 0; i < circleSides; ++i) {
                const float aa = ang + (float)i * 2.0f * kPi / (float)circleSides;
                const float ab = ang + (float)(i + 1) * 2.0f * kPi / (float)circleSides;
                const Vec2 oa{a.x + std::cos(aa) * rad, a.y + std::sin(aa) * rad};
                const Vec2 ob{a.x + std::cos(ab) * rad, a.y + std::sin(ab) * rad};
                const Vec2 ia{a.x + std::cos(aa) * rad * hole, a.y + std::sin(aa) * rad * hole};
                const Vec2 ib{a.x + std::cos(ab) * rad * hole, a.y + std::sin(ab) * rad * hole};
                fill.push_back(oa);
                fill.push_back(ob);
                fill.push_back(ib);
                fill.push_back(oa);
                fill.push_back(ib);
                fill.push_back(ia);
            }
            renderer.drawTriangles(fill, kPreviewFill);
            // Contours : périmètre extérieur puis pourtour du trou.
            rimPts(circleSides, rad, ang, poly);
            drawPolygon(poly);
            rimPts(circleSides, rad * hole, ang, poly);
            drawPolygon(poly);
            return;
        }
        case Tool::Crown: {
            const float hole = drag_.shapeStage >= 2 ? drag_.shapeInner : 0.5f;
            // La forme intérieure suit l'angle du curseur pendant la phase 2
            // (comme la forme extérieure s'orientait au 2e clic) ; en phase 1,
            // elle reste alignée sur la forme extérieure.
            const float innerAng = drag_.shapeStage >= 2
                                       ? std::atan2(c.y - a.y, c.x - a.x)
                                       : ang;
            const int on = circleSides, inn = crownInnerSides;
            // Bandes de points (sans sommet de fermeture : indices modulo).
            std::vector<Vec2> oPts, iPts;
            oPts.reserve(on);
            iPts.reserve(inn);
            for (int i = 0; i < on; ++i) {
                const float aa = ang + (float)i * 2.0f * kPi / (float)on;
                oPts.push_back({a.x + std::cos(aa) * rad, a.y + std::sin(aa) * rad});
            }
            for (int i = 0; i < inn; ++i) {
                const float aa = innerAng + (float)i * 2.0f * kPi / (float)inn;
                iPts.push_back({a.x + std::cos(aa) * rad * hole,
                                a.y + std::sin(aa) * rad * hole});
            }
            // Remplissage : même triangulation « zipper » que addCrown.
            std::vector<int> band;
            triangulateBand(oPts, iPts, band);
            fill.clear();
            auto pt = [&](int ring, int idx) -> const Vec2& {
                return ring == 0 ? oPts[idx] : iPts[idx];
            };
            for (size_t k = 0; k + 5 < band.size(); k += 6) {
                fill.push_back(pt(band[k], band[k + 1]));
                fill.push_back(pt(band[k + 2], band[k + 3]));
                fill.push_back(pt(band[k + 4], band[k + 5]));
            }
            renderer.drawTriangles(fill, kPreviewFill);
            // Contours : périmètre extérieur puis pourtour du trou (orienté).
            rimPts(on, rad, ang, poly);
            drawPolygon(poly);
            rimPts(inn, rad * hole, innerAng, poly);
            drawPolygon(poly);
            return;
        }
        default: break;
    }
    renderer.drawTriangles(fill, kPreviewFill);
    drawPolygon(poly);
}

}  // namespace mesh
