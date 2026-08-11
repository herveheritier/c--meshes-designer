#include "ui.h"
#include "svgicon.h"
#include "triangulate.h"

#include <SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace mesh::ui {

namespace {

bool g_quit = false;

const ImVec4 kGreen(0.20f, 0.62f, 0.36f, 1.0f);
const ImVec4 kAmber(0.95f, 0.63f, 0.20f, 1.0f);
const ImVec4 kBlue(0.26f, 0.48f, 0.90f, 1.0f);
const ImVec4 kRed(0.82f, 0.32f, 0.30f, 1.0f);
const ImU32 kTextCol = IM_COL32(190, 200, 220, 220);
const ImU32 kDimCol = IM_COL32(150, 160, 180, 160);
// Rembourrage des boutons à icônes : cadre plus haut et plus large pour une
// meilleure lisibilité (la hauteur du bouton = police + 2 × FramePadding.y).
const ImVec2 kBtnFramePad(7.0f, 5.0f);

// Hauteur d'un bouton à icône (police + rembourrage vertical du cadre).
float btnFrameHeight() { return ImGui::GetFontSize() + 2.0f * kBtnFramePad.y; }

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
const char* selModeName(SelMode m) {
    switch (m) {
        case SelMode::Vertex: return "Sommet";
        case SelMode::Edge: return "Segment";
        case SelMode::Face: return "Triangle";
    }
    return "?";
}

std::string zoomText(float mult) {
    char buf[32];
    if (std::fabs(mult - std::round(mult)) < 0.01f)
        std::snprintf(buf, sizeof(buf), "%.0fx", mult);
    else
        std::snprintf(buf, sizeof(buf), "%.1fx", mult);
    return buf;
}

// Bouton à icône avec état, infobulle et libellé optionnel. L'icône est un
// SVG du dossier assets/ (repli sur le texte si l'icône est introuvable).
// `width` > 0 impose une largeur fixe (contenu centré, ex. dialogues).
// Icône de 18 px et cadre haut (kBtnFramePad) pour la lisibilité.
bool toolBtnIcon(const char* icon, const char* tip, bool active,
                 const ImVec4& activeCol, bool disabled,
                 const char* text = nullptr, float width = 0.0f) {
    const bool hasText = text && text[0];
    const float iconSize = std::max(18.0f, ImGui::GetFontSize() - 1.0f);
    const ImVec2 ts = hasText ? ImGui::CalcTextSize(text) : ImVec2(0, 0);
    ImVec2 size;
    if (width > 0.0f)
        size = ImVec2(width, 0.0f);
    else if (hasText)
        size = ImVec2(ts.x + iconSize + 22.0f, 0.0f);
    else
        size = ImVec2(iconSize + 14.0f, 0.0f);
    if (disabled) ImGui::BeginDisabled();
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, activeCol);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              ImVec4(activeCol.x * 1.2f, activeCol.y * 1.2f,
                                     activeCol.z * 1.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, activeCol);
    }
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, kBtnFramePad);
    ImGui::PushID(icon);
    const bool clicked = ImGui::Button("##tb", size);
    ImGui::PopID();
    ImGui::PopStyleVar();
    if (active) ImGui::PopStyleColor(3);

    // Couleur de l'icône selon l'état (le fond du bouton gère déjà le survol).
    const bool hovered = ImGui::IsItemHovered();
    ImU32 col;
    if (disabled) col = IM_COL32(130, 142, 162, 100);
    else if (hovered) col = IM_COL32(255, 255, 255, 255);
    else if (active) col = IM_COL32(255, 255, 255, 240);
    else col = IM_COL32(198, 208, 226, 215);

    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float cy = (min.y + max.y) * 0.5f;
    // Départ du contenu : centré si largeur fixe, sinon collé à gauche.
    const float contentW = hasText ? iconSize + 8.0f + ts.x : iconSize;
    const float x0 = width > 0.0f
                         ? min.x + (max.x - min.x - contentW) * 0.5f
                         : (hasText ? min.x + 8.0f
                                    : (min.x + max.x - iconSize) * 0.5f);
    const bool drawn =
        drawSvgIconNamed(dl, icon, ImVec2(x0, cy - iconSize * 0.5f), iconSize,
                         col);
    if (hasText) {
        if (drawn)
            dl->AddText(ImVec2(x0 + iconSize + 8.0f, cy - ts.y * 0.5f), col,
                        text);
        else  // repli : le texte seul, centré
            dl->AddText(ImVec2((min.x + max.x - ts.x) * 0.5f,
                               (min.y + max.y - ts.y) * 0.5f),
                        col, text);
    } else if (!drawn) {  // repli : glyphe discret si l'icône est introuvable
        const float fs = ImGui::GetFontSize();
        dl->AddText(ImVec2((min.x + max.x - fs) * 0.5f, cy - fs * 0.5f), col,
                    "?");
    }
    if (disabled) ImGui::EndDisabled();
    if (tip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", tip);
    return clicked && !disabled;
}

// Pilule verte (ou ambre) lisible avec un compteur, icône facultative.
// Hauteur alignée sur les boutons à icônes (kBtnFramePad.y) pour la lisibilité.
// `minW` impose une largeur minimale : les compteurs (fps, plan, historique…)
// ne font plus bouger la mise en page quand leur valeur change.
void pill(const char* id, const char* text, const ImVec4& bg,
          const char* icon = nullptr, float minW = 0.0f) {
    const float ico = 14.0f;
    const bool hasIcon = icon != nullptr;
    const ImVec2 tsize = ImGui::CalcTextSize(text);
    const float w =
        std::max(tsize.x + 18.0f + (hasIcon ? ico + 6.0f : 0.0f), minW);
    const float h = btnFrameHeight();
    const ImVec2 size(w, h);
    ImGui::InvisibleButton(id, size);
    const ImVec2 min = ImGui::GetItemRectMin();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(min, ImVec2(min.x + size.x, min.y + size.y), ImColor(bg), 5.0f);
    float tx = min.x + 9.0f;
    const float cy = min.y + (size.y - tsize.y) * 0.5f;
    if (hasIcon) {
        if (drawSvgIconNamed(dl, icon,
                             ImVec2(min.x + 9.0f, min.y + (size.y - ico) * 0.5f),
                             ico, IM_COL32(255, 255, 255, 240)))
            tx += ico + 6.0f;
    }
    dl->AddText(ImVec2(tx, cy), IM_COL32(255, 255, 255, 240), text);
}

// Libellé de valeur à largeur FIXE (zoom, pas de grille, nombre de côtés,
// rayon de fusion…) : la largeur du texte ne fait plus bouger la barre
// d'outils quand la valeur change (texte centré dans la cellule réservée).
void valueLabel(const char* text, float width) {
    ImGui::Dummy(ImVec2(width, btnFrameHeight()));
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 ts = ImGui::CalcTextSize(text);
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(min.x + (width - ts.x) * 0.5f,
               min.y + (btnFrameHeight() - ts.y) * 0.5f),
        kDimCol, text);
}

// Séparateur vertical entre groupes de la barre d'outils.
void groupSep() {
    ImGui::SameLine(0, 7.0f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, p.y),
                                        ImVec2(p.x, p.y + btnFrameHeight()),
                                        IM_COL32(255, 255, 255, 55));
    ImGui::Dummy(ImVec2(2.0f, 0.0f));
    ImGui::SameLine(0, 7.0f);
}

void drawText(ImDrawList* dl, float x, float y, const char* text, ImU32 col = kTextCol) {
    dl->AddText(ImVec2(x, y), col, text);
}

// Déclarations anticipées (définies plus bas, utilisées par le viewport et la
// barre d'outils).
void drawPlaneCard(App& app, ImDrawList* dl, int pi, const ImVec2& tl, const ImVec2& br,
                   float squash, float alpha, bool front);
void kioskOverlay(App& app, ImDrawList* dl, const ImVec2& pos, const ImVec2& size);
void shapesMenu(App& app);

