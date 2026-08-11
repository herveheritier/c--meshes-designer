#include "io.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace mesh {

namespace {

bool writeText(const std::string& path, const std::string& text, std::string& err) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        err = "Impossible d'écrire le fichier : " + path;
        return false;
    }
    f << text;
    return true;
}

// ---------------------------------------------------------------------------
// Mini JSON (analyse + sérialisation) — suffisant pour les fichiers de scène.
// ---------------------------------------------------------------------------
struct JVal {
    enum class T { Null, Bool, Num, Str, Arr, Obj } t = T::Null;
    bool b = false;
    double n = 0.0;
    std::string s;
    std::vector<JVal> arr;
    std::vector<std::pair<std::string, JVal>> obj;

    static JVal num(double v) { JVal j; j.t = T::Num; j.n = v; return j; }
    static JVal str(const std::string& v) { JVal j; j.t = T::Str; j.s = v; return j; }
    static JVal boolean(bool v) { JVal j; j.t = T::Bool; j.b = v; return j; }
    static JVal array() { JVal j; j.t = T::Arr; return j; }
    static JVal object() { JVal j; j.t = T::Obj; return j; }

    const JVal* find(const std::string& key) const {
        if (t != T::Obj) return nullptr;
        for (const auto& kv : obj)
            if (kv.first == key) return &kv.second;
        return nullptr;
    }
    bool getNum(const std::string& key, double& out) const {
        const JVal* v = find(key);
        if (!v || v->t != T::Num) return false;
        out = v->n;
        return true;
    }
    bool getBool(const std::string& key, bool& out) const {
        const JVal* v = find(key);
        if (!v || v->t != T::Bool) return false;
        out = v->b;
        return true;
    }
    bool getStr(const std::string& key, std::string& out) const {
        const JVal* v = find(key);
        if (!v || v->t != T::Str) return false;
        out = v->s;
        return true;
    }
};

struct JsonParser {
    const char* p = nullptr;
    std::string err;

    bool fail(const std::string& e) {
        if (err.empty()) err = e;
        return false;
    }
    void ws() {
        while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
    }
    bool parse(JVal& out) {
        ws();
        return value(out);
    }
    bool match(const char* w) {
        const size_t n = std::strlen(w);
        if (std::strncmp(p, w, n) == 0) {
            p += n;
            return true;
        }
        return false;
    }
    bool value(JVal& out) {
        ws();
        if (*p == '{') return object(out);
        if (*p == '[') return array(out);
        if (*p == '"') return string(out);
        if (*p == 't') { if (match("true")) { out = JVal::boolean(true); return true; } return fail("« true » attendu"); }
        if (*p == 'f') { if (match("false")) { out = JVal::boolean(false); return true; } return fail("« false » attendu"); }
        if (*p == 'n') { if (match("null")) return true; return fail("« null » attendu"); }
        return number(out);
    }
    bool string(JVal& out) {
        ++p;  // ouvre le guillemet
        std::string s;
        while (*p && *p != '"') {
            if (*p == '\\') {
                ++p;
                if (!*p) return fail("chaîne non terminée");  // garde anti dépassement
                switch (*p) {
                    case 'n': s += '\n'; break;
                    case 't': s += '\t'; break;
                    case 'r': s += '\r'; break;
                    case '"': s += '"'; break;
                    case '\\': s += '\\'; break;
                    case '/': s += '/'; break;
                    default: s += *p; break;  // \uXXXX : non traduit, conservé
                }
                ++p;
            } else {
                s += *p++;
            }
        }
        if (*p != '"') return fail("chaîne non terminée");
        ++p;
        out = JVal::str(s);
        return true;
    }
    bool number(JVal& out) {
        char* end = nullptr;
        const double v = std::strtod(p, &end);
        if (end == p) return fail("nombre attendu");
        p = end;
        out = JVal::num(v);
        return true;
    }
    bool array(JVal& out) {
        ++p;
        out = JVal::array();
        ws();
        if (*p == ']') { ++p; return true; }
        for (;;) {
            JVal v;
            if (!value(v)) return false;
            out.arr.push_back(v);
            ws();
            if (*p == ',') { ++p; continue; }
            if (*p == ']') { ++p; return true; }
            return fail("« , » ou « ] » attendu");
        }
    }
    bool object(JVal& out) {
        ++p;
        out = JVal::object();
        ws();
        if (*p == '}') { ++p; return true; }
        for (;;) {
            JVal k;
            if (!string(k)) return fail("clé attendue");
            ws();
            if (*p != ':') return fail("« : » attendu");
            ++p;
            JVal v;
            if (!value(v)) return false;
            out.obj.emplace_back(k.s, v);
            ws();
            if (*p == ',') { ++p; continue; }
            if (*p == '}') { ++p; return true; }
            return fail("« , » ou « } » attendu");
        }
    }
};

