#include "svgicon.h"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

namespace mesh::ui {

namespace {

bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return !out.empty();
}

// Trace une polyligne (espace viewBox) en « trait rond » : un quad par
// segment + un disque à chaque sommet (extrémités et jonctions arrondies,
// comme stroke-linecap/linejoin="round"). Si `closed`, le segment de
// fermeture (dernier → premier sommet) est aussi tracé : indispensable pour
// les contours de <rect> et <polygon>, stockés sans premier sommet dupliqué
// (les <path d="…Z"> et <circle> le dupliquent déjà : le segment de
// fermeture y est de longueur nulle et sauté par le test len < 1e-6).
template <typename Transform>
void strokePolyline(ImDrawList* dl, const std::vector<svg::Pt>& pts,
                    const Transform& t, float w, ImU32 col, bool closed) {
    const float half = w * 0.5f;
    auto seg = [&](const svg::Pt& a, const svg::Pt& b) {
        const float dx = b.x - a.x, dy = b.y - a.y;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-6f) return;
        const float nx = -dy / len * half, ny = dx / len * half;
        dl->AddQuadFilled(t(a.x + nx, a.y + ny), t(a.x - nx, a.y - ny),
                          t(b.x - nx, b.y - ny), t(b.x + nx, b.y + ny), col);
    };
    for (size_t i = 1; i < pts.size(); ++i) seg(pts[i - 1], pts[i]);
    if (closed && pts.size() >= 2) seg(pts.back(), pts.front());
    for (const svg::Pt& p : pts)
        dl->AddCircleFilled(t(p.x, p.y), half, col, 12);
}

}  // namespace

// ---------------------------------------------------------------------------
// IconSet
// ---------------------------------------------------------------------------

IconSet& IconSet::instance() {
    static IconSet s;
    return s;
}

bool IconSet::load() {
    // Dossier assets/ : près de l'exécutable (SDL_GetBasePath), puis dans le
    // répertoire courant et ses parents (ex. lancement depuis build/).
    std::vector<std::string> candidates;
    if (char* base = SDL_GetBasePath()) {
        candidates.push_back(std::string(base) + "assets");
        SDL_free(base);
    }
    candidates.push_back("assets");
    candidates.push_back("../assets");
    candidates.push_back("../../assets");

    for (const auto& dir : candidates) {
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec)) continue;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (ec || !entry.is_regular_file()) continue;
            const auto path = entry.path();
            if (path.extension() != ".svg") continue;
            std::string content;
            if (!readFile(path.string(), content)) continue;
            map_[path.stem().string()] = std::move(content);
        }
        return true;
    }
    std::fprintf(stderr, "IconSet : dossier assets/ introuvable, icônes absentes\n");
    return false;
}

const svg::Icon* IconSet::parsed(const char* name) {
    if (!tried_) {
        tried_ = true;
        load();
    }
    auto it = parsed_.find(name);
    if (it != parsed_.end()) return &it->second;
    const auto raw = map_.find(name);
    if (raw == map_.end()) return nullptr;
    svg::Icon icon = svg::parseSvg(raw->second);
    if (!icon.ok) return nullptr;
    // Les références vers les éléments d'un unordered_map restent valides
    // après insertion (aucune réallocation de nœuds).
    return &parsed_.emplace(name, std::move(icon)).first->second;
}

// ---------------------------------------------------------------------------
// Rendu
// ---------------------------------------------------------------------------

bool drawSvgIcon(ImDrawList* dl, const svg::Icon& icon, const ImVec2& pos,
                 float size, ImU32 color) {
    if (size <= 0.0f || !dl) return false;

    // Transformation viewBox → écran (échelle uniforme, recentrée).
    const float s = size / std::max(icon.vbW, icon.vbH);
    const float ox = pos.x + (size - icon.vbW * s) * 0.5f - icon.vbMinX * s;
    const float oy = pos.y + (size - icon.vbH * s) * 0.5f - icon.vbMinY * s;
    const auto t = [=](float x, float y) { return ImVec2(ox + x * s, oy + y * s); };

    // Remplissages.
    for (const auto& fc : icon.fillCircles)
        dl->AddCircleFilled(t(fc.c.x, fc.c.y), fc.r * s, color, 24);
    for (const auto& fr : icon.fillRects)
        dl->AddRectFilled(t(fr.min.x, fr.min.y), t(fr.max.x, fr.max.y), color,
                          fr.rounding * s);
    for (const auto& fp : icon.fillPolys) {
        if (fp.pts.size() < 3) continue;
        dl->PathClear();
        dl->PathLineTo(t(fp.pts[0].x, fp.pts[0].y));
        for (size_t i = 1; i < fp.pts.size(); ++i)
            dl->PathLineTo(t(fp.pts[i].x, fp.pts[i].y));
        dl->PathFillConvex(color);
    }

    // Contours.
    const float w = icon.strokeWidth * s;
    for (const auto& st : icon.strokes)
        if (st.pts.size() >= 2)
            strokePolyline(dl, st.pts, t, w, color, st.closed);
    return true;
}

bool drawSvgIconNamed(ImDrawList* dl, const char* name, const ImVec2& pos,
                      float size, ImU32 color) {
    const svg::Icon* icon = IconSet::instance().parsed(name);
    if (!icon) return false;
    return drawSvgIcon(dl, *icon, pos, size, color);
}

}  // namespace mesh::ui
