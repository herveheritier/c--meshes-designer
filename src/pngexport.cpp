#include "pngexport.h"

#include <algorithm>
#include <cstdint>
#include <fstream>

namespace mesh {

namespace {

// CRC-32 (polynôme IEEE 0xEDB88320), utilisé pour les blocs PNG.
uint32_t crc32(const unsigned char* data, size_t len) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) c = table[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

// Adler-32, exigé en fin de flux zlib.
uint32_t adler32(const unsigned char* data, size_t len) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < len; ++i) {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

void put32(std::vector<unsigned char>& out, uint32_t v) {
    out.push_back((unsigned char)(v >> 24));
    out.push_back((unsigned char)(v >> 16));
    out.push_back((unsigned char)(v >> 8));
    out.push_back((unsigned char)v);
}

// Bloc PNG : longueur, type, données, CRC (calculé sur type + données).
void chunk(std::vector<unsigned char>& out, const char type[4],
           const std::vector<unsigned char>& data) {
    put32(out, (uint32_t)data.size());
    const size_t start = out.size();
    for (int i = 0; i < 4; ++i) out.push_back((unsigned char)type[i]);
    out.insert(out.end(), data.begin(), data.end());
    put32(out, crc32(out.data() + start, out.size() - start));
}

// Flux zlib = en-tête + blocs deflate « stockés » (non compressés, autorisés
// par le format) + Adler-32. Évite toute dépendance à zlib.
std::vector<unsigned char> zlibStored(const unsigned char* raw, size_t len) {
    std::vector<unsigned char> z;
    z.push_back(0x78);
    z.push_back(0x01);
    const size_t kBlock = 65535;
    size_t pos = 0;
    do {
        const size_t n = std::min(kBlock, len - pos);
        const bool last = (pos + n == len);
        z.push_back(last ? 0x01u : 0x00u);  // BFINAL + BTYPE=00 (stocké)
        const uint16_t L = (uint16_t)n;
        z.push_back((unsigned char)(L & 0xFF));
        z.push_back((unsigned char)(L >> 8));
        const uint16_t NL = (uint16_t)(~L);
        z.push_back((unsigned char)(NL & 0xFF));
        z.push_back((unsigned char)(NL >> 8));
        for (size_t i = 0; i < n; ++i) z.push_back(raw[pos + i]);
        pos += n;
    } while (pos < len);
    put32(z, adler32(raw, len));
    return z;
}

}  // namespace

bool writePng(const std::string& path, int w, int h, const unsigned char* rgba) {
    if (w <= 0 || h <= 0 || !rgba || path.empty()) return false;

    // Lignes d'image : filtre 0 puis RGB ; renversées (RGBA « bas en haut »).
    std::vector<unsigned char> raw;
    raw.reserve((size_t)h * (1u + (size_t)w * 3u));
    for (int y = h - 1; y >= 0; --y) {
        raw.push_back(0);
        const unsigned char* row = rgba + (size_t)y * (size_t)w * 4u;
        for (int x = 0; x < w; ++x) {
            raw.push_back(row[x * 4u + 0u]);
            raw.push_back(row[x * 4u + 1u]);
            raw.push_back(row[x * 4u + 2u]);
        }
    }
    const std::vector<unsigned char> idat = zlibStored(raw.data(), raw.size());

    std::vector<unsigned char> png;
    const unsigned char sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    png.insert(png.end(), sig, sig + 8);

    std::vector<unsigned char> ihdr;
    put32(ihdr, (uint32_t)w);
    put32(ihdr, (uint32_t)h);
    ihdr.push_back(8);  // profondeur 8 bits
    ihdr.push_back(2);  // type de couleur : RGB (sans alpha)
    ihdr.push_back(0);  // compression
    ihdr.push_back(0);  // filtre
    ihdr.push_back(0);  // entrelacement
    chunk(png, "IHDR", ihdr);
    chunk(png, "IDAT", idat);
    chunk(png, "IEND", {});

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write((const char*)png.data(), (std::streamsize)png.size());
    return (bool)f;
}

}  // namespace mesh