std::string dumpJson(const JVal& v) {
    switch (v.t) {
        case JVal::T::Null: return "null";
        case JVal::T::Bool: return v.b ? "true" : "false";
        case JVal::T::Num: {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.9g", v.n);
            return buf;
        }
        case JVal::T::Str: {
            std::string o = "\"";
            for (char c : v.s) {
                switch (c) {
                    case '"': o += "\\\""; break;
                    case '\\': o += "\\\\"; break;
                    case '\n': o += "\\n"; break;
                    case '\t': o += "\\t"; break;
                    case '\r': o += "\\r"; break;
                    default: o += c; break;
                }
            }
            o += "\"";
            return o;
        }
        case JVal::T::Arr: {
            std::string o = "[";
            for (size_t i = 0; i < v.arr.size(); ++i) {
                if (i) o += ",";
                o += dumpJson(v.arr[i]);
            }
            o += "]";
            return o;
        }
        case JVal::T::Obj: {
            std::string o = "{";
            for (size_t i = 0; i < v.obj.size(); ++i) {
                if (i) o += ",";
                o += dumpJson(JVal::str(v.obj[i].first)) + ":" + dumpJson(v.obj[i].second);
            }
            o += "}";
            return o;
        }
    }
    return "null";
}

// ---------------------------------------------------------------------------
// Sérialisation du maillage
// ---------------------------------------------------------------------------
JVal meshToJson(const Mesh2D& m) {
    JVal v = JVal::object();
    // Nom du plan (facultatif) : émis seulement s'il est défini, pour que les
    // fichiers existants restent identiques (repli « Plan n » à la lecture).
    if (!m.name.empty()) v.obj.emplace_back("name", JVal::str(m.name));
    JVal verts = JVal::array();
    for (const Vec2& pt : m.vertices) {
        JVal p = JVal::array();
        p.arr.push_back(JVal::num(pt.x));
        p.arr.push_back(JVal::num(pt.y));
        verts.arr.push_back(p);
    }
    v.obj.emplace_back("verts", verts);
    JVal faces = JVal::array();
    for (const Face& f : m.faces) {
        JVal fj = JVal::object();
        JVal fv = JVal::array();
        for (int i : f.verts) fv.arr.push_back(JVal::num(i));
        fj.obj.emplace_back("v", fv);
        if (f.hasColor) {
            JVal c = JVal::array();
            c.arr.push_back(JVal::num(f.color.r));
            c.arr.push_back(JVal::num(f.color.g));
            c.arr.push_back(JVal::num(f.color.b));
            c.arr.push_back(JVal::num(f.color.a));
            fj.obj.emplace_back("color", c);
        }
        faces.arr.push_back(fj);
    }
    v.obj.emplace_back("faces", faces);
    return v;
}

