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
  l'**identifiant court du commit** qui l'a livrée, sous la forme uniforme
  `- date (id commit) - description` (ex. `- 2026-08-12 (17e7cac) - …`).
- Pour les évolutions effectuées, donner une description non technique et concise.
- Une évolution trop large doit être scindée en plusieurs items plus petits et
  livrables séparément.

---

## A réaliser

## Effectuées

- 2026-08-15 (1c52185) - Opérations ensemblistes (5.12) étendues aux cibles
  **sommet** et **segment** : « Mémoriser A / B » capture les **faces formées
  par la sélection courante** — cible triangle : les faces sélectionnées ;
  cible **sommet** : les faces dont **tous les sommets** sont sélectionnés (les
  4 coins d'un rectangle forment ses triangles) ; cible **segment** : les faces
  dont **toutes les arêtes** sont sélectionnées (le pourtour d'un rectangle
  triangulé doit inclure sa diagonale interne). Une sélection qui ne forme
  aucune face ne mémorise rien (message d'aide). L'opération reste annulable et
  le résultat **reste sélectionné dans la cible active** (triangle : ses faces,
  sommet : ses sommets, segment : ses arêtes — pas de bascule forcée en cible
  triangle) ; les sommets orphelins des faces remplacées sont retirés.
- 2026-08-13 (6f03df3) - Clic cyclique dans la pile de faces superposées
  (cible « triangle ») : quand plusieurs faces se chevauchent au même endroit,
  chaque clic au même endroit sélectionne la face **suivante en dessous**
  (retour en haut après la plus basse), un clic ailleurs repart de la face du
  dessus, Maj ajoute/retire la face choisie — le survol signale l'empilement et
  la barre d'état donne la position dans la pile. Fini l'impossibilité de
  sélectionner les triangles cachés d'un ensemble pour les opérations
  ensemblistes (5.12) : les deux ensembles peuvent se chevaucher entièrement.
- 2026-08-13 (6f03df3) - Opérations ensemblistes entre **deux ensembles de
  triangles** : les boutons « Mémoriser A » / « Mémoriser B » du paquet
  Sélection capturent chacun la sélection courante (cible triangle), puis
  quatre opérations booléennes géométriques combinent les deux ensembles —
  **union (A ∪ B)**, **intersection (A ∩ B)**, **différence (A − B)** et
  **différence symétrique (A △ B)**. Chaque ensemble est traité comme une
  **région polygonale** et les régions sont combinées **par leur frontière**
  (arêtes de frontière découpées aux intersections, classification de part et
  d'autre, trous gérés), puis le résultat est **triangulé une seule fois à la
  fin** : la triangulation est **minimale** — pas de coutures internes ni de
  fragments le long des diagonales internes des ensembles (l'ancien découpage
  en cellules sur-triangulait et laissait dans une différence A−B les faces de
  B entièrement recouvertes par A). Les chevauchements partiels sont gérés, la
  couleur de chaque triangle du résultat est reprise de la face source qu'il
  recouvre et, dans la zone des deux ensembles, **seule la géométrie du
  résultat est conservée** (les restes sont retirés du plan, le reste du plan
  est intact) ; le résultat devient la sélection triangle du plan actif. Un
  ensemble est oublié si le plan actif ou la géométrie change (annuler /
  rétablir inclus) ; l'opération est annulable.
- 2026-08-13 (6f03df3) - Échelles X/Y de l'anneau : **rétrécissement généreux**
  piloté par la distance du curseur au **centre** de l'anneau — le module
  décroît proportionnellement en glissant vers l'intérieur (quasi nul au
  centre, seuil au centre et non au point de saisie), sans saut à la saisie ;
  l'agrandissement vers l'extérieur garde la réponse exponentielle en pixels.
- 2026-08-13 (6f03df3) - L'anneau de manipulation (maillages et calque)
  **suit la cible pendant les déplacements** : en glissant le centre ou les
  flèches, l'ancre se déplace d'autant et les poignées restent autour de la
  cible (auparavant l'anneau restait en place et la cible s'éloignait). Le
  pivot des rotations / échelles suit donc le déplacement.
- 2026-08-13 (6f03df3) - Échelle de l'anneau de manipulation plus efficace :
  le facteur d'échelle (X, Y, uniforme) est désormais proportionnel aux
  **pixels glissés** (décalage monde × zoom) au lieu des unités monde — la
  réponse est identique quel que soit le zoom (auparavant faible zoomé,
  excessive dézoomé) et le coefficient passe de ×exp(0,005/px) à ×exp(0,01/px)
  (~×2,7 pour 100 px). L'échelle **uniforme** utilise une distance radiale
  signée le long de la direction d'ouverture du point de saisie : glisser vers
  l'intérieur **rétrécit** (la distance seule, toujours positive, ne permettait
  que d'agrandir). Même correction pour les poignées d'échelle du calque
  (7.7), inchangées au zoom 1.
- 2026-08-13 (6f03df3) - La cible de l'anneau de manipulation des maillages
  est choisie par **trois boutons radio** du paquet Scène — un par cible
  (Sélection / Plan / Scène) : la cible active est mise en évidence et
  **désactive les autres**. Clic sur une cible : arme l'anneau pour cette
  cible ; re-clic sur la cible active : désarme. Le bouton unique à libellé
  cyclant et son menu contextuel sont retirés (cycleRingTarget supprimé). Le
  bouton « Plan » utilise l'icône duplicate-plane (l'icône layer, réservée au
  bouton Calque, provoquait un conflit d'ID ImGui dans le même paquet).
- 2026-08-13 (6f03df3) - L'anneau de manipulation des maillages (sélection /
  plan / scène) s'arme **au centre de la cible** (centre de la boîte
  englobante) au lieu de suivre le curseur : dès l'armement, les poignées sont
  prêtes autour de la sélection / du plan / de la scène. Un **clic gauche**
  ailleurs déplace l'anneau (pivot libre) ; si la cible est vide (ex.
  sélection vide), l'anneau suit encore le curseur jusqu'au premier clic.
  Statuts, infobulles et documentation (8.5) mis à jour.
