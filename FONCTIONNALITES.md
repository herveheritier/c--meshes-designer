# Fonctionnalités — « meshes designer »

> **Objet de ce document** : décrire, de façon **non technique**, ce que fait l'application
> « meshes designer » — un éditeur de maillages 2D. Il s'agit d'une **spécification
> fonctionnelle** : chaque chapitre décrit un comportement observable (ce que voit et
> fait l'utilisateur, les règles qui s'appliquent), sans jamais parler de langage, de
> framework ou de technologie d'implémentation. Ce document est destiné à servir de
> référence pour **fabriquer une application identique dans n'importe quel langage** :
> l'objectif est que deux implémentations différentes, suivies sur ce texte, produisent
> la même expérience utilisateur.
>
> Les valeurs chiffrées citées (plages, seuils, défauts) font partie du comportement
> attendu et doivent être reproduites telles quelles (voir le chapitre 16).

---

## 1. Vue d'ensemble

« meshes designer » est un **éditeur de maillages 2D** : l'utilisateur dessine des
points dans un plan, les relie pour former des triangles, et assemble plusieurs
« plans » pour composer une scène complète (par exemple les pièces d'un objet
décomposé en vues).

L'application offre :

- la **création libre** de points et de triangles, avec aimantation optionnelle sur une grille ;
- des **outils de construction** de formes prédéfinies (cercle, rectangle, étoile…) ;
- la **sélection et la manipulation** de points, segments et triangles (déplacement, alignement, répartition, rotation, fusion, copier/couper/coller) ;
- la **coloration** des triangles (palette modifiable, pinceau, opacité) ;
- la gestion de **plusieurs plans** dans une scène (navigation, ordre d'empilement, ajout, suppression) ;
- la **navigation de la vue** (zoom, déplacement, rotation de la scène) ;
- la **sauvegarde et le chargement** de scènes complètes, la persistance automatique entre deux sessions, et l'annulation/rétablissement des actions ;
- un ensemble d'**aides visuelles** : grille, réticule, prévisualisation, compteurs de performance, messages contextuels.

L'usage se fait entièrement à la **souris et au clavier**, sur une grande zone de
dessin, aidé d'une barre d'outils flottante, d'un affichage d'état (HUD) et de
fenêtres de dialogue.

---

## 2. Concepts fondamentaux

### 2.1 La scène

La **scène** est le document de travail complet. Elle est composée d'un ou plusieurs
**plans**, plus quelques réglages de vue (voir 8).

### 2.2 Les plans

Un **plan** est une feuille indépendante de dessin à l'intérieur de la scène.
Chaque plan possède :

- une liste de **points** (sommets) ;
- une liste de **triangles** reliant ces points ;
- éventuellement un **nom** (défaut : « Plan n », n étant son numéro d'ordre).

**Un seul plan est actif à la fois** : les opérations d'édition (création, sélection,
déplacement, suppression, peinture) ne s'appliquent qu'au plan actif. Les autres
plans restent visibles en arrière-plan, estompés (lignes en pointillés, points
atténués), pour servir de contexte.

L'**ordre des plans** est significatif : c'est l'ordre d'empilement. Dans les vues
qui montrent tous les plans remplis, le plan d'indice le plus élevé recouvre les
précédents là où ils se chevauchent.

### 2.3 Les points et les triangles

- Un **point** a deux coordonnées (X, Y).
- Un **triangle** désigne trois points du plan (par leur position dans la liste des points).
- Chaque triangle peut porter une **couleur de remplissage** (facultatif ; sans couleur, il garde le rendu par défaut).

Le maillage d'un plan est la structure formée par l'ensemble de ses points et de
ses triangles. Un point peut être partagé par plusieurs triangles ; il n'existe
qu'une seule entrée par point.

### 2.4 Le repère

Le dessin se fait dans un **repère mathématique** : l'axe X pointe vers la droite,
l'axe Y vers le **haut**. Les coordonnées affichées au curseur suivent ce repère.
Le point d'origine (0, 0) est matérialisé par des axes (facultatifs) sur lesquels
s'ancrent également la grille.

### 2.5 La grille

Une **grille** orthogonale peut être affichée. Elle est définie par un **pas**
(espacement entre deux lignes). Deux rôles :

- **visuel** : lignes horizontales et verticales régulières ;
- **magnétique** : quand la grille est active, tout point posé par l'utilisateur
  est **aimanté** à l'intersection la plus proche.

La grille peut être affichée ou masquée, son pas ajusté (voir 9.1).

---

## 3. L'interface

### 3.1 Le canvas

La zone de travail occupe presque tout l'écran. C'est là que sont dessinés la
grille, les axes, les points, les triangles et tous les repères de sélection.
Toutes les interactions de dessin s'y font.

### 3.2 La barre d'outils

Carte flottante en **haut à gauche**, organisée en **groupes** séparés par des
traits verticaux. Les boutons sont des icônes (sans libellé visible) ; le verbe
de chaque action est indiqué dans une **infobulle** au survol et dans la fenêtre
d'aide. Les boutons reflètent leur état : actif (vert), mode particulier (ambre,
anneau…), ou **grisé** quand l'action n'est pas disponible.