bool meshFromJson(const JVal& v, Mesh2D& out, std::string& err) {
    v.getStr("name", out.name);  // nom du plan (absent dans les anciens fichiers)
    const JVal* jv = v.find("verts");
    if (!jv || jv->t != JVal::T::Arr) {
        err = "JSON invalide : « verts » manquant ou mal formé";
        return false;
    }
    for (const JVal& p : jv->arr) {
        if (p.t != JVal::T::Arr || p.arr.size() < 2) {
            err = "JSON invalide : sommet mal formé";
            return false;
        }
        out.addVertex({(float)p.arr[0].n, (float)p.arr[1].n});
    }
    const JVal* jf = v.find("faces");
    if (jf && jf->t == JVal::T::Arr) {
        for (const JVal& fj : jf->arr) {
            if (fj.t != JVal::T::Obj) {
                err = "JSON invalide : face mal formée";
                return false;
            }
            const JVal* fv = fj.find("v");
            if (!fv || fv->t != JVal::T::Arr || fv->arr.size() < 3) {
                err = "JSON invalide : face sans sommets valides";
                return false;
            }
            std::vector<int> loop;
            for (const JVal& iv : fv->arr) {
                const int idx = (int)iv.n;
                if (idx < 0 || idx >= (int)out.vertices.size()) {
                    err = "JSON invalide : indice de sommet hors limites";
                    return false;
                }
                loop.push_back(idx);
            }
            Face f;
            f.verts = loop;
            const JVal* col = fj.find("color");
            if (col && col->t == JVal::T::Arr && col->arr.size() >= 4) {
                f.color = {(float)col->arr[0].n, (float)col->arr[1].n,
                           (float)col->arr[2].n, (float)col->arr[3].n};
                f.hasColor = true;
            }
            out.faces.push_back(f);
        }
    }
    return true;
}

JVal sceneToJson(const SceneSnapshot& s) {
    JVal v = JVal::object();
    v.obj.emplace_back("app", JVal::str("meshes-designer"));
    v.obj.emplace_back("name", JVal::str(s.name));
    v.obj.emplace_back("zoom", JVal::num(s.zoomMult));
    v.obj.emplace_back("cx", JVal::num(s.cx));
    v.obj.emplace_back("cy", JVal::num(s.cy));
    v.obj.emplace_back("grid", JVal::boolean(s.grid));
    v.obj.emplace_back("gridStep", JVal::num(s.gridStep));
    v.obj.emplace_back("active", JVal::num(s.scene.active));
    JVal planes = JVal::array();
    for (const Mesh2D& m : s.scene.planes) planes.arr.push_back(meshToJson(m));
    v.obj.emplace_back("planes", planes);
    return v;
}

// Géométrie d'une scène seule (pour les historiques annuler/rétablir).
JVal sceneGeomToJson(const Scene& s) {
    JVal v = JVal::object();
    v.obj.emplace_back("active", JVal::num(s.active));
    JVal planes = JVal::array();
    for (const Mesh2D& m : s.planes) planes.arr.push_back(meshToJson(m));
    v.obj.emplace_back("planes", planes);
    return v;
}

bool sceneGeomFromJson(const JVal& v, Scene& out, std::string& err) {
    const JVal* planes = v.find("planes");
    if (!planes || planes->t != JVal::T::Arr) {
        err = "JSON invalide : « planes » manquant";
        return false;
    }
    for (const JVal& pj : planes->arr) {
        Mesh2D p;
        if (!meshFromJson(pj, p, err)) return false;
        out.planes.push_back(std::move(p));
    }
    double d = 0.0;
    if (v.getNum("active", d)) out.active = (int)d;
    if (out.planes.empty()) {
        err = "JSON invalide : aucun plan";
        return false;
    }
    out.active = std::clamp(out.active, 0, (int)out.planes.size() - 1);
    return true;
}

