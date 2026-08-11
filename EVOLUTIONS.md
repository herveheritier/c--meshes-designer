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

## À réaliser

### Manquantes par rapport à `FONCTIONNALITES.md`

> Évolutions décrites dans la spécification mais pas encore implémentées.
> Les références (§) renvoient aux chapitres de `FONCTIONNALITES.md`.

- [ ] **Aide prospective au survol des entités** (13) : le toast décrit le geste possible
  au survol d'un sommet (« sélectionner ce sommet — clic droit pour le déplacer ») et
  d'une arête de triangle (« Clic gauche pour créer un nouveau triangle à partir de ce
  segment ») — actuellement le toast ne se déclenche que sur les actions et au kiosque.

### Idées d'évolutions supplémentaires (hors spec)

> Exemples à compléter, ajuster et prioriser au fil du temps.

- [ ] **Tout afficher** : ajuster automatiquement le zoom pour voir la scène entière en un raccourci.
- [ ] **Dupliquer la sélection** (Ctrl+D) : crée une copie des éléments sélectionnés, légèrement décalée, prête à être déplacée.
- [ ] **Rotation précise** : saisir un angle exact dans une fenêtre de dialogue pour faire pivoter la sélection.
- [ ] **Exporter en image** : générer un fichier image (PNG) de la scène depuis le mode prévisualisation.
- [ ] **Recherche dans la console** : filtrer les messages par mot-clé.
- [ ] **Raccourcis des formes** : touche dédiée pour chaque forme prédéfinie (cercle, rectangle, étoile, anneau…).
- [ ] **Image inversée dans kiosque** : les plans sont dessinées à tort avec inversion du haut et du bas ; corriger.
- [ ] **Détection** : mémoriser entre 2 sessions les distances de détection qui sont paramétrées par l'utilisateur
- [ ] **Inverser la sélection** : bouton et raccourci créer ; ce bouton et le bouton tout sélectionner sont des boutons qui doivent apparaitre au clic droit sur un bouton dédié à la sélection ; comportement similaire à un menu contextuel
---

## Effectuées

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