- 2026-08-13 (6f03df3) - Anneau de manipulation unifié étendu aux maillages :
  le même anneau de poignées que le calque (une poignée par action) manipule
  désormais la **sélection** du plan actif, le **plan courant** ou la **scène
  complète** — bouton « Manipuler » du paquet Scène (cible affichée en
  libellé : clic = changer de cible et armer, clic droit = menu + désarmer).
  Déplacement (centre/flèches), rotation (anneau), échelle X/Y/uniforme
  (carreaux/losange) autour du point d'ancrage, symétries instantanées
  (pastilles rouges). Le mode Scène à trois boutons (saisir / pivoter /
  redimensionner) est retiré (8.5 réécrit, préférence sceneTool supprimée) :
  une seule façon de manipuler chaque chose. Chaque geste est annulable, un
  clic sans glisser ne crée rien, clic droit ou Échap désarme.
- 2026-08-13 (6f03df3) - Anneau de manipulation du calque sans redondance :
  **une seule poignée par action** au lieu de poignées répétées (l'échelle X,
  l'échelle Y, l'échelle uniforme et le déplacement X/Y occupaient 2 à 4
  poignées chacune, la rotation était en double). Le cercle de 40 px accueille
  désormais 8 poignées distinctes : échelle X (cyan, E), échelle Y (ambre, N),
  échelle uniforme (losange blanc, NE), déplacement X/Y (flèches vertes, W/S)
  et symétries (pastilles rouges, clic instantané : miroir X/Y/X-Y, SE/NW/SW) ;
  la rotation est la bande annulaire de l'anneau lui-même. Les boutons Miroir
  du popup Calque sont retirés (chaque action n'est présente qu'une seule fois),
  documentation (7.7) mise à jour.
- 2026-08-12 (6f03df3) - Sélection chaînée : un bouton du paquet Sélection
  sélectionne, à partir de la sélection courante, **tous les éléments qui lui
  sont liés par des chaînes d'adjacence** — selon la cible active : triangles
  liés par **au moins un sommet partagé**, segments par un sommet partagé,
  sommets reliés par un segment. Le clic **remplace** la sélection par
  l'ensemble chaîné ; avec **Ctrl ou Maj** enfoncés, il **ajoute** les
  éléments chaînés à la sélection. Sans sélection, rien n'est sélectionné (un
  message l'indique). Icône linked.svg, entrée dans le menu contextuel du
  bouton de sélection et dans l'aide, documentation (5.11) mise à jour.
