# Éditeur de Meshes 2D

Éditeur de **maillages 2D** (triangles) rapide et cross-platform — **Windows / Linux / Mac (WLM)**.

- **C++17** · **SDL2** · **OpenGL 3.x** (core 3.3, avec repli automatique sur compatibilité 3.1,
  ex. Raspberry Pi) · **Dear ImGui**
- Aucune dépendance externe au-delà de SDL2 et d'une carte graphique supportant OpenGL.

Ce projet suit la spécification fonctionnelle **`FONCTIONNALITES.md`** (éditeur « meshes
designer ») : les chapitres de la spec décrivent le comportement attendu de l'application.
Les évolutions prévues et réalisées (avec dates et identifiants de commit) sont consignées
dans **`EVOLUTIONS.md`**.

## Fonctionnalités

| Domaine | Détail |
|---|---|
| **Construction** | Clic gauche sur une zone vide : 1er point, 2e point (segment), 3e clic ferme le triangle. Un clic **près d'un segment** crée directement un triangle complet accroché à ce segment. Aimantation sur la grille. |
| **Formes prédéfinies** | Cercle, rectangle, carré, triangle, pentagone, hexagone (2 clics), étoile et anneau (3 clics). La molette règle le nombre de côtés (cercle/anneau, mémorisé). Touche `C` : mode cercle. |
| **Sélection** | Cibles sommet / segment / triangle (bouton dédié). Clic, Maj+clic (bascule), Ctrl+clic droit (ajouter), clic droit (entité la plus proche), rectangle de sélection au clic gauche+glisser. |
| **Manipulation** | Clic droit+glisser : déplacer la sélection · Molette avec ≥ 2 points sélectionnés : rotation de 5°/cran autour du curseur · AltGr+molette : rotation de tous les plans · AltGr+clic droit+glisser : déplacer tous les plans ensemble · Aligner X/Y et Répartir X/Y (panneau) · Tout sélectionner. |
| **Presse-papiers** | Copier / couper / coller interne (Ctrl+C/X/V) : points + triangles entièrement contenus ; chaque collage décale d'un demi-pas de grille. |
| **Peinture** | Palette personnalisable (8 défauts, ajout / suppression / modification / « Défauts »), pinceau, opacité (défaut 45 %). La couleur survit aux suppressions et aux annulations. |
| **Grille & réticule** | Grille affichable (G), pas ajustable à la molette (clic milieu : défaut). Réticule 3 états (R). Zoom ×1,1 par cran, borné [0,1× ; 10×], Ctrl+0 recentre. |
| **Plans (multi-plans)** | Scène composée de **plans empilés** (navigation ◀▶, compteur i/N, monter/descendre Alt+↑/↓, ajout « + » avant/après, suppression « × » avec confirmation — tout annulable). Plans inactifs estompés en pointillés. Mode « toutes couleurs » (AC), mode **kiosque** (Alt+K) de sélection en couverture. |
| **Prévisualisation** | Mode œil (P) : aperçu simple → tous les plans → édition ; sortie par Échap, clic gauche ou Ctrl+S. |
| **Annuler / Rétablir** | Historique de **50 entrées** (Ctrl+Z, Ctrl+Maj+Z / Ctrl+Y), compteurs, conservé entre deux sessions. |
| **Sauvegarde** | Fenêtre d'enregistrement (Ctrl+S) avec liste des emplacements (20 max), renommage, fichier `<nom>.json`. Persistance automatique de la scène, de la vue et des préférences. |
| **Import / Export** | « meshes » (texte : **une ligne = un plan**, triplets de sommets, coordonnées dédupliquées) et JSON complet multi-plans (aller-retour exact) ; modes Remplacer / Fusionner mémorisés ; glisser-déposer d'un fichier JSON. |
| **Interface** | Barre d'outils flottante groupée avec pilules vertes (fps, plan i/N, historique, sélection), HUD bas-gauche (zoom, position, angle de rotation cumulé `rot X°`, redessins, état de la scène — en kiosque : redessins + angle), messages contextuels (toast), console d'événements horodatée, fenêtre d'aide (`?`). |

