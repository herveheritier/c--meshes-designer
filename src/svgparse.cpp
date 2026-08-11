#include "svgparse.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace mesh::svg {

namespace {

constexpr float kPi = 3.14159265358979f;

// ---------------------------------------------------------------------------
// Décodage des éléments <tag attrs…> d'un document SVG
// ---------------------------------------------------------------------------

struct El {
    std::string tag;
    std::vector<std::pair<std::string, std::string>> attrs;
};

bool isSep(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ',';
}

bool parseElements(const std::string& s, std::vector<El>& out) {
    const char* p = s.c_str();
    while (*p) {
        const char* lt = std::strchr(p, '<');
        if (!lt) break;
        p = lt + 1;
        // Fermeture, déclaration XML ou commentaire : on saute jusqu'au '>'.
        if (*p == '/' || *p == '?' || *p == '!') {
            const char* gt = std::strchr(p, '>');
            if (!gt) return false;
            p = gt + 1;
            continue;
        }
        El el;
        const char* s0 = p;
        while (*p && std::isalnum((unsigned char)*p)) ++p;
        el.tag.assign(s0, p - s0);
        if (el.tag.empty()) return false;

        // Attributs jusqu'à '>' (les éléments du jeu d'icônes sont vides :
        // self-closants, ou avec contenu sans élément imbriqué).
        while (*p && *p != '>') {
            if (*p == '/' || std::isspace((unsigned char)*p)) {
                ++p;
                continue;
            }
            const char* an = p;
            while (*p && *p != '=' && *p != '>' && *p != '/' &&
                   !std::isspace((unsigned char)*p))
                ++p;
            std::string name(an, p - an);
            while (*p == ' ' || *p == '\t') ++p;
            std::string value;
            if (*p == '=') {
                ++p;
                while (*p == ' ' || *p == '\t') ++p;
                if (*p == '"' || *p == '\'') {
                    const char q = *p++;
                    const char* vs = p;
                    while (*p && *p != q) ++p;
                    value.assign(vs, p - vs);
                    if (*p == q) ++p;
                } else {
                    const char* vs = p;
                    while (*p && *p != '>' && !std::isspace((unsigned char)*p)) ++p;
                    value.assign(vs, p - vs);
                }
            }
            el.attrs.emplace_back(name, value);
        }
        if (*p == '>') ++p;
        out.push_back(std::move(el));
    }
    return true;
}

const std::string* attrOf(const El& el, const char* name) {
    for (const auto& kv : el.attrs)
        if (kv.first == name) return &kv.second;
    return nullptr;
}

std::string resolveAttr(const El& el, const El* root, const char* name,
                        const char* dflt) {
    if (const std::string* v = attrOf(el, name)) return *v;
    if (root)
        if (const std::string* v = attrOf(*root, name)) return *v;
    return dflt ? dflt : "";
}

float attrNum(const El& el, const char* name, float dflt) {
    const std::string* v = attrOf(el, name);
    return v ? (float)std::atof(v->c_str()) : dflt;
}

// ---------------------------------------------------------------------------
// Lecture de nombres (séparateurs espaces/virgules, comme dans les SVG)
// ---------------------------------------------------------------------------

struct NumReader {
    const char* p = nullptr;
    bool read(float& v) {
        while (isSep(*p)) ++p;
        if (!*p) return false;
        char* end = nullptr;
        v = (float)std::strtod(p, &end);
        if (end == p) return false;
        p = end;
        return true;
    }
    // Les drapeaux d'arc SVG sont des caractères isolés '0'/'1' (ils peuvent
    // être collés au nombre suivant : « a2 2 0 014 0 » = large=0, sweep=1,
    // x=4). strtod lirait « 014 » comme 14 : il faut lire un seul caractère.
    bool readFlag(bool& out) {
        while (isSep(*p)) ++p;
        if (*p != '0' && *p != '1') return false;
        out = (*p == '1');
        ++p;
        return true;
    }
};

std::vector<Pt> parsePoints(const std::string& s) {
    std::vector<Pt> pts;
    NumReader nr;
    nr.p = s.c_str();
    while (true) {
        float x = 0.0f, y = 0.0f;
        if (!nr.read(x) || !nr.read(y)) break;
        pts.push_back({x, y});
    }
    return pts;
}

// ---------------------------------------------------------------------------
// Tracés : commandes M/L/H/V/C/S/Q/T/A/Z (formes relatives acceptées),
// courbes de Bézier et arcs échantillonnés en polylignes.
// ---------------------------------------------------------------------------

void cubicTo(std::vector<Pt>& pts, const Pt& p0, const Pt& c1, const Pt& c2,
             const Pt& p1, int n) {
    for (int i = 1; i <= n; ++i) {
        const float t = (float)i / (float)n;
        const float mt = 1.0f - t;
        const float a = mt * mt * mt;
        const float b = 3.0f * mt * mt * t;
        const float c = 3.0f * mt * t * t;
        const float d = t * t * t;
        pts.push_back({a * p0.x + b * c1.x + c * c2.x + d * p1.x,
                       a * p0.y + b * c1.y + c * c2.y + d * p1.y});
    }
}

void quadTo(std::vector<Pt>& pts, const Pt& p0, const Pt& c, const Pt& p1,
            int n) {
    for (int i = 1; i <= n; ++i) {
        const float t = (float)i / (float)n;
        const float mt = 1.0f - t;
        const float a = mt * mt;
        const float b = 2.0f * mt * t;
        const float d = t * t;
        pts.push_back({a * p0.x + b * c.x + d * p1.x,
                       a * p0.y + b * c.y + d * p1.y});
    }
}

// Arc elliptique SVG : conversion point final → centre (spéc. SVG F.6.5),
// puis échantillonnage. Gère aussi rx == ry (cercles) et rx/ry == 0.
void arcTo(std::vector<Pt>& pts, Pt cur, float rx, float ry, float rotDeg,
           bool large, bool sweep, Pt end, int n) {
    rx = std::fabs(rx);
    ry = std::fabs(ry);
    const float dx2 = (cur.x - end.x) * 0.5f;
    const float dy2 = (cur.y - end.y) * 0.5f;
    const float phi = rotDeg * kPi / 180.0f;
    const float cphi = std::cos(phi);
    const float sphi = std::sin(phi);

    // Étape 1 : dans le repère de rotation nulle.
    const float x1p = cphi * dx2 + sphi * dy2;
    const float y1p = -sphi * dx2 + cphi * dy2;

    // Correction des rayons (échelle si le rayon est trop petit).
    const float lambda = x1p * x1p / (rx * rx) + y1p * y1p / (ry * ry);
    if (lambda > 1.0f) {
        const float k = std::sqrt(lambda);
        rx *= k;
        ry *= k;
    }
    if (rx <= 0.0f || ry <= 0.0f) {
        pts.push_back(end);  // arc dégénéré : segment droit
        return;
    }

    // Étape 2 : centre dans le repère de rotation nulle.
    const float num = rx * rx * ry * ry - rx * rx * y1p * y1p -
                      ry * ry * x1p * x1p;
    const float den = rx * rx * y1p * y1p + ry * ry * x1p * x1p;
    float coef = den > 0.0f ? std::sqrt(std::fmax(0.0f, num / den)) : 0.0f;
    if (large == sweep) coef = -coef;
    const float cxp = coef * (rx * y1p) / ry;
    const float cyp = coef * (-ry * x1p) / rx;
    const float cx = cphi * cxp - sphi * cyp + (cur.x + end.x) * 0.5f;
    const float cy = sphi * cxp + cphi * cyp + (cur.y + end.y) * 0.5f;

    auto angle = [](float ux, float uy, float vx, float vy) {
        const float dot = ux * vx + uy * vy;
        const float len = std::sqrt((ux * ux + uy * uy) * (vx * vx + vy * vy));
        float a = len > 0.0f ? std::acos(std::fmax(-1.0f, std::fmin(1.0f, dot / len)))
                             : 0.0f;
        if (ux * vy - uy * vx < 0.0f) a = -a;
        return a;
    };

    // Étape 3 : angles de départ et de balayage.
    const float theta1 = angle(1.0f, 0.0f, (x1p - cxp) / rx, (y1p - cyp) / ry);
    float dtheta = angle((x1p - cxp) / rx, (y1p - cyp) / ry,
                         (-x1p - cxp) / rx, (-y1p - cyp) / ry);
    if (!sweep && dtheta > 0.0f) dtheta -= 2.0f * kPi;
    if (sweep && dtheta < 0.0f) dtheta += 2.0f * kPi;

    // Échantillonnage.
    for (int i = 1; i <= n; ++i) {
        const float a = theta1 + (float)i / (float)n * dtheta;
        const float ex = rx * std::cos(a);
        const float ey = ry * std::sin(a);
        pts.push_back({cx + cphi * ex - sphi * ey, cy + sphi * ex + cphi * ey});
    }
}

struct PathBuilder {
    NumReader nr;
    float curX = 0.0f, curY = 0.0f;
    float startX = 0.0f, startY = 0.0f;
    float ctrlX = 0.0f, ctrlY = 0.0f;  // dernier point de contrôle (S/T)
    char lastCmd = 0;
    std::vector<Pt> pts;  // sous-tracé courant
    bool closed = false;