- 2026-08-12 (5f6c54c) - Prévisualisation « Plans » sans contours : dans le
  mode œil, l'état « tous les plans » rend les plans remplis de leurs couleurs
  sans tracer de contours — ni périmètre des plans, ni arêtes internes des
  triangles — pour une vue de composition épurée (l'état « aperçu simple »
  conserve ses contours).
- 2026-08-12 (a4b3715) - Barre d'outils repliable par paquet : chaque paquet
  de boutons possède un bouton dédié qui l'ouvre ou le ferme — le bouton porte
  un **symbole SVG identifiant le sujet du paquet** (grille, œil, cible,
  étoile, engrenage… — réutilisation des icônes des boutons) suivi d'un petit
  chevron d'état (▼ ouvert / ► fermé) ; fermé, seul le bouton reste (le paquet
  se rouvre d'un clic) et les boutons masqués libèrent de la place (la barre
  continue de se replier sur la largeur de la fenêtre). L'état ouvert/fermé de
  chaque paquet est mémorisé entre les sessions (prefs.json, champ
  « toolbarPacks », tous ouverts par défaut). Statuts et infobulles mis à jour.

- 2026-08-12 (9a0588c) - Enchaînement des découpes en une étape annulable :
  l'outil Découpe reste armé après chaque découpe — on peut tracer et
  appliquer plusieurs découpes d'affilée (sur le même plan) qui ne forment
  qu'**une seule étape annulable** (l'historique n'est poussé qu'à la 1re
  découpe de la chaîne). La chaîne s'achève par Échap (sans tracé en cours),
  par le bouton Découper (D) ou par le changement d'outil / de mode ; elle
  est aussi close par une annulation/rétablissement ou le remplacement de la
  scène. Une découpe qui ne touche aucune face est ignorée sans terminer la
  chaîne. Statuts et infobulle mis à jour.
- 2026-08-12 (9a0588c) - Découpe entièrement triangulée : après une
  soustraction de polygone, les nouvelles pièces restent **triangulées** — les
  arêtes internes créées par la triangulation du résultat sont conservées
  (aucun réassemblage). Une entaille de coin devient 4 triangles (L à 6
  sommets), une entaille d'arête un octogone de 6 triangles, un trou
  intérieur n'est jamais comblé (anneau de 8 triangles), une découpe qui
  sépare produit des triangles par morceau, les faces non touchées restent
  intactes, et la couleur de la face découpée est conservée par toutes ses
  pièces. Tests meshtest : toute pièce issue d'une découpe est un triangle,
  aire conservée, trou non comblé, couleurs jamais mélangées.
- 2026-08-12 (9a0588c) - Polygone libre : un nouvel outil (bouton « Polygone » du
  paquet Outils, raccourci U) permet de tracer un polygone libre au canvas
  (clics gauches : sommets, re-clic près du 1er point : fermer, clic droit ou
  Entrée : valider) et le transforme automatiquement en un assemblage de
  triangles dans le plan actif. L'aperçu est vert (contour + remplissage
  translucide), distinct du cyan de la découpe. Retour arrière retire le
  dernier point, Échap annule le tracé ou désarme l'outil.
- 2026-08-12 (9a0588c) - Triangulation répartie des polygones : la sélection de
  la « meilleure oreille » privilégie le triangle le plus équilatéral puis le
  sommet le plus proche du milieu de la boucle (à une tolérance d'angle près,
  pour les polygones symétriques) — au lieu du premier-oreille-valide qui
  fabriquait un éventail où tous les triangles partageaient le même sommet.
  Sur un polygone régulier, chaque sommet n'apparaît plus que dans ~3
  triangles (vérifié par test : octogone → aucun sommet dans tous les
  triangles, aire conservée).