// ---------------------------------------------------------------------------
// Raccourcis (spec ch. 15)
// ---------------------------------------------------------------------------
void handleShortcuts(App& app) {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;
    // Ne jamais éditer la scène derrière une fenêtre modale.
    if (app.dlgSaveOpen || app.dlgImportOpen || app.dlgResetOpen ||
        app.dlgDeletePlaneOpen || app.dlgRotateOpen || app.dlgScaleOpen ||
        app.dlgPngOpen || app.dlgSvgOpen || app.dlgVersionsOpen)
        return;

    // En mode kiosque : seuls la sortie et le bouton du mode sont disponibles.
    if (app.kiosk) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) app.onEscape();
        if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_K)) {
            app.kiosk = false;
            app.setStatus("Kiosque : aucun changement");
        }
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        if (app.dlgHelpOpen) app.dlgHelpOpen = false;
        else app.onEscape();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Slash)) app.dlgHelpOpen = !app.dlgHelpOpen;

    if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
        if (app.isShapeTracing()) app.cancelShapeTrace();
        else app.deleteSelection();
    }
    if (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
        app.dlgResetOpen = true;
        return;
    }

    if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z)) app.undo();
    if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z)) app.redo();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) app.redo();
    if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_C)) app.copySelection();
    if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_X)) app.cutSelection();
    if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_V)) app.pasteClipboard();
    if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S)) {
        if (app.preview != PreviewMode::Off) app.exitPreview();
        app.dlgSaveOpen = true;
        if (app.sceneName.empty()) {
            if (!app.saveLocations.empty())
                std::snprintf(app.dlgSaveName, sizeof(app.dlgSaveName), "%s",
                              app.saveLocations.front().c_str());
            else
                app.dlgSaveName[0] = '\0';
        } else {
            std::snprintf(app.dlgSaveName, sizeof(app.dlgSaveName), "%s",
                          app.sceneName.c_str());
        }
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_0)) {
        app.camera.zoom = 40.0f;
        app.camera.cx = 0.0f;
        app.camera.cy = 0.0f;
        app.rotDeg = 0.0f;
        app.setStatus("Zoom 100 % recentré sur l'origine, angle de rotation remis à zéro (Ctrl+0)");
    }

    // --- Sélection ---
    if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_D))
        app.duplicateSelection();
    if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_A))
        app.selectAll();
    if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_I))
        app.invertSelection();

    // Tout afficher : zoom automatique sur la scène entière (Accueil).
    if (ImGui::IsKeyPressed(ImGuiKey_Home)) {
        app.frameView();
        app.setStatus("Tout afficher : zoom automatique sur la scène entière (Accueil)");
    }
    // Cadrer la sélection : zoom automatique sur la sélection courante (Ctrl+F).
    if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_F)) {
        app.frameSelection();
        app.setStatus("Cadrer la sélection : zoom automatique (Ctrl+F)");
    }
    // Miroir de la sélection : M = X, Maj+M = Y (autour du 1er point choisi).
    if (!io.KeyCtrl && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_M)) {
        if (io.KeyShift) app.mirrorSelectionY();
        else app.mirrorSelectionX();
    }
    // Outil mesure : distance entre deux points (Ctrl+M).
    if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_M))
        app.toggleMeasure();
    // Dupliquer le plan actif (Alt+D).
    if (io.KeyAlt && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_D))
        app.duplicatePlane();

    // G : afficher/masquer la grille · Maj+G : aimantation seule (indépendante
    // de l'affichage). NB : Maj+G produit la même touche physique que G — il ne
    // faut PAS laisser un gestionnaire « G seul » se déclencher en plus.
    if (ImGui::IsKeyPressed(ImGuiKey_G)) {
        if (io.KeyShift && !io.KeyCtrl && !io.KeyAlt) {
            app.snapOn = !app.snapOn;
            app.setStatus(app.snapOn ? "Aimantation activée (Maj+G)"
                                     : "Aimantation désactivée (Maj+G)");
        } else if (!io.KeyCtrl && !io.KeyAlt) {
            app.gridOn = !app.gridOn;
            app.setStatus(app.gridOn ? "Grille affichée (G)" : "Grille masquée (G)");
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Y)) app.cycleReticle();  // Y : R est pris par le rectangle
    if (ImGui::IsKeyPressed(ImGuiKey_F)) {
        app.showRedraw = !app.showRedraw;
        app.setStatus(app.showRedraw ? "Compteur de redessins affiché (F)"
                                     : "Compteur de redessins masqué (F)");
    }
    if (ImGui::IsKeyPressed(ImGuiKey_P)) app.cyclePreview();

    // Raccourcis des formes : une touche dédiée par forme prédéfinie (4.2).
    // Ctrl/Alt exclus (Ctrl+C copie, Alt+R ouvre la rotation précise…).
    auto toggleShape = [&](Tool t, ImGuiKey key, const char* name) {
        if (!io.KeyCtrl && !io.KeyAlt && ImGui::IsKeyPressed(key)) {
            if (app.tool == t) {
                app.cancelShapeTrace();  // abandonne un éventuel tracé en cours
                app.tool = Tool::Select;
                app.setStatus(std::string("Forme « ") + name + " » désarmée");
            } else {
                app.startShapeTool(t);
            }
        }
    };
    toggleShape(Tool::Circle, ImGuiKey_C, "cercle");
    toggleShape(Tool::Rectangle, ImGuiKey_R, "rectangle");
    toggleShape(Tool::Triangle, ImGuiKey_T, "triangle");
    toggleShape(Tool::Square, ImGuiKey_Q, "carré");
    toggleShape(Tool::Pentagon, ImGuiKey_N, "pentagone");
    toggleShape(Tool::Hexagon, ImGuiKey_H, "hexagone");
    toggleShape(Tool::Star, ImGuiKey_E, "étoile");
    toggleShape(Tool::Ring, ImGuiKey_A, "anneau");

    // Rotation précise : saisie d'un angle exact (Alt+R).
    if (io.KeyAlt && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_R))
        app.dlgRotateOpen = true;
    // Mise à l'échelle précise : saisie d'un facteur (Alt+S).
    if (io.KeyAlt && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S))
        app.dlgScaleOpen = true;

    if (io.KeyAlt && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
        app.toVertexSelection();
        app.alignX();
    }
    if (io.KeyAlt && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
        app.toVertexSelection();
        app.alignY();
    }
    if (io.KeyAlt && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
        app.toVertexSelection();
        app.distributeX();
    }
    if (io.KeyAlt && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
        app.toVertexSelection();
        app.distributeY();
    }

    // Plans : ordre d'empilement (7.2) et kiosque (7.5).
    if (io.KeyAlt && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_UpArrow))
        app.planeUp();
    if (io.KeyAlt && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_DownArrow))
        app.planeDown();
    if (io.KeyAlt && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_K))
        app.toggleKiosk();
}

