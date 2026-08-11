#include "app.h"
#include "pngexport.h"
#include "triangulate.h"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>

namespace mesh {

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kBasePxPerUnit = 40.0f;   // zoom ×1 = 40 px/unité
constexpr float kMinZoomPx = 4.0f;        // ×0.1
constexpr float kMaxZoomPx = 400.0f;      // ×10

// Couleurs de la scène
const Color kBg{0.078f, 0.086f, 0.102f, 1.0f};
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
        // Scène de démonstration au premier lancement : plusieurs plans.
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
    }
    setStatus("Bienvenue ! Clic gauche : poser un point (3 clics = un triangle). "
              "Clic droit : déplacer la sélection. Molette : zoom.");
    logMsg("Meshes Designer démarré");
}

void App::shutdown() {
    saveAutoFile();
    savePrefsFile();
    renderer.shutdown();
}

void App::newDocument() {
    scene.clear();
    clearSelection();
    undoStack.clear();
    redoStack.clear();
    currentFile.clear();
    sceneName.clear();
    triP1 = triP2 = -1;
    dirty = false;
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
            char msg[192];
            std::snprintf(msg, sizeof(msg),
                          "Plan %d/%d%s — %d point(s), %d triangle(s) — "
                          "clic gauche : choisir ce plan · Échap / clic droit : sortir",
                          t + 1, scene.count(), t == scene.active ? " (actif)" : "",
                          (int)p.vertices.size(), p.triangleCount());
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

    // --- Molette : rotation globale (AltGr), côtés du cercle/anneau,
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
        } else if (isShapeTool(tool) &&
                   (tool == Tool::Circle || tool == Tool::Ring || tool == Tool::Star)) {
            circleSides = std::clamp(circleSides + (int)std::lround(io.MouseWheel), 3, 64);
        } else if (drag_.kind == DragKind::None && tool == Tool::Select &&
                   selectionVertices().size() >= 2) {
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

    // Survol (mode sommets) : sommet le plus proche d'abord, sinon le segment
    // le plus proche (il s'illumine : un clic y accrochera un nouveau sommet
    // pour former un triangle). Pendant un clic (drag Box), le segment de la
    // frame du clic est conservé : c'est lui que le relâchement utilisera.
    hoverVertex = -1;
    if (tool == Tool::Select && selMode == SelMode::Vertex) {
        if (drag_.kind == DragKind::None) {
            hoverVertex = pickVertex(mouseWorld, 8.0f);
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
            addEntityToSelection(mouseWorld);  // ajoute sans déplacer, jamais de doublon
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
                const int v = pickVertex(mouseWorld, 8.0f);
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
                const int fi = pickFace(mouseWorld);
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
                setStatus("1er clic posé — déplacez la souris, puis validez au 2e clic");
            } else if (drag_.kind == DragKind::Shape) {
                advanceShapeClick(mouseWorld);
            }
        } else if (drag_.kind == DragKind::None) {
            if (brushArmed) {
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
    renderer.clear(kBg);

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
    // (le plan d'indice le plus élevé recouvre les précédents).
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
            renderer.drawTriangles(triPts, f.hasColor ? f.color : kFaceFill);
        }
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

// ---------------------------------------------------------------------------
// Sélection & picking
// ---------------------------------------------------------------------------
void App::clearSelection() {
    selVerts.clear();
    selEdges.clear();
    selFaces.clear();
    hoverVertex = -1;
    hoverEdge = {-1, -1};
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
        const int v = pickVertex(world, 8.0f);
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
        const int fi = pickFace(world);
        if (fi >= 0) {
            const auto it = std::find(selFaces.begin(), selFaces.end(), fi);
            if (io.KeyShift) {
                if (it != selFaces.end()) selFaces.erase(it);
                else selFaces.push_back(fi);
            } else {
                clearSelection();
                selFaces.push_back(fi);
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
    if (selMode == SelMode::Vertex) {
        for (int i = 0; i < (int)scene.activePlane().vertices.size(); ++i)
            if (inBox(scene.activePlane().vertices[i])) selVerts.push_back(i);
    } else if (selMode == SelMode::Edge) {
        for (const auto& e : scene.activePlane().edges()) {
            const Vec2 mid = (scene.activePlane().vertices[e.first] + scene.activePlane().vertices[e.second]) * 0.5f;
            if (inBox(mid)) selEdges.push_back(e);
        }
    } else {
        for (int fi = 0; fi < (int)scene.activePlane().faces.size(); ++fi) {
            Vec2 c;
            for (int v : scene.activePlane().faces[fi].verts) c = c + scene.activePlane().vertices[v];
            c = c / (float)scene.activePlane().faces[fi].verts.size();
            if (inBox(c)) selFaces.push_back(fi);
        }
    }
    setStatus("Sélection rectangulaire (" + std::to_string(selectionCount()) + " élément(s))");
}

bool App::pickNearestOnly(const Vec2& world) {
    if (selMode == SelMode::Vertex) {
        const int v = pickVertex(world, 8.0f);
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

void App::addEntityToSelection(const Vec2& world) {
    if (selMode == SelMode::Vertex) {
        const int v = pickVertex(world, 8.0f);
        if (v < 0) return;
        if (std::find(selVerts.begin(), selVerts.end(), v) == selVerts.end())
            selVerts.push_back(v);
    } else if (selMode == SelMode::Edge) {
        const Mesh2D::Edge e = pickEdge(world, edgePickTol);
        if (e.first < 0) return;
        if (std::find(selEdges.begin(), selEdges.end(), e) == selEdges.end())
            selEdges.push_back(e);
    } else {
        const int fi = pickFace(world);
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
    drag_.kind = DragKind::None;
    drag_.shapeStage = 0;
    shapesOpen = false;
    setStatus("Forme « " + std::string(toolName(t)) +
              " » armée — 1er clic : ancre, puis déplacez la souris, 2e clic : valider");
    logMsg("Forme « " + std::string(toolName(t)) + " » armée");
}

void App::advanceShapeClick(const Vec2& world) {
    if (drag_.shapeStage == 1) {
        drag_.shapeCur = snappedPoint(world);
        if (tool == Tool::Star || tool == Tool::Ring) {
            const Vec2 d = drag_.shapeCur - drag_.shapeAnchor;
            drag_.shapeRadius = length(d);
            drag_.shapeAngle = std::atan2(d.y, d.x);
            drag_.shapeInner = 0.5f;
            drag_.shapeStage = 2;
            setStatus(tool == Tool::Star
                          ? "Étoile : 2e clic verrouille rayon et orientation — "
                            "déplacez pour la profondeur, 3e clic valide"
                          : "Anneau : 2e clic verrouille — déplacez pour la taille du trou, "
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
    // Anneau et étoile : le 2e clic VERROUILLE le rayon et l'orientation ; en
    // phase 3, le curseur ne règle plus que la taille du trou (anneau) ou la
    // profondeur (étoile) — le rayon créé doit rester celui verrouillé.
    const bool locked = drag_.shapeStage >= 2 && (tool == Tool::Ring || tool == Tool::Star);
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
    setStatus("Pinceau armé — clic gauche sur un triangle pour le peindre");
}

void App::paintFace(int fi) {
    Face& f = scene.activePlane().faces[fi];
    f.color = brushColor;
    f.color.a = brushOpacity;
    f.hasColor = true;
    setStatus("Triangle peint");
    logMsg("Triangle peint");
}

// ---------------------------------------------------------------------------
// Cibles, réticule, prévisualisation
// ---------------------------------------------------------------------------
void App::cycleTarget() {
    selMode = (SelMode)(((int)selMode + 1) % 3);
    clearSelection();
    setStatus(selMode == SelMode::Vertex   ? "Cible : sommet"
              : selMode == SelMode::Edge   ? "Cible : segment"
                                           : "Cible : triangle");
}

void App::cycleReticle() {
    reticle = (ReticleState)(((int)reticle + 1) % 3);
    setStatus(reticle == ReticleState::Off        ? "Réticule désactivé"
              : reticle == ReticleState::Simple   ? "Réticule simple"
                                                  : "Réticule symétrique");
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
    clearSelection();
    triP1 = triP2 = -1;
    setStatus("Plan " + std::to_string(i + 1) + " / " + std::to_string(scene.count()));
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
    scene.active = at;
    clearSelection();
    triP1 = triP2 = -1;
    setStatus("Plan dupliqué (n° " + std::to_string(at + 1) + "/" +
              std::to_string(scene.count()) + ") — Ctrl+Z pour annuler");
    logMsg("Plan dupliqué (n° " + std::to_string(at + 1) + ")");
}

void App::deletePlane() {
    if (scene.count() <= 1) return;
    pushUndo();
    scene.planes.erase(scene.planes.begin() + scene.active);
    if (scene.active >= scene.count()) scene.active = scene.count() - 1;
    clearSelection();
    triP1 = triP2 = -1;
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
        setStatus("Kiosque — déplacez la souris : le plan en avant est pré-sélectionné ; "
                  "clic gauche : choisir ; Échap ou clic droit : sortir");
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
}

void App::undo() {
    if (undoStack.empty()) return;
    redoStack.push_back(scene);
    scene = undoStack.back();
    undoStack.pop_back();
    if (redoStack.size() > kMaxUndo) redoStack.erase(redoStack.begin());
    clearSelection();
    triP1 = triP2 = -1;
    dirty = true;
    setStatus("Annulation (Ctrl+Z)");
}

void App::redo() {
    if (redoStack.empty()) return;
    undoStack.push_back(scene);
    scene = redoStack.back();
    redoStack.pop_back();
    clearSelection();
    triP1 = triP2 = -1;
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

void App::resetScene() {
    scene.clear();
    clearSelection();
    undoStack.clear();
    redoStack.clear();
    currentFile.clear();
    sceneName.clear();
    triP1 = triP2 = -1;
    dirty = false;
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
        undoStack.clear();
        redoStack.clear();
        dirty = false;
        cameraFramed = false;
        setStatus("Scène remplacée depuis « " + path + " »");
    } else {
        pushUndo();
        const int n0 = (int)snap.scene.planes.size();
        for (Mesh2D& p : snap.scene.planes) scene.planes.push_back(std::move(p));
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
    p.edgePickTol = edgePickTol;
    p.mergeRadius = mergeRadius;
    p.locations = saveLocations;
    p.importMode = dlgImportReplace ? 0 : 1;
    p.allColors = allColors;
    p.snapOn = snapOn;
    p.versions = versionFiles;
    p.consoleVisible = consoleVisible;
    p.consoleX = consolePos.x;
    p.consoleY = consolePos.y;
    p.consoleW = consoleSize.x;
    p.consoleH = consoleSize.y;
    savePrefsJson(p, prefsDir() + "prefs.json");
}

void App::loadPrefsFile() {
    PrefsData p;
    p.palette = palette;
    p.brushOpacity = brushOpacity;
    p.circleSides = circleSides;
    p.edgePickTol = edgePickTol;
    p.locations = saveLocations;
    const IoResult r = loadPrefsJson(p, prefsDir() + "prefs.json");
    if (!r.ok) return;
    if (!p.palette.empty()) palette = p.palette;
    brushOpacity = p.brushOpacity;
    circleSides = p.circleSides;
    edgePickTol = std::clamp(p.edgePickTol, 2.0f, 30.0f);
    mergeRadius = std::clamp(p.mergeRadius, 8, 64);
    saveLocations = p.locations;
    dlgImportReplace = (p.importMode == 0);
    consoleVisible = p.consoleVisible;
    consolePos = {p.consoleX, p.consoleY};
    consoleSize = {p.consoleW, p.consoleH};
    allColors = p.allColors;
    snapOn = p.snapOn;
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
    // Construction d'une forme prédéfinie (4.2) : guide la phase suivante.
    if (drag_.kind == DragKind::Shape) {
        const char* msg = nullptr;
        if (drag_.shapeStage == 1) {
            msg = "1er clic posé — déplacez la souris, puis validez au 2e clic";
        } else if (drag_.shapeStage >= 2) {
            msg = (tool == Tool::Star)
                      ? "Étoile : 2e clic verrouille rayon et orientation — "
                        "déplacez pour la profondeur, 3e clic valide"
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
    // Pinceau armé : la peinture d'une face a priorité sur le survol.
    const int face = brushArmed ? pickFace(mouseWorld) : -1;
    if (face >= 0) {
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
        msg = (pickFace(mouseWorld) >= 0)
                  ? "Sélectionner ce triangle — clic droit pour le déplacer"
                  : "Zone vide — clic gauche : rectangle de sélection · "
                    "clic droit : déplacer la sélection";
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
void App::drawPlane(const Mesh2D& p, bool isActive) {
    // Faces.
    if (isActive || allColors) {
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
}

// Aperçu du tracé de forme : la forme est VISIBLE (remplissage cyan
// translucide) avec son contour (périmètre, et pourtour du trou pour l'anneau)
// — sans les arêtes internes (rayons de l'éventail, parois du trou).
void App::drawShapeOutline() {
    const Vec2& a = drag_.shapeAnchor;
    const Vec2& c = drag_.shapeCur;
    const Vec2 d = c - a;
    // Comme dans completeShape : une fois verrouillé (2e clic), le rayon et
    // l'orientation de l'anneau/étoile ne bougent plus avec le curseur.
    const bool locked = drag_.shapeStage >= 2 && (tool == Tool::Ring || tool == Tool::Star);
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
        default: break;
    }
    renderer.drawTriangles(fill, kPreviewFill);
    drawPolygon(poly);
}

}  // namespace mesh
