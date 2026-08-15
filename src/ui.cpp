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
#include <initializer_list>

namespace mesh::ui {

namespace {

bool g_quit = false;

// Vrai si un dialogue modal est déjà ouvert : une demande de fermeture est
// alors ignorée (l'utilisateur est en plein flux, il finit d'abord son action).
bool anyModalOpen(const App& app) {
    return app.dlgSaveOpen || app.dlgImportOpen || app.dlgResetOpen ||
           app.dlgDeletePlaneOpen || app.dlgRotateOpen || app.dlgScaleOpen ||
           app.dlgPngOpen || app.dlgSvgOpen || app.dlgVersionsOpen ||
           app.dlgRenameOpen || app.dlgQuitOpen;
}

const ImVec4 kGreen(0.20f, 0.62f, 0.36f, 1.0f);
const ImVec4 kAmber(0.95f, 0.63f, 0.20f, 1.0f);
const ImVec4 kBlue(0.26f, 0.48f, 0.90f, 1.0f);
const ImVec4 kRed(0.82f, 0.32f, 0.30f, 1.0f);
const ImU32 kTextCol = IM_COL32(190, 200, 220, 220);
const ImU32 kDimCol = IM_COL32(150, 160, 180, 160);
// Rembourrage des boutons à icônes : cadre plus haut et plus large pour une
// meilleure lisibilité (la hauteur du bouton = police + 2 × FramePadding.y).
const ImVec2 kBtnFramePad(7.0f, 5.0f);
// Pi local (app.cpp en a sa propre copie dans un autre TU).
constexpr float kPiF = 3.14159265358979323846f;
// Largeur automatique d'un bouton à libellé : texte + icône (18 px) + cette
// marge. Partagée entre toolBtnIcon (largeur auto) et dialogBtnWidth (largeur
// commune des boutons de dialogues) pour qu'elles ne puissent pas dériver.
constexpr float kBtnAutoPad = 22.0f;

// Paquets de la barre d'outils (3.2) : chaque paquet possède un bouton dédié
// qui l'ouvre / le ferme — le bouton porte un SYMBOLE SVG identifiant le
// sujet du paquet (réutilise les icônes des boutons) suivi d'un petit chevron
// d'état (▼ ouvert / ► fermé). L'état (1 bit par paquet) est mémorisé dans
// les préférences (prefs.json, champ « toolbarPacks »).
enum ToolbarPack : int {
    kPackCanevas, kPackAffichage, kPackVue, kPackSelection,
    kPackPressePapiers, kPackOutils, kPackOrdreZ, kPackFusion,
    kPackAnnuler, kPackSauvegarde, kPackEntrees, kPackPlansNav,
    kPackPlansEdition, kPackPlansOrdre, kPackPlansGestion, kPackScene,
    kPackInterface, kPackCount
};
static_assert(kPackCount <= 32, "un paquet = un bit de App::toolbarPacks (uint32)");

// Largeur d'un bouton de bascule de paquet (3.2) : symbole SVG du paquet
// (14 px) + petit chevron d'état (▼/►) — plus étroit que les boutons à
// icônes (~32 px) pour rester discret quand le paquet est replié (une rangée
// de boutons ne prend que quelques pixels chacun).
constexpr float kPackToggleW = 30.0f;

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

// Nom affiché d'un plan (spec 2.2) : le nom personnalisé s'il existe, sinon
// « Plan n » (n = numéro d'ordre, 1-based).
std::string planeLabel(const App& app, int i) {
    const Mesh2D& p = app.scene.planes[i];
    if (!p.name.empty()) return p.name;
    return "Plan " + std::to_string(i + 1);
}

// Ouvre la fenêtre d'enregistrement avec le nom pré-rempli : nom courant de la
// scène, ou dernier emplacement utilisé si la scène est sans nom.
void openSaveDialog(App& app) {
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

std::string zoomText(float mult) {
    char buf[32];
    if (std::fabs(mult - std::round(mult)) < 0.01f)
        std::snprintf(buf, sizeof(buf), "%.0fx", mult);
    else
        std::snprintf(buf, sizeof(buf), "%.1fx", mult);
    return buf;
}

// Largeur rendue par toolBtnIcon : libellé + icône (18 px) + kBtnAutoPad en
// largeur auto. Une largeur fixe demandée ne peut jamais être plus étroite que
// cette valeur (garde anti-débordement, ex. « Quitter quand même » à 150 px).
// Partagée avec toolbar() pour calculer la largeur des paquets de boutons.
float toolBtnWidth(const char* text, float width = 0.0f) {
    const bool hasText = text && text[0];
    const float iconSize = std::max(18.0f, ImGui::GetFontSize() - 1.0f);
    const float ts = hasText ? ImGui::CalcTextSize(text).x : 0.0f;
    const float autoW =
        hasText ? ts + iconSize + kBtnAutoPad : iconSize + 14.0f;
    return width > 0.0f ? std::max(width, autoW) : autoW;
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
    // Largeur du contenu (icône + espace + libellé), pour le centrage du dessin.
    const float contentW = hasText ? iconSize + 8.0f + ts.x : iconSize;
    ImVec2 size(toolBtnWidth(text, width), 0.0f);
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

// Largeur commune des boutons d'un dialogue de confirmation : le plus large
// des libellés, avec l'icône (18 px) et les marges identiques aux boutons à
// largeur auto de toolBtnIcon (ts.x + iconSize + 22). Tous les boutons d'un
// même dialogue partagent cette largeur : ils restent alignés et aucun libellé
// ne déborde, quelle que soit la police chargée sur la plateforme.
float dialogBtnWidth(std::initializer_list<const char*> labels) {
    float maxText = 0.0f;
    for (const char* l : labels)
        maxText = std::max(maxText, ImGui::CalcTextSize(l).x);
    const float iconSize = std::max(18.0f, ImGui::GetFontSize() - 1.0f);
    return maxText + iconSize + kBtnAutoPad;
}

// Largeur rendue par pill() : texte + marges [+ icône de 14 px], au moins minW.
// Partagée avec toolbar() pour calculer la largeur des paquets de boutons.
float pillWidth(const char* text, const char* icon, float minW = 0.0f) {
    const float ico = 14.0f;
    const bool hasIcon = icon != nullptr;
    return std::max(ImGui::CalcTextSize(text).x + 18.0f +
                        (hasIcon ? ico + 6.0f : 0.0f),
                    minW);
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
    const float w = pillWidth(text, icon, minW);
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
                   float squash, float alpha, bool front, float topMargin,
                   float bottomMargin);
void kioskOverlay(App& app, ImDrawList* dl, const ImVec2& pos, const ImVec2& size);
void shapesMenu(App& app);
void bgColorPopup(App& app);
void layerPopup(App& app);
void layerLoadDialog(App& app);
void opacityPopup(App& app);

// ---------------------------------------------------------------------------
// Raccourcis (spec ch. 15)
// ---------------------------------------------------------------------------
void handleShortcuts(App& app) {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;
    // Ne jamais éditer la scène derrière une fenêtre modale.
    if (app.dlgSaveOpen || app.dlgImportOpen || app.dlgResetOpen ||
        app.dlgDeletePlaneOpen || app.dlgRotateOpen || app.dlgScaleOpen ||
        app.dlgPngOpen || app.dlgSvgOpen || app.dlgVersionsOpen ||
        app.dlgRenameOpen)
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

    // Ctrl+0 : réinitialisation du zoom (valable partout, y compris en
    // prévisualisation — c'est une navigation de vue).
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_0)) {
        app.camera.zoom = 40.0f;
        app.camera.cx = 0.0f;
        app.camera.cy = 0.0f;
        app.rotDeg = 0.0f;
        app.setStatus("Zoom 100 % recentré sur l'origine, angle de rotation remis à zéro (Ctrl+0)");
    }

    // En prévisualisation (9.3), aucune édition n'est possible : seuls la
    // navigation de la vue (Accueil, Ctrl+F, Ctrl+0) et la sortie (Échap, P,
    // Ctrl+S) restent actives — aucun raccourci ne modifie la géométrie.
    if (app.preview != PreviewMode::Off) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) app.exitPreview();
        if (ImGui::IsKeyPressed(ImGuiKey_P)) app.cyclePreview();
        if (ImGui::IsKeyPressed(ImGuiKey_Home)) {
            app.frameView();
            app.setStatus("Tout afficher : zoom automatique sur la scène entière (Accueil)");
        }
        if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_F)) {
            app.frameSelection();
            app.setStatus("Cadrer la sélection : zoom automatique (Ctrl+F)");
        }
        if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S)) {
            app.exitPreview();
            openSaveDialog(app);
        }
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        if (app.dlgHelpOpen) app.dlgHelpOpen = false;
        else app.onEscape();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Slash)) app.dlgHelpOpen = !app.dlgHelpOpen;

    if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
        if (app.isCutTracing()) app.removeLastCutPoint();
        else if (app.isPolygonTracing()) app.removeLastPolygonPoint();
        else if (app.isShapeTracing()) app.cancelShapeTrace();
        else app.deleteSelection();
    }
    // Entrée : applique la découpe tracée (outil découpe armé).
    if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
        if (app.isCutArmed()) app.applyCut();
        else if (app.isPolygonArmed()) app.applyPolygon();
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
        openSaveDialog(app);
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
    // Ordre z des faces : ] vers l'avant (dessus), [ vers l'arrière (dessous).
    if (!io.KeyCtrl && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_RightBracket))
        app.faceForward();
    if (!io.KeyCtrl && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_LeftBracket))
        app.faceBackward();
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
    toggleShape(Tool::Crown, ImGuiKey_O, "couronne");

    // Outil découpe (D) : polygone soustrait au plan actif.
    if (!io.KeyCtrl && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_D))
        app.toggleCutTool();
    if (!io.KeyCtrl && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_U))
        app.togglePolygonTool();

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

    // Déplacement au clavier de la sélection (outil Sélection uniquement) :
    // flèches = 1 pas de grille, Maj = ×5 — Alt exclu (aligner/répartir),
    // Ctrl exclu. Une salve de flèches = une seule étape annulable.
    if (app.tool == Tool::Select && !io.KeyCtrl && !io.KeyAlt) {
        const float step = app.gridStep * (io.KeyShift ? 5.0f : 1.0f);
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) app.nudgeSelection(-step, 0.0f);
        else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) app.nudgeSelection(step, 0.0f);
        else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) app.nudgeSelection(0.0f, step);
        else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) app.nudgeSelection(0.0f, -step);
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
    // La barre occupe toute la largeur de la fenêtre, et chaque paquet de
    // boutons passe sur la ligne suivante dès qu'il n'y tient plus : aucun
    // bouton n'est jamais masqué, quelle que soit la largeur de la fenêtre.
    const float margin = 8.0f;
    // Largeur de la barre = largeur de la fenêtre de l'application moins les
    // marges. Le repli est calculé par rapport à CETTE valeur (déterministe),
    // pas par rapport à la largeur courante de la fenêtre ImGui : avec
    // AlwaysAutoResize, celle-ci n'est connue qu'en fin de frame, ce qui
    // empêchait le repli de se déclencher (tout restait sur une ligne).
    // Largeur de la barre : celle de la fenêtre de l'application (moins les
    // marges), jamais plus large que la fenêtre elle-même (fenêtres très
    // étroites), jamais plus étroite qu'un minimum lisible.
    const float maxW = std::min(
        std::max(260.0f, io.DisplaySize.x - 2.0f * margin), io.DisplaySize.x);
    ImGui::SetNextWindowPos(ImVec2(margin, margin), ImGuiCond_Always);
    // Largeur FORCÉE à celle de la fenêtre (min = max) : la barre remplit
    // toute la largeur quelle que soit la ligne la plus large. Le repli est
    // déterministe (largeurs réelles des paquets, cf. placePack), il ne dépend
    // pas de la largeur courante de la fenêtre — pas de boucle de taille.
    ImGui::SetNextWindowSizeConstraints(ImVec2(maxW, 0.0f),
                                        ImVec2(maxW, FLT_MAX));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.11f, 0.14f, 0.92f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::Begin("##toolbar", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_AlwaysAutoResize);

    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float groupSepW = 16.0f;  // groupSep() : 7 + 2 + 7 px
    const float bw = toolBtnWidth(nullptr);  // bouton à icône seule
    // Largeur de contenu disponible : largeur cible de la barre moins le
    // rembourrage de la fenêtre ImGui (padding gauche + droite).
    const float contentW = maxW - 2.0f * ImGui::GetStyle().WindowPadding.x;

    // Largeur totale d'un paquet : somme des items + espacements internes.
    auto packW = [&](std::initializer_list<float> ws) {
        float total = 0.0f;
        int n = 0;
        for (float w : ws) {
            total += w;
            ++n;
        }
        return total + (n > 0 ? (float)(n - 1) * spacing : 0.0f);
    };
    // Place le paquet suivant : même ligne avec séparateur s'il tient dans la
    // largeur restante, sinon en tête de la ligne suivante — aucun bouton n'est
    // jamais masqué, la barre se replie dynamiquement sur la largeur de la fenêtre.
    // Largeur déjà consommée sur la ligne courante. On ne peut PAS mesurer la
    // ligne avec GetCursorPosX() : après chaque item, ImGui ramène le curseur X
    // au début de ligne (ItemSize), SameLine() ne l'avance que temporairement
    // pour l'item suivant — après le dernier item d'un paquet, le curseur est
    // donc TOUJOURS au début de ligne. On tient notre propre comptabilité, et
    // le repli est calculé sur les largeurs réelles des paquets (packW), qui
    // correspondent exactement au rendu (toolBtnWidth / pillWidth partagés).
    // Un paquet passe TOUJOURS EN ENTIER à la ligne suivante : aucun bouton
    // d'un paquet ne peut se retrouver séparé du reste de son paquet.
    float lineX = 0.0f;
    auto placePack = [&](float w) {
        if (lineX == 0.0f) {
            lineX = w;  // premier paquet de la ligne : pas de séparateur
            return;
        }
        if (lineX + groupSepW + w <= contentW) {
            groupSep();
            lineX += groupSepW + w;
        } else {
            // Repli : AUCUN déplacement de curseur n'est nécessaire — après le
            // dernier item du paquet précédent, ImGui (ItemSize) a déjà placé
            // le curseur en tête de la ligne suivante (X au début, Y sur la
            // ligne d'après). Un NewLine() ajouterait une hauteur de ligne
            // supplémentaire (~20 px d'espace mort entre les lignes) : les
            // lignes sont donc naturellement serrées.
            lineX = w;
        }
    };

    // Bouton dédié d'un paquet (3.2) : chevron ▼ (ouvert) / ► (fermé) — un
    // clic ouvre/ferme le paquet, l'état (1 bit) est mémorisé dans les
    // préférences. Le chevron et le contenu forment UNE seule unité de repli :
    // fermé, seul le chevron reste (le paquet se rouvre d'un clic). À appeler
    // AVANT le contenu du paquet (avec sa largeur réelle `contentW`) : rend le
    // chevron puis place la suite sur la même ligne si le paquet est ouvert.
    // Retourne vrai si le contenu doit être affiché.
    auto packToggle = [&](ToolbarPack idx, const char* icon, const char* name,
                          const char* detail, float contentW) -> bool {
        const bool open = (app.toolbarPacks & (1u << idx)) != 0;
        // Comptabilité exacte : bouton + contenu = (n+1) items avec n écarts —
        // `contentW` (packW) ne compte que les écarts ENTRE les items du
        // contenu (n-1) ; l'écart bouton → 1er item est ajouté ici.
        placePack(open ? spacing + kPackToggleW + contentW : kPackToggleW);
        const float h = btnFrameHeight();
        ImGui::PushID(2000 + idx);  // ID stable, distinct des boutons d'icônes
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.10f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.16f));
        const bool clicked = ImGui::Button("##pack", ImVec2(kPackToggleW, h));
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();
        ImGui::PopID();
        if (clicked) {
            app.toolbarPacks ^= (1u << idx);
            app.setStatus(open ? std::string("Paquet « ") + name + " » masqué"
                               : std::string("Paquet « ") + name + " » affiché");
        }
        // Symbole du paquet à gauche (son sujet) + petit chevron d'état à
        // droite : ▼ vert quand le contenu est visible, ► sinon. Le symbole
        // identifie le paquet même replié ; si l'icône est introuvable, seul
        // le chevron reste (le bouton fonctionne quand même).
        const ImVec2 min = ImGui::GetItemRectMin();
        const float cy = (min.y + ImGui::GetItemRectMax().y) * 0.5f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        drawSvgIconNamed(dl, icon, ImVec2(min.x + 3.0f, cy - 7.0f), 14.0f,
                         open ? IM_COL32(230, 236, 246, 240) : kDimCol);
        const float chx = min.x + kPackToggleW - 8.0f;
        const float r = 3.0f;
        if (open) {
            dl->AddTriangleFilled(ImVec2(chx - r, cy - r), ImVec2(chx + r, cy - r),
                                  ImVec2(chx, cy + r), IM_COL32(120, 220, 140, 245));
        } else {
            dl->AddTriangleFilled(ImVec2(chx - r, cy - r), ImVec2(chx - r, cy + r),
                                  ImVec2(chx + r, cy), kDimCol);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("« %s » — %s · clic : %s", name, detail,
                              open ? "replier" : "déplier");
        if (open) ImGui::SameLine();
        return open;
    };

    // --- Paquet Canevas : grille, aimant, pas, réticule ---
    {
        const float stepW = ImGui::CalcTextSize("100.00").x + 10.0f;
        if (packToggle(kPackCanevas, "grid", "Canevas",
                       "grille, aimant, pas, réticule",
                       packW({bw, bw, stepW, bw}))) {
            if (toolBtnIcon("grid",
                            "Grille (G) : afficher/masquer · molette : ajuster le pas · "
                            "clic du milieu : réinitialiser",
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
            ImGui::SameLine();
            // Bouton Aimant : active / désactive l'aimantation, indépendamment de
            // l'affichage de la grille. Vert = aimantation active.
            if (toolBtnIcon("magnet", "Aimantation (Maj+G) : aimanter les points posés et "
                            "déplacés sur la grille",
                            app.snapOn, kGreen, false)) {
                app.snapOn = !app.snapOn;
                app.setStatus(app.snapOn ? "Aimantation activée (Maj+G)"
                                         : "Aimantation désactivée (Maj+G)");
            }
            ImGui::SameLine();
            // Cellule du pas de grille à largeur FIXE (la valeur ne décale pas la barre).
            {
                char stepbuf[16];
                std::snprintf(stepbuf, sizeof(stepbuf), "%.2f", app.gridStep);
                valueLabel(stepbuf, stepW);
            }
            ImGui::SameLine();
            if (toolBtnIcon("reticle", "Réticule (Y) : désactivé / simple / symétrique / miroir",
                            app.reticle != ReticleState::Off, kGreen, false))
                app.cycleReticle();
        }
    }

    // --- Paquet Affichage : prévisualisation, toutes couleurs, fps ---
    {
        char fpsbuf[32];
        std::snprintf(fpsbuf, sizeof(fpsbuf), "%.0f fps", app.fps);
        const float fpsW =
            pillWidth(fpsbuf, "fps", ImGui::CalcTextSize("120 fps").x + 18.0f + 20.0f);
        if (packToggle(kPackAffichage, "preview", "Affichage",
                       "prévisualisation, couleurs, images par seconde",
                       packW({bw, bw, fpsW}))) {
            if (toolBtnIcon("preview", "Prévisualiser (P) : aperçu simple → tous les plans → édition",
                            app.preview != PreviewMode::Off, kAmber, false))
                app.cyclePreview();
            ImGui::SameLine();
            // Affichage des plans (7.6) : clic = cycle normal → toutes couleurs →
            // filaire → normal. L'icône et l'infobulle suivent l'état courant.
            if (toolBtnIcon(app.wireframe ? "wireframe" : "show-all-fills",
                            app.wireframe
                                ? "Mode filaire : arêtes seules, sans remplissage — "
                                  "clic : revenir au rendu normal (7.6)"
                                : app.allColors
                                    ? "Toutes couleurs : remplir tous les plans pendant "
                                      "l'édition — clic : mode filaire (7.6)"
                                    : "Rendu normal : seul le plan actif est rempli — "
                                      "clic : toutes couleurs (7.6)",
                            app.allColors || app.wireframe, kGreen, false))
                app.cycleFillMode();
            ImGui::SameLine();
            pill("##pillfps", fpsbuf, app.fpsPillGreen ? kGreen : kAmber, "fps",
                 ImGui::CalcTextSize("120 fps").x + 18.0f + 20.0f);
        }
    }

    // --- Paquet Vue : tout afficher, cadrer, mesure ---
    {
        if (packToggle(kPackVue, "fit-view", "Vue",
                       "tout afficher, cadrer la sélection, mesure",
                       packW({bw, bw, bw}))) {
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
        }
    }

    // --- Paquet Sélection : cible, tout sélectionner (+menu), compteur ---
    {
        const char* targetLabel = selModeName(app.selMode);
        const std::string selCount = std::to_string(app.selectionCount());
        const float selPillW =
            pillWidth(selCount.c_str(), nullptr, ImGui::CalcTextSize("9999").x + 18.0f);
        // Sélection chaînée (5.11) : grisée quand la sélection courante est vide
        // (l'action n'a rien à chaîner — convention « actions indisponibles »).
        const bool linkedEnabled =
            app.selMode == SelMode::Face   ? !app.selFaces.empty()
            : app.selMode == SelMode::Edge ? !app.selEdges.empty()
                                           : !app.selVerts.empty();
        // Opérations ensemblistes (5.12) : Mémoriser A / Mémoriser B capturent
        // chacun les FACES formées par la sélection courante (cible triangle :
        // faces sélectionnées · sommet : faces dont tous les sommets sont
        // sélectionnés · segment : faces dont toutes les arêtes sont
        // sélectionnées) ; le bouton « Booléennes » ouvre le popup des
        // opérations (union, intersection, différence, symétrique).
        const bool anySel =
            app.selMode == SelMode::Face   ? !app.selFaces.empty()
            : app.selMode == SelMode::Edge ? !app.selEdges.empty()
                                           : !app.selVerts.empty();
        const std::string boolTip =
            "Opérations ensemblistes (5.12) : deux ensembles de faces A et B, "
            "chacun mémorisé depuis la sélection courante — les faces formées "
            "par la sélection : cible triangle = faces sélectionnées, cible "
            "sommet = faces dont TOUS les sommets sont sélectionnés (4 coins "
            "d'un rectangle → ses triangles), cible segment = faces dont TOUTES "
            "les arêtes sont sélectionnées (le pourtour d'un rectangle doit "
            "inclure sa diagonale interne) — union (A∪B), intersection (A∩B), "
            "différence (A−B), symétrique (A△B) · dans la zone des deux "
            "ensembles, seule la géométrie du résultat est conservée (le reste "
            "du plan est intact) · le résultat reste sélectionné dans la cible "
            "active (triangle : ses faces · sommet : ses sommets · segment : "
            "ses arêtes)";
        if (packToggle(kPackSelection, "selection-mode", "Sélection",
                       "cible, tout sélectionner, chaînée, lasso, booléennes, compteur",
                       packW({toolBtnWidth(targetLabel), bw, bw, bw,
                              toolBtnWidth("A"), toolBtnWidth("B"),
                              toolBtnWidth("Booléennes"), selPillW}))) {
            if (toolBtnIcon("selection-mode", "Cible d'édition : sommet / segment / triangle",
                            false, kGreen, false, targetLabel))
                app.cycleTarget();
            ImGui::SameLine();
            // Clic gauche = tout sélectionner (Ctrl+A) ; clic droit = menu contextuel.
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
                if (toolBtnIcon("invert-selection", "Inverser la sélection (Ctrl+I)", false,
                                kGreen, false, "Inverser la sélection", 190.0f)) {
                    app.invertSelection();
                    ImGui::CloseCurrentPopup();
                }
                if (toolBtnIcon("linked", "Sélection chaînée : tous les éléments liés à la "
                                "sélection courante (triangles par ≥ 1 sommet partagé, "
                                "segments par sommet partagé, sommets par segment) — Ctrl ou "
                                "Maj : ajouter, sinon remplacer", false, kGreen, !linkedEnabled,
                                "Sélection chaînée", 190.0f)) {
                    app.selectLinked();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            // Lasso (5.9) : tracé libre autour des éléments à sélectionner d'un
            // coup (sommet par sa position, segment par son milieu, triangle par
            // son centre) — Maj au relâchement ajoute à la sélection.
            ImGui::SameLine();
            if (toolBtnIcon(
                    "lasso",
                    app.lassoArmed
                        ? "Sélection au lasso armée — clic gauche + glisser : encercler "
                          "les éléments à sélectionner (Maj : ajouter) · clic droit ou "
                          "Échap : désarmer"
                        : "Sélection au lasso (5.9) : tracer librement autour des "
                          "sommets, segments ou faces à sélectionner d'un coup",
                    app.lassoArmed, kGreen, false))
                app.toggleLasso();
            // Sélection chaînée (5.11) : tous les éléments liés à la sélection
            // courante par des chaînes d'adjacence — triangles par ≥ 1 sommet
            // partagé, segments par un sommet partagé, sommets par un segment.
            // Ctrl ou Maj : ajoute à la sélection, sinon remplace.
            ImGui::SameLine();
            if (toolBtnIcon("linked", "Sélection chaînée : sélectionne tous les éléments liés "
                             "à la sélection courante (triangles par un sommet partagé, "
                             "segments par un sommet partagé, sommets par un segment) · "
                             "Ctrl ou Maj : ajouter, sinon remplacer", false, kGreen,
                             !linkedEnabled))
                app.selectLinked();
            // Opérations ensemblistes (5.12) : mémoriser l'ensemble A puis
            // l'ensemble B (chacun = les faces formées par la sélection courante),
            // puis appliquer une opération entre eux. Grisé sans sélection (rien
            // à capturer — convention « actions indisponibles »).
            ImGui::SameLine();
            if (toolBtnIcon(
                    "set-a",
                    app.boolSetValid(0)
                        ? ("Ensemble A mémorisé : " +
                           std::to_string(app.boolSetCount(0)) +
                           " face(s) — re-clic : remplacer par la sélection "
                           "courante").c_str()
                        : "Mémoriser l'ensemble A = les faces formées par la "
                          "sélection courante (cible triangle : faces "
                          "sélectionnées · sommet : tous les sommets d'un "
                          "polygone · segment : toutes ses arêtes) · puis "
                          "mémoriser B et choisir une opération (bouton « "
                          "Booléennes »)",
                    app.boolSetValid(0), kGreen, !anySel, "A"))
                app.memorizeBoolSet(0);
            ImGui::SameLine();
            if (toolBtnIcon(
                    "set-b",
                    app.boolSetValid(1)
                        ? ("Ensemble B mémorisé : " +
                           std::to_string(app.boolSetCount(1)) +
                           " face(s) — re-clic : remplacer par la sélection "
                           "courante").c_str()
                        : "Mémoriser l'ensemble B = les faces formées par la "
                          "sélection courante (cible triangle : faces "
                          "sélectionnées · sommet : tous les sommets d'un "
                          "polygone · segment : toutes ses arêtes) · puis "
                          "choisir une opération (bouton « Booléennes »)",
                    app.boolSetValid(1), kGreen, !anySel, "B"))
                app.memorizeBoolSet(1);
            ImGui::SameLine();
            // Popup des opérations : union / intersection / différence /
            // symétrique, actives quand les deux ensembles sont mémorisés sur le
            // plan actif. Chaque opération est une seule étape annulable (Ctrl+Z).
            if (toolBtnIcon("bool", boolTip.c_str(), false, kGreen, false, "Booléennes"))
                ImGui::OpenPopup("##boolmenu");
            if (ImGui::BeginPopup("##boolmenu")) {
                ImGui::TextDisabled("Ensembles mémorisés (plan actif) :");
                ImGui::TextDisabled("A : %zu face(s) · B : %zu face(s)",
                                    app.boolSetCount(0), app.boolSetCount(1));
                if (app.boolSetValid(0) && app.boolSetValid(1)) {
                    ImGui::Separator();
                    const float bw2 = dialogBtnWidth(
                        {"Union (A ∪ B)", "Intersection (A ∩ B)", "Différence (A − B)",
                         "Différence symétrique (A △ B)"});
                    // Chaque opération dans une portée d'ID dédiée : les quatre
                    // boutons partagent la même icône « bool » — sans cette portée,
                    // toolBtnIcon (PushID(icône)) confondrait leurs états.
                    ImGui::PushID("bool-union");
                    if (toolBtnIcon("bool",
                                    "Union : tout ce qui est dans A ou dans B "
                                    "(A ∪ B) — le résultat devient la sélection",
                                    false, kGreen, false, "Union (A ∪ B)", bw2)) {
                        app.applyBoolOp(SetOp::Union);
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::PopID();
                    ImGui::PushID("bool-inter");
                    if (toolBtnIcon("bool",
                                    "Intersection : la zone commune aux deux "
                                    "ensembles (A ∩ B)",
                                    false, kGreen, false, "Intersection (A ∩ B)", bw2)) {
                        app.applyBoolOp(SetOp::Intersection);
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::PopID();
                    ImGui::PushID("bool-diff");
                    if (toolBtnIcon("bool",
                                    "Différence : ce qui est dans A mais pas dans B "
                                    "(A − B)",
                                    false, kGreen, false, "Différence (A − B)", bw2)) {
                        app.applyBoolOp(SetOp::Difference);
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::PopID();
                    ImGui::PushID("bool-sym");
                    if (toolBtnIcon("bool",
                                    "Différence symétrique : ce qui est dans l'un ou "
                                    "l'autre, pas dans les deux (A △ B)",
                                    false, kGreen, false, "Différence symétrique (A △ B)",
                                    bw2)) {
                        app.applyBoolOp(SetOp::SymDiff);
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::PopID();
                    ImGui::Separator();
                    if (toolBtnIcon("clear-console", "Oublier les deux ensembles mémorisés",
                                    false, kRed, false, "Effacer", bw2)) {
                        app.clearBoolSets();
                        ImGui::CloseCurrentPopup();
                    }
                } else {
                    ImGui::TextDisabled("Mémorisez d'abord les deux ensembles A et B "
                                        "(boutons « A » et « B » — les faces formées "
                                        "par la sélection).");
                }
                ImGui::EndPopup();
            }
            // Compteur TOUJOURS présent (0 inclus), à largeur fixe : la barre ne
            // change pas de dimension selon la sélection.
            ImGui::SameLine();
            pill("##pillsel", selCount.c_str(), kGreen, nullptr,
                 ImGui::CalcTextSize("9999").x + 18.0f);
        }
    }

    // --- Paquet Presse-papiers : copier, couper, coller, dupliquer ---
    {
        if (packToggle(kPackPressePapiers, "copy", "Presse-papiers",
                       "copier, couper, coller, dupliquer",
                       packW({bw, bw, bw, bw}))) {
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
        }
    }

    // --- Paquet Outils : peinture, pipette, aligner, rotation, échelle,
    // découpe, polygone, formes ---
    {
        if (packToggle(kPackOutils, "shapes", "Outils",
                       "peinture, pipette, aligner, rotation, échelle, découpe, "
                       "polygone, formes",
                       packW({bw, bw, bw, bw, bw, bw, bw, bw}))) {
            if (toolBtnIcon("triangle-color", "Peinture : palette de couleurs et pinceau",
                            app.paletteOpen, kGreen, false))
                app.paletteOpen = !app.paletteOpen;
            ImGui::SameLine();
            // Pipette (6.5) : prélever la couleur affichée au canvas (faces, image
            // de fond, couleur du fond…) et la poser comme couleur de pinceau.
            if (toolBtnIcon("pipette",
                            app.pipetteArmed
                                ? "Pipette armée — clic gauche sur le canvas : prélever la "
                                  "couleur affichée · clic droit ou Échap : désarmer"
                                : "Pipette (6.5) : prélever une couleur affichée au canvas "
                                  "(faces, calque d'image, fond…) et en faire la couleur "
                                  "du pinceau",
                            app.pipetteArmed, kGreen, false))
                app.togglePipette();
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
            if (toolBtnIcon("shape-cut",
                            "Découper le plan avec un polygone (D) — clics gauches : "
                            "sommets du polygone · clic droit ou Entrée : découper "
                            "(soustraction) · l'outil reste armé : chaque découpe "
                            "s'ajoute à la même étape annulable jusqu'à Échap / D · "
                            "Retour arrière : retirer le dernier point",
                            app.isCutArmed(), kGreen, false))
                app.toggleCutTool();
            ImGui::SameLine();
            if (toolBtnIcon("shape-polygon",  // icône distincte pour éviter conflit ID ImGui
                            "Tracer un polygone et le trianguler automatiquement (U) — "
                            "clics gauches : sommets du polygone · clic droit ou Entrée : "
                            "valider et créer les triangles · Retour arrière : dernier point",
                            app.isPolygonArmed(), kGreen, false))
                app.togglePolygonTool();
            ImGui::SameLine();
            if (toolBtnIcon("shapes",
                            "Formes prédéfinies — clic : menu contextuel "
                            "(cercle, carré, étoile, anneau, couronne…) · molette : "
                            "côtés/pointes si une forme à côtés est armée",
                            app.isShapeArmed(), kGreen, false))
                ImGui::OpenPopup("##shapesmenu");
            // Molette sur le bouton : règle les côtés/pointes si la forme armée en a.
            // Couronne : la molette suit la phase du tracé — extérieurs tant que le
            // rayon n'est pas verrouillé, intérieurs après le 2e clic ; Maj+molette
            // force l'autre jeu de côtés.
            if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f &&
                (app.tool == Tool::Circle || app.tool == Tool::Ring ||
                 app.tool == Tool::Star || app.tool == Tool::Crown)) {
                // La molette suit la phase du tracé (comme le canvas) : extérieurs
                // tant que le rayon n'est pas verrouillé, intérieurs après le 2e
                // clic ; Maj+molette force l'autre jeu de côtés.
                if (app.tool == Tool::Crown) {
                    const bool adjustInner = io.KeyShift != app.crownInnerPhase();
                    if (adjustInner)
                        app.crownInnerSides =
                            std::clamp(app.crownInnerSides + (int)std::lround(io.MouseWheel), 3, 64);
                    else
                        app.circleSides =
                            std::clamp(app.circleSides + (int)std::lround(io.MouseWheel), 3, 64);
                    app.statusCrown();
                } else {
                    app.circleSides =
                        std::clamp(app.circleSides + (int)std::lround(io.MouseWheel), 3, 64);
                    app.setStatus("Nombre de " +
                                  std::string(app.tool == Tool::Star
                                                  ? "pointes de l'étoile"
                                                  : "côtés du " +
                                                        std::string(app.tool == Tool::Circle
                                                                        ? "cercle"
                                                                        : "anneau")) +
                                  " : " + std::to_string(app.circleSides));
                }
            }
            shapesMenu(app);
        }
    }

    // --- Paquet Ordre z des faces : avant (]) / arrière ([) ---
    {
        const bool hasFaces = app.selMode == SelMode::Face && !app.selFaces.empty();
        if (packToggle(kPackOrdreZ, "face-front", "Ordre z",
                       "faces sélectionnées devant / derrière",
                       packW({bw, bw}))) {
            if (toolBtnIcon("face-front",
                            "Faces sélectionnées vers l'avant (]) : dessinées au-dessus "
                            "de celles qui les recouvraient",
                            false, kGreen, !hasFaces))
                app.faceForward();
            ImGui::SameLine();
            if (toolBtnIcon("face-back",
                            "Faces sélectionnées vers l'arrière ([) : passées sous celles "
                            "qui les recouvraient",
                            false, kGreen, !hasFaces))
                app.faceBackward();
        }
    }

    // --- Paquet Fusion des points (5.5 / 5.6) ---
    {
        const bool mergeArmed = app.mergeMode != App::MergeMode::Off;
        const bool mergeCanArm = app.selMode == SelMode::Vertex && app.selVerts.size() == 1;
        const bool mergeCanGroup = app.selMode == SelMode::Vertex && app.selVerts.size() >= 2;
        const float radW = ImGui::CalcTextSize("64").x;
        if (packToggle(kPackFusion, "merge-points", "Fusion",
                       "fusionner les points superposés (5.5/5.6)",
                       mergeArmed ? packW({bw, radW}) : packW({bw}))) {
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
            // Molette sur le bouton : rayon de fusion (8 à 64 px écran).
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
                valueLabel(radbuf, radW);
            }
        }
    }

    // --- Paquet Annuler / Rétablir (avec compteurs) ---
    {
        const std::string undoN = std::to_string(app.undoStack.size());
        const std::string redoN = std::to_string(app.redoStack.size());
        const float undoW = pillWidth(undoN.c_str(), nullptr, ImGui::CalcTextSize("50").x + 18.0f);
        const float redoW = pillWidth(redoN.c_str(), nullptr, ImGui::CalcTextSize("50").x + 18.0f);
        if (packToggle(kPackAnnuler, "undo", "Annuler / Rétablir",
                       "annuler, rétablir (avec compteurs)",
                       packW({bw, undoW, bw, redoW}))) {
            if (toolBtnIcon("undo", "Annuler (Ctrl+Z)", false, kGreen, app.undoStack.empty()))
                app.undo();
            ImGui::SameLine();
            pill("##pillundo", undoN.c_str(), kGreen, nullptr,
                 ImGui::CalcTextSize("50").x + 18.0f);
            ImGui::SameLine();
            if (toolBtnIcon("redo", "Rétablir (Ctrl+Maj+Z ou Ctrl+Y)", false, kGreen,
                            app.redoStack.empty()))
                app.redo();
            ImGui::SameLine();
            pill("##pillredo", redoN.c_str(), kGreen, nullptr,
                 ImGui::CalcTextSize("50").x + 18.0f);
        }
    }

    // --- Paquet Sauvegarde : scène, SVG, PNG, historique ---
    {
        if (packToggle(kPackSauvegarde, "export", "Sauvegarde",
                       "scène, SVG, PNG, historique",
                       packW({bw, bw, bw, bw}))) {
            if (toolBtnIcon("export", "Enregistrer la scène (Ctrl+S) — fenêtre d'emplacement",
                            false, kGreen, false))
                openSaveDialog(app);
            ImGui::SameLine();
            if (toolBtnIcon("export-svg", "Exporter le plan actif en SVG vectoriel",
                            false, kGreen, false))
                app.dlgSvgOpen = true;
            ImGui::SameLine();
            if (toolBtnIcon("image", "Exporter l'image de la vue actuelle en PNG "
                                      "(édition ou prévisualisation, sans l'interface)",
                            false, kGreen, false))
                app.dlgPngOpen = true;
            ImGui::SameLine();
            if (toolBtnIcon("history", "Historique : versions horodatées de l'autosave "
                                        "(restaurer un état antérieur)",
                            false, kGreen, app.versionFiles.empty()))
                app.dlgVersionsOpen = true;
        }
    }

    // --- Paquet Entrées : meshes, JSON, OBJ ---
    {
        if (packToggle(kPackEntrees, "import-json", "Entrées",
                       "charger meshes, JSON, OBJ",
                       packW({bw, bw, bw}))) {
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
        }
    }

    // --- Paquets Plans (7) : découpés en sous-paquets compacts pour que
    // chacun tienne même dans une fenêtre très étroite (jamais de clipping).
    {
        int n = app.scene.count();
        bool canNav = n >= 2;
        char planbuf[24];
        std::snprintf(planbuf, sizeof(planbuf), "%d/%d", app.scene.active + 1, n);
        const float planPillW =
            pillWidth(planbuf, nullptr, ImGui::CalcTextSize("12/12").x + 18.0f);
        // Navigation : précédent, suivant, compteur.
        if (packToggle(kPackPlansNav, "next-shape", "Plans — navigation",
                       "plan précédent / suivant, compteur",
                       packW({bw, bw, planPillW}))) {
            if (toolBtnIcon("prev-shape", "Plan précédent (i-1)", false, kGreen, !canNav))
                app.prevPlane();
            ImGui::SameLine();
            if (toolBtnIcon("next-shape", "Plan suivant (i+1)", false, kGreen, !canNav))
                app.nextPlane();
            ImGui::SameLine();
            pill("##pillplan", planbuf, kGreen, nullptr,
                 ImGui::CalcTextSize("12/12").x + 18.0f);
        }
        // Édition : dupliquer, renommer.
        if (packToggle(kPackPlansEdition, "duplicate-plane", "Plans — édition",
                       "dupliquer, renommer",
                       packW({bw, bw}))) {
            if (toolBtnIcon("duplicate-plane",
                            "Dupliquer le plan actif (Alt+D) — copie complète "
                            "avec ses couleurs, insérée juste au-dessus",
                            false, kGreen, n < 1))
                app.duplicatePlane();
            ImGui::SameLine();
            if (toolBtnIcon("rename", "Renommer le plan actif (nom affiché au kiosque et au HUD)",
                            app.dlgRenameOpen, kGreen, n < 1)) {
                app.dlgRenameOpen = true;
                std::snprintf(app.dlgRenameName, sizeof(app.dlgRenameName), "%s",
                              planeLabel(app, app.scene.active).c_str());
            }
        }
        // Ordre : monter, descendre.
        if (packToggle(kPackPlansOrdre, "move-shape-up", "Plans — ordre",
                       "monter / descendre",
                       packW({bw, bw}))) {
            if (toolBtnIcon("move-shape-up",
                            "Monter le plan actif (Alt+Flèche haut) — il recouvre davantage",
                            false, kGreen, app.scene.active >= n - 1))
                app.planeUp();
            ImGui::SameLine();
            if (toolBtnIcon("move-shape-down", "Descendre le plan actif (Alt+Flèche bas)", false,
                            kGreen, app.scene.active <= 0))
                app.planeDown();
        }
        // Gestion : ajouter, supprimer, kiosque, opacité.
        if (packToggle(kPackPlansGestion, "new-shape", "Plans — gestion",
                       "ajouter, supprimer, kiosque, opacité",
                       packW({bw, bw, bw, bw}))) {
            const bool plusClicked = toolBtnIcon(
                "new-shape", "Ajouter un plan vide — clic gauche : avant le plan courant ; "
                             "clic droit : après",
                false, kGreen, false);
            if (plusClicked) app.addPlane(false);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) app.addPlane(true);
            n = app.scene.count();  // rafraîchit après un ajout (états ×/K à jour)
            canNav = n >= 2;
            ImGui::SameLine();
            if (toolBtnIcon("delete-shape", "Supprimer le plan actif (confirmation)", false,
                            kRed, n <= 1))
                app.dlgDeletePlaneOpen = true;
            ImGui::SameLine();
            if (toolBtnIcon("kiosk", "Kiosque : choisir le plan en couverture (Alt+K)", app.kiosk,
                            kGreen, !canNav))
                app.toggleKiosk();
            // Opacité du plan actif (7.8) : popup avec curseur ; molette sur le
            // bouton : ajuste par pas de 5 % (une seule étape annulable par salve).
            ImGui::SameLine();
            if (toolBtnIcon("opacity",
                            "Opacité du plan actif — clic : régler la transparence "
                            "(superposer les plans) · molette : ajuster vite",
                            ImGui::IsPopupOpen("##opacmenu"), kGreen, false))
                ImGui::OpenPopup("##opacmenu");
            if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f && n >= 1) {
                Mesh2D& m = app.scene.activePlane();
                if (!app.opacUndoPushed_) {
                    app.pushUndo();
                    app.opacUndoPushed_ = true;
                }
                m.opacity = std::clamp(m.opacity + 0.05f * io.MouseWheel, 0.0f, 1.0f);
                app.dirty = true;
                app.setStatus("Opacité du plan actif : " +
                              std::to_string((int)std::lround(m.opacity * 100.0f)) + " %");
            }
            opacityPopup(app);
        }
        // Réarmement de la salve de molette : hors du bloc du paquet pour que
        // la fermeture du paquet en pleine salve ne fige pas le drapeau (deux
        // ajustements séparés doivent rester deux étapes annulables).
        if (io.MouseWheel == 0.0f) app.opacUndoPushed_ = false;
    }

    // --- Paquet Scène : manipuler (anneau : sélection / plan / scène), fond,
    // reset, calque ---
    {
        // Portée d'ID dédiée : « rotate » et « scale » sont déjà utilisés par
        // le paquet Outils de la même fenêtre — ImGui identifie les items par
        // la pile d'ID (ici l'icône), sans cette portée les états des boutons
        // se confondraient (survol, clic, actif). Le popup « ##bgmenu » reste
        // cohérent : OpenPopup / IsPopupOpen / BeginPopup sont dans la même
        // portée.
        ImGui::PushID("scene-pack");
        if (packToggle(kPackScene, "grab", "Scène",
                       "manipuler (sélection / plan / scène), fond, reset, calque",
                       packW({toolBtnWidth("Sélection"), toolBtnWidth("Plan"),
                              toolBtnWidth("Scène"), bw, bw, bw}))) {
            // Anneau de manipulation unifié des maillages (sélection / plan
            // courant / scène complète) : même principe que le calque (7.7) —
            // une poignée par action. TROIS boutons radio, un par cible : la
            // cible active est mise en évidence et désactive les autres ; clic
            // sur une autre cible = changer de cible et armer l'anneau, re-clic
            // sur la cible active = désarmer (comme le re-clic du bouton).
            auto ringBtn = [&](const char* icon, RingTarget t, const char* label,
                               const char* tip) {
                const bool active = app.ringArmed && app.ringTarget == t;
                if (toolBtnIcon(icon, tip, active, kGreen, false, label)) {
                    if (active)
                        app.setRingTarget(RingTarget::None);  // re-clic : désarmer
                    else
                        app.setRingTarget(t);  // change de cible et arme
                }
            };
            ringBtn("selection-mode", RingTarget::Selection, "Sélection",
                    "Manipuler la sélection courante du plan actif avec l'anneau "
                    "de poignées (une action par poignée) — clic : armer · re-clic "
                    "sur la cible active : désarmer");
            ImGui::SameLine();
            // Icône « duplicate-plane » (et non « layer », réservée au bouton
            // Calque du même paquet) : les boutons du paquet partagent la même
            // portée d'ID ImGui — deux boutons avec la même icône se
            // confondraient (survol, clic, actif).
            ringBtn("duplicate-plane", RingTarget::Plane, "Plan",
                    "Manipuler tout le plan actif (tous ses sommets) avec l'anneau "
                    "de poignées — clic : armer · re-clic sur la cible active : "
                    "désarmer");
            ImGui::SameLine();
            ringBtn("fit-view", RingTarget::Scene, "Scène",
                    "Manipuler la scène complète (tous les plans ensemble) avec "
                    "l'anneau de poignées — clic : armer · re-clic sur la cible "
                    "active : désarmer");
            ImGui::SameLine();
            if (toolBtnIcon("background",
                            "Couleur du fond du canvas — clic : choisir une teinte "
                            "(teinte en direct, molette : nuances rapides)",
                            ImGui::IsPopupOpen("##bgmenu"), kBlue, false))
                ImGui::OpenPopup("##bgmenu");
            // Molette sur le bouton : éclaircit (haut) / fonce (bas) le fond.
            if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f) {
                const float k = std::pow(1.1f, io.MouseWheel);  // haut → plus clair
                app.bgColor.r = std::clamp(app.bgColor.r * k, 0.0f, 1.0f);
                app.bgColor.g = std::clamp(app.bgColor.g * k, 0.0f, 1.0f);
                app.bgColor.b = std::clamp(app.bgColor.b * k, 0.0f, 1.0f);
                app.bgColor.a = 1.0f;
                app.setStatus("Couleur du fond : " + std::to_string((int)(app.bgColor.r * 255)) +
                              "," + std::to_string((int)(app.bgColor.g * 255)) + "," +
                              std::to_string((int)(app.bgColor.b * 255)));
            }
            ImGui::SameLine();
            if (toolBtnIcon("reset", "Réinitialiser entièrement la scène (Maj+Retour arrière)",
                            false, kRed, false))
                app.dlgResetOpen = true;
            ImGui::SameLine();
            // Calque d'image de fond (7.7) : clic = popup (charger, opacité,
            // manipulation au canvas). Vert = outil de manipulation armé, ambre =
            // calque chargé mais non armé.
            const bool layerArmed = app.layerArmed;
            const bool layerLoaded = app.imageTex != 0;
            if (toolBtnIcon("layer",
                            layerArmed
                                ? "Manipulation du calque armée — voir l'outil choisi dans "
                                  "le popup · clic droit ou Échap au canvas : désarmer"
                                : "Image de fond (calque) : clic pour charger une image, "
                                  "régler son opacité (0 à 100 %), ou la manipuler au canvas "
                                  "(7.7) · molette : ajuster l'opacité vite",
                            layerArmed, layerLoaded ? kAmber : kGreen, false))
                ImGui::OpenPopup("##layermenu");
            // Molette sur le bouton : opacité du calque par pas de 5 % (une seule
            // étape annulable par salve, comme le bouton « Opacité » des plans).
            if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f && layerLoaded) {
                ImageLayer& il = app.scene.image;
                if (!app.layerOpacUndoPushed_) {
                    app.pushUndo();
                    app.layerOpacUndoPushed_ = true;
                }
                il.opacity = std::clamp(il.opacity + 0.05f * io.MouseWheel, 0.0f, 1.0f);
                app.dirty = true;
                app.setStatus("Opacité du calque : " +
                              std::to_string((int)std::lround(il.opacity * 100.0f)) + " %");
            }
            layerPopup(app);
            bgColorPopup(app);
        }
        // Réarmement de la salve de molette (opacité du calque) : hors du bloc
        // du paquet pour la même raison que l'opacité des plans.
        if (io.MouseWheel == 0.0f) app.layerOpacUndoPushed_ = false;
        ImGui::PopID();
    }

    // --- Paquet Interface : console, aide, réglages ---
    {
        if (packToggle(kPackInterface, "settings", "Interface",
                       "console, aide, réglages",
                       packW({bw, bw, bw}))) {
            if (toolBtnIcon("console", "Afficher / masquer la console de messages",
                            app.consoleVisible, kGreen, false))
                app.consoleVisible = !app.consoleVisible;
            ImGui::SameLine();
            if (toolBtnIcon("help", "Fenêtre d'aide et raccourcis (?)", app.dlgHelpOpen, kGreen,
                            false))
                app.dlgHelpOpen = !app.dlgHelpOpen;
            ImGui::SameLine();
            if (toolBtnIcon("settings", "Réglages : distances de détection des sommets et des segments",
                            app.settingsOpen, kGreen, false))
                app.settingsOpen = !app.settingsOpen;
        }
    }

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

    // Statistiques du plan actif : nom (éventuel), sommets, triangles et aire.
    {
        const Mesh2D& m = app.scene.activePlane();
        std::snprintf(buf, sizeof(buf), "%s (%d/%d) : %d sommets · %d triangles · "
                                         "aire %.2f",
                      planeLabel(app, app.scene.active).c_str(),
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

    // Anneau de manipulation unifié (calque 7.7 / maillage : sélection, plan
    // courant, scène complète) : une SEULE poignée par action, sur le cercle de
    // 40 px — échelle (X cyan, Y ambre, uniforme blanc), déplacement contraint
    // (flèches vertes) et symétries (pastilles rouges, clic instantané).
    //  - Centre : déplacement libre (disque blanc)
    //  - Anneau rotation (55px) : piste bleue avec graduations
    const bool ringActive = app.ringArmed ||
                            (app.layerArmed && app.scene.image.path.size() > 0 &&
                             app.scene.image.visible);
    if (ringActive && app.preview == PreviewMode::Off) {
        const Vec2 ringAnchor = app.layerArmed ? app.layerAnchor : app.ringAnchor;
        const bool ringAnchored = app.layerArmed ? app.layerAnchored : app.ringAnchored;
        const float kScaleR = 40.0f;   // rayon des poignées
        const float kRotR   = 55.0f;   // rayon rotation
        const Vec2 vps = app.viewportVec2();
        const ImVec2 mp = io.MousePos;
        Vec2 centerScreen;
        if (ringAnchored) {
            const Vec2 sp = app.camera.worldToScreen(ringAnchor, vps);
            centerScreen = {pos.x + sp.x, pos.y + sp.y};
        } else {
            centerScreen = {mp.x, mp.y};
        }
        const ImVec2 c(centerScreen.x, centerScreen.y);

        // ---- Centre : disque MoveFree ----
        {
            const bool hover = std::sqrt((mp.x - c.x) * (mp.x - c.x) +
                                         (mp.y - c.y) * (mp.y - c.y)) < 14.0f;
            // Halo diffus autour du centre
            dl->AddCircle(c, 22.0f, IM_COL32(200, 210, 230, 25), 0, 8.0f);
            dl->AddCircle(c, 18.0f, IM_COL32(220, 230, 245, 40), 0, 4.0f);
            const ImU32 col = hover ? IM_COL32(255, 255, 255, 240) : IM_COL32(255, 255, 255, 160);
            dl->AddCircleFilled(c, hover ? 12.0f : 10.0f, col);
            dl->AddCircle(c, 14.0f, IM_COL32(255, 255, 255, 60));
            if (hover) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                ImGui::SetTooltip("Déplacer librement — clic + glisser");
            }
        }

        // ---- Anneau rotation (55px) : piste avec graduations ----
        {
            // Halo lumineux : anneaux diffus dégradés pour lisibilité sur fond clair
            dl->AddCircle(c, kRotR, IM_COL32(40, 140, 210, 20), 0, 16.0f);
            dl->AddCircle(c, kRotR, IM_COL32(50, 160, 225, 35), 0, 10.0f);
            dl->AddCircle(c, kRotR, IM_COL32(60, 170, 235, 55), 0, 6.0f);
            // Anneau principal
            dl->AddCircle(c, kRotR, IM_COL32(70, 180, 240, 140), 0, 2.5f);
            dl->AddCircle(c, kRotR, IM_COL32(70, 180, 240, 60), 0, 1.0f);
            // Graduations aux 4 coins
            for (int i = 0; i < 4; ++i) {
                const float rad = (45.0f + i * 90.0f) * kPiF / 180.0f;
                const float r1 = kRotR - 5.0f, r2 = kRotR + 5.0f;
                dl->AddLine(ImVec2(c.x + std::cos(rad) * r1, c.y + std::sin(rad) * r1),
                            ImVec2(c.x + std::cos(rad) * r2, c.y + std::sin(rad) * r2),
                            IM_COL32(70, 180, 240, 180), 2.0f);
            }
            // Zone de rotation : la bande annulaire autour de l'anneau
            // (40-68 px) — survol mis en évidence, glisser = pivoter.
            const float dc = std::sqrt((mp.x - c.x) * (mp.x - c.x) +
                                       (mp.y - c.y) * (mp.y - c.y));
            if (dc > 40.0f && dc < 68.0f) {
                dl->AddCircle(c, kRotR, IM_COL32(130, 215, 255, 230), 0, 3.5f);
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                ImGui::SetTooltip("Rotation — glissez autour de l'anneau");
            }
        }

        // ---- 8 poignées sur le cercle 40px, UNE par action ----
        // Échelle : carreau cyan (X, E) / ambre (Y, N) + losange blanc
        // (uniforme, NE) · déplacement : flèches vertes (W = X, S = Y) ·
        // symétries : pastilles rouges (SE = miroir X, NW = miroir Y,
        // SW = miroir X/Y) — clic instantané.
        struct RingSlot { float deg; LayerHandleKind kind; const char* tip; };
        static const RingSlot slots[] = {
            {0.0f,   LayerHandleKind::ScaleX,     "Échelle largeur (X) — clic + glisser"},
            {45.0f,  LayerHandleKind::ScaleBoth,  "Échelle uniforme (X/Y conservé) — clic + glisser"},
            {90.0f,  LayerHandleKind::ScaleY,     "Échelle hauteur (Y) — clic + glisser"},
            {135.0f, LayerHandleKind::MirrorY,    "Miroir vertical (Y) — clic"},
            {180.0f, LayerHandleKind::MoveX,      "Déplacer horizontalement (X) — clic + glisser"},
            {225.0f, LayerHandleKind::MirrorBoth, "Miroir X et Y — clic"},
            {270.0f, LayerHandleKind::MoveY,      "Déplacer verticalement (Y) — clic + glisser"},
            {315.0f, LayerHandleKind::MirrorX,    "Miroir horizontal (X) — clic"},
        };
        // Positions : mêmes calculs que la détection de clic (monde → écran
        // via worldToScreen) — le dessin et le clic visent toujours la même
        // poignée, malgré l'inversion de l'axe Y entre le monde (Y↑) et
        // l'écran (Y↓). Avant ancrage, le centre de l'anneau est le curseur.
        const float rw = kScaleR / std::max(app.camera.zoom, 1e-3f);
        const Vec2 wc = ringAnchored
                            ? ringAnchor
                            : app.camera.screenToWorld({mp.x - pos.x, mp.y - pos.y}, vps);
        for (const RingSlot& s : slots) {
            const float rad = s.deg * kPiF / 180.0f;
            const Vec2 wp{wc.x + std::cos(rad) * rw, wc.y + std::sin(rad) * rw};
            const Vec2 sp = app.camera.worldToScreen(wp, vps);
            const float px = pos.x + sp.x;
            const float py = pos.y + sp.y;
            const bool hover = std::sqrt((mp.x - px) * (mp.x - px) +
                                         (mp.y - py) * (mp.y - py)) < 11.0f;
            const bool isX = s.kind == LayerHandleKind::ScaleX;
            if (s.kind == LayerHandleKind::ScaleX ||
                s.kind == LayerHandleKind::ScaleY) {
                // Carreau cyan (X) / ambre (Y)
                const ImU32 col = hover ? (isX ? IM_COL32(120, 235, 255, 255)
                                               : IM_COL32(255, 205, 120, 255))
                                        : (isX ? IM_COL32(120, 235, 255, 160)
                                               : IM_COL32(255, 205, 120, 160));
                const float hs = hover ? 7.5f : 6.0f;
                dl->AddRectFilled(ImVec2(px - hs, py - hs), ImVec2(px + hs, py + hs), col);
                dl->AddRect(ImVec2(px - hs, py - hs), ImVec2(px + hs, py + hs),
                            IM_COL32(255, 255, 255, 100));
            } else if (s.kind == LayerHandleKind::ScaleBoth) {
                // Diamant blanc (échelle uniforme)
                const ImU32 col = hover ? IM_COL32(255, 255, 255, 255)
                                        : IM_COL32(255, 255, 255, 160);
                const float hs = hover ? 6.5f : 5.0f;
                dl->AddQuadFilled(ImVec2(px, py - hs), ImVec2(px + hs, py),
                                  ImVec2(px, py + hs), ImVec2(px - hs, py), col);
                dl->AddQuad(ImVec2(px, py - hs), ImVec2(px + hs, py),
                            ImVec2(px, py + hs), ImVec2(px - hs, py),
                            IM_COL32(255, 255, 255, 100));
            } else if (s.kind == LayerHandleKind::MoveX ||
                       s.kind == LayerHandleKind::MoveY) {
                // Flèche verte pointant vers l'extérieur (déplacement contraint)
                const float cs = std::cos(rad), sn = std::sin(rad);
                const ImU32 col = hover ? IM_COL32(90, 240, 130, 255)
                                        : IM_COL32(90, 240, 130, 170);
                const float sz = hover ? 8.0f : 6.5f;
                dl->AddTriangleFilled(
                    ImVec2(px + cs * sz * 2.5f, py + sn * sz * 2.5f),
                    ImVec2(px - cs * sz * 0.5f + sn * sz, py - sn * sz * 0.5f - cs * sz),
                    ImVec2(px - cs * sz * 0.5f - sn * sz, py - sn * sz * 0.5f + cs * sz), col);
                if (hover)
                    dl->AddTriangle(
                        ImVec2(px + cs * sz * 2.5f, py + sn * sz * 2.5f),
                        ImVec2(px - cs * sz * 0.5f + sn * sz, py - sn * sz * 0.5f - cs * sz),
                        ImVec2(px - cs * sz * 0.5f - sn * sz, py - sn * sz * 0.5f + cs * sz),
                        IM_COL32(255, 255, 255, 130));
            } else {
                // Symétrie : pastille rouge (clic instantané)
                const ImU32 col = hover ? IM_COL32(245, 95, 95, 255)
                                        : IM_COL32(245, 95, 95, 180);
                dl->AddCircleFilled(ImVec2(px, py), hover ? 8.0f : 7.0f, col);
                if (hover)
                    dl->AddCircle(ImVec2(px, py), 9.5f, IM_COL32(255, 255, 255, 160));
            }
            if (hover) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                ImGui::SetTooltip("%s", s.tip);
            }
        }
    }

    // Curseur : croix de visée, disque de peinture ou cible de pipette.
    if (app.viewportHovered && app.preview == PreviewMode::Off && !app.kiosk) {
        const ImVec2 mp = io.MousePos;
        if (app.pipetteArmed) {
            // Pipette : petite cible (cercle + croix) pour viser précisément.
            dl->AddCircle(mp, 9.0f, IM_COL32(255, 255, 255, 210));
            dl->AddLine(ImVec2(mp.x - 5, mp.y), ImVec2(mp.x + 5, mp.y),
                        IM_COL32(255, 255, 255, 210));
            dl->AddLine(ImVec2(mp.x, mp.y - 5), ImVec2(mp.x, mp.y + 5),
                        IM_COL32(255, 255, 255, 210));
            ImGui::SetMouseCursor(ImGuiMouseCursor_None);
        } else if (app.brushArmed) {
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
            } else if (app.reticle == ReticleState::Symmetric) {
                dl->AddLine(ImVec2(0, mp.y), ImVec2(size.x, mp.y), col);
                dl->AddLine(ImVec2(mp.x, 0), ImVec2(mp.x, size.y), col);
            } else {
                // Miroir : croix de visée à la position du curseur et à ses
                // trois reflets à travers les axes du monde (X, Y et symétrie
                // centrale) — on voit où tomberaient les points symétriques
                // de la position courante pendant le tracé.
                const Vec2 vps = app.viewportVec2();
                const Vec2 rel{mp.x - pos.x, mp.y - pos.y};
                const Vec2 wp = app.camera.screenToWorld(rel, vps);
                const Vec2 mir[4] = {
                    wp,
                    {-wp.x, wp.y},    // reflet à travers l'axe Y
                    {wp.x, -wp.y},    // reflet à travers l'axe X
                    {-wp.x, -wp.y},   // symétrie centrale
                };
                auto cross = [&](const Vec2& p) {
                    const Vec2 sp = app.camera.worldToScreen(p, vps) + Vec2{pos.x, pos.y};
                    dl->AddLine(ImVec2(sp.x - 5, sp.y), ImVec2(sp.x + 5, sp.y), col);
                    dl->AddLine(ImVec2(sp.x, sp.y - 5), ImVec2(sp.x, sp.y + 5), col);
                };
                for (const Vec2& p : mir) cross(p);
            }
            ImGui::SetMouseCursor(ImGuiMouseCursor_None);
        }
    }

    // Compteurs « ext. / int. » de la couronne : badge en direct près de
    // l'ancre pendant le tracé (4.2). Le nombre réglé par la molette (selon la
    // phase : extérieurs avant le 2e clic, intérieurs après — Maj inverse) est
    // mis en évidence en cyan, l'autre reste estompé.
    if (app.tool == Tool::Crown && app.isShapeTracing()) {
        const Vec2 as = app.camera.worldToScreen(app.shapeAnchor(),
                                                 app.viewportVec2());
        const bool innerPhase = app.crownInnerPhase();
        char extBuf[12], intBuf[12];
        std::snprintf(extBuf, sizeof(extBuf), "%d ext.", app.circleSides);
        std::snprintf(intBuf, sizeof(intBuf), "%d int.", app.crownInnerSides);
        const ImVec2 we = ImGui::CalcTextSize(extBuf);
        const ImVec2 ws = ImGui::CalcTextSize(" · ");
        const ImVec2 wi = ImGui::CalcTextSize(intBuf);
        const float padX = 10.0f;
        const float padY = 5.0f;
        const float bw = padX * 2.0f + we.x + ws.x + wi.x;
        const float bh = padY * 2.0f + std::max(we.y, wi.y);
        // Position : à droite de l'ancre et légèrement au-dessus, mais toujours
        // dans le viewport (ancre éventuellement hors écran).
        const float bx = std::max(pos.x + 6.0f,
                                  std::min(pos.x + as.x + 16.0f,
                                           pos.x + size.x - bw - 6.0f));
        const float by = std::max(pos.y + 6.0f,
                                  std::min(pos.y + as.y - bh - 16.0f,
                                           pos.y + size.y - bh - 6.0f));
        dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + bh),
                          IM_COL32(18, 24, 32, 220), 5.0f);
        dl->AddRect(ImVec2(bx, by), ImVec2(bx + bw, by + bh),
                    IM_COL32(90, 160, 255, 140), 5.0f);
        const float tx = bx + padX;
        const float ty = by + padY;
        const ImU32 colOn = IM_COL32(130, 235, 255, 255);
        const ImU32 colOff = IM_COL32(170, 180, 200, 170);
        if (innerPhase) {
            dl->AddText(ImVec2(tx, ty), colOff, extBuf);
            dl->AddText(ImVec2(tx + we.x, ty), kDimCol, " · ");
            dl->AddText(ImVec2(tx + we.x + ws.x, ty), colOn, intBuf);
        } else {
            dl->AddText(ImVec2(tx, ty), colOn, extBuf);
            dl->AddText(ImVec2(tx + we.x, ty), kDimCol, " · ");
            dl->AddText(ImVec2(tx + we.x + ws.x, ty), colOff, intBuf);
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
        {"shape-crown", "Couronne",
         "Couronne — 3 clics (centre, rayon, trou) ; après le 2e clic, l'angle "
         "du curseur oriente la forme intérieure ; côtés extérieurs et "
         "intérieurs indépendants (molette / Maj+molette)",
         Tool::Crown},
    };
    for (const auto& s : shapes) {
        if (toolBtnIcon(s.icon, s.tip, app.tool == s.tool, kGreen, false, s.label)) {
            app.startShapeTool(s.tool);
            ImGui::CloseCurrentPopup();
        }
        // 4.2 : la molette sur la ligne Cercle / Anneau / Étoile règle le
        // nombre de côtés (comme sur le canvas) ; pour la Couronne, la molette
        // suit la phase du tracé — extérieurs tant que le rayon n'est pas
        // verrouillé, intérieurs après le 2e clic (Maj+molette : l'autre jeu).
        const bool sidesShape =
            s.tool == Tool::Circle || s.tool == Tool::Ring || s.tool == Tool::Star ||
            s.tool == Tool::Crown;
        if (sidesShape) {
            if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f) {
                // La molette suit la phase du tracé (comme le canvas).
                if (s.tool == Tool::Crown) {
                    const bool adjustInner = io.KeyShift != app.crownInnerPhase();
                    if (adjustInner)
                        app.crownInnerSides =
                            std::clamp(app.crownInnerSides + (int)std::lround(io.MouseWheel), 3, 64);
                    else
                        app.circleSides =
                            std::clamp(app.circleSides + (int)std::lround(io.MouseWheel), 3, 64);
                    app.statusCrown();
                } else {
                    app.circleSides =
                        std::clamp(app.circleSides + (int)std::lround(io.MouseWheel), 3, 64);
                    app.setStatus("Nombre de " +
                                  std::string(s.tool == Tool::Star
                                                  ? "pointes de l'étoile"
                                                  : "côtés du " +
                                                        std::string(s.tool == Tool::Circle
                                                                        ? "cercle"
                                                                        : "anneau")) +
                                  " : " + std::to_string(app.circleSides));
                }
            }
            ImGui::SameLine();
            char sidesbuf[24];
            float labelW;
            if (s.tool == Tool::Crown) {
                std::snprintf(sidesbuf, sizeof(sidesbuf), "%d / %d côtés",
                              app.circleSides, app.crownInnerSides);
                labelW = ImGui::CalcTextSize("64 / 64 côtés").x;
            } else {
                std::snprintf(sidesbuf, sizeof(sidesbuf), "%d %s", app.circleSides,
                              s.tool == Tool::Star ? "pointes" : "côtés");
                labelW = ImGui::CalcTextSize("64 côtés").x;
            }
            valueLabel(sidesbuf, labelW);
        }
    }
    ImGui::Separator();
    ImGui::TextDisabled("2 clics : ancre puis valider · étoile, anneau et couronne : 3 clics.");
    ImGui::TextDisabled("Molette (cercle/anneau) : côtés · couronne : extérieurs puis intérieurs pendant le tracé (Maj+molette : l'autre jeu).");
    ImGui::TextDisabled("Raccourcis : C R T Q N H É A O · Retour arrière : annuler le tracé.");
    ImGui::EndPopup();
}