// ---------------------------------------------------------------------------
// Barre d'outils flottante (spec 3.2)
// ---------------------------------------------------------------------------
void toolbar(App& app) {
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(8, 8), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.11f, 0.14f, 0.92f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::Begin("##toolbar", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_AlwaysAutoResize);

    // --- Groupe 1 : canevas / édition ---
    if (toolBtnIcon("grid",
                    "Grille (G) — clic gauche : afficher/masquer · molette : ajuster "
                    "le pas · clic du milieu : réinitialiser · clic droit : "
                    "aimantation on/off (Maj+G)",
                    app.gridOn, kGreen, false))
        app.gridOn = !app.gridOn;
    if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f) {
        app.gridStep = std::clamp(app.gridStep * std::pow(1.25f, io.MouseWheel),
                                  0.01f, 100.0f);
        app.setStatus("Pas de grille : " + std::to_string(app.gridStep));
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Middle)) {
        app.gridStep = 1.0f;
        app.setStatus("Pas de grille réinitialisé (1.0)");
    }
    // Clic droit sur le bouton Grille : aimantation on/off (sans toucher à
    // l'affichage) — accessible là où l'utilisateur cherche.
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        app.snapOn = !app.snapOn;
        app.setStatus(app.snapOn ? "Aimantation activée (clic droit — Maj+G)"
                                 : "Aimantation désactivée (clic droit — Maj+G)");
    }
    // Badge « aimant barré » rouge sur le coin du bouton Grille quand
    // l'aimantation est désactivée (même technique que le cadenas de fusion).
    if (!app.snapOn) {
        const ImVec2 bmin = ImGui::GetItemRectMin();
        const ImVec2 bmax = ImGui::GetItemRectMax();
        drawSvgIconNamed(ImGui::GetWindowDrawList(), "magnet-off",
                         ImVec2(bmax.x - 12.0f, bmin.y + 1.0f), 11.0f,
                         IM_COL32(255, 130, 120, 255));
    }
    ImGui::SameLine();
    // Cellule du pas de grille à largeur FIXE : sert aussi d'INDICATEUR
    // d'aimantation. Active : icône « aimant » verte + valeur normale.
    // Désactivée : icône « aimant barré » rouge + valeur en ambre — sans
    // décaler la barre ni déplacer le texte d'un état à l'autre.
    {
        char stepbuf[16];
        std::snprintf(stepbuf, sizeof(stepbuf), "%.2f", app.gridStep);
        const float cellW = ImGui::CalcTextSize("100.00").x + 16.0f;  // réserve l'icône
        ImGui::Dummy(ImVec2(cellW, btnFrameHeight()));
        const ImVec2 cmin = ImGui::GetItemRectMin();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 ts = ImGui::CalcTextSize(stepbuf);
        const float cy = cmin.y + btnFrameHeight() * 0.5f;
        const float iconW = 16.0f;
        float cx = cmin.x + (cellW - ts.x - iconW) * 0.5f;
        drawSvgIconNamed(dl, app.snapOn ? "magnet-on" : "magnet-off",
                         ImVec2(cx, cy - 7.0f), 14.0f,
                         app.snapOn ? IM_COL32(90, 190, 120, 255)
                                    : IM_COL32(245, 115, 105, 250));
        cx += iconW;
        dl->AddText(ImVec2(cx, cy - ts.y * 0.5f),
                    app.snapOn ? kDimCol : IM_COL32(245, 190, 90, 245), stepbuf);
    }
    ImGui::SameLine();
    if (toolBtnIcon("reticle", "Réticule (Y) : désactivé / simple / symétrique",
                    app.reticle != ReticleState::Off, kGreen, false))
        app.cycleReticle();

    ImGui::SameLine();
    if (toolBtnIcon("preview", "Prévisualiser (P) : aperçu simple → tous les plans → édition",
                    app.preview != PreviewMode::Off, kAmber, false))
        app.cyclePreview();

    ImGui::SameLine();
    if (toolBtnIcon("show-all-fills", "Toutes couleurs : remplir tous les plans pendant l'édition (7.6)",
                    app.allColors, kGreen, false))
        app.allColors = !app.allColors;

    ImGui::SameLine();
    if (toolBtnIcon("fit-view",
                    "Tout afficher (Accueil) : zoom automatique sur la scène entière",
                    false, kGreen, false))
        app.frameView();

    ImGui::SameLine();
    if (toolBtnIcon("fit-selection",
                    "Cadrer la sélection (Ctrl+F) : zoom automatique sur la "
                    "sélection courante",
                    false, kGreen, false))
        app.frameSelection();

    ImGui::SameLine();
    if (toolBtnIcon("measure",
                    app.measureActive
                        ? "Outil mesure armé — 2 clics : distance affichée au HUD "
                          "(Ctrl+M pour désarmer)"
                        : "Outil mesure (Ctrl+M) : distance entre deux points",
                    app.measureActive, kGreen, false))
        app.toggleMeasure();

    ImGui::SameLine();
    char fpsbuf[32];
    std::snprintf(fpsbuf, sizeof(fpsbuf), "%.0f fps", app.fps);
    pill("##pillfps", fpsbuf, app.fpsPillGreen ? kGreen : kAmber, "fps",
         ImGui::CalcTextSize("120 fps").x + 18.0f + 20.0f);

    ImGui::SameLine();
    const char* targetLabel = selModeName(app.selMode);
    if (toolBtnIcon("selection-mode", "Cible d'édition : sommet / segment / triangle",
                    false, kGreen, false, targetLabel))
        app.cycleTarget();

    ImGui::SameLine();
    if (toolBtnIcon("copy", "Copier (Ctrl+C) — points + triangles entièrement contenus",
                    false, kGreen, app.selectionCount() == 0))
        app.copySelection();
    ImGui::SameLine();
    if (toolBtnIcon("cut", "Couper (Ctrl+X)", false, kGreen, app.selectionCount() == 0))
        app.cutSelection();
    ImGui::SameLine();
    if (toolBtnIcon("paste", "Coller (Ctrl+V) — chaque collage décale d'un demi-pas de grille",
                    false, kGreen, !app.hasClip))
        app.pasteClipboard();
    ImGui::SameLine();
    if (toolBtnIcon("duplicate",
                    "Dupliquer la sélection (Ctrl+D) — copie légèrement décalée, "
                    "prête à déplacer",
                    false, kGreen, app.selectionCount() == 0))
        app.duplicateSelection();

    ImGui::SameLine();
    if (toolBtnIcon("triangle-color", "Peinture : palette de couleurs et pinceau",
                    app.paletteOpen, kGreen, false))
        app.paletteOpen = !app.paletteOpen;

    ImGui::SameLine();
    if (toolBtnIcon("align", "Aligner / répartir la sélection", app.alignOpen, kGreen,
                    false))
        app.alignOpen = !app.alignOpen;

    ImGui::SameLine();
    if (toolBtnIcon("rotate", "Rotation précise (Alt+R) : saisir un angle exact",
                    app.dlgRotateOpen, kGreen, false))
        app.dlgRotateOpen = !app.dlgRotateOpen;

    ImGui::SameLine();
    if (toolBtnIcon("scale", "Mise à l'échelle précise (Alt+S) : saisir un facteur",
                    app.dlgScaleOpen, kGreen, false))
        app.dlgScaleOpen = !app.dlgScaleOpen;

    ImGui::SameLine();
    if (toolBtnIcon("shapes",
                    "Formes prédéfinies — clic : menu contextuel "
                    "(cercle, carré, étoile, anneau…) · molette : côtés/pointes "
                    "si une forme à côtés est armée",
                    app.isShapeArmed(), kGreen, false))
        ImGui::OpenPopup("##shapesmenu");
    // Molette sur le bouton : règle les côtés/pointes si la forme armée en a.
    if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f &&
        (app.tool == Tool::Circle || app.tool == Tool::Ring || app.tool == Tool::Star)) {
        app.circleSides =
            std::clamp(app.circleSides + (int)std::lround(io.MouseWheel), 3, 64);
        app.setStatus("Nombre de " +
                      std::string(app.tool == Tool::Star ? "pointes de l'étoile"
                                                        : "côtés du " +
                                                              std::string(app.tool == Tool::Circle
                                                                              ? "cercle"
                                                                              : "anneau")) +
                      " : " + std::to_string(app.circleSides));
    }
    shapesMenu(app);

    ImGui::SameLine();
    if (toolBtnIcon("reset", "Réinitialiser entièrement la scène (Maj+Retour arrière)",
                    false, kRed, false))
        app.dlgResetOpen = true;

    ImGui::SameLine();
    // Bouton dédié à la sélection : clic gauche = tout sélectionner (Ctrl+A) ;
    // clic droit = menu contextuel (tout sélectionner / inverser la sélection).
    if (toolBtnIcon("select-all",
                    "Sélection — clic gauche : tout sélectionner (Ctrl+A) · "
                    "clic droit : menu (tout / inverser)",
                    false, kGreen, false))
        app.selectAll();
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) ImGui::OpenPopup("##selmenu");
    // Les choix contextuels s'affichent sous forme de BOUTONS (visibles et
    // larges), pas de simples entrées de menu.
    if (ImGui::BeginPopup("##selmenu")) {
        ImGui::TextDisabled("Actions sur la sélection (selon la cible) :");
        if (toolBtnIcon("select-all", "Tout sélectionner (Ctrl+A)", false, kGreen,
                        false, "Tout sélectionner", 190.0f)) {
            app.selectAll();
            ImGui::CloseCurrentPopup();
        }
        if (toolBtnIcon("select-all", "Inverser la sélection (Ctrl+I)", false, kGreen,
                        false, "Inverser la sélection", 190.0f)) {
            app.invertSelection();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (app.selectionCount() > 0)
        pill("##pillsel", std::to_string(app.selectionCount()).c_str(), kGreen,
             nullptr, ImGui::CalcTextSize("9999").x + 18.0f);

    // --- Fusion des points (5.5 / 5.6) ---
    const bool mergeArmed = app.mergeMode != App::MergeMode::Off;
    const bool mergeCanArm = app.selMode == SelMode::Vertex && app.selVerts.size() == 1;
    const bool mergeCanGroup = app.selMode == SelMode::Vertex && app.selVerts.size() >= 2;
    if (toolBtnIcon(
            "merge-points",
            mergeArmed
                ? "Fusion par déplacement armée — glissez le point sélectionné près "
                  "d'un autre point pour les fusionner · molette : rayon (8-64 px) · "
                  "re-clic : verrouiller puis désarmer"
                : "Fusionner les points superposés (5.5, anneau orange) · avec 1 point "
                  "sélectionné : armer la fusion par déplacement (5.6)",
            mergeArmed, kGreen, !(mergeArmed || mergeCanArm || mergeCanGroup)))
        app.toggleMergeMode();
    // Cadenas sur le coin du bouton quand le mode est verrouillé (5.6).
    if (app.mergeMode == App::MergeMode::Locked) {
        const ImVec2 bmin = ImGui::GetItemRectMin();
        const ImVec2 bmax = ImGui::GetItemRectMax();
        drawSvgIconNamed(ImGui::GetWindowDrawList(), "merge-lock",
                         ImVec2(bmax.x - 13.0f, bmin.y + 1.0f), 11.0f,
                         IM_COL32(255, 255, 255, 235));
    }
    // Molette sur le bouton : rayon de fusion (8 à 64 px écran, indépendant du zoom).
    if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f) {
        // lround : un cran de molette vaut ±1 (les roues à défilement lisse
        // envoient des fractions ; l'arrondi évite un réglage muet).
        app.mergeRadius =
            std::clamp(app.mergeRadius + (int)std::lround(io.MouseWheel) * 2, 8, 64);
        app.setStatus("Rayon de fusion : " + std::to_string(app.mergeRadius) + " px");
    }
    // Le rayon s'affiche à côté du bouton tant que le mode est armé (5.6),
    // à largeur FIXE pour ne pas décaler la barre quand la valeur change.
    if (mergeArmed) {
        ImGui::SameLine();
        char radbuf[8];
        std::snprintf(radbuf, sizeof(radbuf), "%d", app.mergeRadius);
        valueLabel(radbuf, ImGui::CalcTextSize("64").x);
    }

    // --- Groupe 2 : annuler / rétablir (avec compteur) ---
    groupSep();
    if (toolBtnIcon("undo", "Annuler (Ctrl+Z)", false, kGreen, app.undoStack.empty()))
        app.undo();
    ImGui::SameLine();
    pill("##pillundo", std::to_string(app.undoStack.size()).c_str(), kGreen, nullptr,
         ImGui::CalcTextSize("50").x + 18.0f);
    ImGui::SameLine();
    if (toolBtnIcon("redo", "Rétablir (Ctrl+Maj+Z ou Ctrl+Y)", false, kGreen,
                    app.redoStack.empty()))
        app.redo();
    ImGui::SameLine();
    pill("##pillredo", std::to_string(app.redoStack.size()).c_str(), kGreen, nullptr,
         ImGui::CalcTextSize("50").x + 18.0f);

    // --- Ligne 2 : simple retour à la ligne. L'ancien code sautait de
    // `GetCursorPosY() + rowH`, mais le curseur inclut déjà l'espacement :
    // cela créait un grand espace vide entre les deux lignes. ---

    // --- Groupe 3 : sauvegarde ---
    if (toolBtnIcon("export", "Enregistrer la scène (Ctrl+S) — fenêtre d'emplacement",
                    false, kGreen, false)) {
        app.dlgSaveOpen = true;
        if (app.sceneName.empty()) {
            if (!app.saveLocations.empty())
                std::snprintf(app.dlgSaveName, sizeof(app.dlgSaveName), "%s",
                              app.saveLocations.front().c_str());
            else
                app.dlgSaveName[0] = '\0';
        } else {
            std::snprintf(app.dlgSaveName, sizeof(app.dlgSaveName), "%s",
                          app.sceneName.c_str());
        }
    }
    ImGui::SameLine();
    if (toolBtnIcon("export-svg", "Exporter le plan actif en SVG vectoriel",
                    false, kGreen, false))
        app.dlgSvgOpen = true;
    ImGui::SameLine();
    if (toolBtnIcon("history", "Historique : versions horodatées de l'autosave "
                                "(restaurer un état antérieur)",
                    false, kGreen, app.versionFiles.empty()))
        app.dlgVersionsOpen = true;

    // --- Groupe 4 : entrées ---
    groupSep();
    if (toolBtnIcon("import-meshes", "Charger un fichier au format texte « meshes »",
                    false, kBlue, false))
        app.openImportDialog(0);
    ImGui::SameLine();
    if (toolBtnIcon("import-json", "Charger un fichier de scène JSON (ou glisser-déposer)",
                    false, kBlue, false))
        app.openImportDialog(1);
    ImGui::SameLine();
    if (toolBtnIcon("import-obj", "Charger un fichier OBJ (v/f — les faces sont triangulées)",
                    false, kBlue, false))
        app.openImportDialog(2);

    // --- Groupe 5 : navigation entre plans (7) ---
    groupSep();
    int n = app.scene.count();
    bool canNav = n >= 2;
    if (toolBtnIcon("prev-shape", "Plan précédent (i-1)", false, kGreen, !canNav))
        app.prevPlane();
    ImGui::SameLine();
    if (toolBtnIcon("next-shape", "Plan suivant (i+1)", false, kGreen, !canNav))
        app.nextPlane();
    ImGui::SameLine();
    char planbuf[24];
    std::snprintf(planbuf, sizeof(planbuf), "%d/%d", app.scene.active + 1, n);
    pill("##pillplan", planbuf, kGreen, nullptr, ImGui::CalcTextSize("12/12").x + 18.0f);
    ImGui::SameLine();
    if (toolBtnIcon("duplicate-plane", "Dupliquer le plan actif (Alt+D) — copie complète "
                                        "avec ses couleurs, insérée juste au-dessus",
                    false, kGreen, n < 1))
        app.duplicatePlane();
    ImGui::SameLine();
    if (toolBtnIcon("move-shape-up", "Monter le plan actif (Alt+Flèche haut) — il recouvre davantage",
                    false, kGreen, app.scene.active >= n - 1))
        app.planeUp();
    ImGui::SameLine();
    if (toolBtnIcon("move-shape-down", "Descendre le plan actif (Alt+Flèche bas)", false,
                    kGreen, app.scene.active <= 0))
        app.planeDown();
    ImGui::SameLine();
    const bool plusClicked = toolBtnIcon(
        "new-shape", "Ajouter un plan vide — clic gauche : avant le plan courant ; "
                     "clic droit : après",
        false, kGreen, false);
    if (plusClicked) app.addPlane(false);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) app.addPlane(true);
    n = app.scene.count();  // rafraîchit après un ajout éventuel (états ×/K à jour)
    canNav = n >= 2;
    ImGui::SameLine();
    if (toolBtnIcon("delete-shape", "Supprimer le plan actif (confirmation)", false,
                    kRed, n <= 1))
        app.dlgDeletePlaneOpen = true;
    ImGui::SameLine();
    if (toolBtnIcon("kiosk", "Kiosque : choisir le plan en couverture (Alt+K)", app.kiosk,
                    kGreen, !canNav))
        app.toggleKiosk();

    // --- Groupe 6 : console ---
    groupSep();
    if (toolBtnIcon("console", "Afficher / masquer la console de messages",
                    app.consoleVisible, kGreen, false))
        app.consoleVisible = !app.consoleVisible;

    // --- Groupe 7 : aide ---
    ImGui::SameLine();
    if (toolBtnIcon("help", "Fenêtre d'aide et raccourcis (?)", app.dlgHelpOpen, kGreen,
                    false))
        app.dlgHelpOpen = !app.dlgHelpOpen;

    // --- Groupe 8 : réglages ---
    ImGui::SameLine();
    if (toolBtnIcon("settings", "Réglages : distance de détection des segments",
                    app.settingsOpen, kGreen, false))
        app.settingsOpen = !app.settingsOpen;

    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------------------