Groupes, de gauche à droite :

1. **Canevas / édition** : grille, réticule, prévisualiser, mode « toutes couleurs », compteur d'images par seconde (toujours visible), cible de sélection (sommet / segment / triangle), presse-papiers (copier / couper / coller), peinture (palette), aligner / répartir, formes prédéfinies, réinitialiser la scène, tout sélectionner, fusionner les points.
2. **Annuler / Rétablir** : deux boutons avec compteur.
3. **Sauvegarde** : bouton vert SAVE.
4. **Entrées** : boutons bleus « Charger meshes » et « Charger JSON ».
5. **Navigation entre plans** : plan précédent / suivant, compteur « i/N », monter / descendre, ajouter un plan vide, supprimer le plan actif.
6. **Console** : afficher / masquer la console de messages.
7. **Aide** : fenêtre des raccourcis.

### 3.3 Le HUD (affichage d'état)

En **bas à gauche** de l'écran, une pile d'informations :

- **zoom** courant (par ex. `1.2x`) et position du curseur en coordonnées du repère (par ex. `pos(45, -30)`) ; après une rotation, l'angle cumulé (`rot X°`) ;
- **compteur d'affichage** : nombre de redessins par seconde (indicateur de performance, activable) ;
- **état de la scène** : nom de la scène et signalement des **modifications non enregistrées** ;
- **message contextuel** (toast) : aide prospective, voir 13.

Dans la barre d'outils, des **pilules vertes** lisibles affichent des compteurs :
plan actif / total (`i/N`), profondeur de l'historique annuler, nombre d'éléments
sélectionnés, images par seconde.

### 3.4 Le curseur de dessin

À la place du pointeur système sur la zone de dessin, une **croix de visée**
suit la souris (elle peut être désactivée ou symétrique, voir 9.3). En mode
pinceau, le curseur devient un **disque de la couleur de peinture** courante.

### 3.5 Les fenêtres (modales)

Les dialogues (aide, enregistrement, import, confirmations de suppression ou de
réinitialisation) s'ouvrent dans des fenêtres au centre de l'écran, fermables
par la touche Échap, par clic à l'extérieur, ou par un bouton de fermeture.

### 3.6 La console de messages

Fenêtre flottante à part (voir chapitre 14).

---

## 4. Édition de la géométrie

### 4.1 Création de points et de triangles

Le geste de base est le **clic gauche sur une zone vide** du plan actif :

1. **1er clic** : pose un point. Ce point est le premier sommet (`p1`) d'un triangle en cours de construction.
2. **2e clic** : pose un deuxième point ; les deux sont reliés par un **segment** (le triangle partiel `p1, p2` est affiché, sans remplissage).
3. **3e clic** : pose le troisième point et **ferme le triangle** `p1, p2, p3`.

Au-delà, chaque nouveau clic sur une zone vide crée un nouveau point ; s'il est
posé **près d'un segment existant** (une arête de triangle du plan actif), le
triangle créé est directement **complet** et **s'accroche à ce segment** : deux
de ses sommets sont les extrémités du segment, le troisième est le nouveau point
(clic gauche sur un segment en mode sommet = « créer un nouveau triangle à partir
de ce segment »). C'est le moyen naturel d'étendre un maillage arête par arête.

Tant que la grille est active, chaque point posé est aimanté à l'intersection de
grille la plus proche.

### 4.2 Formes prédéfinies