- 2026-08-12 (9a0588c) - Découpe : les arêtes internes des triangles de la
  découpe (diagonales partagées) sont annulées avant le tracé des boucles —
  une entaille de coin qui débordait du plan actif renvoyait un résultat
  vidé. La découpe peut désormais déborder du plan (seule la partie
  chevauchante est retirée), et le résultat est triangulé en zigzag comme
  l'outil polygone (tests : entaille de coin → « L » d'aire 39, entaille
  d'arête → 56, anneau → 48, sommet dans ~3 triangles).
- 2026-08-12 (9a0588c) - Anneau de manipulation unifié du calque : le
  calque se manipule désormais avec un seul bouton au lieu de trois — un
  anneau de poignées apparaît autour du curseur (il suit la souris avant
  ancrage, puis se fixe au clic). L'anneau contient 12 poignées colorées :
  cyan pour le redimensionnement en X, ambre pour le redimensionnement en Y,
  blanc pour l'échelle uniforme (rapport x/y conservé), et rouge pour les
  symétries (miroir horizontal, vertical ou les deux). Quatre flèches vertes
  à l'extérieur de l'anneau permettent le déplacement contraint (X ou Y
  seulement), le centre permet le déplacement libre, et l'anneau lui-même
  permet la rotation. Un clic droit ou Échap désarme le mode comme avant.
- 2026-08-12 (17e7cac) - Poignées de redimensionnement du calque : l'outil
  Échelle du calque affiche 8 poignées au canvas — milieux des arêtes
  gauche/droite = axe X (largeur), haut/bas = axe Y (hauteur), coins = les
  deux axes ; la saisie d'une poignée redimensionne en gardant fixe l'arête /
  le coin opposé (rotation conservée), les gestes libres restant disponibles.
- 2026-08-12 (cb6cc75) - Poignées de coin du calque : le rapport x/y est
  préservé — un seul facteur le long de la diagonale, pas de distorsion.
- 2026-08-12 (a92d849) - Calque mémorisé : la position, la rotation et
  l'échelle du calque d'image sont persistées dans prefs.json et rappelées au
  démarrage ; la synchronisation de la texture en drawScene réapplique la
  transformée restaurée (au lieu de la réinitialiser), et la scène de
  démonstration affiche le calque mémorisé.
- 2026-08-12 (151b672) - Transparence du calque d'image : le bouton Calque
  gagne la molette pour ajuster l'opacité par pas de 5 % (une étape annulable
  par salve), et le curseur du popup est exprimé de 0 à 100 % avec un
  incrément de 1.
- 2026-08-12 (151b672) - Transparence de la palette : le curseur d'opacité du
  pinceau est exprimé de 0 à 100 avec un incrément de 1 (défaut 45).
