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

- [ ] ajouter la possibilité de tracer un polygone et le transformer automatiquement en un assemblage de triangles à la validation
- [ ] améliorer gestion de la boite à outils ; les paquets de boutons doivent pouvoir être ouverts ou fermés chaque paquet est générer par un bouton dédié

## Effectuées

- **Poignées de redimensionnement du calque** (17e7cac) — l'outil Échelle du
  calque affiche 8 poignées au canvas : milieux des arêtes gauche/droite =
  axe X (largeur), haut/bas = axe Y (hauteur), coins = les deux axes ; la
  saisie d'une poignée redimensionne en gardant fixe l'arête / le coin opposé
  (rotation conservée), les gestes libres restant disponibles.
- **Transparence du calque d'image** (151b672) — le bouton Calque gagne la
  molette pour ajuster l'opacité par pas de 5 % (une étape annulable par
  salve), et le curseur du popup est exprimé de 0 à 100 % avec un incrément
  de 1.
- **Transparence de la palette** (151b672) — le curseur d'opacité du pinceau
  est exprimé de 0 à 100 avec un incrément de 1 (défaut 45).

- 2026-08-12 · `2e006b2` — Pipette de couleur (6.5) : bouton « Pipette » du
  paquet Outils — armé, le curseur devient une petite cible et un clic gauche
  sur le canvas prélève la couleur réellement affichée (faces peintes, calque
  d'image, couleur du fond…) puis la pose comme couleur du pinceau (valeurs
  RGB + hexa affichées, désarmement automatique après prélèvement, clic droit
  ou Échap pour désarmer). L'échantillonnage lit le framebuffer juste après le
  dessin de la scène (avant l'interface) avec l'échelle HiDPI ; lecture pixel
  unique ajoutée au renderer. Icône pipette.svg, aide mise à jour. Portée : le
  canvas de l'application (les zones hors fenêtre ne sont pas lues).
- 2026-08-12 · `5def6bb` — Réticule miroir (9.2) : 4e état du réticule (Y) —
  croix de visée au curseur et à ses trois reflets à travers les axes du monde
  (axe X, axe Y, symétrie centrale) : pendant un tracé ou un déplacement, on
  voit en direct où tomberaient les points symétriques de la position courante
  — un guide pour dessiner des formes symétriques. Cycle off / simple /
  symétrique / miroir, statut et aide mis à jour.
- 2026-08-12 · `bd6668b` — Opacité par plan (7.8) : chaque plan a sa propre
  transparence, indépendante de celle des faces — le bouton « Opacité » du
  groupe Plans ouvre un curseur pour le plan actif, la molette sur le bouton
  ajuste par pas de 5 % (une salve = une étape annulable). L'opacité multiplie
  le remplissage des faces (rendu d'édition et prévisualisation « tous les
  plans ») ; arêtes et points restent affichés, même à 0 % (plan « fantôme »).
  Conservée dans le JSON de scène (champ « opacity » émis seulement si ≠ 1,
  compatibilité ascendante), annulable. Icône opacity.svg, aide mise à jour,
  tests meshtest (round-trip + opacité absente = 1.0).
- 2026-08-12 · `64e93f8` — Sélection au lasso (5.10) : bouton « Lasso » du paquet
  Sélection — tracer librement un contour au canvas (polygone affiché en
  direct, échantillonné tous les ~6 px écran) puis sélectionner d'un coup les
  sommets (position), segments (milieu) ou triangles (centre) du plan actif
  contenus dans le contour ; Maj au relâchement ajoute à la sélection au lieu
  de remplacer. Le mode armé monopolise le canvas (clic droit ou Échap
  désarme, désarme aussi les autres modes) ; le tracé ne déplace jamais la
  géométrie. Icône lasso.svg, aide et infobulles mises à jour.
- 2026-08-12 · `c4e4d52` — Image de fond en calque (7.7) : bouton « Calque » du
  groupe Scène — charger une image PNG/JPEG (stb_image) affichée derrière la
  grille et les plans, opacité et visibilité réglables, manipulation à la
  souris au canvas (déplacer, pivoter autour du centre, échelle — vertical
  proportionnel / horizontal largeur / Maj hauteur), ajuster à la vue, retirer
  — chaque geste annulable (l'état du calque vit dans la scène). L'image est
  enregistrée avec la scène (JSON, chemin + transformée, compatibilité
  ascendante) et apparaît dans l'export PNG. Rendu texturé OpenGL dédié
  (quad + teinte), décodeur stb_image embarqué (external/stb_image.h),
  icône layer.svg, tests meshtest (round-trip scène + autosave).
- 2026-08-12 · `8400bc2` — Mode filaire (7.6) : le bouton d'affichage des plans fait
  désormais défiler trois modes — rendu normal, toutes couleurs, puis filaire
  (arêtes seules, sans remplissage, pour voir la structure de toute la scène à
  travers les plans superposés). L'icône et l'infobulle du bouton suivent le
  mode courant, qui est conservé d'une session à l'autre.
- 2026-08-12 · `8d12b9f` — Kiosque : la forme de chaque plan est agrandie au maximum dans
  sa carte sans jamais déborder (~80 % de la surface pour les proportions
  proches de la carte, un peu moins pour les formes « rondes ») — la taille
  rendue ne dépend plus de la taille de la forme dans le monde (l'ancien
  calcul divisait deux fois par l'envergure : une forme 10× plus grande
  rendait 10× plus petite).
- 2026-08-12 · `8d12b9f` — Ordre z des faces : une face (ou une sélection de faces, cible
  « triangle ») se déplace d'un cran vers l'avant ou l'arrière dans l'ordre de
  dessin du plan — boutons avant / arrière du paquet « Ordre z » de la barre
  d'outils, raccourcis ] / [. Une face avancée est dessinée au-dessus de celles
  qui la recouvraient (et se sélectionne en premier) ; une sélection de
  plusieurs faces garde son ordre relatif et pousse devant / derrière elle les
  faces non sélectionnées ; aux bornes, rien ne change. Chaque déplacement est
  une étape annulable.
- 2026-08-12 · `8d12b9f` — Peinture : avec des triangles sélectionnés (cible
  « triangle »), un seul clic du pinceau peint tous les triangles sélectionnés
  d'un coup ; sans sélection, seul le triangle cliqué est peint (comportement
  historique conservé).
- 2026-08-12 · `8d12b9f` — Mode Scène (8.5) : un paquet de boutons agit sur
  toute la scène — saisir (déplacer tous les plans d'un même décalage),
  rotation (autour du point de saisie), mise à l'échelle, couleur du fond du
  canvas et réinitialisation. Chaque action se pilote à la souris (clic
  gauche + glisser au canvas) avec un repère visuel (cercle de pivot, badge en
  direct) ; un geste = une étape annulable, un clic sans glisser ne crée rien,
  clic droit ou Échap désarme. Armer un outil Scène désarme les modes
  transitoires (pinceau, mesure, fusion, tracés).
- 2026-08-12 · `8d12b9f` — Préférences : la couleur du fond du canvas et
  l'outil du mode Scène armé sont persistés dans prefs.json (compatibilité
  ascendante : un fichier sans ces champs garde les valeurs par défaut) ; au
  passage, le parseur JSON accepte désormais les espaces après une virgule
  (bug latent corrigé).

- 2026-08-12 · `0244129` — Découpe d'un plan par un polygone (outil découpe,
  raccourci D) : on trace à main levée un polygone qui chevauche des faces déjà
  dessinées et la zone est soustraite, en conservant intactes les faces
  restantes autour du trou (découpage des faces par la frontière, polygones à
  trous et entailles triangulés proprement). Clics gauches : sommets (re-clic
  près du 1er point : fermer et découper) · clic droit ou Entrée : découper ·
  Retour arrière : retirer le dernier point · Échap : annuler. La découpe se
  calcule sur une copie (annulable) et ne modifie le plan que si elle touche
  des faces ; aperçu du tracé au canvas.