bool sceneFromJson(const JVal& v, SceneSnapshot& out, std::string& err) {
    v.getStr("name", out.name);
    double d = 0.0;
    if (v.getNum("zoom", d)) out.zoomMult = (float)d;
    if (v.getNum("cx", d)) out.cx = (float)d;
    if (v.getNum("cy", d)) out.cy = (float)d;
    bool g = true;
    if (v.getBool("grid", g)) out.grid = g;
    if (v.getNum("gridStep", d)) out.gridStep = (float)d;
    const JVal* planes = v.find("planes");
    if (planes && planes->t == JVal::T::Arr) {
        for (const JVal& pj : planes->arr) {
            Mesh2D p;
            if (!meshFromJson(pj, p, err)) return false;
            out.scene.planes.push_back(std::move(p));
        }
    } else {
        // Ancien format : un seul maillage « mesh » → un seul plan.
        const JVal* mesh = v.find("mesh");
        if (!mesh) {
            err = "JSON invalide : « planes » ou « mesh » manquant";
            return false;
        }
        if (!meshFromJson(*mesh, out.scene.planes.emplace_back(), err)) return false;
    }
    if (v.getNum("active", d)) out.scene.active = (int)d;
    if (out.scene.planes.empty()) {
        err = "JSON invalide : aucun plan";
        return false;
    }
    out.scene.active = std::clamp(out.scene.active, 0, (int)out.scene.planes.size() - 1);
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// Formats historiques (meshcore / meshtest)
// ---------------------------------------------------------------------------
IoResult saveNative(const Mesh2D& m, const std::string& path) {
    std::ostringstream s;
    s << "MESH2D 1\n";
    s << m.vertices.size() << "\n";
    for (const Vec2& v : m.vertices) s << v.x << " " << v.y << "\n";
    s << m.faces.size() << "\n";
    for (const Face& f : m.faces) {
        s << f.verts.size();
        for (int i : f.verts) s << " " << i;
        s << "\n";
    }
    IoResult r;
    r.ok = writeText(path, s.str(), r.error);
    return r;
}

IoResult loadNative(Mesh2D& m, const std::string& path) {
    std::ifstream f(path);
    if (!f) return {false, "Impossible d'ouvrir le fichier : " + path};

    std::string magic;
    int ver = 0;
    if (!(f >> magic >> ver) || magic != "MESH2D" || ver != 1)
        return {false, "Format de fichier non reconnu (" + path + ")"};

    Mesh2D out;
    size_t nv = 0;
    f >> nv;
    if (nv > 1000000) return {false, "Fichier corrompu (trop de sommets)."};
    for (size_t i = 0; i < nv; ++i) {
        float x = 0.0f, y = 0.0f;
        f >> x >> y;
        out.addVertex({x, y});
    }
    size_t nf = 0;
    f >> nf;
    if (nf > 1000000) return {false, "Fichier corrompu (trop de faces)."};
    for (size_t i = 0; i < nf; ++i) {
        int k = 0;
        f >> k;
        if (k < 3 || k > 100000) return {false, "Fichier corrompu (face invalide)."};
        std::vector<int> loop;
        loop.reserve(k);
        for (int j = 0; j < k; ++j) {
            int idx = -1;
            f >> idx;
            loop.push_back(idx);
        }
        out.addFace(loop);
    }
    if (!f.good() && !f.eof()) return {false, "Fichier corrompu (lecture interrompue)."};
    m = std::move(out);
    return {true, ""};
}

IoResult exportOBJ(const Mesh2D& m, const std::string& path) {
    std::ostringstream s;
    s << "# Export Mesh2D (editeur de meshes) — " << m.vertices.size() << " sommets, "
      << m.faces.size() << " faces\n";
    for (const Vec2& v : m.vertices) s << "v " << v.x << " " << v.y << " 0\n";
    for (const Face& f : m.faces) {
        s << "f";
        for (int i : f.verts) s << " " << (i + 1);  // OBJ : indices 1-based
        s << "\n";
    }
    IoResult r;
    r.ok = writeText(path, s.str(), r.error);
    return r;
}

IoResult loadObj(Mesh2D& m, const std::string& path) {
    std::ifstream f(path);
    if (!f) return {false, "Impossible d'ouvrir le fichier : " + path};
    Mesh2D out;
    std::string line;
    int lineNo = 0;
    while (std::getline(f, line)) {
        ++lineNo;
        std::istringstream ls(line);
        std::string kw;
        ls >> kw;
        if (kw == "v") {
            float x = 0.0f, y = 0.0f;
            if (!(ls >> x >> y)) continue;  // z et w facultatifs, ignorés (maillage 2D)
            out.addVertex({x, y});
        } else if (kw == "f") {
            std::vector<int> loop;
            std::string tok;
            while (ls >> tok) {
                // Formes acceptées : a | a/b | a//b | a/b/c — on ne garde que
                // l'indice de sommet (les indices vt/vn sont ignorés).
                int idx = 0;
                if (std::sscanf(tok.c_str(), "%d", &idx) != 1 || idx == 0) continue;
                loop.push_back(idx > 0 ? idx - 1 : (int)out.vertices.size() + idx);
            }
            if ((int)loop.size() < 3) continue;
            out.addFace(loop);  // polygones > 3 sommets acceptés ; invalides ignorés
        }
        // vt, vn, usemtl, o, g, s, mtllib… : ignorés.
    }
    if (out.vertices.empty())
        return {false, "Aucun sommet trouvé dans le fichier OBJ (ligne " +
                           std::to_string(lineNo) + ")"};
    m = std::move(out);
    return {true, ""};
}

IoResult exportPlaneSVG(const Mesh2D& m, const std::string& path) {
    if (m.vertices.empty()) return {false, "Le plan actif est vide"};
    Vec2 mn = m.vertices[0], mx = m.vertices[0];
    for (const Vec2& v : m.vertices) {
        mn.x = std::min(mn.x, v.x);
        mn.y = std::min(mn.y, v.y);
        mx.x = std::max(mx.x, v.x);
        mx.y = std::max(mx.y, v.y);
    }
    const float pad = std::max(mx.x - mn.x, mx.y - mn.y) * 0.03f + 0.1f;
    std::ostringstream s;
    s << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    // Le monde a Y vers le haut, le SVG Y vers le bas : on inverse simplement y
    // (aucune transformation, la vue est définie sur la boîte englobante).
    s << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"";
    s << (mn.x - pad) << " " << -(mx.y + pad) << " " << (mx.x - mn.x + 2.0f * pad)
      << " " << (mx.y - mn.y + 2.0f * pad) << "\">\n";
    for (const Face& f : m.faces) {
        if ((int)f.verts.size() < 3) continue;
        s << "<polygon points=\"";
        for (int v : f.verts) {
            const float px = m.vertices[v].x;
            const float py = m.vertices[v].y == 0.0f ? 0.0f : -m.vertices[v].y;
            s << px << "," << py << " ";
        }
        s << "\"";
        if (f.hasColor) {
            s << " fill=\"rgba(" << (int)(f.color.r * 255.0f) << ","
              << (int)(f.color.g * 255.0f) << "," << (int)(f.color.b * 255.0f)
              << "," << f.color.a << ")\"";
        } else {
            s << " fill=\"none\" stroke=\"#c7d2e6\" stroke-width=\"0.03\"";
        }
        s << "/>\n";
    }
    s << "</svg>\n";
    IoResult r;
    r.ok = writeText(path, s.str(), r.error);
    return r;
}

IoResult exportQB64(const Mesh2D& m, const std::string& path) {
    std::vector<int> tris;
    m.triangulated(tris);
    std::ostringstream s;
    s << "' Mesh 2D exporte pour QB64 — " << m.vertices.size() << " sommets, "
      << m.faces.size() << " faces, " << (tris.size() / 3) << " triangles\n";
    s << "MESH2D\n";
    s << m.vertices.size() << "\n";
    for (const Vec2& v : m.vertices) s << v.x << " " << v.y << "\n";
    s << m.faces.size() << "\n";
    for (const Face& f : m.faces) {
        s << f.verts.size();
        for (int i : f.verts) s << " " << i;
        s << "\n";
    }
    s << (tris.size() / 3) << "\n";
    for (size_t i = 0; i < tris.size(); i += 3)
        s << tris[i] << " " << tris[i + 1] << " " << tris[i + 2] << "\n";
    IoResult r;
    r.ok = writeText(path, s.str(), r.error);
    return r;
}

// ---------------------------------------------------------------------------
// Formats de la spec « meshes designer »
// ---------------------------------------------------------------------------
IoResult saveSceneJson(const SceneSnapshot& s, const std::string& pathNoExt) {
    const std::string path = pathNoExt + ".json";
    IoResult r;
    r.ok = writeText(path, dumpJson(sceneToJson(s)) + "\n", r.error);
    return r;
}

IoResult loadSceneJson(SceneSnapshot& s, const std::string& pathNoExt) {
    std::ifstream f(pathNoExt + ".json");
    if (!f) return {false, "Impossible d'ouvrir le fichier : " + pathNoExt + ".json"};
    std::stringstream buf;
    buf << f.rdbuf();
    const std::string text = buf.str();
    JsonParser parser;
    parser.p = text.c_str();
    JVal root;
    if (!parser.parse(root)) return {false, "JSON invalide : " + parser.err};
    SceneSnapshot out;
    std::string err;
    if (!sceneFromJson(root, out, err)) return {false, err};
    s = std::move(out);
    return {true, ""};
}

IoResult saveAutoJson(const SceneSnapshot& s, const std::vector<Scene>& undo,
                      const std::vector<Scene>& redo, const std::string& path) {
    JVal v = sceneToJson(s);
    JVal u = JVal::array();
    for (const auto& sc : undo) u.arr.push_back(sceneGeomToJson(sc));
    JVal r = JVal::array();
    for (const auto& sc : redo) r.arr.push_back(sceneGeomToJson(sc));
    v.obj.emplace_back("undo", u);
    v.obj.emplace_back("redo", r);
    IoResult res;
    res.ok = writeText(path, dumpJson(v) + "\n", res.error);
    return res;
}

IoResult loadAutoJson(SceneSnapshot& s, std::vector<Scene>& undo,
                      std::vector<Scene>& redo, const std::string& path) {
    std::ifstream f(path);
    if (!f) return {false, ""};
    std::stringstream buf;
    buf << f.rdbuf();
    const std::string text = buf.str();
    JsonParser parser;
    parser.p = text.c_str();
    JVal root;
    if (!parser.parse(root)) return {false, "JSON invalide : " + parser.err};
    SceneSnapshot out;
    std::string err;
    if (!sceneFromJson(root, out, err)) return {false, err};
    const JVal* ju = root.find("undo");
    if (ju && ju->t == JVal::T::Arr) {
        for (const JVal& sj : ju->arr) {
            Scene sc;
            std::string e2;
            if (sceneGeomFromJson(sj, sc, e2)) undo.push_back(std::move(sc));
        }
    }
    const JVal* jr = root.find("redo");
    if (jr && jr->t == JVal::T::Arr) {
        for (const JVal& sj : jr->arr) {
            Scene sc;
            std::string e2;
            if (sceneGeomFromJson(sj, sc, e2)) redo.push_back(std::move(sc));
        }
    }
    s = std::move(out);
    return {true, ""};
}

IoResult saveMeshesText(const Scene& sc, const std::string& path) {
    // Une ligne par plan : sommets `x,y` séparés par des points-virgules,
    // chaque triplet consécutif formant un triangle (spec 12.1).
    std::ostringstream s;
    for (const Mesh2D& m : sc.planes) {
        std::vector<int> tris;
        m.triangulated(tris);
        std::vector<Vec2> pts;
        std::vector<int> map(m.vertices.size(), -1);
        std::ostringstream line;
        bool first = true;
        auto emit = [&](int idx) {
            if (map[idx] < 0) {
                for (int k = 0; k < (int)pts.size(); ++k)
                    if (pts[k].x == m.vertices[idx].x && pts[k].y == m.vertices[idx].y) {
                        map[idx] = k;
                        break;
                    }
                if (map[idx] < 0) {
                    map[idx] = (int)pts.size();
                    pts.push_back(m.vertices[idx]);
                }
            }
            if (!first) line << ";";
            first = false;
            line << pts[map[idx]].x << "," << pts[map[idx]].y;
        };
        for (size_t i = 0; i + 2 < tris.size(); i += 3) {
            emit(tris[i]);
            emit(tris[i + 1]);
            emit(tris[i + 2]);
        }
        // Sommets orphelins (aucun triangle) : reliquat filtré à l'import.
        for (int i = 0; i < (int)m.vertices.size(); ++i)
            if (map[i] < 0) emit(i);
        line << "\n";
        s << line.str();
    }
    IoResult r;
    r.ok = writeText(path, s.str(), r.error);
    return r;
}

IoResult loadMeshesText(Scene& sc, const std::string& path) {
    std::ifstream f(path);
    if (!f) return {false, "Impossible d'ouvrir le fichier : " + path};
    Scene out;
    std::string line;
    int lineNo = 0;
    while (std::getline(f, line)) {
        ++lineNo;
        if (line.empty()) continue;
        // Une ligne = un plan.
        Mesh2D plan;
        // 1. Collecte des sommets, coordonnées identiques dédupliquées.
        std::vector<Vec2> pts;
        std::istringstream ls(line);
        std::string tok;
        while (std::getline(ls, tok, ';')) {
            if (tok.empty()) continue;
            float x = 0.0f, y = 0.0f;
            int consumed = 0;
            if (std::sscanf(tok.c_str(), "%f,%f%n", &x, &y, &consumed) != 2 ||
                consumed != (int)tok.size()) {
                return {false, "Ligne " + std::to_string(lineNo) +
                                   " : « " + tok + " » n'est pas un couple x,y valide"};
            }
            bool dup = false;
            for (const Vec2& p : pts)
                if (p.x == x && p.y == y) {
                    dup = true;
                    break;
                }
            if (!dup) pts.push_back({x, y});
        }
        // 2. Chaque triplet consécutif forme un triangle ; reliquat filtré.
        for (size_t i = 0; i + 2 < pts.size(); i += 3) {
            const Vec2& a = pts[i];
            const Vec2& b = pts[i + 1];
            const Vec2& c = pts[i + 2];
            if ((a.x == b.x && a.y == b.y) || (a.x == c.x && a.y == c.y) ||
                (b.x == c.x && b.y == c.y))
                continue;  // triangle dégénéré
            const int ia = (int)plan.vertices.size();
            plan.addVertex(a);
            const int ib = (int)plan.vertices.size();
            plan.addVertex(b);
            const int ic = (int)plan.vertices.size();
            plan.addVertex(c);
            plan.addFace({ia, ib, ic});
        }
        out.planes.push_back(std::move(plan));
    }
    if (out.planes.empty()) return {false, "Le fichier ne contient aucun plan"};
    sc = std::move(out);
    return {true, ""};
}

IoResult savePrefsJson(const PrefsData& p, const std::string& path) {
    JVal v = JVal::object();
    JVal pal = JVal::array();
    for (const Color& c : p.palette) {
        JVal pc = JVal::array();
        pc.arr.push_back(JVal::num(c.r));
        pc.arr.push_back(JVal::num(c.g));
        pc.arr.push_back(JVal::num(c.b));
        pal.arr.push_back(pc);
    }
    v.obj.emplace_back("palette", pal);
    v.obj.emplace_back("brushOpacity", JVal::num(p.brushOpacity));
    v.obj.emplace_back("circleSides", JVal::num(p.circleSides));
    v.obj.emplace_back("edgePickTol", JVal::num(p.edgePickTol));
    v.obj.emplace_back("mergeRadius", JVal::num(p.mergeRadius));
    JVal loc = JVal::array();
    for (const auto& s : p.locations) loc.arr.push_back(JVal::str(s));
    v.obj.emplace_back("locations", loc);
    v.obj.emplace_back("importMode", JVal::num(p.importMode));
    v.obj.emplace_back("allColors", JVal::boolean(p.allColors));
    v.obj.emplace_back("snapOn", JVal::boolean(p.snapOn));
    JVal ver = JVal::array();
    for (const auto& s : p.versions) ver.arr.push_back(JVal::str(s));
    v.obj.emplace_back("versions", ver);
    JVal con = JVal::object();
    con.obj.emplace_back("visible", JVal::boolean(p.consoleVisible));
    con.obj.emplace_back("x", JVal::num(p.consoleX));
    con.obj.emplace_back("y", JVal::num(p.consoleY));
    con.obj.emplace_back("w", JVal::num(p.consoleW));
    con.obj.emplace_back("h", JVal::num(p.consoleH));
    v.obj.emplace_back("console", con);
    IoResult r;
    r.ok = writeText(path, dumpJson(v) + "\n", r.error);
    return r;
}

IoResult loadPrefsJson(PrefsData& p, const std::string& path) {
    std::ifstream f(path);
    if (!f) return {false, ""};
    std::stringstream buf;
    buf << f.rdbuf();
    const std::string text = buf.str();
    JsonParser parser;
    parser.p = text.c_str();
    JVal root;
    if (!parser.parse(root)) return {false, "JSON invalide : " + parser.err};
    PrefsData out = p;
    double d = 0.0;
    if (root.getNum("brushOpacity", d)) out.brushOpacity = (float)d;
    if (root.getNum("circleSides", d)) out.circleSides = (int)d;
    if (root.getNum("edgePickTol", d)) out.edgePickTol = (float)d;
    if (root.getNum("mergeRadius", d)) out.mergeRadius = (int)d;
    if (root.getNum("importMode", d)) out.importMode = (int)d;
    bool ac = out.allColors;
    if (root.getBool("allColors", ac)) out.allColors = ac;
    bool sn = out.snapOn;
    if (root.getBool("snapOn", sn)) out.snapOn = sn;
    const JVal* ver = root.find("versions");
    if (ver && ver->t == JVal::T::Arr) {
        out.versions.clear();
        for (const JVal& s : ver->arr)
            if (s.t == JVal::T::Str) out.versions.push_back(s.s);
    }
    const JVal* pal = root.find("palette");
    if (pal && pal->t == JVal::T::Arr && !pal->arr.empty()) {
        out.palette.clear();
        for (const JVal& pc : pal->arr) {
            if (pc.t == JVal::T::Arr && pc.arr.size() >= 3)
                out.palette.push_back({(float)pc.arr[0].n, (float)pc.arr[1].n,
                                       (float)pc.arr[2].n, 1.0f});
        }
        if (out.palette.empty()) out.palette = p.palette;
    }
    const JVal* loc = root.find("locations");
    if (loc && loc->t == JVal::T::Arr) {
        out.locations.clear();
        for (const JVal& s : loc->arr)
            if (s.t == JVal::T::Str) out.locations.push_back(s.s);
    }
    const JVal* con = root.find("console");
    if (con && con->t == JVal::T::Obj) {
        bool vis = out.consoleVisible;
        if (con->getBool("visible", vis)) out.consoleVisible = vis;
        if (con->getNum("x", d)) out.consoleX = (float)d;
        if (con->getNum("y", d)) out.consoleY = (float)d;
        if (con->getNum("w", d)) out.consoleW = (float)d;
        if (con->getNum("h", d)) out.consoleH = (float)d;
    }
    p = std::move(out);
    return {true, ""};
}

}  // namespace mesh
