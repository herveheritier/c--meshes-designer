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

- **Clic-droit boutons** les choix contextuels doivent apparaître sous la forme de boutons
- **Choix des formes** le choix des formes doit s'activer en tant que menu contextuel plutot que sous-fenetre
- **Etoile variable** pour la forme étoile choisir comme pour le cercle et l'anneau le nombre de cotés
- **Importer un fichier OBJ** : charger les triangles d'un fichier `.obj` (l'export
  existe déjà), via le même dialogue d'import que JSON/meshes (remplacer ou fusionner).
- **Miroir de la sélection** : symétrie X ou Y de la sélection par rapport au premier
  point sélectionné (comme Aligner/Répartir), avec un raccourci dédié.
- **Mettre à l'échelle la sélection** : agrandir ou réduire la sélection par saisie
  d'un facteur, dans un dialogue comme la rotation précise (Alt+R).
- **Cadrer la sélection** : zoom automatique sur la sélection courante (complément de
  « Tout afficher »), avec un raccourci dédié.
- **Aimantation indépendante de la grille** : pouvoir aimanter sur la grille sans
  l'afficher, ou afficher la grille sans aimanter.
- **Exporter un plan en SVG** : générer un fichier SVG vectoriel du plan actif
  (contours des triangles).
- **Sauvegardes horodatées** : conserver un historique des versions successives de
  l'autosave pour pouvoir revenir en arrière.
- **Statistiques du plan dans le HUD** : afficher le nombre de sommets, de triangles
  et l'aire totale du plan actif.
- **Outil mesure** : mode dédié pour mesurer la distance entre deux points, résultat
  affiché dans le HUD.
- **Dupliquer le plan actif** : copie complète du plan courant avec ses couleurs,
  insérée juste au-dessus.
- **Champs dynamiques de la barre d'outils à largeur fixe** : donner aux champs
  dynamiques (zoom, angle de rotation, nombre de côtés, compteurs…) une taille fixe
  indépendante du contenu, pour éviter les à-coups de mise en page quand les valeurs
  changent.

## Effectuées

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