- 2026-08-11 · `1709f7d` — Forme « couronne » : un anneau dont le nombre de
  côtés extérieurs et intérieurs est indépendant (tracé en 3 clics : centre,
  rayon, trou). Après le 2e clic, l'angle du curseur oriente la forme
  intérieure ; la molette règle les côtés selon la phase du tracé (extérieurs
  tant que le rayon n'est pas verrouillé, intérieurs ensuite, Maj+molette pour
  l'autre jeu), avec un badge « ext. / int. » affiché en direct au canvas.
  Raccourci O, compteurs mémorisés entre sessions, triangulation de la bande
  sans croisement ni chevauchement.
- 2026-08-11 · `8aed28a` — Interface : barre d'outils réorganisée en paquets
  cohérents (canevas, affichage, vue, sélection, presse-papiers, outils,
  fusion, annuler/rétablir, sauvegarde, entrées, plans, scène, interface) qui
  se replient automatiquement à la largeur de la fenêtre — aucun bouton n'est
  jamais masqué, quelle que soit la taille de la fenêtre.
- 2026-08-11 · correctif du repli : la largeur de ligne se mesure avec une
  comptabilité propre (GetCursorPosX() renvoie toujours le début de ligne
  après un item ImGui, le repli ne se déclenchait donc jamais) ; largeur de
  la barre forcée à celle de la fenêtre (remplit toute la largeur) ; les
  paquets passent TOUJOURS en entier à la ligne suivante (jamais coupés au
  milieu) ; lignes serrées (plus de NewLine() en trop : ~20 px d'espace mort
  supprimés entre les lignes) ; 2 lignes à 1560 px, 4 lignes à 700 px, tout
  bouton toujours visible.
- 2026-08-11 · `e8e0e03` — Correctifs : en prévisualisation, plus aucun
  raccourci ne modifie la géométrie (seuls zoom, cadrage et sortie restent
  actifs, spec 9.3) ; la duplication d'un plan reprend le nom par défaut ;
  icône dédiée (image) pour l'export PNG, distincte de la sauvegarde ; le
  pré-remplissage du dialogue d'enregistrement est factorisé.
- 2026-08-11 · `99558d9` — Interface : bouton d'export d'image PNG dans la
  barre d'outils (groupe Sauvegarde) — la vue actuelle s'exporte en image
  depuis l'édition comme depuis la prévisualisation (sans l'interface).
- 2026-08-11 · `58f302f` — Kiosque : navigation au clavier avec les flèches
  gauche/droite (le pointeur reste la méthode principale) ; le toast et le
  message d'activation le rappellent.
- 2026-08-11 · `61f8810` — Sélection : déplacement au clavier avec les
  flèches — 1 pas de grille par pression (Maj : ×5), une salve de flèches
  rapprochées = une seule étape annulable ; la navigation clavier d'ImGui est
  désactivée pour que les flèches restent déterministes.
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
