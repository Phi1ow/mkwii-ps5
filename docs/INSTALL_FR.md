# Kart PS5 : installation

Portage natif de **Mario Kart Wii (version PAL, RMCP01)** sur PS5 jailbreakée. Le jeu est
recompilé statiquement (WiiCompiled) et le rendu passe directement par le GPU de la PS5 :
ce n'est pas un émulateur.

**Les fichiers du disque ne sont pas fournis séparément. Le binaire recompilé conserve du code/des données intégrés provenant du jeu.** Il faut sa propre copie de Mario Kart Wii PAL.

## Ce que vous devez fournir vous-même

Les téléchargements incluent le binaire recompilé, sans image disque ni fichiers extraits séparés. Télécharger le paquet applicatif depuis la release, puis fournir les fichiers ci-dessous.

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
| `docs/INSTALL_FR.md` | Ce guide |

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

Placer le dossier `DATA` **dans** `PPSA99611/` :

```
PPSA99611/
├── DATA/            <- vos données extraites
├── eboot.bin
├── sce_module/
├── sce_sys/
├── shaders/
├── runtime/
├── wii_bootstrap/
├── UserData/

```

Le jeu ne peut pas lire de fichiers hors de son propre dossier. `DATA` doit donc se trouver dans `PPSA99611`.

## 3. Copier sur la PS5

1. Démarrer la console, envoyer **kstuff**, puis **ShadowMountPlus**.
2. Avec le client FTP, copier tout le dossier `PPSA99611` dans **`/data/`**. Le résultat doit être `/data/PPSA99611/eboot.bin`, `/data/PPSA99611/DATA/sys/main.dol`, etc.
   Le transfert de `DATA` prend du temps.
3. Rendre l'application exécutable en envoyant `Outils/droits-kart-ps5.elf` au chargeur d'ELF (port 9021), comme n'importe quel payload.
   Ce payload fait seulement un `chmod 755` sur `/data/PPSA99611/eboot.bin` et `/data/PPSA99611/sce_module/libc.prx`.
   Sans cette étape, le lancement échoue, en général avec l'erreur **CE-107750-0**.
   Une commande FTP `SITE CHMOD 755` ne suffit pas forcément : certains serveurs FTP répondent « OK » sans appliquer le changement.

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

ShadowMountPlus analyse `/data` toutes les 15 secondes environ. Une notification « installed game PPSA99611 » apparaît,
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

**Le jeu est lent ou « au ralenti »** : kstuff n'est pas en pause. Voir l'étape 4, en particulier le délai dans `autotune.ini`.

Chaque lancement écrit un journal dans `/data/PPSA99611/UserData/Logs/` (dossier `base_<date>_pid<N>`,
fichiers `console.log` et `stderr.log`, parfois `crash_exception.txt`). Pour signaler un plantage ou un bug, récupérer ce dossier par FTP
et l'envoyer avec une description (moment du bug, circuit, photo de l'écran si possible).

## Mise à jour et désinstallation

- **Mettre à jour** : remplacer uniquement `/data/PPSA99611/eboot.bin` par FTP, puis renvoyer `Outils/droits-kart-ps5.elf`.
  Ne pas toucher à `DATA` ni à `UserData`.
- **Désinstaller** : supprimer le dossier `/data/PPSA99611` par FTP. Cela efface aussi les sauvegardes.
  Le retrait de l'icône dépend de ShadowMountPlus et n'a pas été testé avec cette version.