// HUD bas-gauche + toast (spec 3.3 / 13)
// ---------------------------------------------------------------------------
void hud(App& app, ImDrawList* dl, const ImVec2& size) {
    const ImGuiIO& io = ImGui::GetIO();
    const Vec2 w = app.camera.screenToWorld(
        {io.MousePos.x - app.viewportPos.x, io.MousePos.y - app.viewportPos.y},
        app.viewportVec2());
    char buf[256];

    float y = size.y - 8.0f;
    std::snprintf(buf, sizeof(buf), "%s   pos(%d, %d)",
                  zoomText(app.zoomMult()).c_str(), (int)std::round(w.x),
                  (int)std::round(w.y));
    drawText(dl, 10.0f, y - 18.0f, buf);
    y -= 18.0f;

    // Angle de rotation cumulé (spec 3.3 : « rot X° » après une rotation).
    if (app.rotDeg != 0.0f) {
        std::snprintf(buf, sizeof(buf), "rot %d°", (int)std::lround(app.rotDeg));
        drawText(dl, 10.0f, y - 18.0f, buf, kDimCol);
        y -= 18.0f;
    }

    if (app.showRedraw) {
        std::snprintf(buf, sizeof(buf), "%d redessins/s", app.redraws);
        drawText(dl, 10.0f, y - 18.0f, buf, kDimCol);
        y -= 18.0f;
    }

    std::snprintf(buf, sizeof(buf), "Scène : %s%s",
                  app.sceneName.empty() ? "sans nom" : app.sceneName.c_str(),
                  app.dirty ? " *" : "");
    drawText(dl, 10.0f, y - 18.0f, buf, kDimCol);
    y -= 18.0f;

    // Statistiques du plan actif : sommets, triangles et aire totale.
    {
        const Mesh2D& m = app.scene.activePlane();
        std::snprintf(buf, sizeof(buf),
                      "Plan %d/%d : %d sommets · %d triangles · aire %.2f",
                      app.scene.active + 1, app.scene.count(), (int)m.vertices.size(),
                      m.triangleCount(), app.activePlaneArea());
        drawText(dl, 10.0f, y - 18.0f, buf, kDimCol);
        y -= 18.0f;
    }

    // Outil mesure : distance entre les deux points posés (ou aperçu en cours,
    // aimanté comme le tracé pour rester cohérent avec le segment dessiné).
    if (app.measureActive) {
        const bool complete = app.measureHasB && !app.measureHasA;
        if (complete || app.measureHasA) {
            const Vec2 mw = app.camera.screenToWorld(
                {io.MousePos.x - app.viewportPos.x, io.MousePos.y - app.viewportPos.y},
                app.viewportVec2());
            Vec2 end = app.snapOn
                           ? Vec2{std::round(mw.x / app.gridStep) * app.gridStep,
                                  std::round(mw.y / app.gridStep) * app.gridStep}
                           : mw;
            if (complete) end = app.measureB;
            std::snprintf(buf, sizeof(buf), "mesure : %.2f unités",
                          distance(app.measureA, end));
            drawText(dl, 10.0f, y - 18.0f, buf, kDimCol);
            y -= 18.0f;
        }
    }

    // Toast contextuel (aide prospective) au-dessus du HUD.
    if (app.toastAge > 0.0f && !app.toast.empty()) {
        const ImVec2 tsize = ImGui::CalcTextSize(app.toast.c_str());
        const ImVec2 boxMin(10.0f, y - tsize.y - 16.0f);
        const ImVec2 boxMax(boxMin.x + tsize.x + 16.0f, boxMin.y + tsize.y + 12.0f);
        dl->AddRectFilled(boxMin, boxMax, IM_COL32(20, 26, 34, 215), 5.0f);
        dl->AddRect(boxMin, boxMax, IM_COL32(90, 160, 255, 120), 5.0f);
        dl->AddText(ImVec2(boxMin.x + 8.0f, boxMin.y + 6.0f),
                    IM_COL32(220, 230, 245, 240), app.toast.c_str());
    }
}

// HUD du kiosque : zoom + position du curseur, angle de rotation cumulé,
// compteur de redessins et état de la scène — texte agrandi pour rester
// lisible sur les grands écrans (le HUD complet d'édition est masqué ici).
void kioskHud(App& app, ImDrawList* dl, const ImVec2& size) {
    const ImGuiIO& io = ImGui::GetIO();
    const Vec2 w = app.camera.screenToWorld(
        {io.MousePos.x - app.viewportPos.x, io.MousePos.y - app.viewportPos.y},
        app.viewportVec2());

    // Police agrandie : ×1,5 (×1,8 sur les très grands écrans), interligne
    // proportionnel — le kiosque vise les présentations à distance.
    const float fs = ImGui::GetStyle().FontSizeBase * (size.y > 1200.0f ? 1.8f : 1.5f);
    const float lineH = fs * 1.18f;
    ImGui::PushFont(nullptr, fs);
    const auto bigText = [&](float x, float y, const char* text, ImU32 col = kTextCol) {
        dl->AddText(ImVec2(x, y), col, text);
    };

    char buf[160];
    float y = size.y - 8.0f - lineH;

    // Panneau translucide derrière les lignes d'info (lisibilité sur l'ardoise).
    const int nLines = 1 + (app.rotDeg != 0.0f ? 1 : 0) +
                       (app.showRedraw ? 1 : 0) + 1;
    const float panelH = (float)nLines * lineH + 12.0f;
    dl->AddRectFilled(ImVec2(6.0f, y - panelH + 6.0f + lineH),
                      ImVec2(size.x * 0.42f, y + lineH + 6.0f),
                      IM_COL32(12, 16, 22, 140), 6.0f);

    std::snprintf(buf, sizeof(buf), "%s   pos(%d, %d)",
                  zoomText(app.zoomMult()).c_str(), (int)std::round(w.x),
                  (int)std::round(w.y));
    bigText(14.0f, y, buf);
    y -= lineH;

    if (app.rotDeg != 0.0f) {
        std::snprintf(buf, sizeof(buf), "rot %d°", (int)std::lround(app.rotDeg));
        bigText(14.0f, y, buf, kDimCol);
        y -= lineH;
    }
    if (app.showRedraw) {
        std::snprintf(buf, sizeof(buf), "%d redessins/s", app.redraws);
        bigText(14.0f, y, buf, kDimCol);
        y -= lineH;
    }

    std::snprintf(buf, sizeof(buf), "Scène : %s%s",
                  app.sceneName.empty() ? "sans nom" : app.sceneName.c_str(),
                  app.dirty ? " *" : "");
    bigText(14.0f, y, buf, kDimCol);
    y -= lineH;

    // Toast contextuel (aide prospective) au-dessus du HUD, lui aussi agrandi.
    if (app.toastAge > 0.0f && !app.toast.empty()) {
        const ImVec2 tsize = ImGui::CalcTextSize(app.toast.c_str());
        const float padX = 14.0f;
        const float padY = 10.0f;
        const ImVec2 boxMin(10.0f, y - tsize.y - padY * 2.0f);
        const ImVec2 boxMax(boxMin.x + tsize.x + padX * 2.0f,
                            boxMin.y + tsize.y + padY * 2.0f);
        dl->AddRectFilled(boxMin, boxMax, IM_COL32(20, 26, 34, 215), 6.0f);
        dl->AddRect(boxMin, boxMax, IM_COL32(90, 160, 255, 120), 6.0f);
        dl->AddText(ImVec2(boxMin.x + padX, boxMin.y + padY),
                    IM_COL32(220, 230, 245, 240), app.toast.c_str());
    }
    ImGui::PopFont();
}

// ---------------------------------------------------------------------------
// Viewport plein écran
// ---------------------------------------------------------------------------
void viewport(App& app) {
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 pos = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    app.viewportPos = pos;
    app.viewportSize = size;
    app.viewportHovered = ImGui::IsWindowHovered();

    if (app.window && size.x > 1.0f && size.y > 1.0f) {
        int fbW = 0, fbH = 0, winW = 0, winH = 0;
        SDL_GL_GetDrawableSize(app.window, &fbW, &fbH);
        SDL_GetWindowSize(app.window, &winW, &winH);
        const float fx = winW > 0 ? (float)fbW / (float)winW : 1.0f;
        const float fy = winH > 0 ? (float)fbH / (float)winH : 1.0f;
        app.renderer.setViewport((int)(pos.x * fx), (int)((winH - pos.y - size.y) * fy),
                                 (int)(size.x * fx), (int)(size.y * fy));
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(55, 65, 85, 255));

    // Le voile du kiosque (dessiné en dernier par frame) recouvre exactement ce
    // rectangle, donc la souris est toujours « sur » le canvas — kioskX, zoom
    // molette et clics suivent. L'interface d'édition reste visible derrière le
    // voile mais inerte.
    if (app.kiosk) {
        app.viewportHovered = true;
        app.update(io.DeltaTime);
        return;
    }

    app.viewportHovered = ImGui::IsWindowHovered();
    app.update(io.DeltaTime);

    // Rectangle de sélection (lasso).
    if (app.isBoxDragging()) {
        const ImVec2 a(pos.x + app.boxStart().x, pos.y + app.boxStart().y);
        const ImVec2 b(pos.x + app.boxCur().x, pos.y + app.boxCur().y);
        dl->AddRectFilled(a, b, IM_COL32(90, 160, 255, 22));
        dl->AddRect(a, b, IM_COL32(140, 190, 255, 220));
    }

    // Curseur : croix de visée ou disque de peinture.
    if (app.viewportHovered && app.preview == PreviewMode::Off && !app.kiosk) {
        const ImVec2 mp = io.MousePos;
        if (app.brushArmed) {
            const Color& c = app.brushColor;
            dl->AddCircleFilled(mp, 8.0f,
                                IM_COL32((int)(c.r * 255), (int)(c.g * 255),
                                         (int)(c.b * 255), 200));
            dl->AddCircle(mp, 8.0f, IM_COL32(255, 255, 255, 190));
            ImGui::SetMouseCursor(ImGuiMouseCursor_None);
        } else if (app.reticle != ReticleState::Off) {
            const ImU32 col = IM_COL32(120, 230, 255, 200);
            if (app.reticle == ReticleState::Simple) {
                dl->AddLine(ImVec2(mp.x - 8, mp.y), ImVec2(mp.x + 8, mp.y), col);
                dl->AddLine(ImVec2(mp.x, mp.y - 8), ImVec2(mp.x, mp.y + 8), col);
            } else {
                dl->AddLine(ImVec2(0, mp.y), ImVec2(size.x, mp.y), col);
                dl->AddLine(ImVec2(mp.x, 0), ImVec2(mp.x, size.y), col);
            }
            ImGui::SetMouseCursor(ImGuiMouseCursor_None);
        }
    }

    // HUD bas-gauche (masqué en prévisualisation et en kiosque).
    if (app.preview == PreviewMode::Off && !app.kiosk) hud(app, dl, size);
}