## Compilation

### Linux
```bash
sudo apt install build-essential cmake libsdl2-dev   # Debian/Ubuntu
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/mesheditor
```

### Windows (MSVC)
1. Installer [SDL2](https://libsdl.org) (développement) et définir `SDL2_DIR` sur le chemin CMake.
2. `cmake -S . -B build` puis ouvrir `build/MeshEditor2D.sln` dans Visual Studio.

### macOS
```bash
brew install sdl2 cmake
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/mesheditor
```

### Tests (sans GUI)
```bash
cmake --build build --target meshtest
./build/meshtest
```

## Raccourcis

| Touche | Action |
|---|---|
| `Retour arrière` | Supprimer l'élément sélectionné (selon la cible) |
| `Maj+Retour arrière` | Réinitialiser complètement la scène (confirmation) |
| `Ctrl+Z` / `Ctrl+Maj+Z` ou `Ctrl+Y` | Annuler / rétablir |
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Copier / couper / coller |
| `Ctrl+S` | Enregistrer (fenêtre d'emplacement) |
| `Ctrl+0` | Zoom 100 % recentré sur l'origine |
| `G` / `R` / `F` / `P` | Grille / réticule / compteur de redessins / prévisualiser |
| `C` | Mode cercle |
| `Alt+←` / `Alt+→` | Aligner X / Y · `Alt+Maj+←/→` : répartir X / Y |
| `Alt+↑` / `Alt+↓` | Monter / descendre le plan actif dans l'empilement |
| `Alt+K` | Kiosque : choisir le plan en couverture (≥ 2 plans) |
| `Échap` | Quitter le mode en cours (construction, kiosque, panneau) ou fermer une fenêtre |
| `?` | Aide et raccourcis |

Souris : clic gauche (vide) = poser un point (3 clics = un triangle) · clic gauche (entité) =
sélectionner · Maj+clic = basculer · clic gauche+glisser = rectangle de sélection ·
clic droit = entité la plus proche · clic droit+glisser = déplacer · molette = zoom ou
rotation (≥ 2 points) · AltGr+molette = rotation de tous les plans (5°/cran) ·
AltGr+clic droit+glisser = déplacer tous les plans ensemble · clic du milieu+glisser =
déplacer la vue.

## Formats de fichiers

### JSON (`*.json`)
Format complet de la scène : **liste de plans** (sommets, faces avec couleurs), plan actif,
vue (zoom, centre), grille, nom. Aller-retour **exact** (enregistrement = chargement sans
perte). L'ancien format mono-maillage (`mesh`) reste lisible. Le fichier `autosave.json`
(répertoire de préférences) contient en plus les historiques annuler/rétablir (scènes complètes).

### « meshes » (`*.meshes`)
Texte simple : une ligne par plan ; sommets `x,y` séparés par des points-virgules ;
chaque **triplet consécutif forme un triangle** ; coordonnées identiques dédupliquées ;
un reliquat de 1-2 sommets en fin de ligne est filtré à l'import.

## Architecture

```
src/
  mesh.h/.cpp        modèle : sommets + faces (avec couleur), scène multi-plans, opérations topologiques
  triangulate.h/.cpp triangulation ear clipping + tests géométriques
  camera.h/.cpp      caméra 2D (pan / zoom ×0,1–×10 / conversion écran↔monde)
  renderer.h/.cpp    rendu OpenGL (shaders, lignes, triangles, points)
  io.h/.cpp          JSON (scène, préférences, autosave) + « meshes » + formats hérités
  app.h/.cpp         état global, sélection, outils, formes, presse-papiers, undo
  ui.h/.cpp          interface imgui (barre d'outils, HUD, panneaux, dialogues, console)
  main.cpp           SDL + imgui + boucle principale + glisser-déposer
external/imgui/      dear imgui (vendored)
```