    void flushTo(std::vector<Stroke>& out) {
        if (!pts.empty()) {
            out.push_back(Stroke{std::move(pts), closed});
            pts.clear();
        }
        closed = false;
    }

    void lineTo(float x, float y) {
        curX = x;
        curY = y;
        pts.push_back({curX, curY});
    }

    // Exécute une répétition de la commande `cmd`. Retourne false si les
    // nombres manquent (arrêt défensif du tracé).
    bool exec(char cmd, std::vector<Stroke>& out) {
        const bool rel = cmd >= 'a' && cmd <= 'z';
        switch (std::toupper((unsigned char)cmd)) {
            case 'M': {
                float x = 0.0f, y = 0.0f;
                if (!nr.read(x) || !nr.read(y)) return false;
                flushTo(out);
                curX = rel ? curX + x : x;
                curY = rel ? curY + y : y;
                startX = curX;
                startY = curY;
                pts.push_back({curX, curY});
                lastCmd = 'M';
                return true;
            }
            case 'L': {
                float x = 0.0f, y = 0.0f;
                if (!nr.read(x) || !nr.read(y)) return false;
                lineTo(rel ? curX + x : x, rel ? curY + y : y);
                lastCmd = 'L';
                return true;
            }
            case 'H': {
                float x = 0.0f;
                if (!nr.read(x)) return false;
                lineTo(rel ? curX + x : x, curY);
                lastCmd = 'H';
                return true;
            }
            case 'V': {
                float y = 0.0f;
                if (!nr.read(y)) return false;
                lineTo(curX, rel ? curY + y : y);
                lastCmd = 'V';
                return true;
            }
            case 'C': {
                float x1, y1, x2, y2, x, y;
                if (!nr.read(x1) || !nr.read(y1) || !nr.read(x2) || !nr.read(y2) ||
                    !nr.read(x) || !nr.read(y))
                    return false;
                if (rel) {
                    x1 += curX; y1 += curY; x2 += curX; y2 += curY;
                    x += curX; y += curY;
                }
                cubicTo(pts, {curX, curY}, {x1, y1}, {x2, y2}, {x, y}, 14);
                curX = x;
                curY = y;
                ctrlX = x2;
                ctrlY = y2;
                lastCmd = 'C';
                return true;
            }
            case 'S': {
                float x2, y2, x, y;
                if (!nr.read(x2) || !nr.read(y2) || !nr.read(x) || !nr.read(y))
                    return false;
                if (rel) {
                    x2 += curX; y2 += curY; x += curX; y += curY;
                }
                float x1 = curX, y1 = curY;
                if (lastCmd == 'C' || lastCmd == 'S') {
                    x1 = 2.0f * curX - ctrlX;
                    y1 = 2.0f * curY - ctrlY;
                }
                cubicTo(pts, {curX, curY}, {x1, y1}, {x2, y2}, {x, y}, 14);
                curX = x;
                curY = y;
                ctrlX = x2;
                ctrlY = y2;
                lastCmd = 'S';
                return true;
            }
            case 'Q': {
                float x1, y1, x, y;
                if (!nr.read(x1) || !nr.read(y1) || !nr.read(x) || !nr.read(y))
                    return false;
                if (rel) {
                    x1 += curX; y1 += curY; x += curX; y += curY;
                }
                quadTo(pts, {curX, curY}, {x1, y1}, {x, y}, 10);
                curX = x;
                curY = y;
                ctrlX = x1;
                ctrlY = y1;
                lastCmd = 'Q';
                return true;
            }
            case 'T': {
                float x, y;
                if (!nr.read(x) || !nr.read(y)) return false;
                if (rel) {
                    x += curX;
                    y += curY;
                }
                float x1 = curX, y1 = curY;
                if (lastCmd == 'Q' || lastCmd == 'T') {
                    x1 = 2.0f * curX - ctrlX;
                    y1 = 2.0f * curY - ctrlY;
                }
                quadTo(pts, {curX, curY}, {x1, y1}, {x, y}, 10);
                curX = x;
                curY = y;
                ctrlX = x1;
                ctrlY = y1;
                lastCmd = 'T';
                return true;
            }
            case 'A': {
                float rx, ry, rot, x, y;
                bool large, sweep;
                if (!nr.read(rx) || !nr.read(ry) || !nr.read(rot) ||
                    !nr.readFlag(large) || !nr.readFlag(sweep) || !nr.read(x) ||
                    !nr.read(y))
                    return false;
                if (rel) {
                    x += curX;
                    y += curY;
                }
                arcTo(pts, {curX, curY}, rx, ry, rot, large, sweep, {x, y}, 20);
                curX = x;
                curY = y;
                lastCmd = 'A';
                return true;
            }
            case 'Z': {
                if (!pts.empty()) {
                    const Pt& first = pts.front();
                    const Pt& lastp = pts.back();
                    const float dx = first.x - lastp.x;
                    const float dy = first.y - lastp.y;
                    if (dx * dx + dy * dy > 1e-6f) pts.push_back(first);
                    closed = true;
                }
                curX = startX;
                curY = startY;
                lastCmd = 'Z';
                return true;
            }
        }
        return false;
    }
};

void parsePathData(const std::string& d, std::vector<Stroke>& out) {
    PathBuilder pb;
    pb.nr.p = d.c_str();
    char last = 0;
    while (true) {
        while (isSep(*pb.nr.p)) ++pb.nr.p;
        if (!*pb.nr.p) break;
        const char c = *pb.nr.p;
        if (std::isalpha((unsigned char)c)) {
            last = c;
            ++pb.nr.p;
        } else if (last == 0) {
            break;  // aucun tracé : données non valides
        }
        // Après Z, un nombre sans lettre ne constitue pas une commande.
        if (!std::isalpha((unsigned char)c) && last == 'Z') break;
        if (!pb.exec(last, out)) break;  // nombres insuffisants : arrêt prudent
    }
    pb.flushTo(out);
}

}  // namespace