// Couleur du fond du canvas (bouton du groupe Scène, 8.5) : roue chromatique,
// pastilles rapides (sombres / claires) et retour au fond par défaut — le tout
// pilotable à la souris, effet visible immédiatement derrière le menu.
void bgColorPopup(App& app) {
    if (!ImGui::BeginPopup("##bgmenu")) return;
    ImGui::TextDisabled("Couleur du fond du canvas :");
    ImGui::SetNextItemWidth(190.0f);
    if (ImGui::ColorEdit3("##bg", &app.bgColor.r))
        app.bgColor.a = 1.0f;
    ImGui::Separator();
    const Color presets[] = {
        kBgDefault,  // ardoise (défaut)
        {0.12f, 0.13f, 0.16f, 1.0f},
        {0.16f, 0.19f, 0.24f, 1.0f},
        {0.21f, 0.24f, 0.30f, 1.0f},
        {0.93f, 0.94f, 0.96f, 1.0f},      // clair
        {1.0f, 1.0f, 1.0f, 1.0f},         // blanc
    };
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3.0f, 4.0f));
    for (size_t i = 0; i < sizeof(presets) / sizeof(presets[0]); ++i) {
        ImGui::PushID((int)i);
        const ImVec4 c(presets[i].r, presets[i].g, presets[i].b, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, c);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              ImVec4(c.x * 1.25f, c.y * 1.25f, c.z * 1.25f, 1.0f));
        if (ImGui::Button("##sw", ImVec2(26, 26))) app.bgColor = presets[i];
        ImGui::PopStyleColor(2);
        if (i < sizeof(presets) / sizeof(presets[0]) - 1) ImGui::SameLine();
        ImGui::PopID();
    }
    ImGui::PopStyleVar();
    ImGui::Separator();
    if (toolBtnIcon("reset", "Revenir au fond par défaut (ardoise)", false, kGreen,
                    false, "Par défaut")) {
        app.bgColor = kBgDefault;
        app.setStatus("Couleur du fond : ardoise (défaut)");
    }
    ImGui::TextDisabled("La molette sur le bouton fonce / éclaircit.");
    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// Calque d'image de fond (7.7) : popup du bouton « Calque » (paquet Scène) +