// ---------------------------------------------------------------------------
// Panneaux flottants
// ---------------------------------------------------------------------------
// Menu contextuel des formes prédéfinies (ouvert depuis le bouton de la barre
// d'outils) : remplace l'ancienne fenêtre « Formes » — le choix des formes
// s'active en tant que menu, pas en sous-fenêtre. La molette sur une ligne
// cercle / anneau / étoile règle le nombre de côtés (ou pointes) et le
// compteur reste affiché à largeur FIXE.
void shapesMenu(App& app) {
    if (!ImGui::BeginPopup("##shapesmenu")) return;
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::TextDisabled("Choisir une forme l'arme, puis la tracer au canvas.");
    ImGui::Separator();
    struct ShapeEntry {
        const char* icon;
        const char* label;
        const char* tip;
        Tool tool;
    };
    const ShapeEntry shapes[] = {
        {"shape-circle", "Cercle", "Cercle régulier — 2 clics (centre puis rayon)",
         Tool::Circle},
        {"shape-rect", "Rectangle", "Rectangle — 2 clics (coin puis étendue)",
         Tool::Rectangle},
        {"shape-square", "Carré", "Carré — 2 clics", Tool::Square},
        {"shape-triangle", "Triangle", "Triangle — 2 clics", Tool::Triangle},
        {"shape-pentagon", "Pentagone", "Pentagone — 2 clics", Tool::Pentagon},
        {"shape-hexagon", "Hexagone", "Hexagone — 2 clics", Tool::Hexagon},
        {"shape-star", "Étoile", "Étoile — 3 clics (centre, rayon, profondeur)",
         Tool::Star},
        {"shape-annulus", "Anneau", "Anneau — 3 clics (centre, rayon, trou)",
         Tool::Ring},
    };
    for (const auto& s : shapes) {
        if (toolBtnIcon(s.icon, s.tip, app.tool == s.tool, kGreen, false, s.label)) {
            app.startShapeTool(s.tool);
            ImGui::CloseCurrentPopup();
        }
        // 4.2 : la molette sur la ligne Cercle / Anneau / Étoile règle le
        // nombre de côtés (comme sur le canvas).
        const bool sidesShape =
            s.tool == Tool::Circle || s.tool == Tool::Ring || s.tool == Tool::Star;
        if (sidesShape) {
            if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f) {
                app.circleSides =
                    std::clamp(app.circleSides + (int)std::lround(io.MouseWheel), 3, 64);
                app.setStatus("Nombre de " +
                              std::string(s.tool == Tool::Star ? "pointes de l'étoile"
                                                               : "côtés du " +
                                                                     std::string(s.tool == Tool::Circle
                                                                                     ? "cercle"
                                                                                     : "anneau")) +
                              " : " + std::to_string(app.circleSides));
            }
            ImGui::SameLine();
            char sidesbuf[24];
            std::snprintf(sidesbuf, sizeof(sidesbuf), "%d %s", app.circleSides,
                          s.tool == Tool::Star ? "pointes" : "côtés");
            valueLabel(sidesbuf, ImGui::CalcTextSize("64 côtés").x);
        }
    }
    ImGui::Separator();
    ImGui::TextDisabled("2 clics : ancre puis valider · étoile et anneau : 3 clics.");
    ImGui::TextDisabled("Molette sur une ligne (cercle/anneau/étoile) : nombre de côtés.");
    ImGui::TextDisabled("Raccourcis : C R T Q N H É A · Retour arrière : annuler le tracé.");
    ImGui::EndPopup();
}

void alignPanel(App& app) {
    if (!app.alignOpen) return;
    ImGui::SetNextWindowPos(ImVec2(12, 96), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(220, 0), ImGuiCond_FirstUseEver);
    // Les actions portent sur des points : bascule en cible « sommet » si besoin.
    auto ensureVertexSel = [&]() { app.toVertexSelection(); };
    if (ImGui::Begin("Aligner / Répartir", &app.alignOpen)) {
        const size_t n = app.selMode == SelMode::Vertex ? app.selVerts.size()
                                                        : app.selectionVertices().size();
        const bool align = n >= 2;
        const bool dist = n >= 3;
        if (toolBtnIcon("align-x", "Aligner X (Alt+←)", false, kGreen, !align,
                        "Aligner X (Alt+←)")) {
            ensureVertexSel();
            app.alignX();
        }
        if (toolBtnIcon("align-y", "Aligner Y (Alt+→)", false, kGreen, !align,
                        "Aligner Y (Alt+→)")) {
            ensureVertexSel();
            app.alignY();
        }
        if (toolBtnIcon("distribute-x", "Répartir X (Alt+Maj+←)", false, kGreen, !dist,
                        "Répartir X (Alt+Maj+←)")) {
            ensureVertexSel();
            app.distributeX();
        }
        if (toolBtnIcon("distribute-y", "Répartir Y (Alt+Maj+→)", false, kGreen, !dist,
                        "Répartir Y (Alt+Maj+→)")) {
            ensureVertexSel();
            app.distributeY();
        }
        ImGui::Separator();
        if (toolBtnIcon("mirror-x", "Miroir X (M) — symétrie autour du 1er point choisi",
                        false, kGreen, !align, "Miroir X (M)"))
            app.mirrorSelectionX();
        if (toolBtnIcon("mirror-y", "Miroir Y (Maj+M) — symétrie autour du 1er point choisi",
                        false, kGreen, !align, "Miroir Y (Maj+M)"))
            app.mirrorSelectionY();
        ImGui::Separator();
        ImGui::TextDisabled("≥ 2 points pour aligner, ≥ 3 pour répartir.");
        ImGui::TextDisabled("L'ancre est le premier point sélectionné.");
        ImGui::TextDisabled("Le panneau reste ouvert pour enchaîner les actions.");
    }
    ImGui::End();
}

void palettePanel(App& app) {
    if (!app.paletteOpen) return;
    ImGui::SetNextWindowPos(ImVec2(12, 128), ImGuiCond_FirstUseEver);
    // 8 pastilles × 28 px + 7 × 2 px d'espacement = 238 px : la fenêtre est
    // assez large pour les afficher toutes sur une seule ligne.
    ImGui::SetNextWindowSize(ImVec2(256, 0), ImGuiCond_FirstUseEver);
    static int editing = -1;
    static Color editColor{1.0f, 1.0f, 1.0f, 1.0f};
    if (ImGui::Begin("Peinture", &app.paletteOpen)) {
        int removeIdx = -1;
        // Espacement resserré pendant la rangée de pastilles uniquement.
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 6.0f));
        for (int i = 0; i < (int)app.palette.size(); ++i) {
            const Color& c = app.palette[i];
            ImGui::PushID(i);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(c.r, c.g, c.b, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                  ImVec4(c.r * 1.2f, c.g * 1.2f, c.b * 1.2f, 1.0f));
            if (ImGui::Button("##sw", ImVec2(28, 28))) app.setBrushColor(c);
            ImGui::PopStyleColor(2);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) removeIdx = i;
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                editing = i;
                editColor = c;
            }
            if (i < (int)app.palette.size() - 1) ImGui::SameLine();
            ImGui::PopID();
        }
        ImGui::PopStyleVar();
        if (removeIdx >= 0 && app.palette.size() > 1) {
            app.palette.erase(app.palette.begin() + removeIdx);
            if (editing >= removeIdx) editing = -1;
        }
        ImGui::Separator();

        if (editing >= 0 && editing < (int)app.palette.size()) {
            ImGui::TextDisabled("Modifier la pastille — Entrée valide, Échap annule.");
            if (ImGui::ColorEdit3("##edit", &editColor.r)) app.palette[editing] = editColor;
            if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_Escape))
                editing = -1;
            ImGui::Separator();
        }

        static float picker[3] = {1.0f, 0.55f, 0.2f};
        // Le ColorEdit3 occupe toute la largeur : on place « Ajouter » et
        // « Défauts » côte à côte sur la ligne suivante (un SameLine ici
        // pousserait le bouton hors du panneau).
        ImGui::ColorEdit3("Couleur", picker);
        if (toolBtnIcon("new-shape", "Ajouter la couleur à la palette", false,
                        kGreen, false, "Ajouter")) {
            app.palette.push_back({picker[0], picker[1], picker[2], 1.0f});
        }
        ImGui::SameLine();
        if (toolBtnIcon("reset", "Revenir à la palette par défaut", false, kGreen,
                        false, "Défauts")) {
            app.palette = {rgba(1.00f, 0.35f, 0.35f), rgba(1.00f, 0.65f, 0.20f),
                           rgba(1.00f, 0.85f, 0.25f), rgba(0.40f, 0.85f, 0.35f),
                           rgba(0.30f, 0.80f, 0.85f), rgba(0.40f, 0.55f, 1.00f),
                           rgba(0.75f, 0.45f, 0.95f), rgba(0.92f, 0.95f, 1.00f)};
        }
        ImGui::Separator();
        ImGui::TextUnformatted("Opacité appliquée à chaque peinture :");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##op", &app.brushOpacity, 0.05f, 1.0f, "%.0f %%"))
            app.brushOpacity = std::clamp(app.brushOpacity, 0.05f, 1.0f);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Opacité des prochaines peintures (défaut 45 %%).");
        ImGui::Separator();
        if (toolBtnIcon("brush-off", "Désarmer le pinceau", false, kGreen, false,
                        "Désarmer le pinceau"))
            app.brushArmed = false;
    }
    ImGui::End();
}

void consoleWindow(App& app) {
    if (!app.consoleVisible) return;
    ImGui::SetNextWindowPos(app.consolePos, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(app.consoleSize, ImGuiCond_FirstUseEver);
    bool open = app.consoleVisible;
    if (ImGui::Begin("Console", &open, ImGuiWindowFlags_NoCollapse)) {
        app.consoleVisible = open;
        static char filter[64] = {0};
        if (toolBtnIcon("clear-console", "Vider la console", false, kGreen, false,
                        "Vider"))
            app.consoleLog.clear();
        ImGui::SameLine();
        ImGui::TextDisabled("%zu entrée(s)", app.consoleLog.size());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150.0f);
        ImGui::InputText("##filter", filter, sizeof(filter));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Filtrer les messages par mot-clé "
                              "(sans tenir compte des majuscules).");
        ImGui::SameLine();
        if (toolBtnIcon("search", "Effacer le filtre de recherche", filter[0] != 0,
                        kGreen, false))
            filter[0] = '\0';
        ImGui::Separator();
        ImGui::BeginChild("##log", ImVec2(0, 0), false);
        std::string needle = filter;
        std::transform(needle.begin(), needle.end(), needle.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        size_t shown = 0;
        for (const auto& line : app.consoleLog) {
            if (!needle.empty()) {
                std::string l = line;
                std::transform(l.begin(), l.end(), l.begin(),
                               [](unsigned char c) { return (char)std::tolower(c); });
                if (l.find(needle) == std::string::npos) continue;
            }
            ImGui::TextUnformatted(line.c_str());
            ++shown;
        }
        if (shown == 0 && !needle.empty())
            ImGui::TextDisabled("Aucun message ne correspond à « %s ».", filter);
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f)
            ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
    }
    if (app.consoleVisible) {
        app.consolePos = ImGui::GetWindowPos();
        app.consoleSize = ImGui::GetWindowSize();
    }
    ImGui::End();
}

