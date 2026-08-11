#pragma once
#include "app.h"

namespace mesh::ui {

// Construit toute l'interface de la frame courante (après ImGui::NewFrame()).
// Remplit le viewport (position/taille), met à jour l'application, dessine la
// scène GL, puis soumet le HUD. Appelle ImGui::EndFrame() implicitement ?
// Non : le rendu final est géré par main.cpp. Cette fonction ne fait que la
// soumission des fenêtres et le rendu GL du viewport.
void frame(App& app);

// Vrai si l'utilisateur a demandé de quitter (fermeture confirmée).
bool quitRequested();

// Demande de fermeture (bouton de fenêtre, Alt+F4…) : quitte immédiatement si
// la scène est propre ; sinon ouvre la confirmation « Quitter sans enregistrer ? ».
void requestQuit(App& app);

}  // namespace mesh::ui