// dialogue de chargement. L'état vit dans app.scene.image (persisté dans le
// JSON de scène) ; la texture GL est synchronisée par drawScene.
// ---------------------------------------------------------------------------
void layerPopup(App& app) {
    if (!ImGui::BeginPopup("##layermenu")) return;
    const ImageLayer& il = app.scene.image;
    if (il.path.empty()) {
        ImGui::TextDisabled("Aucun calque d'image.");
        if (toolBtnIcon("layer", "Charger une image (PNG/JPEG) comme fond", false, kGreen,
                        false, "Charger une image…", 210.0f)) {
            app.dlgLayerOpen = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::TextDisabled("L'image est enregistrée avec la scène (JSON).");
        ImGui::EndPopup();
        return;
    }
    ImGui::TextDisabled("Calque : %s", il.path.c_str());
    ImGui::TextDisabled("%d × %d px · opacité %.0f %%", il.w, il.h, il.opacity * 100.0f);
    ImGui::Separator();

    // Manipulation au canvas : bouton unifié (anneau autour du curseur avec
    // une poignée par action : déplacement, rotation, échelle et symétries).
    ImGui::TextDisabled("Manipulation au canvas (anneau de poignées) :");
    const float bw = dialogBtnWidth({"Manipuler", "Ajuster", "Retirer"});
    if (toolBtnIcon("grab",
                    app.layerArmed
                        ? "Manipulation du calque armée — anneau de poignées visible "
                          "au canvas · clic pour ancrer · clic droit ou Échap : désarmer"
                        : "Manipuler le calque — active l'anneau de poignées autour "
                          "du curseur (déplacement, rotation, échelle, symétries) · "
                          "clic droit ou Échap : désarmer",
                    app.layerArmed, kGreen, false, "Manipuler", bw))
        app.toggleLayerMode();
    ImGui::Separator();

    // Opacité du calque : exprimée de 0 à 100 % avec un incrément de 1 (une
    // étape annulable par manipulation complète du curseur).
    ImGui::TextDisabled("Opacité :");
    ImGui::SetNextItemWidth(210.0f);
    int layerOpac = (int)std::lround(app.scene.image.opacity * 100.0f);
    if (ImGui::SliderInt("##layerop", &layerOpac, 0, 100, "%d %%")) {
        if (ImGui::IsItemActivated()) app.pushUndo();
        app.scene.image.opacity = layerOpac / 100.0f;
        app.dirty = true;
    }
    if (ImGui::Checkbox("Calque visible", &app.scene.image.visible)) {
        if (ImGui::IsItemActivated()) app.pushUndo();
        app.dirty = true;
    }
    ImGui::Separator();
    if (toolBtnIcon("fit-view", "Ajuster le calque à la vue courante (taille + position)",
                    false, kGreen, false, "Ajuster à la vue", bw))
        app.fitLayerToView();
    if (toolBtnIcon("delete-shape", "Retirer le calque d'image de la scène", false, kRed,
                    false, "Retirer le calque", bw))
        app.removeImageLayer();
    if (app.layerArmed) {
        ImGui::Separator();
        ImGui::TextDisabled("Armé : anneau de poignées au canvas — "
                            "clic droit ou Échap pour désarmer.");
    }
    ImGui::EndPopup();
}

void layerLoadDialog(App& app) {
    if (!app.dlgLayerOpen) return;
    const char* title = "Charger une image (calque)";
    ImGui::OpenPopup(title);
    ImGui::SetNextWindowSize(ImVec2(500, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Chemin du fichier (PNG ou JPEG) :");
        ImGui::SetNextItemWidth(470);
        ImGui::InputText("##layerpath", app.dlgLayerPath, sizeof(app.dlgLayerPath));
        const float bw = dialogBtnWidth({"Charger", "Annuler"});
        bool doLoad = false;
        if (toolBtnIcon("check", "Charger l'image en calque de fond", false, kGreen, false,
                        "Charger", bw) ||
            (ImGui::IsKeyPressed(ImGuiKey_Enter) && ImGui::IsWindowFocused()))
            doLoad = true;
        if (doLoad) {
            // En cas d'échec, l'erreur est signalée (statut) sans fermer.
            if (app.loadImageLayer(app.dlgLayerPath)) {
                app.dlgLayerOpen = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (toolBtnIcon("close", "Annuler", false, kGreen, false, "Annuler", bw)) {
            app.dlgLayerOpen = false;
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            app.dlgLayerOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// Opacité du plan actif (7.8) : curseur de transparence (une étape annulable
// par manipulation complète) et rappel du comportement (arêtes conservées).
void opacityPopup(App& app) {
    if (!ImGui::BeginPopup("##opacmenu")) return;
    if (app.scene.count() < 1) {
        ImGui::EndPopup();
        return;
    }
    Mesh2D& m = app.scene.activePlane();
    ImGui::TextDisabled("Opacité du plan actif :");
    ImGui::SetNextItemWidth(190.0f);
    if (ImGui::SliderFloat("##opac", &m.opacity, 0.0f, 1.0f, "%.2f")) {
        if (ImGui::IsItemActivated()) app.pushUndo();
        app.dirty = true;
        app.setStatus("Opacité du plan actif : " +
                      std::to_string((int)std::lround(m.opacity * 100.0f)) + " %");
    }
    ImGui::TextDisabled("%d %% — arêtes et points restent affichés.",
                        (int)std::lround(m.opacity * 100.0f));
    if (m.opacity < 1.0f) {
        ImGui::Separator();
        if (toolBtnIcon("reset", "Revenir à 100 % d'opacité", false, kGreen, false,
                        "Rétablir (100 %)", 130.0f)) {
            app.pushUndo();
            m.opacity = 1.0f;
            app.dirty = true;
            app.setStatus("Opacité du plan actif : 100 %");
        }
    }
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
        // Exprimée de 0 à 100 % avec un incrément de 1 (0 % = peinture invisible,
        // défaut 45 %).
        int brushOpac = (int)std::lround(app.brushOpacity * 100.0f);
        if (ImGui::SliderInt("##op", &brushOpac, 0, 100, "%d %%"))
            app.brushOpacity = std::clamp(brushOpac / 100.0f, 0.0f, 1.0f);
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

// Panneau « Réglages » : distances de détection des sommets et des segments
// au survol (et en mode « sommet » / « segment »). Mémorisées dans les
// préférences (prefs.json).
void settingsPanel(App& app) {
    if (!app.settingsOpen) return;
    if (ImGui::Begin("Réglages", &app.settingsOpen)) {
        ImGui::TextUnformatted("Détection des segments :");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##edgeTol", &app.edgePickTol, 2.0f, 150.0f, "%.0f px"))
            app.edgePickTol = std::clamp(app.edgePickTol, 2.0f, 150.0f);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Distance (pixels écran) à laquelle un segment "
                              "s'illumine au survol et sert de base à un nouveau "
                              "triangle (mode sommet) — vaut aussi pour la "
                              "sélection en cible « segment ». Réglable de 2 à 150 px.");
        ImGui::TextUnformatted("Détection des sommets :");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##vertTol", &app.vertexPickTol, 2.0f, 150.0f, "%.0f px"))
            app.vertexPickTol = std::clamp(app.vertexPickTol, 2.0f, 150.0f);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Distance (pixels écran) à laquelle un sommet "
                              "s'illumine au survol et est attrapé au clic "
                              "(sélection et construction en cible « sommet »). "
                              "Réglable de 2 à 150 px.");
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
        ImGui::BulletText("Formes : C cercle · R rectangle · T triangle · Q carré · N pentagone · H hexagone · É étoile · A anneau · O couronne · D découpe (polygone soustrait)");
        ImGui::BulletText("Ctrl+D : dupliquer la sélection · Ctrl+A : tout sélectionner · Ctrl+I : inverser la sélection");
        ImGui::BulletText("Sélection chaînée (bouton du paquet Sélection) : tous les éléments liés à la sélection — triangles par ≥ 1 sommet partagé, segments par un sommet partagé, sommets par un segment (Ctrl ou Maj : ajouter, sinon remplacer)");
        ImGui::BulletText("M / Maj+M : miroir X / Y de la sélection · Alt+S : mise à l'échelle précise (facteur)");
        ImGui::BulletText("Ctrl+M : outil mesure (2 clics : distance au HUD) · Alt+D : dupliquer le plan actif");
        ImGui::BulletText("Maj+G : aimantation sur la grille sans son affichage (ou l'inverse)");
        ImGui::BulletText("Alt+R : rotation précise (saisie d'un angle)");
        ImGui::BulletText("Flèches : déplacer la sélection d'un pas de grille · Maj : ×5 (une salve = une étape annulable)");
        ImGui::BulletText("? : cette aide · Échap : quitter le mode en cours");
        ImGui::BulletText("Alt+← / Alt+→ : aligner X / Y · Alt+Maj+←/→ : répartir X / Y");
        ImGui::BulletText("Alt+↑ / Alt+↓ : monter / descendre le plan actif (empilement)");
        ImGui::BulletText("Bouton Opacité (groupe plans) : transparence du plan actif — curseur dans le popup, ou molette sur le bouton (pas de 5 %%) — les arêtes restent affichées pour superposer les plans");
        ImGui::BulletText("] / [ : face(s) sélectionnée(s) vers l'avant / l'arrière — ordre z dans le plan (boutons de la barre d'outils)");
        ImGui::BulletText("Alt+K : kiosque de sélection des plans (au moins 2 plans — flèches ←/→ pour naviguer)");
        ImGui::Separator();
        ImGui::TextUnformatted("Souris");
        ImGui::BulletText("Clic gauche (vide) : poser un point — 3 clics ferment un triangle");
        ImGui::BulletText("Clic gauche (entité) : sélectionner · Maj+clic : basculer");
        ImGui::BulletText("Clic cyclique (cible triangle) : quand plusieurs faces se superposent au même endroit, re-cliquer sélectionne la face suivante en dessous — pour atteindre les triangles cachés d'un ensemble (5.12) · Maj : ajouter/retirer la face choisie · un clic ailleurs repart de la face du dessus");
        ImGui::BulletText("Pinceau : clic gauche peint le triangle survolé — ou tous les triangles sélectionnés (cible « triangle »)");
        ImGui::BulletText("Pipette (bouton du paquet Outils) : un clic gauche sur le canvas prélève la couleur affichée (faces, calque d'image, fond…) et la pose comme couleur de pinceau · clic droit ou Échap : désarmer");
        ImGui::BulletText("Clic gauche + glisser : rectangle de sélection (ne déplace jamais)");
        ImGui::BulletText("Lasso (bouton du paquet Sélection) : tracer librement autour des éléments à sélectionner d'un coup — sommet par sa position, segment par son milieu, triangle par son centre · Maj au relâchement : ajouter · clic droit ou Échap : désarmer");
        ImGui::BulletText("Opérations ensemblistes (paquet Sélection) : Mémoriser A puis Mémoriser B capturent chacun la sélection courante (cible triangle) — bouton « Booléennes » : union (A∪B), intersection (A∩B), différence (A−B) ou symétrique (A△B) · dans la zone des deux ensembles seule la géométrie du résultat reste (le reste du plan est intact), le résultat devient la sélection");
        ImGui::BulletText("Clic droit : saisir l'entité la plus proche — modes sommet / segment / triangle : l'entité devient la seule sélectionnée et se saisit aussitôt · Ctrl+clic droit : ajouter · Maj+clic droit : basculer");
        ImGui::BulletText("Clic droit + glisser : déplacer la sélection");
        ImGui::BulletText("Molette : zoom — ou rotation des points sélectionnés (≥ 2)");
        ImGui::BulletText("PNG (icône image de la barre d'outils ou de la prévisualisation) : exporter la vue actuelle en image");
        ImGui::BulletText("AltGr + molette : rotation de tous les plans autour du curseur (5° par cran)");
        ImGui::BulletText("AltGr + clic droit + glisser : déplacer tous les plans d'un même décalage");
        ImGui::BulletText("Groupe Scène — bouton Manipuler (anneau de poignées : une action par poignée) : cible « Sélection » / « Plan » / « Scène » (clic : changer de cible et armer · clic droit : menu) — clic gauche au canvas ancre l'anneau, puis déplacement (centre/flèches), rotation (anneau), échelle (carreaux/losange), symétries (pastilles rouges, clic) · clic droit ou Échap désarme");
        ImGui::BulletText("Groupe Scène — bouton fond : couleur du canvas (molette sur le bouton : foncer / éclaircir) · bouton réinitialiser : vider la scène (confirmation)");
        ImGui::BulletText("Groupe Scène — bouton calque (7.7) : image de fond chargée (PNG/JPEG), enregistrée avec la scène — popup : opacité, visibilité, manipuler au canvas (anneau de poignées : une par action — déplacement, rotation, échelle, symétries — clic droit ou Échap désarme)");
        ImGui::BulletText("Clic du milieu + glisser : déplacer la vue");
        ImGui::BulletText("Molette sur un bouton actif : réglage contextuel (pas de grille, côtés, pointes de l'étoile, rayon de fusion)");
        ImGui::BulletText("Bouton Aimant de la barre d'outils : activer / désactiver l'aimantation (indépendante de l'affichage) · Maj+G : même raccourci");
        ImGui::BulletText("Bouton Renommer (crayon, groupe plans) : nommer le plan actif — nom affiché au kiosque et au HUD");
        ImGui::BulletText("Historique (barre d'outils) : versions horodatées de l'autosave — restaurer un état antérieur");
        ImGui::BulletText("Anneau orange : points superposés — clic pour les sélectionner tous, « Fusionner » les regroupe à la position moyenne (5.5)");
        ImGui::BulletText("Fusion par déplacement (5.6) : 1 point sélectionné + bouton Fusionner, puis glisser le point près d'un autre — molette sur le bouton : rayon 8-64 px, re-clic : verrouiller (cadenas)");
        ImGui::BulletText("Engrenage (barre d'outils) : distances de détection des sommets et des segments (illumination au survol, 2 à 150 px)");
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
        "image", "Exporter l'image actuelle en PNG (prévisualisation)", false, kGreen,
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
                   const ImVec2& br, float squash, float alpha, bool front,
                   float topMargin, float bottomMargin) {
    const Mesh2D& p = app.scene.planes[pi];
    if (p.vertices.empty()) return;
    Vec2 mn = p.vertices[0], mx = p.vertices[0];
    for (const Vec2& v : p.vertices) {
        mn.x = std::min(mn.x, v.x);
        mn.y = std::min(mn.y, v.y);
        mx.x = std::max(mx.x, v.x);
        mx.y = std::max(mx.y, v.y);
    }
    const float bw = mx.x - mn.x;
    const float bh = mx.y - mn.y;
    if (bw < 1e-6f || bh < 1e-6f) return;
    const Vec2 cw = {(mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f};
    // Marges : 5 px sur les côtés ; en haut et en bas, une marge au moins égale
    // à la hauteur des bandeaux texte (nom du plan en haut, compteurs en bas)
    // pour que la forme agrandie ne passe jamais derrière le texte. La forme
    // couvre ~80-90 % de la zone utile pour les proportions proches de celles
    // de la carte (~57 % pour les formes « rondes », la carte étant en paysage).
    const float sidePad = 5.0f;
    const float innerW = br.x - tl.x - sidePad * 2.0f;
    const float innerH = br.y - tl.y - topMargin - bottomMargin;
    if (innerW < 1e-6f || innerH < 1e-6f) return;  // carte trop petite
    // Ajustement « contenir » par axe (échelle uniforme) : la forme remplit la
    // zone utile au maximum sans jamais déborder, quelle que soit sa taille
    // dans le monde. L'ancien code divisait par span dans scale PUIS dans la
    // projection : la taille rendue était inversement proportionnelle à la
    // taille monde (une forme 10× plus grande rendait 10× plus petite).
    const float scale = std::min(innerW / bw, innerH / bh);
    // Centre horizontal de la carte ; centre vertical de la zone utile (entre
    // les bandeaux texte), pas de la carte entière.
    const float cx = (tl.x + br.x) * 0.5f;
    const float cy = (tl.y + topMargin + br.y - bottomMargin) * 0.5f;
    // Le monde a Y vers le haut, l'écran Y vers le bas : on soustrait pour que
    // le haut de la forme apparaisse en haut de la carte (l'ancien code
    // inversait les plans verticalement dans le kiosque).
    auto toCard = [&](const Vec2& w) {
        return ImVec2(cx + (w.x - cw.x) * scale * squash,
                      cy - (w.y - cw.y) * scale);
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
        const Mesh2D& p = app.scene.planes[i];

        // Bandeaux texte de la carte : nom du plan en haut, compteurs en bas.
        // Les hauteurs de texte sont calculées AVANT la forme pour lui réserver
        // une marge verticale au moins égale à la hauteur du texte (plus un
        // léger espace) : la forme agrandie ne passe jamais derrière le texte.
        const std::string label = planeLabel(app, i);
        const ImVec2 ns = ImGui::CalcTextSize(label.c_str());
        char counts[48];
        std::snprintf(counts, sizeof(counts), "%d pt · %d tri",
                      (int)p.vertices.size(), p.triangleCount());
        const ImVec2 csize = ImGui::CalcTextSize(counts);
        // Marge réservée à la forme = hauteur du bandeau texte (texte + 8 px
        // d'air) quand le bandeau est affiché ; simple liseré de 5 px sinon
        // (cartes trop petites : le bandeau n'est pas dessiné).
        const float topMargin = (cw > 44.0f) ? ns.y + 8.0f : 5.0f;
        const float bottomMargin = (cw > 56.0f) ? csize.y + 8.0f : 5.0f;

        // Vignettes de fond lisibles : toutes de même dimension (voir kioskOverlay).
        drawPlaneCard(app, dl, i, tl, br, std::fabs(squash), alpha, front,
                      topMargin, bottomMargin);

        // Reflet discret en haut de la carte (dégradé clair → transparent,
        // légèrement rentré pour respecter les coins arrondis).
        const float reflH = std::min(ch * 0.32f, 46.0f);
        dl->AddRectFilledMultiColor(
            ImVec2(tl.x + 3.0f, tl.y + 2.0f), ImVec2(br.x - 3.0f, tl.y + reflH),
            IM_COL32(255, 255, 255, (int)(26.0f * alpha)),
            IM_COL32(255, 255, 255, (int)(26.0f * alpha)),
            IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));

        // Nom du plan directement sur la carte (bandeau discret en haut) :
        // le nom personnalisé s'il existe, sinon « Plan n ».
        const float nameY = tl.y + 5.0f;
        if (cw > 44.0f) {
            dl->AddRectFilled(ImVec2(px - ns.x * 0.5f - 6.0f, nameY - 2.0f),
                              ImVec2(px + ns.x * 0.5f + 6.0f, nameY + ns.y + 2.0f),
                              IM_COL32(0, 0, 0, (int)(95.0f * alpha)), 3.0f);
            dl->AddText(ImVec2(px - ns.x * 0.5f, nameY),
                        front ? IM_COL32(235, 245, 255, 240)
                              : IM_COL32(200, 210, 225, 150),
                        label.c_str());
        }

        // Compteurs points / triangles en bas de carte : bandeau sombre derrière
        // le texte (comme l'étiquette du haut) pour rester lisible quand la
        // forme agrandie passe derrière.
        if (cw > 56.0f) {
            const float cy0 = br.y - csize.y - 5.0f;
            dl->AddRectFilled(ImVec2(px - csize.x * 0.5f - 6.0f, cy0 - 2.0f),
                              ImVec2(px + csize.x * 0.5f + 6.0f, cy0 + csize.y + 2.0f),
                              IM_COL32(0, 0, 0, (int)(95.0f * alpha)), 3.0f);
            dl->AddText(ImVec2(px - csize.x * 0.5f, cy0),
                        IM_COL32(180, 195, 215, front ? 205 : 115), counts);
        }

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
        const float bw = dialogBtnWidth({"Enregistrer", "Annuler"});
        if (toolBtnIcon("export", "Enregistrer la scène (Ctrl+S)", false, kGreen,
                        false, "Enregistrer", bw) ||
            (ImGui::IsKeyPressed(ImGuiKey_Enter) && ImGui::IsWindowFocused())) {
            app.saveToLocation(app.dlgSaveName);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (toolBtnIcon("close", "Annuler", false, kGreen, false, "Annuler", bw)) {
            app.dlgSaveOpen = false;
            app.quitPending = false;  // sortie différée abandonnée
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            app.dlgSaveOpen = false;
            app.quitPending = false;  // sortie différée abandonnée
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
        const float bw = dialogBtnWidth({"Valider", "Annuler"});
        bool doImport = false;
        if (toolBtnIcon("check", "Valider l'import", false, kGreen, false,
                        "Valider", bw) ||
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
        if (toolBtnIcon("close", "Annuler", false, kGreen, false, "Annuler", bw)) {
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
        const float bw = dialogBtnWidth({"Réinitialiser", "Annuler"});
        if (toolBtnIcon("reset", "Vider entièrement la scène", false, kRed, false,
                        "Réinitialiser", bw)) {
            app.resetScene();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (toolBtnIcon("close", "Annuler", false, kGreen, false, "Annuler", bw)) {
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
        char buf[128];
        std::snprintf(buf, sizeof(buf), "Supprimer « %s » ?",
                      planeLabel(app, app.scene.active).c_str());
        ImGui::TextUnformatted(buf);
        ImGui::TextUnformatted("L'opération est annulable (Ctrl+Z).");
        const float bw = dialogBtnWidth({"Supprimer", "Annuler"});
        if (toolBtnIcon("delete-shape", "Supprimer le plan actif", false, kRed,
                        false, "Supprimer", bw)) {
            app.deletePlane();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (toolBtnIcon("close", "Annuler", false, kGreen, false, "Annuler", bw)) {
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

// Renommer le plan actif (spec 2.2) : le nom est affiché au kiosque, au HUD
// et dans les dialogues. Champ pré-rempli (nom courant ou « Plan n »),
// Entrée valide, Échap annule ; champ vidé = retour au nom par défaut.
void renameDialog(App& app) {
    if (!app.dlgRenameOpen) return;
    ImGui::OpenPopup("Renommer le plan");
    if (ImGui::BeginPopupModal("Renommer le plan", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Nom du plan actif (vide = nom par défaut) :");
        ImGui::SetNextItemWidth(280);
        ImGui::InputText("##name", app.dlgRenameName, sizeof(app.dlgRenameName),
                         ImGuiInputTextFlags_AutoSelectAll);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Le nom est conservé dans le fichier JSON et affiché "
                              "dans le kiosque, le HUD et les dialogues.");
        const float bw = dialogBtnWidth({"Renommer", "Annuler"});
        if (toolBtnIcon("check", "Appliquer le nouveau nom", false, kGreen, false,
                        "Renommer", bw) ||
            (ImGui::IsKeyPressed(ImGuiKey_Enter) && ImGui::IsWindowFocused())) {
            app.renameActivePlane(app.dlgRenameName);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (toolBtnIcon("close", "Annuler", false, kGreen, false, "Annuler", bw)) {
            app.dlgRenameOpen = false;
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            app.dlgRenameOpen = false;
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
        const float bw = dialogBtnWidth({"Appliquer", "Annuler"});
        if (toolBtnIcon("check", "Appliquer la rotation à la sélection", false,
                        kGreen, false, "Appliquer", bw) ||
            (ImGui::IsKeyPressed(ImGuiKey_Enter) && ImGui::IsWindowFocused())) {
            app.rotateSelectionExact(app.rotateDeg);
            app.dlgRotateOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (toolBtnIcon("close", "Annuler", false, kGreen, false, "Annuler", bw)) {
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
        const float bw = dialogBtnWidth({"Appliquer", "Annuler"});
        if (toolBtnIcon("scale", "Appliquer la mise à l'échelle à la sélection", false,
                        kGreen, false, "Appliquer", bw) ||
            (ImGui::IsKeyPressed(ImGuiKey_Enter) && ImGui::IsWindowFocused())) {
            app.scaleSelectionExact(app.scaleFactor);
            app.dlgScaleOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (toolBtnIcon("close", "Annuler", false, kGreen, false, "Annuler", bw)) {
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
        const float bw = dialogBtnWidth({"Exporter", "Annuler"});
        bool doExport = false;
        if (toolBtnIcon("export-svg", "Exporter le plan actif en SVG", false, kGreen,
                        false, "Exporter", bw) ||
            (ImGui::IsKeyPressed(ImGuiKey_Enter) && ImGui::IsWindowFocused()))
            doExport = true;
        if (doExport) {
            app.exportSvgTo(app.dlgSvgPath);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (toolBtnIcon("close", "Annuler", false, kGreen, false, "Annuler", bw)) {
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
        const float bw = dialogBtnWidth({"Restaurer", "Fermer"});
        ImGui::BeginChild("##versions", ImVec2(500, 170), true);
        if (app.versionFiles.empty()) {
            ImGui::TextDisabled("Aucune version pour l'instant.");
        } else {
            for (size_t i = 0; i < app.versionFiles.size(); ++i) {
                ImGui::PushID((int)i);
                ImGui::TextUnformatted(app.versionFiles[i].c_str());
                ImGui::SameLine();
                if (toolBtnIcon("history", "Restaurer cette version", false, kGreen,
                                false, "Restaurer", bw))
                    app.restoreVersionFile(app.versionFiles[i]);
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
        ImGui::TextDisabled("Restaurer remplace la scène courante par cette version "
                            "(annulable avec Ctrl+Z).");
        if (toolBtnIcon("close", "Fermer", false, kGreen, false, "Fermer", bw)) {
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
        const float bw = dialogBtnWidth({"Exporter", "Annuler"});
        bool doExport = false;
        if (toolBtnIcon("export", "Exporter la vue actuelle en PNG", false, kGreen,
                        false, "Exporter", bw) ||
            (ImGui::IsKeyPressed(ImGuiKey_Enter) && ImGui::IsWindowFocused()))
            doExport = true;
        if (doExport) {
            app.exportPngPath = app.dlgPngPath;
            app.exportPngRequested = true;
            app.dlgPngOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (toolBtnIcon("close", "Annuler", false, kGreen, false, "Annuler", bw)) {
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

// Confirmation de sortie : ouverte par requestQuit() quand la scène contient
// des modifications non enregistrées. Trois issues : Enregistrer (ouvre la
// fenêtre d'enregistrement, la sortie reprend une fois la scène enregistrée),
// Quitter quand même, ou Annuler.
void quitDialog(App& app) {
    if (!app.dlgQuitOpen) return;
    ImGui::OpenPopup("Quitter sans enregistrer ?");
    if (ImGui::BeginPopupModal("Quitter sans enregistrer ?", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("La scène contient des modifications non enregistrées.");
        ImGui::TextUnformatted("Que voulez-vous faire ?");
        ImGui::Separator();
        const float btnW =
            dialogBtnWidth({"Enregistrer", "Quitter quand même", "Annuler"});
        if (toolBtnIcon("export", "Enregistrer la scène puis quitter (Ctrl+S)", false,
                        kGreen, false, "Enregistrer", btnW)) {
            app.dlgQuitOpen = false;
            openSaveDialog(app);
        }
        ImGui::SameLine();
        if (toolBtnIcon("close", "Quitter sans enregistrer", false, kRed, false,
                        "Quitter quand même", btnW)) {
            g_quit = true;
        }
        ImGui::SameLine();
        if (toolBtnIcon("undo", "Revenir à l'édition", false, kGreen, false,
                        "Annuler", btnW)) {
            app.dlgQuitOpen = false;
            app.quitPending = false;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            app.dlgQuitOpen = false;
            app.quitPending = false;
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
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings);
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
        quitDialog(app);
        return;
    }
    // Prévisualisation : interface masquée (barre d'outils, panneaux, HUD),
    // seul le bouton de bascule reste visible — le rendu d'aperçu occupe tout.
    if (app.preview != PreviewMode::Off) {
        previewButton(app);
        pngDialog(app);
        quitDialog(app);
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
    layerLoadDialog(app);
    resetDialog(app);
    deletePlaneDialog(app);
    renameDialog(app);
    rotateDialog(app);
    scaleDialog(app);
    pngDialog(app);
    svgDialog(app);
    versionsDialog(app);
    quitDialog(app);

    // Sortie différée après « Enregistrer puis quitter » : la scène enregistrée
    // n'est plus modifiée (dirty = false), la fermeture peut avoir lieu.
    if (app.quitPending && !app.dirty) g_quit = true;
}

bool quitRequested() { return g_quit; }

void requestQuit(App& app) {
    // Un dialogue est déjà ouvert (sauvegarde, import…) : on ignore la demande,
    // l'utilisateur est en plein flux et reviendra à l'édition ensuite.
    if (anyModalOpen(app)) return;
    if (!app.dirty) {
        g_quit = true;  // scène propre : sortie immédiate
        return;
    }
    app.quitPending = true;
    app.dlgQuitOpen = true;
}

}  // namespace mesh::ui