- 2026-08-12 (2e006b2) - Pipette de couleur (6.5) : bouton « Pipette » du
  paquet Outils — armé, le curseur devient une petite cible et un clic gauche
  sur le canvas prélève la couleur réellement affichée (faces peintes, calque
  d'image, couleur du fond…) puis la pose comme couleur du pinceau (valeurs
  RGB + hexa affichées, désarmement automatique après prélèvement, clic droit
  ou Échap pour désarmer). L'échantillonnage lit le framebuffer juste après le
  dessin de la scène (avant l'interface) avec l'échelle HiDPI ; lecture pixel
  unique ajoutée au renderer. Icône pipette.svg, aide mise à jour. Portée : le
  canvas de l'application (les zones hors fenêtre ne sont pas lues).
- 2026-08-12 (5def6bb) - Réticule miroir (9.2) : 4e état du réticule (Y) —
  croix de visée au curseur et à ses trois reflets à travers les axes du monde
  (axe X, axe Y, symétrie centrale) : pendant un tracé ou un déplacement, on
  voit en direct où tomberaient les points symétriques de la position courante
  — un guide pour dessiner des formes symétriques. Cycle off / simple /
  symétrique / miroir, statut et aide mis à jour.
- 2026-08-12 (bd6668b) - Opacité par plan (7.8) : chaque plan a sa propre
  transparence, indépendante de celle des faces — le bouton « Opacité » du
  groupe Plans ouvre un curseur pour le plan actif, la molette sur le bouton
  ajuste par pas de 5 % (une salve = une étape annulable). L'opacité multiplie
  le remplissage des faces (rendu d'édition et prévisualisation « tous les
  plans ») ; arêtes et points restent affichés, même à 0 % (plan « fantôme »).
  Conservée dans le JSON de scène (champ « opacity » émis seulement si ≠ 1,
  compatibilité ascendante), annulable. Icône opacity.svg, aide mise à jour,
  tests meshtest (round-trip + opacité absente = 1.0).
- 2026-08-12 (64e93f8) - Sélection au lasso (5.10) : bouton « Lasso » du
  paquet Sélection — tracer librement un contour au canvas (polygone affiché
  en direct, échantillonné tous les ~6 px écran) puis sélectionner d'un coup
  les sommets (position), segments (milieu) ou triangles (centre) du plan
  actif contenus dans le contour ; Maj au relâchement ajoute à la sélection au
  lieu de remplacer. Le mode armé monopolise le canvas (clic droit ou Échap
  désarme, désarme aussi les autres modes) ; le tracé ne déplace jamais la
  géométrie. Icône lasso.svg, aide et infobulles mises à jour.
- 2026-08-12 (c4e4d52) - Image de fond en calque (7.7) : bouton « Calque » du
  groupe Scène — charger une image PNG/JPEG (stb_image) affichée derrière la
  grille et les plans, opacité et visibilité réglables, manipulation à la
  souris au canvas (déplacer, pivoter autour du centre, échelle — vertical
  proportionnel / horizontal largeur / Maj hauteur), ajuster à la vue, retirer
  — chaque geste annulable (l'état du calque vit dans la scène). L'image est
  enregistrée avec la scène (JSON, chemin + transformée, compatibilité
  ascendante) et apparaît dans l'export PNG. Rendu texturé OpenGL dédié
  (quad + teinte), décodeur stb_image embarqué (external/stb_image.h),
  icône layer.svg, tests meshtest (round-trip scène + autosave).
- 2026-08-12 (8400bc2) - Mode filaire (7.6) : le bouton d'affichage des plans
  fait désormais défiler trois modes — rendu normal, toutes couleurs, puis
  filaire (arêtes seules, sans remplissage, pour voir la structure de toute la
  scène à travers les plans superposés). L'icône et l'infobulle du bouton
  suivent le mode courant, qui est conservé d'une session à l'autre.
- 2026-08-12 (8d12b9f) - Kiosque : la forme de chaque plan est agrandie au
  maximum dans sa carte sans jamais déborder (~80 % de la surface pour les
  proportions proches de la carte, un peu moins pour les formes « rondes ») —
  la taille rendue ne dépend plus de la taille de la forme dans le monde
  (l'ancien calcul divisait deux fois par l'envergure : une forme 10× plus
  grande rendait 10× plus petite).
- 2026-08-12 (8d12b9f) - Ordre z des faces : une face (ou une sélection de
  faces, cible « triangle ») se déplace d'un cran vers l'avant ou l'arrière
  dans l'ordre de dessin du plan — boutons avant / arrière du paquet « Ordre
  z » de la barre d'outils, raccourcis ] / [. Une face avancée est dessinée
  au-dessus de celles qui la recouvraient (et se sélectionne en premier) ; une
  sélection de plusieurs faces garde son ordre relatif et pousse devant /
  derrière elle les faces non sélectionnées ; aux bornes, rien ne change.
  Chaque déplacement est une étape annulable.
- 2026-08-12 (8d12b9f) - Peinture : avec des triangles sélectionnés (cible
  « triangle »), un seul clic du pinceau peint tous les triangles sélectionnés
  d'un coup ; sans sélection, seul le triangle cliqué est peint (comportement
  historique conservé).
- 2026-08-12 (8d12b9f) - Mode Scène (8.5) : un paquet de boutons agit sur
  toute la scène — saisir (déplacer tous les plans d'un même décalage),
  rotation (autour du point de saisie), mise à l'échelle, couleur du fond du
  canvas et réinitialisation. Chaque action se pilote à la souris (clic
  gauche + glisser au canvas) avec un repère visuel (cercle de pivot, badge en
  direct) ; un geste = une étape annulable, un clic sans glisser ne crée rien,
  clic droit ou Échap désarme. Armer un outil Scène désarme les modes
  transitoires (pinceau, mesure, fusion, tracés).
- 2026-08-12 (8d12b9f) - Préférences : la couleur du fond du canvas et
  l'outil du mode Scène armé sont persistés dans prefs.json (compatibilité
  ascendante : un fichier sans ces champs garde les valeurs par défaut) ; au
  passage, le parseur JSON accepte désormais les espaces après une virgule
  (bug latent corrigé).
- 2026-08-12 (0244129) - Découpe d'un plan par un polygone (outil découpe,
  raccourci D) : on trace à main levée un polygone qui chevauche des faces déjà
  dessinées et la zone est soustraite, en conservant intactes les faces
  restantes autour du trou (découpage des faces par la frontière, polygones à
  trous et entailles triangulés proprement). Clics gauches : sommets (re-clic
  près du 1er point : fermer et découper) · clic droit ou Entrée : découper ·
  Retour arrière : retirer le dernier point · Échap : annuler. La découpe se
  calcule sur une copie (annulable) et ne modifie le plan que si elle touche
  des faces ; aperçu du tracé au canvas.
