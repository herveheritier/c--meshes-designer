#pragma once

#include <string>
#include <vector>

namespace mesh {

// ---------------------------------------------------------------------------
// Export d'une image PNG (RGB 8 bits) — utilisé pour la prévisualisation
// (évolution « Exporter en image »).
// ---------------------------------------------------------------------------

// Écrit une image PNG depuis des pixels RGBA (w × h × 4 octets). Les lignes
// sont fournies « bas en haut » (comme glReadPixels) : l'écriture les renverse
// pour produire une image à l'endroit. Compression par blocs « stockés » du
// format deflate (valide, sans dépendance zlib). Retourne faux en cas d'échec
// (chemin invalide, dimensions nulles).
bool writePng(const std::string& path, int w, int h, const unsigned char* rgba);

}  // namespace mesh