Un panneau **Formes** (bouton dédié) propose des formes toutes prêtes :
**cercle, rectangle, carré, triangle, pentagone, hexagone, étoile, anneau**
(cercle percé d'un trou). Choisir une forme l'arme, puis la tracer au canvas :

- **Formes en 2 clics** (cercle, rectangle, carré, triangle, pentagone, hexagone) :
  1er clic = point d'ancrage (le **centre** pour les polygones, un **coin** pour le
  rectangle et le carré) ; le mouvement de la souris règle simultanément la **taille**
  et l'**orientation** (le sommet n° 0 du polygone pointe vers la souris) ; 2e clic =
  valider.
- **Étoile en 3 clics** : 1er = centre ; le mouvement règle rayon et orientation ;
  2e clic verrouille ; le mouvement règle la **profondeur des branches** ; 3e clic =
  valider.
- **Anneau en 3 clics** : 1er = centre ; 2e clic verrouille rayon et orientation ;
  le mouvement règle la **taille du trou** ; 3e clic = valider. La molette règle le
  nombre de côtés.

Règles communes :

- le **cercle** est un éventail de triangles dont le **nombre de côtés** se règle à
  la **molette** (sur le canvas ou sur le bouton actif) et se **mémorise** entre les
  sessions ; le mode cercle se désactive **après chaque création** et reste aussi
  accessible par la touche `C` ;
- le **triangle** prédéfini est généré en **un seul triangle** (3 sommets, sans
  point central), contrairement aux autres polygones en éventail ;
- pendant un tracé, le **pointeur reste visible** même sans bouger la souris ;
- **Échap** quitte le mode, **clic droit** ou **Retour arrière** annulent le tracé
  en cours ; la forme est posée dès le 2e (ou 3e) clic, l'opération est annulable.

### 4.3 Suppression

La touche **Retour arrière** (Backspace) supprime l'élément sélectionné selon la
cible active (voir 5.1) : point, segment ou triangle. Les règles de cohérence
s'appliquent (voir 4.4). La suppression est annulable.

### 4.4 Cibles d'édition : sommet / segment / triangle

Un bouton (et la cible courante est affichée) fait défiler trois **modes de
cible** qui déterminent ce que le clic vise et ce que la suppression enlève :

- **Sommet** (mode par défaut) : vise un point. Un clic sur un point le sélectionne ; s'il y a plusieurs points superposés, ils sont tous sélectionnés (voir 5.5).
- **Segment** : vise une arête (un côté de triangle). Un clic sur une arête sélectionne ses deux extrémités.
- **Triangle** : vise une face. Un clic à l'intérieur d'un triangle (ou près de son centre) sélectionne ses trois sommets.

### 4.5 Règles de cohérence du maillage

- La suppression d'un **sommet** retire les triangles qui l'utilisent ; les triangles voisins survivants sont réorganisés et **conservent leur couleur**.
- La suppression d'un **segment** retire le ou les triangles qui le partagent.
- Les **triangles partiels** (1 ou 2 sommets seulement, en cours de construction) peuvent exister en mémoire mais ne sont pas enregistrés (voir 12.3).
- Des **points superposés** (mêmes coordonnées, typiquement après l'import de scènes comportant des doublons) sont tolérés mais signalés (voir 5.5).

---

## 5. Sélection et manipulation

### 5.1 Principes de sélection

La sélection porte sur le **plan actif** uniquement. Les éléments sélectionnés
sont mis en évidence (points agrandis, remplissage des triangles sélectionnés en
mode triangle, etc.).

| Geste | Effet |
|---|---|
| **Clic gauche** sur une entité | Remplace la sélection par cette entité (selon la cible active) |
| **Clic gauche sur le vide** | Crée un point (voir 4.1) |
| **Clic gauche + glisser** | Délimite un **rectangle de sélection** (lasso) : sélectionne tous les points du plan actif à l'intérieur ; ne déplace jamais la géométrie |
| **Maj + clic gauche** | Ajoute ou retire l'entité de la sélection (bascule) |
| **Ctrl + clic droit** | Ajoute l'entité sous le pointeur à la sélection, sans la déplacer (jamais de doublon) |
| **Clic droit simple** | Sélectionne uniquement l'entité la plus proche et désélectionne le reste ; sans entité sous le pointeur, efface la sélection |
| **Clic droit + glisser** | **Déplace la sélection** existante, depuis n'importe quel point du canvas (même loin de la géométrie) ; sans sélection, sélectionne l'entité sous le pointeur puis la déplace |
| **Ctrl + clic droit** | Ajoute l'entité à la sélection sans la déplacer |

Un clic gauche-glissé **ne déplace jamais** la géométrie (le lasso et le
déplacement sont séparés par bouton de souris).

### 5.2 Déplacement

- **Clic droit + glisser** : déplace la sélection en direct, par le même delta que la souris.
- Pendant le glisser, les coordonnées suivent le curseur ; avec la grille active, les points aimantent aux intersections.

### 5.3 Alignement et répartition

Deux paires d'actions, applicables à la sélection du plan actif (chaque action
constitue **une seule** étape annulable) :