// Panneau « Réglages » : distance de détection des segments au survol (et en
// mode « segment »). Mémorisée dans les préférences (prefs.json).
void settingsPanel(App& app) {
    if (!app.settingsOpen) return;
    if (ImGui::Begin("Réglages", &app.settingsOpen)) {
        ImGui::TextUnformatted("Détection des segments :");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##edgeTol", &app.edgePickTol, 2.0f, 30.0f, "%.0f px"))
            app.edgePickTol = std::clamp(app.edgePickTol, 2.0f, 30.0f);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Distance (pixels écran) à laquelle un segment "
                              "s'illumine au survol et sert de base à un nouveau "
                              "triangle (mode sommet) — vaut aussi pour la "
                              "sélection en cible « segment ».");
        ImGui::Separator();
        ImGui::TextUnformatted("Grille :");
        if (ImGui::Checkbox("Afficher la grille (G)", &app.gridOn)) {
            app.setStatus(app.gridOn ? "Grille affichée (G)" : "Grille masquée (G)");
        }
        if (ImGui::Checkbox("Aimanter sur la grille (Maj+G)", &app.snapOn)) {
            app.setStatus(app.snapOn ? "Aimantation activée (Maj+G)"
                                     : "Aimantation désactivée (Maj+G)");
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("L'aimantation accroche les points posés, déplacés "
                              "ou collés aux intersections de la grille — "
                              "indépendamment de son affichage.");
        ImGui::Separator();
        ImGui::TextDisabled("Mémorisé entre les sessions (préférences).");
        ImGui::TextDisabled("Le rayon de fusion par déplacement (molette sur le bouton "
                            "« Fusionner ») est aussi mémorisé.");
    }
    ImGui::End();
}

void helpWindow(App& app) {
    if (!app.dlgHelpOpen) return;
    ImGui::SetNextWindowPos(ImVec2(160, 60), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(460, 0), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Aide — raccourcis", &app.dlgHelpOpen)) {
        ImGui::TextUnformatted("Clavier");
        ImGui::BulletText("Retour arrière : supprimer (sommet / segment / triangle selon la cible)");
        ImGui::BulletText("Maj+Retour arrière : réinitialiser la scène (confirmation)");
        ImGui::BulletText("Ctrl+Z / Ctrl+Maj+Z ou Ctrl+Y : annuler / rétablir");
        ImGui::BulletText("Ctrl+C / Ctrl+X / Ctrl+V : copier / couper / coller");
        ImGui::BulletText("Ctrl+S : enregistrer (fenêtre d'emplacement)");
        ImGui::BulletText("Ctrl+0 : zoom 100 %% recentré");
        ImGui::BulletText("G : grille · Y : réticule · F : compteur de redessins · P : prévisualiser");
        ImGui::BulletText("Accueil : tout afficher · Ctrl+F : cadrer la sélection (zoom automatique)");
        ImGui::BulletText("Formes : C cercle · R rectangle · T triangle · Q carré · N pentagone · H hexagone · É étoile · A anneau");
        ImGui::BulletText("Ctrl+D : dupliquer la sélection · Ctrl+A : tout sélectionner · Ctrl+I : inverser la sélection");
        ImGui::BulletText("M / Maj+M : miroir X / Y de la sélection · Alt+S : mise à l'échelle précise (facteur)");
        ImGui::BulletText("Ctrl+M : outil mesure (2 clics : distance au HUD) · Alt+D : dupliquer le plan actif");
        ImGui::BulletText("Maj+G : aimantation sur la grille sans son affichage (ou l'inverse)");
        ImGui::BulletText("Alt+R : rotation précise (saisie d'un angle)");
        ImGui::BulletText("? : cette aide · Échap : quitter le mode en cours");
        ImGui::BulletText("Alt+← / Alt+→ : aligner X / Y · Alt+Maj+←/→ : répartir X / Y");
        ImGui::BulletText("Alt+↑ / Alt+↓ : monter / descendre le plan actif (empilement)");
        ImGui::BulletText("Alt+K : kiosque de sélection des plans (au moins 2 plans)");
        ImGui::Separator();
        ImGui::TextUnformatted("Souris");
        ImGui::BulletText("Clic gauche (vide) : poser un point — 3 clics ferment un triangle");
        ImGui::BulletText("Clic gauche (entité) : sélectionner · Maj+clic : basculer");
        ImGui::BulletText("Clic gauche + glisser : rectangle de sélection (ne déplace jamais)");
        ImGui::BulletText("Clic droit : saisir l'entité la plus proche — modes sommet / segment / triangle : l'entité devient la seule sélectionnée et se saisit aussitôt · Ctrl+clic droit : ajouter · Maj+clic droit : basculer");
        ImGui::BulletText("Clic droit + glisser : déplacer la sélection");
        ImGui::BulletText("Molette : zoom — ou rotation des points sélectionnés (≥ 2)");
        ImGui::BulletText("PNG (bouton dans la prévisualisation) : exporter la vue actuelle en image");
        ImGui::BulletText("AltGr + molette : rotation de tous les plans autour du curseur (5° par cran)");
        ImGui::BulletText("AltGr + clic droit + glisser : déplacer tous les plans d'un même décalage");
        ImGui::BulletText("Clic du milieu + glisser : déplacer la vue");
        ImGui::BulletText("Molette sur un bouton actif : réglage contextuel (pas de grille, côtés, pointes de l'étoile, rayon de fusion)");
        ImGui::BulletText("Clic droit sur le bouton Grille : activer / désactiver l'aimantation (indépendante de l'affichage) · Maj+G : même raccourci");
        ImGui::BulletText("Historique (barre d'outils) : versions horodatées de l'autosave — restaurer un état antérieur");
        ImGui::BulletText("Anneau orange : points superposés — clic pour les sélectionner tous, « Fusionner » les regroupe à la position moyenne (5.5)");
        ImGui::BulletText("Fusion par déplacement (5.6) : 1 point sélectionné + bouton Fusionner, puis glisser le point près d'un autre — molette sur le bouton : rayon 8-64 px, re-clic : verrouiller (cadenas)");
        ImGui::BulletText("Engrenage (barre d'outils) : distance de détection des segments (illumination au survol)");
    }
    ImGui::End();
}