// ---------------------------------------------------------------------------
// API publique
// ---------------------------------------------------------------------------

Icon parseSvg(const std::string& svg) {
    Icon icon;
    std::vector<El> els;
    if (!parseElements(svg, els)) return icon;

    const El* root = nullptr;
    for (const auto& el : els) {
        if (el.tag == "svg") {
            root = &el;
            break;
        }
    }
    if (!root) return icon;  // pas de racine <svg>
    icon.ok = true;

    // viewBox="minx miny w h" (défaut 16×16 à l'origine).
    if (const std::string* vb = attrOf(*root, "viewBox")) {
        NumReader nr;
        nr.p = vb->c_str();
        float a = 0.0f, b = 0.0f, c = 0.0f, d = 0.0f;
        if (nr.read(a) && nr.read(b) && nr.read(c) && nr.read(d)) {
            icon.vbMinX = a;
            icon.vbMinY = b;
            icon.vbW = c;
            icon.vbH = d;
        }
    }
    icon.strokeWidth =
        (float)std::atof(resolveAttr(*root, root, "stroke-width", "1.5").c_str());

    for (const auto& el : els) {
        if (el.tag == "svg") continue;
        // Défauts conformes à la spec SVG : rempli (noir) par défaut, contour
        // absent — la couleur de remplissage est remplacée par la teinte au
        // rendu. Les vraies icônes précisent toujours fill/stroke à la racine.
        const std::string fill = resolveAttr(el, root, "fill", "currentColor");
        const std::string stroke = resolveAttr(el, root, "stroke", "none");
        const bool doFill = fill != "none";
        const bool doStroke = stroke != "none";

        if (el.tag == "line") {
            std::vector<Pt> pts{{attrNum(el, "x1", 0.0f), attrNum(el, "y1", 0.0f)},
                                {attrNum(el, "x2", 0.0f), attrNum(el, "y2", 0.0f)}};
            if (doStroke) icon.strokes.push_back(Stroke{std::move(pts), false});
        } else if (el.tag == "rect") {
            const float x = attrNum(el, "x", 0.0f);
            const float y = attrNum(el, "y", 0.0f);
            const float w = attrNum(el, "width", 0.0f);
            const float h = attrNum(el, "height", 0.0f);
            const float rx = attrNum(el, "rx", 0.0f);
            if (doFill && w > 0.0f && h > 0.0f)
                icon.fillRects.push_back(FillRect{{x, y}, {x + w, y + h}, rx});
            if (doStroke && w > 0.0f && h > 0.0f) {
                std::vector<Pt> pts{{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}};
                icon.strokes.push_back(Stroke{std::move(pts), true});
            }
        } else if (el.tag == "circle") {
            const float cx = attrNum(el, "cx", 0.0f);
            const float cy = attrNum(el, "cy", 0.0f);
            const float r = attrNum(el, "r", 0.0f);
            if (doFill && r > 0.0f) icon.fillCircles.push_back(FillCircle{{cx, cy}, r});
            if (doStroke && r > 0.0f) {
                std::vector<Pt> pts;
                pts.reserve(25);
                for (int i = 0; i <= 24; ++i) {
                    const float a = 2.0f * kPi * (float)i / 24.0f;
                    pts.push_back({cx + r * std::cos(a), cy + r * std::sin(a)});
                }
                icon.strokes.push_back(Stroke{std::move(pts), true});
            }
        } else if (el.tag == "polyline" || el.tag == "polygon") {
            std::vector<Pt> pts;
            if (const std::string* p = attrOf(el, "points")) pts = parsePoints(*p);
            if (el.tag == "polygon") {
                if (doFill && pts.size() >= 3)
                    icon.fillPolys.push_back(FillPoly{pts});
                if (doStroke && pts.size() >= 2)
                    icon.strokes.push_back(Stroke{std::move(pts), true});
            } else {
                if (doStroke && pts.size() >= 2)
                    icon.strokes.push_back(Stroke{std::move(pts), false});
            }
        } else if (el.tag == "path") {
            if (const std::string* d = attrOf(el, "d")) {
                std::vector<Stroke> subs;
                parsePathData(*d, subs);
                if (doStroke)
                    for (auto& st : subs) icon.strokes.push_back(std::move(st));
                if (doFill)
                    for (auto& st : subs)
                        if (st.pts.size() >= 3)
                            icon.fillPolys.push_back(FillPoly{st.pts});
            }
        }
    }
    return icon;
}

}  // namespace mesh::svg