- **Aligner X / Aligner Y** (≥ 2 points sélectionnés) : tous les points prennent la coordonnée X (ou Y) du **premier point sélectionné** (l'ancre) ; l'autre coordonnée est conservée.
- **Répartir X / Répartir Y** (≥ 3 points) : espace uniformément les points sélectionnés selon l'axe, **entre les deux extrêmes**, qui restent en place.

Les boutons sont grisés quand la sélection est trop petite. Le panneau reste
ouvert après une action pour enchaîner (par ex. Aligner X puis Aligner Y).

### 5.4 Rotation à la molette

Avec **2 points sélectionnés ou plus**, la **molette de la souris sur le canvas**
fait **pivoter les points sélectionnés autour du curseur** (par pas de 5° par
cran). Avec moins de 2 points sélectionnés, la molette zoome (voir 8.1).
La rotation est annulable et l'angle cumulé est affiché dans le HUD.

### 5.5 Points superposés et fusion

- Quand plusieurs points occupent la **même position** (mêmes coordonnées), cette position est signalée par un **anneau orange**.
- Un **clic** sur l'anneau (en mode sommet) **sélectionne tous** les points superposés.
- Le bouton **Fusionner** regroupe alors tous ces points en **un seul point** placé à la position moyenne (centroïde).

### 5.6 Fusion par déplacement

Avec **exactement un point sélectionné**, le bouton Fusionner arme un mode de
**fusion par déplacement** (le bouton passe en vert et affiche un rayon) :

- glisser le point sélectionné puis le **relâcher près d'un autre point** (à moins du rayon affiché) les **fusionne** en un seul point ;
- le rayon de fusion est réglable à la **molette de la souris sur le bouton** (de 8 à 64 pixels à l'écran, défaut 20 ; indépendant du zoom) ;
- un **re-clic** sur le bouton désarme le mode ;
- un **2e clic quand le mode est armé** le **verrouille** (cadenas sur le bouton) : les fusions réussies s'enchaînent sans avoir à réarmer, jusqu'à désarmement.

### 5.7 Tout sélectionner

Un bouton sélectionne **tous les points du plan actif**.

### 5.8 Presse-papiers interne

L'application dispose d'un **presse-papiers interne** (copier / couper / coller),
distinct du presse-papiers du système :

- **Copier** : capture les points sélectionnés **et** les triangles **entièrement contenus** dans la sélection (avec leur couleur).
- **Couper** : copie puis supprime (annulable).
- **Coller** : insère la copie dans le **plan actif**, à la même position que la source ; chaque collage successif décale la copie **d'un demi-pas de grille** (les collages en cascade restent distincts de la source). La copie collée devient la sélection courante.

Le presse-papiers est **perdu** à la fermeture de l'application. Les boutons sont
grisés sans sélection (copier/couper) ou sans contenu (coller).

---

## 6. Couleurs et peinture

### 6.1 Palette

Un bouton **Peinture** ouvre la **palette de couleurs** : une rangée de pastilles
(8 couleurs par défaut) plus des commandes. La palette est **personnalisable** :

- **Ajouter** : enregistre la couleur choisie dans un sélecteur (avec l'opacité courante, voir 6.3) comme nouvelle pastille ;
- **clic droit sur une pastille** : la retire ;
- **double-clic sur une pastille** : la modifie (le sélecteur pilote la pastille en direct ; Entrée valide, Échap annule) ;
- **Défauts** : restaure les 8 couleurs d'origine.

La palette est **conservée** d'une session à l'autre.

### 6.2 Pinceau

Un **clic sur une pastille** arme le pinceau avec cette couleur. Un **clic gauche
sur un triangle** (du plan actif) le peint avec la couleur courante. Le pinceau
désarmé, un clic sur un triangle le sélectionne comme en mode normal.

### 6.3 Opacité

Un curseur d'opacité (défaut 45 %) règle l'opacité appliquée à **chaque** peinture.
Choisir une couleur ne change pas l'opacité courante ; l'opacité est **conservée**
d'une session à l'autre.

### 6.4 Conservation des couleurs

La couleur d'un triangle **survit aux suppressions** de points, de segments ou de
triangles voisins (voir 4.4), aux annulations, et aux enregistrements / chargements.

---

## 7. Gestion des plans

### 7.1 Navigation

Des boutons permettent de passer au **plan précédent / suivant** ; le compteur
`i/N` (pilule verte) indique le plan actif sur le total. Le plan actif peut aussi
être choisi visuellement via le **kiosque** (voir 7.5).

### 7.2 Ordre des plans

Les boutons **monter / descendre** (ou les raccourcis Alt+Flèche haut/bas)
déplacent le plan actif d'un rang dans l'ordre des plans. **Monter** augmente
l'indice (le plan passe au-dessus, il recouvre davantage) ; **descendre** le
réduit. Aux bornes (premier / dernier plan), les boutons sont grisés. Chaque
déplacement est une étape annulable.

### 7.3 Ajout et suppression

- Le bouton **+** (ajouter un plan vide) : **clic gauche** = le nouveau plan est inséré **avant** le plan courant ; **clic droit** = inséré **après**. Le nouveau plan devient le plan actif (l'opération est annulable).
- Le bouton **×** supprime le plan actif, après **confirmation** dans une fenêtre. La suppression est annulable.

### 7.4 Suppression complète de la scène

Le bouton **Réinitialiser** (ou Maj+Retour arrière) vide entièrement la scène,
après **confirmation**. Cette action efface aussi l'historique d'annulation.

### 7.5 Kiosque de sélection (mode couverture)

Le mode **kiosque** (bouton dédié ou Alt+K, disponible dès qu'il y a au moins
2 plans) présente chaque plan comme une **carte inclinée** autour d'un axe
vertical virtuel (effet « kiosque » / cover-flow) :

- chaque carte porte l'étiquette « Plan n » ; le **déplacement horizontal de la souris** fait varier l'inclinaison des cartes et met **un plan en avant** (pleine opacité, un guide pointillé vert marque l'axe du pointeur) ; les autres cartes sont atténuées ;
- un **clic gauche** sélectionne le plan mis en avant et **quitte immédiatement** le mode ;
- **Échap** ou **clic droit** sortent du mode sans changer de plan ;
- aucune édition n'est possible dans ce mode ; la barre d'outils est masquée (sauf le bouton du mode).

### 7.6 Mode d'affichage « toutes couleurs »

Un mode d'affichage (bouton dédié, sans raccourci) montre **tous les plans**
remplis de leurs couleurs de triangles **pendant l'édition** (le plan actif garde
son rendu d'édition complet ; les plans inactifs gardent leurs lignes en pointillés
mais sont remplis). Un re-clic revient au rendu standard. C'est un mode
d'affichage pur : l'édition est inchangée, et le mode est conservé d'une session
à l'autre.

---

## 8. Navigation de la vue

La vue (zoom et position du repère) est indépendante du contenu : on peut
zoomer et se déplacer sans rien modifier.

### 8.1 Zoom

- **Molette de la souris** sur le canvas : zoom **centré sur le curseur**, facteur ×1,1 par cran, borné entre **0,1× et 10×**.
- **Ctrl+0** : revient à 100 %, recentré sur l'origine.
- Le niveau est affiché dans le HUD (`1.2x`).

### 8.2 Déplacement de la vue (pan)

- **Clic du milieu (molette enfoncée) + glisser** : déplace la vue ; le contenu suit le sens du glisser (le repère se déplace, le dessin reste accroché à la souris).

### 8.3 Rotation de chaque plan autour du curseur

- **AltGr + molette** (AltGr = touche Alt droite sur la plupart des claviers) : **fait tourner chaque plan autour du curseur** (5° par cran, cumulatif). Le pivot est la position du curseur dans le repère ; si le curseur bouge pendant le geste, le pivot suit.
- L'angle cumulé est affiché dans le HUD (`rot X°`). **Ctrl+0** le remet à zéro.

### 8.4 Déplacement de tous les plans ensemble

- **AltGr + clic droit + glisser** : déplace **tous les plans** d'un même décalage (la vue ne bouge pas, c'est le contenu entier qui se déplace).

---

## 9. Modes d'affichage et outils visuels

### 9.1 Grille

- Bouton **Grille** (ou touche `G`) : affiche / masque la grille. Le pas courant est affiché à côté de l'icône.
- **Molette sur le bouton Grille** : ajuste le pas.
- **Clic du milieu sur le bouton Grille** : réinitialise le pas au défaut.

### 9.2 Réticule

Le bouton **Réticule** (ou touche `R`) fait défiler **trois états** :
désactivé → simple (croix de visée) → symétrique (croix pleine grandeur).

### 9.3 Prévisualisation (mode œil)

Le mode **Prévisualiser** (bouton œil ou touche `P`) masque tous les outils
(barre d'outils, HUD, points de contrôle, grille, axes) pour ne laisser que la
géométrie. Il fait défiler **trois états** :

1. **Aperçu simple** : la scène telle quelle (plan actif rempli, autres en contours).
2. **Plans** : **tous** les plans rendus remplis de leurs couleurs, **dans leur ordre** (le plan le plus haut recouvre les précédents) — vue de composition de la scène.
3. **Retour à l'édition**.

En prévisualisation : la **molette** zoome toujours, le **clic du milieu** déplace
la vue, et **aucune édition n'est possible**. La sortie se fait par **Échap**, par
**clic gauche** sur le canvas, ou par **Ctrl+S** (qui ouvre l'enregistrement) —
tous quittent directement, quel que soit l'état. Le bouton œil reste seul
flottant pour piloter le cycle au clic (vert en aperçu simple, ambre avec le
libellé « plans » dans le 2e état).

### 9.4 Compteurs de performance

- Un **compteur d'images par seconde** (fps) est **toujours visible** dans la barre d'outils (pilule verte « N fps ») : il mesure la fluidité d'affichage. Il passe en **ambre** quand la fréquence chute sous **42 fps**, redevient vert au-dessus de **48 fps**, et reste stable entre les deux (anti-clignotement). Il est masqué en prévisualisation.
- Le HUD bas-gauche peut afficher (touche `F`) le **nombre de redessins par seconde** (indicateur de charge de rendu, distinct du fps).

### 9.5 Curseur

La croix de visée (ou le disque de peinture) suit la souris (voir 3.4). Elle
reste visible même après le premier clic d'une construction.

---

## 10. Annuler / Rétablir

- **Ctrl+Z** (ou bouton Annuler) : annule la dernière action.
- **Ctrl+Maj+Z** ou **Ctrl+Y** (ou bouton Rétablir) : rétablit une action annulée.
- L'historique conserve jusqu'à **50 entrées** (les plus anciennes sont évincées).
- Les boutons sont **grisés** quand leur pile est vide ; un compteur (pilule verte) affiche la profondeur de l'historique.
- L'historique est **conservé d'une session à l'autre** (restauré au redémarrage tant que la scène restaurée correspond).
- Il est **réinitialisé** par la réinitialisation complète de la scène et par l'import en mode « Remplacer » ; l'import en mode « Fusionner » le **conserve**.

Actions enregistrées comme une seule étape : chaque création de point ou de
triangle, chaque déplacement, suppression, alignement, répartition, rotation,
fusion, peinture, chaque changement de plan (ajout, suppression, ordre), chaque
copier/couper/coller.

---

## 11. Sauvegarde et persistance

### 11.1 Persistance automatique

La scène en cours, ainsi que la vue (zoom, position), la grille et les autres
préférences, sont **conservées automatiquement** : au prochain lancement,
l'utilisateur retrouve son travail et sa vue tels quels.

Les réglages de vue (zoom, pan, grille) sont des **préférences** : les changer ne
marque pas la scène comme « modifiée ». Toute mutation de géométrie, de plan ou
de couleur la marque (l'état « modifié » est signalé dans le HUD).

### 11.2 Enregistrement explicite

Le bouton **SAVE** ou **Ctrl+S** ouvre la **fenêtre d'enregistrement** :

- liste des **emplacements déjà utilisés** (scènes déjà enregistrées, du plus récent au plus ancien), avec l'**emplacement précédent présélectionné** ;
- possibilité de **renommer** (champ pré-rempli, texte sélectionné ; Entrée valide) ;
- validation : télécharge le fichier `<nom>.json`, la scène **adopte ce nom** (affiché dans le HUD), et l'emplacement est **mémorisé** pour la prochaine fois.

---

## 12. Import / Export et formats

### 12.1 Formats

Deux formats sont pris en charge :

- **JSON** : format complet de la scène (plans, points, triangles, couleurs, plan actif, grille, zoom, vue). L'enregistrement et le chargement sont **exacts** (aller-retour sans perte).
- **« meshes »** (texte) : format simple **une ligne = un plan** : les coordonnées des sommets séparées par des points-virgules, X et Y séparés par des virgules (ex. `0,0;10,0;5,8`). Chaque **triplet de sommets consécutifs forme un triangle**. Les coordonnées identiques sont dédupliquées. Un reliquat de 1 ou 2 sommets en fin de ligne forme un triangle partiel, **filtré à l'import** (non chargé).

### 12.2 Chargement

Deux boutons bleus : **Charger meshes** (format texte) et **Charger JSON**.
Après choix du fichier, une fenêtre propose deux modes :

- **Remplacer** (par défaut) : la scène courante est remplacée.
- **Fusionner** : les plans importés sont **ajoutés** aux plans existants (pour assembler des pièces).

Le dernier mode choisi est mémorisé. **Glisser-déposer** d'un fichier JSON sur la
zone de dessin déclenche aussi l'import. Les erreurs de structure ou d'indices
sont signalées **avant** toute modification de la scène.

---

## 13. Messages et aide contextuelle

Au-dessus du HUD bas-gauche, un **toast** donne une aide **prospective** — il ne
dit jamais ce qui vient d'être fait, il dit ce que l'utilisateur **peut faire
maintenant** et comment :

- **au survol** d'un élément, il décrit le geste possible : sur un sommet,
  « sélectionner ce sommet — clic droit pour le déplacer » ; sur une arête de
  triangle, « Clic gauche pour créer un nouveau triangle à partir de ce segment » ;
  sur un triangle (mode triangle), le sélectionner ;
- **pendant une construction** (cercle, étoile, triangle…), il guide la phase
  suivante (« 1er clic gauche : pose le centre… », « poser le 2e/3e sommet ») ;
- en mode pinceau : « Clic gauche pour peindre ce triangle… » ;
- **après une action** (pendant ~3 s), il rappelle le geste ou le raccourci
  suivant utile (par ex. « Cliquez pour poser le 2e sommet — le 3e clic ferme le
  triangle », `Ctrl+Z` pour annuler, `Ctrl+V` pour coller) ;
- sur une zone vide, un message générique.

Le toast est masqué en prévisualisation. Une fenêtre **Aide** (touche `?`)
récapitule tous les raccourcis.

---

## 14. Console de messages

Une **console de messages** (bouton dédié) affiche les événements de l'application
dans une fenêtre flottante :

- cadre redimensionnable (poignée en bas à droite) et **déplaçable** (barre de titre) ;
- zone de logs **défilante**, chaque entrée préfixée d'un **horodatage** `[HH:MM:SS]` ;
- bouton corbeille pour **vider** le contenu ;
- la **visibilité, la position et la taille** du cadre sont conservées d'une session à l'autre.

---

## 15. Raccourcis clavier et gestes souris (récapitulatif)

### Clavier

| Touche | Action |
|---|---|
| `Backspace` | Supprimer l'élément sélectionné (point / segment / triangle selon la cible) |
| `Maj+Backspace` | Réinitialiser complètement la scène (confirmation) |
| `Ctrl+Z` | Annuler |
| `Ctrl+Maj+Z` ou `Ctrl+Y` | Rétablir |
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Copier / couper / coller (presse-papiers interne) |
| `Ctrl+S` | Enregistrer (fenêtre d'emplacement) |
| `Ctrl+0` | Réinitialiser le zoom (100 %, recentré) |
| `G` | Afficher / masquer la grille |
| `R` | Réticule : 3 états (off / simple / symétrique) |
| `F` | Afficher / masquer le compteur de redessins (HUD) |
| `P` | Prévisualiser : cycle aperçu simple → plans → édition |
| `Alt+K` | Kiosque de sélection des plans |
| `C` | Activer / désactiver le mode cercle |
| `Alt+↑` / `Alt+↓` | Monter / descendre le plan actif dans l'ordre |
| `Alt+←` / `Alt+→` | Aligner X / Y sur le premier point sélectionné |
| `Alt+Maj+←` / `Alt+Maj+→` | Répartir X / Y entre les extrêmes |
| `Échap` | Quitter le mode en cours (construction, prévisualisation, kiosque, panneau) ou fermer une fenêtre |
| `?` | Afficher / masquer l'aide |

### Souris

| Geste | Action |
|---|---|
| Clic gauche (vide) | Poser un point / construire un triangle |
| Clic gauche (entité) | Sélectionner (selon la cible) |
| Maj + clic gauche | Basculer la sélection |
| Clic gauche + glisser | Rectangle de sélection (lasso) |
| Clic droit | Sélectionner l'entité la plus proche |
| Ctrl + clic droit | Ajouter à la sélection sans déplacer |
| Clic droit + glisser | Déplacer la sélection |
| Molette | Zoom centré sur le curseur — ou rotation des points sélectionnés (≥ 2 sélectionnés) |
| AltGr + molette | Rotation de chaque plan autour du curseur |
| Clic du milieu + glisser | Déplacer la vue (pan) |
| AltGr + clic droit + glisser | Déplacer tous les plans ensemble |
| Molette sur un bouton actif | Réglage contextuel (pas de grille, côtés du cercle, rayon de fusion) |
| Clic du milieu sur le bouton Grille | Réinitialiser le pas de la grille |

---

## 16. Constantes de comportement (valeurs à reproduire)

Ces valeurs chiffrées font partie du contrat de l'application :

| Constante | Valeur |
|---|---|
| Zoom | ×1,1 par cran de molette, borné à **[0,1× ; 10×]** |
| Rotation (points sélectionnés) | 5° par cran, pivot = curseur |
| Rotation globale (AltGr + molette) | 5° par cran, cumulative |
| Historique annuler / rétablir | **50 entrées** maximum (les plus anciennes évincées) |
| Rayon de fusion par déplacement | 8 à 64 px à l'écran, **défaut 20 px**, indépendant du zoom |
| Nombre de côtés du cercle | réglable à la molette, **mémorisé** entre les sessions |
| Opacité de peinture | défaut **45 %**, mémorisée |
| Palette par défaut | 8 couleurs |
| Compteur fps | ambre sous **42 fps**, vert au-dessus de **48 fps**, état stable entre les deux |
| Détection des clics (points, segments, triangles) | exprimée en **pixels à l'écran**, convertie selon le zoom : la zone de clic reste identique de 0,1× à 10× |
| Aimantation grille | points posés accrochés aux intersections ; décalage du collage = **½ pas de grille** |
| Plans mémorisés (emplacements d'enregistrement) | 20 maximum, du plus récent au plus ancien |
| Sortie de prévisualisation | Échap, clic gauche, ou Ctrl+S (sortie directe des deux états) |

---

## 17. Règles d'expérience transverses

- **Zéro surprise de sélection** : les gestes de sélection et de déplacement sont
  séparés par bouton de souris ; un glisser ne déplace jamais ce qu'on voulait
  seulement sélectionner.
- **Précision constante** : tous les rayons de détection et de fusion sont définis
  à l'écran (pixels) et non dans le repère, pour que le confort d'utilisation soit
  identique quel que soit le zoom.
- **Cohérence visuelle des états** : boutons actifs en vert, modes particuliers en
  ambre ou avec anneau, actions indisponibles grisées ; compteurs en pilules vertes.
- **Tout est annulable** : chaque mutation de la scène (y compris l'ordre des
  plans et la suppression) constitue une étape d'historique.
- **Aide au bon moment** : le message contextuel accompagne chaque geste de
  construction, et chaque action rappelle la suivante.
- **Le dessin ne se perd jamais** : persistance automatique de la scène et des
  préférences entre les sessions ; enregistrement explicite pour produire un
  fichier exportable.