- 2026-08-11 (1709f7d) - Forme « couronne » : un anneau dont le nombre de
  côtés extérieurs et intérieurs est indépendant (tracé en 3 clics : centre,
  rayon, trou). Après le 2e clic, l'angle du curseur oriente la forme
  intérieure ; la molette règle les côtés selon la phase du tracé (extérieurs
  tant que le rayon n'est pas verrouillé, intérieurs ensuite, Maj+molette pour
  l'autre jeu), avec un badge « ext. / int. » affiché en direct au canvas.
  Raccourci O, compteurs mémorisés entre sessions, triangulation de la bande
  sans croisement ni chevauchement.
- 2026-08-11 (8aed28a) - Interface : barre d'outils réorganisée en paquets
  cohérents (canevas, affichage, vue, sélection, presse-papiers, outils,
  fusion, annuler/rétablir, sauvegarde, entrées, plans, scène, interface) qui
  se replient automatiquement à la largeur de la fenêtre — aucun bouton n'est
  jamais masqué, quelle que soit la taille de la fenêtre.
- 2026-08-11 (fccdd3e) - Correctif du repli : la largeur de ligne se mesure
  avec une comptabilité propre (GetCursorPosX() renvoie toujours le début de
  ligne après un item ImGui, le repli ne se déclenchait donc jamais) ;
  largeur de la barre forcée à celle de la fenêtre (remplit toute la
  largeur) ; les paquets passent TOUJOURS en entier à la ligne suivante
  (jamais coupés au milieu) ; lignes serrées (plus de NewLine() en trop :
  ~20 px d'espace mort supprimés entre les lignes) ; 2 lignes à 1560 px,
  4 lignes à 700 px, tout bouton toujours visible.
- 2026-08-11 (e8e0e03) - Correctifs : en prévisualisation, plus aucun
  raccourci ne modifie la géométrie (seuls zoom, cadrage et sortie restent
  actifs, spec 9.3) ; la duplication d'un plan reprend le nom par défaut ;
  icône dédiée (image) pour l'export PNG, distincte de la sauvegarde ; le
  pré-remplissage du dialogue d'enregistrement est factorisé.
- 2026-08-11 (99558d9) - Interface : bouton d'export d'image PNG dans la
  barre d'outils (groupe Sauvegarde) — la vue actuelle s'exporte en image
  depuis l'édition comme depuis la prévisualisation (sans l'interface).
- 2026-08-11 (58f302f) - Kiosque : navigation au clavier avec les flèches
  gauche/droite (le pointeur reste la méthode principale) ; le toast et le
  message d'activation le rappellent.
- 2026-08-11 (61f8810) - Sélection : déplacement au clavier avec les
  flèches — 1 pas de grille par pression (Maj : ×5), une salve de flèches
  rapprochées = une seule étape annulable ; la navigation clavier d'ImGui est
  désactivée pour que les flèches restent déterministes.
- 2026-08-11 (476e4c3) - Plans : nommage des plans (spec 2.2) — bouton
  « Renommer » dans la barre d'outils (dialogue, Entrée valide, champ vidé =
  retour au nom par défaut « Plan n ») ; le nom est conservé dans le fichier
  JSON et affiché dans le kiosque (carte et toast), au HUD, dans la barre de
  statut et dans la confirmation de suppression.
