# Évolutions

Ce fichier est le **journal des évolutions** de l'application. Il contient deux listes :

- **À réaliser** : les évolutions prévues, pas encore livrées ;
- **Effectuées** : les évolutions livrées, avec leur **date** et l'**identifiant du commit** correspondant.

## Consignes d'utilisation

- **Une évolution = un item**, formulé de façon concise.
- **À réaliser** : décrire le comportement attendu du point de vue de l'utilisateur
  (non technique), puis classer par ordre de priorité (les plus importantes en haut).
  Dans cette liste, les évolutions **cochées** (`[x]`) sont celles à réaliser **de
  suite** ; les évolutions non cochées (`[ ]`) restent en attente.
- **Effectuées** : quand une évolution est livrée, la déplacer de « À réaliser » vers
  « Effectuées » en la préfixant de la **date** (format `AAAA-MM-JJ`) et de
  l'**identifiant court du commit** qui l'a livrée.
- Pour les évolutions effectuées, donner une description non technique et concise.
- Une évolution trop large doit être scindée en plusieurs items plus petits et
  livrables séparément.

---

## A réaliser

_(aucune évolution en attente — toutes sont livrées)_

## Effectuées

- 2026-08-11 · `476e4c3` — Plans : nommage des plans (spec 2.2) — bouton
  « Renommer » dans la barre d'outils (dialogue, Entrée valide, champ vidé =
  retour au nom par défaut « Plan n ») ; le nom est conservé dans le fichier
  JSON et affiché dans le kiosque (carte et toast), au HUD, dans la barre de
  statut et dans la confirmation de suppression.
- 2026-08-11 · `505a90e` — Interface : confirmation à la fermeture de la
  fenêtre quand la scène contient des modifications non enregistrées
  (dialogue « Quitter sans enregistrer ? » : Enregistrer puis quitter /
  Quitter quand même / Annuler — la sortie reste immédiate quand la scène est
  propre, et la demande est ignorée si un autre dialogue est déjà ouvert).
- 2026-08-11 · `09eb175` — Interface : le choix des formes s'active en tant que
  menu contextuel (popup sur le bouton de la barre d'outils, molette pour les
  côtés/pointes) au lieu d'une fenêtre flottante, et les choix contextuels (tout
  sélectionner / inverser la sélection) s'affichent sous forme de boutons larges.
- 2026-08-11 · `30561c4` — 12 évolutions : côtés de l'étoile réglables à la molette
  (bouton et canvas, compteur « pointes »), import de fichiers OBJ (remplacer ou
  fusionner, glisser-déposer), miroir X/Y de la sélection autour du 1er point choisi
  (M / Maj+M, panneau Aligner), mise à l'échelle précise par saisie d'un facteur
  (Alt+S, pivot = centre), cadrage de la sélection (Ctrl+F, zoom automatique),
  aimantation sur la grille indépendante de son affichage (Maj+G, préférences),
  export du plan actif en SVG vectoriel (bouton + dialogue), versions horodatées de
  l'autosave (10 conservées, dialogue de restauration annulable), statistiques du
  plan dans le HUD (sommets, triangles, aire), outil mesure de distance entre deux
  points (Ctrl+M, résultat au HUD), duplication du plan actif (Alt+D), et champs
  dynamiques de la barre d'outils à largeur fixe (plus d'à-coups de mise en page).
- 2026-08-11 · `57ec966` — Idées d'évolutions : tout afficher (Accueil : zoom
  automatique sur la scène entière), dupliquer la sélection (Ctrl+D : copie légèrement
  décalée, prête à déplacer), rotation précise (Alt+R : saisie d'un angle, pivot = centre
  de la sélection), export de la vue en image PNG (bouton dans la prévisualisation),
  recherche dans la console (filtre par mot-clé), raccourcis des formes (C cercle,
  R rectangle, T triangle, Q carré, N pentagone, H hexagone, É étoile, A anneau ; le
  réticule passe de R à Y), inversion de la sélection (Ctrl+I ; le bouton « tout
  sélectionner » devient un bouton de sélection dont le clic droit ouvre un menu : tout
  sélectionner / inverser), mémorisation entre sessions du rayon de fusion par
  déplacement, et correction de l'image inversée des plans dans le kiosque.
- 2026-08-11 · `4b7f52a` — Aide prospective au survol des entités (13) : au survol d'un
  sommet, le toast indique « sélectionner ce sommet — clic droit pour le déplacer » ; sur
  une arête de triangle, « Clic gauche pour créer un nouveau triangle à partir de ce
  segment ». L'aide couvre aussi les modes segment et triangle, le pinceau armé et la
  zone vide, et guide la phase en cours pendant une construction (forme prédéfinie ou
  triangle).
- 2026-08-11 · `2469d45` — Molette sur le bouton de la forme active (4.2) : le nombre
  de côtés du cercle et de l'anneau se règle aussi à la molette sur le bouton de la
  forme (plus seulement sur le canvas), avec le compteur de côtés affiché à côté du
  bouton.
- 2026-08-11 · `2469d45` — Aperçu du tracé des formes (4.2) : le contour de la forme
  prévisualisée ne se refermait pas complètement autour de la zone remplie
  (seule une arête sur deux était tracée) ; il suit désormais intégralement le
  pourtour de la zone grise pour toutes les formes (rectangle, carré, triangle,
  cercle, pentagone, hexagone, étoile et anneau).
- 2026-08-11 · `28afde3` — Fusion des points superposés (5.5) : anneau orange à chaque
  position où plusieurs points coïncident, clic sur l'anneau qui sélectionne tous les
  points superposés, bouton « Fusionner » qui les regroupe à la position moyenne. Fusion
  par déplacement (5.6) : avec un point sélectionné, le bouton arme un mode (vert, rayon
  affiché) où relâcher le point près d'un autre les fusionne ; rayon réglable à la molette
  (8 à 64 px, défaut 20), 2e clic qui verrouille (cadenas) pour enchaîner, 3e clic qui
  désarme.
- 2026-08-11 · `e2976ce` — Rotation de tous les plans autour du curseur (AltGr + molette,
  5° par cran, angle cumulé `rot X°` remis à zéro par Ctrl+0) et déplacement de tous les
  plans d'un même décalage (AltGr + clic droit + glisser), sans bouger la vue.
- 2026-08-11 · `6d3cd14` — Création de l'éditeur de maillages 2D : dessin de points et
  de triangles, formes prédéfinies, sélection et manipulation, peinture, multi-plans,
  annuler/rétablir, sauvegarde et import/export.
