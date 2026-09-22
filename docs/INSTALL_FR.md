# Kart PS5 : installation

Portage natif de **Mario Kart Wii (version PAL, RMCP01)** sur PS5 jailbreakée. Le jeu est
recompilé statiquement (WiiCompiled) et le rendu passe directement par le GPU de la PS5 :
ce n'est pas un émulateur.

**Les fichiers du disque ne sont pas fournis séparément. Le binaire recompilé conserve du code/des données intégrés provenant du jeu.** Il faut sa propre copie de Mario Kart Wii PAL.

## Ce que vous devez fournir vous-même

Les téléchargements incluent le binaire recompilé, sans image disque ni fichiers extraits séparés. Télécharger **`Kart-PS5.zip` et `Backport.zip`** depuis la [release](https://github.com/Phi1ow/mkwii-ps5/releases/tag/v1.0.0-rc1), puis fournir les fichiers ci-dessous.

1. Utiliser son propre disque Mario Kart Wii **PAL, RMCP01**. Pour créer une image à partir du disque, consulter le [guide officiel Dolphin](https://dolphin-emu.org/docs/guides/ripping-games/), qui décrit notamment CleanRip. Aucun lien vers une copie du jeu n'est fourni ici.
2. Copier l'image obtenue sur son PC, puis l'ajouter à la liste des jeux de Dolphin. L'extraction de la partition est expliquée à l'étape 1 ci-dessous.
3. Conserver l'image, `DATA/`, `main.dol` et `StaticR.rel` en local. Ils ne doivent pas être envoyés sur GitHub, même pour signaler un bug.
4. Pour construire le portage, suivre les [notes de compilation](BUILDING.md). Le script `ps5/Prepare-Game.ps1 -DiscImage <chemin>` vérifie les empreintes de la révision PAL attendue ; il nécessite les outils indiqués dans les notes. Ne pas lancer ce script sur une extraction déjà préparée sans lire ses vérifications.
5. Fournir localement une bibliothèque Sony signée `libc.prx` adaptée au firmware, à l'emplacement `PPSA99611/sce_module/libc.prx` du paquet personnel. Elle n'est pas distribuée ici ; voir [Obtenir son propre libc.prx](LIBC_EXTRACTION.md#obtenir-son-propre-libcprx).

La compilation actuelle génère des données et du code à partir du disque. Ne pas publier séparément les fichiers extraits du disque ou les blocs de données générés. La procédure complète de compilation à partir de zéro n'est pas encore validée ; ces instructions ne promettent pas un paquet immédiatement constructible.

## Contenu du paquet applicatif

| Élément | Rôle |
|---|---|
| `PPSA99611/` | L'application « Kart PS5 » : exécutable, shaders, configuration, dossier de sauvegarde |
| `Outils/droits-kart-ps5.elf` | Payload qui rend exécutables `eboot.bin` et `sce_module/libc.prx` dans `/data/PPSA99611` (et rien d'autre) |
| `README_FR.md` | Ce guide dans l'archive téléchargée |

L'archive principale contient `Kart-PS5-candidate/PPSA99611/`. L'archive de variantes contient `Backport-candidate/FW_<version>/PPSA99611/`, avec seulement `eboot.bin` et `sce_sys/param.json` pour chaque firmware. Ces deux dossiers parents sont des conteneurs d'archive et ne doivent pas être copiés dans `/data/`.

## Prérequis

- Une PS5 jailbreakée avec un chargeur d'ELF (port 9021) et un serveur FTP.
  Compatible avec **PS5 et PS5 Pro**.
- **kstuff**, envoyé après un démarrage propre de la console. Version testée : EchoStretch kstuff v1.6.7.
- **ShadowMountPlus 1.6beta16**. Les versions 1.7alpha ont refusé les informations du titre pendant nos essais.
  ShadowMountPlus sert aussi à mettre kstuff en pause pendant le jeu (étape 4, indispensable pour les 60 images/s).
- Une copie de **Mario Kart Wii PAL (RMCP01)**. Les versions NTSC-U (RMCE01), japonaise (RMCJ01) et coréenne ne fonctionneront pas : l'exécutable correspond à la version PAL précise.
- Environ 3 Go libres sur la console.
- Sur PC : [Dolphin](https://dolphin-emu.org/) pour extraire le disque, et un client FTP (FileZilla, WinSCP…).

## 1. Extraire les données du jeu avec Dolphin

1. Dans Dolphin, faire un clic droit sur Mario Kart Wii, puis **Propriétés** et l'onglet **Système de fichiers**.
2. Faire un clic droit sur la **partition de données** (« Data Partition »), puis choisir l'extraction de la partition complète.
3. Renommer le dossier obtenu en `DATA`. Il doit contenir directement `sys/` et `files/` :

```
DATA/
├── sys/
│   ├── boot.bin
│   ├── fst.bin
│   └── main.dol
└── files/
    ├── rel/StaticR.rel
    ├── Race/
    ├── Scene/
    └── ...
```

Pour vérifier : environ 2 000 fichiers et 2,7 Go. `DATA/sys/main.dol` et `DATA/files/rel/StaticR.rel` doivent exister.

## 2. Préparer le dossier sur PC

1. Décompresser **les deux** archives. Prendre `Kart-PS5-candidate/PPSA99611/` dans `Kart-PS5.zip` comme dossier de départ.
2. Dans `Backport.zip`, choisir **une seule** variante correspondant au firmware exact de la console. Par exemple, pour 11.60, ouvrir `Backport-candidate/FW_11.60/PPSA99611/`. Copier son `eboot.bin` et son `sce_sys/param.json` dans le dossier de départ `PPSA99611/`, en remplaçant les deux fichiers existants. Conserver tous les autres fichiers du paquet principal ; la variante seule ne contient ni shaders ni icône. La présence d'une variante n'est pas une garantie de fonctionnement sur ce firmware.
3. Copier votre `libc.prx` signé et adapté au firmware dans `PPSA99611/sce_module/libc.prx`. Le fichier `sce_module/README.txt` n'est qu'un rappel : il ne remplace pas la bibliothèque.
4. Placer le dossier `DATA` **dans** `PPSA99611/` et vérifier que `PPSA99611/portable.txt` existe (le créer vide s'il manque). La première archive `v1.0.0-rc1` omettait ce marqueur : le runtime en a besoin pour lire `Config.toml` et la NAND dans `PPSA99611/UserData/` et y écrire les journaux. Son absence a provoqué un écran noir lors de la réinstallation sur PS5 9.40.

```text
PPSA99611/
├── DATA/            <- vos données extraites
├── eboot.bin
├── portable.txt
├── sce_module/libc.prx
├── sce_sys/param.json
├── sce_sys/icon0.png
├── shaders/
├── runtime/
├── wii_bootstrap/
├── UserData/
```

Avant le transfert, vérifier dans **ce même** dossier `PPSA99611/` la présence de `DATA/sys/main.dol`, `DATA/files/rel/StaticR.rel`, `sce_module/libc.prx`, `eboot.bin`, `portable.txt`, `sce_sys/param.json` et `sce_sys/icon0.png`. Ouvrir `param.json` et vérifier que `titleId` vaut `PPSA99611` et que `requiredSystemSoftwareVersion` correspond à la variante choisie (pour 11.60 : `0x1160000000000000`). Le jeu ne peut pas lire de fichiers hors de son propre dossier.

## 3. Copier sur la PS5

1. Démarrer la console, envoyer **kstuff**, puis **ShadowMountPlus**.
2. Avec le client FTP, copier **le dossier `PPSA99611`**, pas `Kart-PS5-candidate` ni `FW_<version>`, dans **`/data/`**. Le résultat doit être `/data/PPSA99611/eboot.bin`, `/data/PPSA99611/sce_module/libc.prx`, `/data/PPSA99611/DATA/sys/main.dol`, etc.
   Le transfert de `DATA` prend du temps.
3. Rendre l'application exécutable en envoyant `Outils/droits-kart-ps5.elf` au chargeur d'ELF (port 9021), comme n'importe quel payload.
   Ce payload fait seulement un `chmod 755` sur `/data/PPSA99611/eboot.bin` et `/data/PPSA99611/sce_module/libc.prx`.
   Sans cette étape, le lancement échoue, en général avec l'erreur **CE-107750-0**.
   Une commande FTP `SITE CHMOD 755` ne suffit pas forcément : certains serveurs FTP répondent « OK » sans appliquer le changement.
4. Par FTP, créer ou modifier `/data/shadowmount/manual.lst` et ajouter **une ligne contenant exactement** `/data/PPSA99611`. Conserver les autres lignes du fichier. [ShadowMountPlus 1.6beta16](https://github.com/drakmor/ShadowMountPlus/tree/1.6beta16#manual-install-list) surveille cette liste, mais ses chemins analysés par défaut ne comprennent pas `/data` lui-même. Sans cette ligne, le dossier placé dans `/data/PPSA99611` peut ne jamais être détecté.

## 4. Mettre kstuff en pause dès le lancement (important)

Tant que kstuff est actif, le jeu tourne à environ 40 images/s au lieu de 60, et il paraît lent.
ShadowMountPlus met kstuff en pause automatiquement quand un jeu est lancé, puis le réactive quand on le quitte.
Par défaut, il attend cependant 15 secondes. Il allonge aussi ce délai tout seul (30, 60, 120 s…) chaque fois qu'il croit que le jeu a planté après la pause.

Pour Kart PS5, la pause peut intervenir 5 secondes après le lancement :

1. Par FTP, ouvrir `/data/shadowmount/config.ini` et ajouter cette ligne à la fin :
   ```
   kstuff_delay=PPSA99611:5
   ```
2. Vérifier que `kstuff_game_auto_toggle` n'est pas à `0` et que PPSA99611 n'apparaît dans aucune ligne `kstuff_no_pause=`.
3. ShadowMountPlus recharge sa configuration pendant qu'il tourne. Au pire, le réglage s'applique après un redémarrage de la PS5 et le renvoi de kstuff puis de ShadowMountPlus.

Si le jeu redevient lent après une notification « Crash detected after KStuff pause: pause delay for PPSA99611 increased… »,
ShadowMountPlus a allongé le délai dans `/data/shadowmount/autotune.ini`. Remettre la ligne `kstuff_delay=PPSA99611:…` de ce fichier à `5`.
Pour éviter ça, quitter le jeu normalement (bouton PS, puis fermer le jeu).

Pendant le jeu, un passage sur l'écran d'accueil réactive kstuff. ShadowMountPlus le remet en pause au retour dans le jeu.

## 5. Lancer

ShadowMountPlus surveille `/data/shadowmount/manual.lst`. Après ajout du chemin, une notification « installed game PPSA99611 » doit apparaître,
puis l'icône **Kart PS5** s'affiche sur l'écran d'accueil. Il suffit de la lancer.

Après un redémarrage de la PS5, renvoyer kstuff puis ShadowMountPlus avant de lancer le jeu.

## Commandes (DualSense)

Commandes par défaut : le portage utilise le mode manette GameCube de Mario Kart Wii. Aucun mouvement de Wiimote n'est nécessaire.

| Bouton DualSense | Action dans Mario Kart Wii |
| --- | --- |
| Joystick gauche | Diriger le véhicule ; naviguer dans les menus |
| Croix (✕) | Accélérer ; valider dans les menus |
| Rond (○) ou R2 | Freiner / reculer ; saut et dérapage en mode manuel. Rond permet aussi de revenir en arrière dans les menus |
| Triangle (△) ou L2 | Utiliser un objet ; maintenir pour garder derrière soi les objets qui le permettent |
| Carré (□) ou R1 | Regarder derrière soi |
| Croix directionnelle, au décollage d'un saut | Effectuer une figure |
| Croix directionnelle haut, sur une moto | Lever la roue avant (wheeling) |
| Croix directionnelle bas, sur une moto | Terminer le wheeling |
| Options | Ouvrir le menu pause |

Pour un dérapage manuel, maintenir Croix pour accélérer, appuyer sur R2 ou Rond et orienter le joystick gauche. Relâcher le bouton de dérapage après les étincelles pour déclencher le mini-turbo. Le mode automatique ne propose pas de mini-turbo manuel.

Correspondances vérifiées dans le code d'entrée du portage et confrontées au [manuel Nintendo de Mario Kart Wii, commandes GameCube et techniques de conduite](https://www.mariomayhem.com/downloads/mario_instruction_booklets/Mario_Kart_Wii-WII.pdf). Cette vérification documentaire n'est pas un nouvel essai sur console. Une configuration personnelle des boutons peut modifier ces commandes.
## État de cette version

- **60 images/s** mesurées sur PS5 Pro une fois kstuff en pause (étape 4) : menus, chargements, intro et course, y compris dans les passages chargés.
  Le jeu est volontairement limité à 60 images/s, car sa logique est calée sur ce rythme.
- Les premières secondes après le lancement, avant la pause de kstuff, sont plus lentes.
- Bug connu : la mini-carte en 2D à droite pendant la course ne s'affiche pas correctement.
- Le jeu en ligne n'est pas disponible.
- Les sauvegardes sont écrites dans `/data/PPSA99611/UserData/NAND`. Ne pas supprimer ce dossier pour conserver sa progression.

## En cas de problème

**ShadowMountPlus n'affiche aucune icône** : vérifier que `/data/shadowmount/manual.lst` contient `/data/PPSA99611`, puis le chemin exact `/data/PPSA99611/sce_sys/param.json` et `/data/PPSA99611/eboot.bin` (sans dossier intermédiaire). Vérifier que `eboot.bin` **et** `param.json` proviennent de la même variante de firmware. Si une installation est annoncée sans icône, relever `/data/shadowmount/debug.log`, la version de ShadowMountPlus, le firmware et ses messages d'erreur. Pour un échec au lancement, vérifier aussi `sce_module/libc.prx` et `DATA/sys/main.dol`. L'archive ne prouve pas la compatibilité de la variante avec la console.

**Écran noir dès le démarrage** : vérifier `/data/PPSA99611/portable.txt`. La première archive `v1.0.0-rc1` l'omettait ; un fichier texte vide à cet emplacement suffit. Sur firmware 9.40, son ajout a permis au binaire public de charger `/app0/UserData/Config.toml` et d'afficher le menu.

**Le jeu est lent ou « au ralenti »** : kstuff n'est pas en pause. Voir l'étape 4, en particulier le délai dans `autotune.ini`.

Chaque lancement écrit un journal dans `/data/PPSA99611/UserData/Logs/` (dossier `base_<date>_pid<N>`,
fichiers `console.log` et `stderr.log`, parfois `crash_exception.txt`). Pour signaler un plantage ou un bug, récupérer ce dossier par FTP
et l'envoyer avec une description (moment du bug, circuit, photo de l'écran si possible).

## Mise à jour et désinstallation

- **Mettre à jour** : remplacer uniquement `/data/PPSA99611/eboot.bin` par FTP, puis renvoyer `Outils/droits-kart-ps5.elf`.
  Ne pas toucher à `DATA` ni à `UserData`.
- **Désinstaller** : supprimer le dossier `/data/PPSA99611` par FTP. Cela efface aussi les sauvegardes.
  Le retrait de l'icône dépend de ShadowMountPlus et n'a pas été testé avec cette version.