- 2026-08-11 (505a90e) - Interface : confirmation à la fermeture de la
  fenêtre quand la scène contient des modifications non enregistrées
  (dialogue « Quitter sans enregistrer ? » : Enregistrer puis quitter /
  Quitter quand même / Annuler — la sortie reste immédiate quand la scène est
  propre, et la demande est ignorée si un autre dialogue est déjà ouvert).
- 2026-08-11 (09eb175) - Interface : le choix des formes s'active en tant que
  menu contextuel (popup sur le bouton de la barre d'outils, molette pour les
  côtés/pointes) au lieu d'une fenêtre flottante, et les choix contextuels (tout
  sélectionner / inverser la sélection) s'affichent sous forme de boutons larges.
- 2026-08-11 (30561c4) - 12 évolutions : côtés de l'étoile réglables à la molette
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
- 2026-08-11 (57ec966) - Idées d'évolutions : tout afficher (Accueil : zoom
  automatique sur la scène entière), dupliquer la sélection (Ctrl+D : copie légèrement
  décalée, prête à déplacer), rotation précise (Alt+R : saisie d'un angle, pivot = centre
  de la sélection), export de la vue en image PNG (bouton dans la prévisualisation),
  recherche dans la console (filtre par mot-clé), raccourcis des formes (C cercle,
  R rectangle, T triangle, Q carré, N pentagone, H hexagone, É étoile, A anneau ; le
  réticule passe de R à Y), inversion de la sélection (Ctrl+I ; le bouton « tout
  sélectionner » devient un bouton de sélection dont le clic droit ouvre un menu : tout
  sélectionner / inverser), mémorisation entre sessions du rayon de fusion par
  déplacement, et correction de l'image inversée des plans dans le kiosque.
- 2026-08-11 (4b7f52a) - Aide prospective au survol des entités (13) : au survol d'un
  sommet, le toast indique « sélectionner ce sommet — clic droit pour le déplacer » ; sur
  une arête de triangle, « Clic gauche pour créer un nouveau triangle à partir de ce
  segment ». L'aide couvre aussi les modes segment et triangle, le pinceau armé et la
  zone vide, et guide la phase en cours pendant une construction (forme prédéfinie ou
  triangle).
- 2026-08-11 (2469d45) - Molette sur le bouton de la forme active (4.2) : le nombre
  de côtés du cercle et de l'anneau se règle aussi à la molette sur le bouton de la
  forme (plus seulement sur le canvas), avec le compteur de côtés affiché à côté du
  bouton.
- 2026-08-11 (2469d45) - Aperçu du tracé des formes (4.2) : le contour de la forme
  prévisualisée ne se refermait pas complètement autour de la zone remplie
  (seule une arête sur deux était tracée) ; il suit désormais intégralement le
  pourtour de la zone grise pour toutes les formes (rectangle, carré, triangle,
  cercle, pentagone, hexagone, étoile et anneau).
- 2026-08-11 (28afde3) - Fusion des points superposés (5.5) : anneau orange à chaque
  position où plusieurs points coïncident, clic sur l'anneau qui sélectionne tous les
  points superposés, bouton « Fusionner » qui les regroupe à la position moyenne. Fusion
  par déplacement (5.6) : avec un point sélectionné, le bouton arme un mode (vert, rayon
  affiché) où relâcher le point près d'un autre les fusionne ; rayon réglable à la molette
  (8 à 64 px, défaut 20), 2e clic qui verrouille (cadenas) pour enchaîner, 3e clic qui
  désarme.
- 2026-08-11 (e2976ce) - Rotation de tous les plans autour du curseur (AltGr + molette,
  5° par cran, angle cumulé `rot X°` remis à zéro par Ctrl+0) et déplacement de tous les
  plans d'un même décalage (AltGr + clic droit + glisser), sans bouger la vue.
- 2026-08-11 (6d3cd14) - Création de l'éditeur de maillages 2D : dessin de points et
  de triangles, formes prédéfinies, sélection et manipulation, peinture, multi-plans,
  annuler/rétablir, sauvegarde et import/export.