// ---------------------------------------------------------------------------
// Bouton flottant du mode prévisualisation
// ---------------------------------------------------------------------------
// En prévisualisation, la barre d'outils et le HUD sont masqués : seul ce
// bouton flottant (en haut à gauche) reste disponible pour basculer
// aperçu simple → tous les plans → retour à l'édition.
void previewButton(App& app) {
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(8, 8), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.11f, 0.14f, 0.85f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::Begin("##previewbtn", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_AlwaysAutoResize);
    const bool planes = app.preview == PreviewMode::Planes;
    // Même échelle que le HUD du kiosque : ×1,5 (×1,8 sur les très grands écrans).
    const float fs =
        ImGui::GetStyle().FontSizeBase * (io.DisplaySize.y > 1200.0f ? 1.8f : 1.5f);
    ImGui::PushFont(nullptr, fs);
    const bool clicked = toolBtnIcon(
        "preview",
        "Prévisualisation — clic : changer d'état · Échap, clic gauche ou Ctrl+S : sortir",
        true, planes ? kAmber : kGreen, false, planes ? "Plans" : "Aperçu");
    ImGui::SameLine();
    const bool pngClicked = toolBtnIcon(
        "export", "Exporter l'image actuelle en PNG (prévisualisation)", false, kGreen,
        false, "PNG");
    ImGui::PopFont();
    if (clicked) app.cyclePreview();
    if (pngClicked) app.dlgPngOpen = true;
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------------------
// Kiosque de sélection des plans (7.5)
// ---------------------------------------------------------------------------
// Miniature d'un plan dans sa carte : géométrie recentrée, inclinée (effet
// cover-flow), remplie de ses couleurs.
void drawPlaneCard(App& app, ImDrawList* dl, int pi, const ImVec2& tl,
                   const ImVec2& br, float squash, float alpha, bool front) {
    const Mesh2D& p = app.scene.planes[pi];
    if (p.vertices.empty()) return;
    Vec2 mn = p.vertices[0], mx = p.vertices[0];
    for (const Vec2& v : p.vertices) {
        mn.x = std::min(mn.x, v.x);
        mn.y = std::min(mn.y, v.y);
        mx.x = std::max(mx.x, v.x);
        mx.y = std::max(mx.y, v.y);
    }
    const float span = std::max(mx.x - mn.x, mx.y - mn.y);
    if (span < 1e-6f) return;
    const Vec2 cw = {(mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f};
    const float pad = 9.0f;
    const float scale =
        std::min((br.x - tl.x - pad * 2.0f) / span, (br.y - tl.y - pad * 2.0f) / span);
    const float cx = (tl.x + br.x) * 0.5f;
    const float cy = (tl.y + br.y) * 0.5f;
    // Le monde a Y vers le haut, l'écran Y vers le bas : on soustrait pour que
    // le haut de la forme apparaisse en haut de la carte (l'ancien code
    // inversait les plans verticalement dans le kiosque).
    auto toCard = [&](const Vec2& w) {
        return ImVec2(cx + (w.x - cw.x) / span * scale * squash,
                      cy - (w.y - cw.y) / span * scale);
    };

    // Faces (triangulées, avec leurs couleurs).
    for (const Face& f : p.faces) {
        if ((int)f.verts.size() < 3) continue;
        std::vector<Vec2> wpts;
        wpts.reserve(f.verts.size());
        for (int v : f.verts) wpts.push_back(p.vertices[v]);
        std::vector<int> local;
        if (!triangulatePolygon(wpts, local)) continue;
        std::vector<ImVec2> pts;
        pts.reserve(local.size());
        for (int idx : local) pts.push_back(toCard(wpts[idx]));
        ImU32 col;
        if (f.hasColor)
            col = IM_COL32((int)(f.color.r * 255.0f * alpha),
                           (int)(f.color.g * 255.0f * alpha),
                           (int)(f.color.b * 255.0f * alpha), (int)(255.0f * alpha));
        else
            col = IM_COL32((int)(72.0f * alpha), (int)(92.0f * alpha),
                           (int)(122.0f * alpha), (int)(255.0f * alpha));
        dl->AddConvexPolyFilled(pts.data(), (int)pts.size(), col);
    }
    // Arêtes.
    const ImU32 lineCol =
        front ? IM_COL32(225, 235, 250, (int)(235.0f * alpha))
              : IM_COL32(180, 190, 210, (int)(130.0f * alpha));
    for (const auto& e : p.edges())
        dl->AddLine(toCard(p.vertices[e.first]), toCard(p.vertices[e.second]), lineCol,
                    1.2f);
}

// Couverture des plans : cartes inclinées, guide vert pointillé à l'axe du
// pointeur, étiquettes « Plan n ».
void kioskOverlay(App& app, ImDrawList* dl, const ImVec2& pos, const ImVec2& size) {
    const int n = app.scene.count();
    if (n <= 1 || size.x < 40.0f || size.y < 40.0f) return;
    const int target = app.kioskTarget();
    const float vw = size.x;
    const float vh = size.y;

    // Cartes plus larges que l'espacement : le chevauchement est assumé,
    // la carte en avant recouvre partiellement ses voisines (arrière → avant).
    const float spacing = std::clamp(vw / (float)(n + 2), 110.0f, 240.0f);
    const float cardW = spacing * 0.95f;
    const float cardH = cardW * 0.66f;
    const float centerY = vh * 0.52f;

    // Carte « visuellement en avant » : celle dont le décalage lissé est le
    // plus proche de zéro (les états visuels suivent l'animation, pas le saut).
    int visFront = target;
    for (int i = 0; i < n; ++i)
        if (app.kioskOff[i] * app.kioskOff[i] <
            app.kioskOff[visFront] * app.kioskOff[visFront])
            visFront = i;

    // Ordre de dessin du plus éloigné au premier plan.
    std::vector<int> order((size_t)n);
    for (int i = 0; i < n; ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return std::fabs(app.kioskOff[a]) > std::fabs(app.kioskOff[b]);
    });

    for (int i : order) {
        const float off = app.kioskOff[i];  // décalage lissé (animation)
        // Inclinaison plus douce pour garder les vignettes reconnaissables.
        const float tilt = std::clamp(off * 0.42f, -0.95f, 0.95f);
        const float squash = std::cos(tilt);
        const float cx = vw * 0.5f + off * spacing;
        const bool front = (i == visFront);
        // Dimension identique pour toutes les cartes de fond : seule la carte
        // en avant est grossie (jusqu'à ×2) — identification immédiate de la
        // vignette recherchée. La transition reste animée (closeness).
        const float closeness = std::clamp(1.0f - std::fabs(off), 0.0f, 1.0f);
        const float alpha = 0.45f + 0.55f * closeness;
        const float zoomMul = 1.0f + closeness;
        const float cw0 =
            std::max(cardW * (0.30f + 0.70f * std::fabs(squash)), 30.0f);
        const float cw = cw0 * zoomMul;
        const float ch = cardH * zoomMul;

        const float px = pos.x + cx;
        const float py = pos.y + centerY;
        const ImVec2 tl(px - cw * 0.5f, py - ch * 0.5f);
        const ImVec2 br(px + cw * 0.5f, py + ch * 0.5f);

        // Ombre portée douce sous la carte (dégradé de rectangles estompés,
        // dessinée avant la carte pour ne dépasser que vers le bas).
        for (int s = 3; s >= 1; --s) {
            const float grow = (float)s * 2.5f;
            const float drop = 2.0f + (float)s * 3.0f;
            const float sa = (s == 1 ? 0.22f : s == 2 ? 0.15f : 0.09f) * alpha;
            dl->AddRectFilled(ImVec2(tl.x - grow, tl.y + 2.0f),
                              ImVec2(br.x + grow, br.y + drop),
                              IM_COL32(0, 0, 0, (int)(255.0f * sa)), 8.0f);
        }

        dl->AddRectFilled(tl, br, IM_COL32(14, 18, 26, (int)(235.0f * alpha)), 7.0f);
        // Bordure : verte pour la carte « en avant », ambre pour le plan actif,
        // neutre sinon (le vert prime sur l'ambre si les deux coïncident).
        const ImU32 borderCol =
            std::fabs(off) < 0.5f ? IM_COL32(90, 220, 130, 220)
                                  : (i == app.scene.active ? IM_COL32(235, 170, 60, 180)
                                                           : IM_COL32(255, 255, 255, 40));
        dl->AddRect(tl, br, borderCol, 7.0f, 0, front ? 1.6f : 1.0f);
        // Vignettes de fond lisibles : toutes de même dimension (voir kioskOverlay).
        drawPlaneCard(app, dl, i, tl, br, std::fabs(squash), alpha, front);

        // Reflet discret en haut de la carte (dégradé clair → transparent,
        // légèrement rentré pour respecter les coins arrondis).
        const float reflH = std::min(ch * 0.32f, 46.0f);
        dl->AddRectFilledMultiColor(
            ImVec2(tl.x + 3.0f, tl.y + 2.0f), ImVec2(br.x - 3.0f, tl.y + reflH),
            IM_COL32(255, 255, 255, (int)(26.0f * alpha)),
            IM_COL32(255, 255, 255, (int)(26.0f * alpha)),
            IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));

        const Mesh2D& p = app.scene.planes[i];

        // Nom du plan directement sur la carte (bandeau discret en haut).
        char name[32];
        std::snprintf(name, sizeof(name), "Plan %d", i + 1);
        const ImVec2 ns = ImGui::CalcTextSize(name);
        const float nameY = tl.y + 5.0f;
        if (cw > 44.0f) {
            dl->AddRectFilled(ImVec2(px - ns.x * 0.5f - 6.0f, nameY - 2.0f),
                              ImVec2(px + ns.x * 0.5f + 6.0f, nameY + ns.y + 2.0f),
                              IM_COL32(0, 0, 0, (int)(95.0f * alpha)), 3.0f);
            dl->AddText(ImVec2(px - ns.x * 0.5f, nameY),
                        front ? IM_COL32(235, 245, 255, 240)
                              : IM_COL32(200, 210, 225, 150),
                        name);
        }

        // Compteurs points / triangles en bas de carte.
        char counts[48];
        std::snprintf(counts, sizeof(counts), "%d pt · %d tri",
                      (int)p.vertices.size(), p.triangleCount());
        const ImVec2 csize = ImGui::CalcTextSize(counts);
        if (cw > 56.0f)
            dl->AddText(ImVec2(px - csize.x * 0.5f, br.y - csize.y - 5.0f),
                        IM_COL32(180, 195, 215, front ? 205 : 115), counts);

        // Marqueur spécial du plan actif (celui en cours d'édition) :
        // pilule ambre « actif » sous la carte.
        if (i == app.scene.active) {
            const char* tag = "actif";
            const ImVec2 ts2 = ImGui::CalcTextSize(tag);
            const float bw = ts2.x + 12.0f;
            const ImVec2 b0(px - bw * 0.5f, br.y + 6.0f);
            const ImVec2 b1(b0.x + bw, b0.y + ts2.y + 4.0f);
            dl->AddRectFilled(b0, b1, IM_COL32(200, 140, 40, 230), 4.0f);
            dl->AddText(ImVec2(b0.x + 6.0f, b0.y + 2.0f), IM_COL32(255, 250, 235, 245),
                        tag);
        }
    }

    // Guide pointillé vert marquant l'axe du pointeur (pleine hauteur).
    const float gx = pos.x + app.kioskX;
    if (gx >= pos.x && gx <= pos.x + vw) {
        const ImU32 gcol = IM_COL32(90, 220, 130, 150);
        for (float y = pos.y + 4.0f; y < pos.y + vh - 4.0f; y += 13.0f)
            dl->AddLine(ImVec2(gx, y),
                        ImVec2(gx, std::min(y + 7.0f, pos.y + vh - 4.0f)), gcol);
    }
}

// Voile plein écran du kiosque : fenêtre translucide dessinée en dernier,
// au-dessus de toute l'interface d'édition (qui reste visible mais inerte
// derrière). Elle porte la couverture des plans, le HUD agrandi et le bouton
// du mode.
void kioskVeil(App& app) {
    const ImGuiIO& io = ImGui::GetIO();
    // Ramène explicitement le voile au premier plan de l'affichage, au-dessus
    // de la barre d'outils et des panneaux dessinés précédemment : sans cela,
    // l'ordre d'affichage ImGui peut laisser ces fenêtres par-dessus le voile.
    ImGui::SetNextWindowFocus();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.22f, 0.25f, 0.31f, 0.86f));
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##kioskveil", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoSavedSettings);
    const ImVec2 pos = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    kioskOverlay(app, dl, pos, size);
    kioskHud(app, dl, size);

    // Bouton du mode kiosque — dernier élément du voile, donc toujours au-dessus
    // des cartes. Clic gauche : sortie (Échap / clic droit font pareil).
    ImGui::SetCursorPos(ImVec2(8, 8));
    const float fs = ImGui::GetStyle().FontSizeBase * (size.y > 1200.0f ? 1.8f : 1.5f);
    ImGui::PushFont(nullptr, fs);
    const bool clicked = toolBtnIcon("kiosk", "Sortir du mode kiosque (Échap ou clic droit)",
                                     true, kGreen, false, "Kiosque");
    ImGui::PopFont();
    if (clicked) app.toggleKiosk();
    // Rectangle du bouton mémorisé : la frame suivante, update() n'interprète
    // pas un clic sur cette zone comme une sélection de plan.
    const ImVec2 itMin = ImGui::GetItemRectMin();
    const ImVec2 itMax = ImGui::GetItemRectMax();
    app.kioskBtnMinX = itMin.x; app.kioskBtnMinY = itMin.y;
    app.kioskBtnMaxX = itMax.x; app.kioskBtnMaxY = itMax.y;

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();  // FrameRounding
    ImGui::PopStyleVar();  // WindowPadding
}

