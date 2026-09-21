# Kart PS5 : installation

Portage natif de **Mario Kart Wii (version PAL, RMCP01)** sur PS5 jailbreakée. Le jeu est
recompilé statiquement (WiiCompiled) et le rendu passe directement par le GPU de la PS5 :
ce n'est pas un émulateur.

**Les données du jeu ne sont pas fournies.** Il faut sa propre copie de Mario Kart Wii PAL.

## Contenu du dossier

| Élément | Rôle |
|---|---|
| `PPSA99611/` | L'application « Kart PS5 » : exécutable, shaders, configuration, dossier de sauvegarde |
| `Outils/droits-kart-ps5.elf` | Payload qui rend exécutables `eboot.bin` et `sce_module/libc.prx` dans `/data/PPSA99611` (et rien d'autre) |
| `docs/INSTALL_FR.md` | Ce guide |

## Prérequis

- Une PS5 jailbreakée avec un chargeur d'ELF (port 9021) et un serveur FTP.
  Testé uniquement sur **PS5 Pro, firmware 9.40**. Une PS5 standard n'a pas été testée.
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

## Commandes (manette DualSense)

La manette se comporte comme une **manette GameCube**. Le jeu utilise donc ses commandes GameCube.

| DualSense | GameCube |
|---|---|
| Croix | A |
| Rond | B |
| Carré | X |
| Triangle | Y |
| Options | Start |
| R1 | Z |
| L2 / R2 | L / R |
| Croix directionnelle et joystick gauche | Croix et stick |

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

