// Décodeur d'images stb_image (PNG/JPEG) — implémentation unique.
// Utilisé par l'application (calque d'image de fond, 7.7) ; les tests
// headless (meshtest) n'en dépendent pas.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "stb_image.h"