// ---------------------------------------------------------------------------
// Dialogues modaux
// ---------------------------------------------------------------------------
void saveDialog(App& app) {
    if (!app.dlgSaveOpen) return;
    ImGui::OpenPopup("Enregistrer la scène");
    ImGui::SetNextWindowSize(ImVec2(450, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Enregistrer la scène", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Emplacements déjà utilisés (du plus récent au plus ancien) :");
        ImGui::BeginChild("##slots", ImVec2(420, 90), true);
        for (int i = (int)app.saveLocations.size() - 1; i >= 0; --i) {
            const bool selected = app.dlgSaveName == app.saveLocations[i];
            if (ImGui::Selectable(app.saveLocations[i].c_str(), selected))
                std::snprintf(app.dlgSaveName, sizeof(app.dlgSaveName), "%s",
                              app.saveLocations[i].c_str());
        }
        if (app.saveLocations.empty()) ImGui::TextDisabled("Aucun emplacement pour l'instant.");
        ImGui::EndChild();
        ImGui::TextUnformatted("Nom de l'emplacement :");
        ImGui::SetNextItemWidth(420);
        ImGui::InputText("##name", app.dlgSaveName, sizeof(app.dlgSaveName));
        if (toolBtnIcon("export", "Enregistrer la scène (Ctrl+S)", false, kGreen,
                        false, "Enregistrer", 150.0f) ||
            (ImGui::IsKeyPressed(ImGuiKey_Enter) && ImGui::IsWindowFocused())) {
            app.saveToLocation(app.dlgSaveName);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (toolBtnIcon("close", "Annuler", false, kGreen, false, "Annuler", 150.0f)) {
            app.dlgSaveOpen = false;
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            app.dlgSaveOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void importDialog(App& app) {
    if (!app.dlgImportOpen) return;
    const char* title = app.dlgImportFmt == 0   ? "Charger meshes"
                      : app.dlgImportFmt == 1   ? "Charger JSON"
                                                : "Charger OBJ";
    ImGui::OpenPopup(title);
    ImGui::SetNextWindowSize(ImVec2(500, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        int mode = app.dlgImportReplace ? 1 : 0;
        ImGui::TextUnformatted("Chemin du fichier :");
        ImGui::SetNextItemWidth(470);
        ImGui::InputText("##path", app.dlgImportPath, sizeof(app.dlgImportPath));
        ImGui::TextUnformatted("Mode d'import :");
        ImGui::RadioButton("Remplacer (défaut)", &mode, 1);
        ImGui::SameLine();
        ImGui::RadioButton("Fusionner (ajoute aux plans existants)", &mode, 0);
        bool doImport = false;
        if (toolBtnIcon("check", "Valider l'import", false, kGreen, false,
                        "Valider", 150.0f) ||
            (ImGui::IsKeyPressed(ImGuiKey_Enter) && ImGui::IsWindowFocused()))
            doImport = true;
        if (doImport) {
            app.dlgImportReplace = (mode == 1);
            const bool ok = app.dlgImportFmt == 0
                                ? app.importMeshes(app.dlgImportPath, mode == 1)
                                : app.dlgImportFmt == 1
                                      ? app.importJson(app.dlgImportPath, mode == 1)
                                      : app.importObj(app.dlgImportPath, mode == 1);
            if (ok) ImGui::CloseCurrentPopup();
            // En cas d'échec, l'erreur est signalée (status + console) sans fermer.
        }
        ImGui::SameLine();
        if (toolBtnIcon("close", "Annuler", false, kGreen, false, "Annuler", 150.0f)) {
            app.dlgImportOpen = false;
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            app.dlgImportOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void resetDialog(App& app) {
    if (!app.dlgResetOpen) return;
    ImGui::OpenPopup("Réinitialiser la scène ?");
    if (ImGui::BeginPopupModal("Réinitialiser la scène ?", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Vider entièrement la scène ?");
        ImGui::TextUnformatted("Cette action efface aussi l'historique d'annulation.");
        if (toolBtnIcon("reset", "Vider entièrement la scène", false, kRed, false,
                        "Réinitialiser", 150.0f)) {
            app.resetScene();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (toolBtnIcon("close", "Annuler", false, kGreen, false, "Annuler", 150.0f)) {
            app.dlgResetOpen = false;
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            app.dlgResetOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void deletePlaneDialog(App& app) {
    if (!app.dlgDeletePlaneOpen) return;
    ImGui::OpenPopup("Supprimer le plan actif ?");
    if (ImGui::BeginPopupModal("Supprimer le plan actif ?", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "Supprimer le plan n° %d ?",
                      app.scene.active + 1);
        ImGui::TextUnformatted(buf);
        ImGui::TextUnformatted("L'opération est annulable (Ctrl+Z).");
        if (toolBtnIcon("delete-shape", "Supprimer le plan actif", false, kRed,
                        false, "Supprimer", 150.0f)) {
            app.deletePlane();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (toolBtnIcon("close", "Annuler", false, kGreen, false, "Annuler", 150.0f)) {
            app.dlgDeletePlaneOpen = false;
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            app.dlgDeletePlaneOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// Rotation précise : saisie d'un angle exact (Alt+R).
void rotateDialog(App& app) {
    if (!app.dlgRotateOpen) return;
    ImGui::OpenPopup("Rotation précise");
    ImGui::SetNextWindowSize(ImVec2(340, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Rotation précise", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Angle de rotation (degrés, sens trigonométrique) :");
        ImGui::SetNextItemWidth(180);
        ImGui::InputFloat("##deg", &app.rotateDeg, 1.0f, 15.0f, "%.1f°");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Valeur positive : sens trigonométrique ; "
                              "négative : sens horaire.");
        ImGui::TextDisabled("Le pivot est le centre de la sélection (≥ 2 sommets).");
        if (toolBtnIcon("check", "Appliquer la rotation à la sélection", false,
                        kGreen, false, "Appliquer", 120.0f) ||
            (ImGui::IsKeyPressed(ImGuiKey_Enter) && ImGui::IsWindowFocused())) {
            app.rotateSelectionExact(app.rotateDeg);
            app.dlgRotateOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (toolBtnIcon("close", "Annuler", false, kGreen, false, "Annuler", 120.0f)) {
            app.dlgRotateOpen = false;
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            app.dlgRotateOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// Mise à l'échelle précise : saisie d'un facteur (Alt+S). Pivot = centre de la
// sélection, comme la rotation précise.
void scaleDialog(App& app) {
    if (!app.dlgScaleOpen) return;
    ImGui::OpenPopup("Mise à l'échelle");
    ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Mise à l'échelle", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Facteur d'échelle (×2 = double, ×0,5 = moitié) :");
        ImGui::SetNextItemWidth(180);
        ImGui::InputFloat("##scale", &app.scaleFactor, 0.25f, 1.0f, "×%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Facteur strictement positif, différent de 1 ; "
                              "le pivot est le centre de la sélection.");
        ImGui::TextDisabled("Le pivot est le centre de la sélection (≥ 2 sommets).");
        if (toolBtnIcon("scale", "Appliquer la mise à l'échelle à la sélection", false,
                        kGreen, false, "Appliquer", 130.0f) ||
            (ImGui::IsKeyPressed(ImGuiKey_Enter) && ImGui::IsWindowFocused())) {
            app.scaleSelectionExact(app.scaleFactor);
            app.dlgScaleOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (toolBtnIcon("close", "Annuler", false, kGreen, false, "Annuler", 130.0f)) {
            app.dlgScaleOpen = false;
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            app.dlgScaleOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// Export SVG du plan actif : chemin du fichier.
void svgDialog(App& app) {
    if (!app.dlgSvgOpen) return;
    ImGui::OpenPopup("Exporter le plan en SVG");
    ImGui::SetNextWindowSize(ImVec2(500, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Exporter le plan en SVG", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Chemin du fichier SVG (par ex. ~/plan.svg) :");
        ImGui::SetNextItemWidth(470);
        ImGui::InputText("##svgpath", app.dlgSvgPath, sizeof(app.dlgSvgPath));
        ImGui::TextDisabled("Le fichier contient un polygone par face du plan actif, "
                            "avec les couleurs de remplissage — édition vectorielle.");
        bool doExport = false;
        if (toolBtnIcon("export-svg", "Exporter le plan actif en SVG", false, kGreen,
                        false, "Exporter", 150.0f) ||
            (ImGui::IsKeyPressed(ImGuiKey_Enter) && ImGui::IsWindowFocused()))
            doExport = true;
        if (doExport) {
            app.exportSvgTo(app.dlgSvgPath);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (toolBtnIcon("close", "Annuler", false, kGreen, false, "Annuler", 150.0f)) {
            app.dlgSvgOpen = false;
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            app.dlgSvgOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// Historique des versions horodatées de l'autosave.
void versionsDialog(App& app) {
    if (!app.dlgVersionsOpen) return;
    ImGui::OpenPopup("Historique des sauvegardes");
    ImGui::SetNextWindowSize(ImVec2(540, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Historique des sauvegardes", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Versions horodatées de l'autosave "
                               "(10 conservées, de la plus récente à la plus ancienne) :");
        ImGui::BeginChild("##versions", ImVec2(500, 170), true);
        if (app.versionFiles.empty()) {
            ImGui::TextDisabled("Aucune version pour l'instant.");
        } else {
            for (size_t i = 0; i < app.versionFiles.size(); ++i) {
                ImGui::PushID((int)i);
                ImGui::TextUnformatted(app.versionFiles[i].c_str());
                ImGui::SameLine();
                if (toolBtnIcon("history", "Restaurer cette version", false, kGreen,
                                false, "Restaurer", 110.0f))
                    app.restoreVersionFile(app.versionFiles[i]);
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
        ImGui::TextDisabled("Restaurer remplace la scène courante par cette version "
                            "(annulable avec Ctrl+Z).");
        if (toolBtnIcon("close", "Fermer", false, kGreen, false, "Fermer", 150.0f)) {
            app.dlgVersionsOpen = false;
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            app.dlgVersionsOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// Export d'image : chemin du fichier PNG (la capture est honorée à la frame
// suivante par App::exportPngIfRequested, après le rendu de la scène).
void pngDialog(App& app) {
    if (!app.dlgPngOpen) return;
    ImGui::OpenPopup("Exporter l'image (PNG)");
    ImGui::SetNextWindowSize(ImVec2(500, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Exporter l'image (PNG)", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Chemin du fichier PNG (par ex. ~/scene.png) :");
        ImGui::SetNextItemWidth(470);
        ImGui::InputText("##pngpath", app.dlgPngPath, sizeof(app.dlgPngPath));
        ImGui::TextDisabled("L'image correspond à la vue actuelle (prévisualisation "
                            "ou édition), sans l'interface.");
        bool doExport = false;
        if (toolBtnIcon("export", "Exporter la vue actuelle en PNG", false, kGreen,
                        false, "Exporter", 150.0f) ||
            (ImGui::IsKeyPressed(ImGuiKey_Enter) && ImGui::IsWindowFocused()))
            doExport = true;
        if (doExport) {
            app.exportPngPath = app.dlgPngPath;
            app.exportPngRequested = true;
            app.dlgPngOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (toolBtnIcon("close", "Annuler", false, kGreen, false, "Annuler", 150.0f)) {
            app.dlgPngOpen = false;
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            app.dlgPngOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Frame principale
// ---------------------------------------------------------------------------
void frame(App& app) {
    handleShortcuts(app);

    // Titre de la fenêtre.
    if (app.window) {
        std::string title = "Meshes Designer — " +
                            (app.sceneName.empty() ? std::string("sans nom")
                                                   : app.sceneName) +
                            (app.dirty ? " *" : "");
        SDL_SetWindowTitle(app.window, title.c_str());
    }

    const ImGuiIO& io = ImGui::GetIO();

    // Viewport plein écran (dessiné en premier, sous l'interface).
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##viewport", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse |
                     ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoSavedSettings);
    viewport(app);
    ImGui::End();
    ImGui::PopStyleColor();

    if (app.kiosk) {
        // L'interface d'édition est dessinée normalement, puis recouverte par
        // le voile plein écran (dessiné en dernier) : elle reste visible mais
        // inerte — le voile capte la souris et update() court-circuite le mode.
        toolbar(app);
        alignPanel(app);
        palettePanel(app);
        consoleWindow(app);
        helpWindow(app);
        settingsPanel(app);
        kioskVeil(app);
        return;
    }
    // Prévisualisation : interface masquée (barre d'outils, panneaux, HUD),
    // seul le bouton de bascule reste visible — le rendu d'aperçu occupe tout.
    if (app.preview != PreviewMode::Off) {
        previewButton(app);
        pngDialog(app);
        return;
    }

    toolbar(app);
    alignPanel(app);
    palettePanel(app);
    consoleWindow(app);
    helpWindow(app);
    settingsPanel(app);
    saveDialog(app);
    importDialog(app);
    resetDialog(app);
    deletePlaneDialog(app);
    rotateDialog(app);
    scaleDialog(app);
    pngDialog(app);
    svgDialog(app);
    versionsDialog(app);
}

bool quitRequested() { return g_quit; }

}  // namespace mesh::ui
