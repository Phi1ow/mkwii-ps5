# Mario Kart Wii pour PS5

[English](README.md) · **Français**

Portage natif de **Mario Kart Wii PAL (RMCP01)** sur PlayStation 5, basé sur [WiiCompiled](https://github.com/patchzyy/Wiicompiled).

- Lancement depuis l'écran d'accueil sous le nom **Kart PS5**.
- Recompilation statique du jeu Wii et rendu sur le GPU PS5 avec AGC.
- Commandes DualSense correspondant à une manette GameCube.

> Il faut fournir les données de sa propre copie de Mario Kart Wii PAL. Cet exécutable ne prend pas en charge les autres régions.
>
> Compatible avec **PS5 et PS5 Pro** jailbreakées.

---

## Fonctionnalités

| Fonction | État |
| --- | --- |
| Rendu GPU natif | Moteur AGC |
| Fréquence d'affichage | 60 images/s rapportées sur PS5 Pro après la mise en pause de kstuff |
| Manette | DualSense avec correspondance GameCube |
| Sauvegardes | NAND locale dans `UserData/NAND` |
| Région | PAL, RMCP01 |
| Identifiant du titre | `PPSA99611` |
| Jeu en ligne | Indisponible |

Le jeu est limité à 60 images/s car sa logique dépend de ce rythme. Les premières secondes sont plus lentes, tant que kstuff reste actif.

### Limites connues

- La mini-carte en 2D pendant les courses ne s'affiche pas correctement.
- La présence d'un paquet par firmware ne confirme pas son fonctionnement : voir les [notes sur les firmwares](docs/FIRMWARE.md).

## Prérequis

- Une PS5 jailbreakée, un serveur FTP et un chargeur d'ELF.
- kstuff ; la version indiquée dans les essais est EchoStretch v1.6.7.
- ShadowMountPlus 1.6beta16 ; les versions 1.7alpha essayées ont refusé les informations du titre.
- Une extraction personnelle de Mario Kart Wii PAL contenant `DATA/sys/main.dol` et `DATA/files/rel/StaticR.rel`.
- Environ 3 Go disponibles sur la console.

## Téléchargement et fichiers personnels du jeu

[Télécharger l'application recompilée et les variantes de firmware](https://github.com/Phi1ow/mkwii-ps5/releases/tag/v1.0.0-rc1).

Les exécutables recompilés sont fournis. Les images disque (ISO/RVZ/WBFS), les fichiers extraits `main.dol` / `StaticR.rel` et le dossier DATA du jeu ne sont **pas** fournis. Extraire sa propre copie PAL RMCP01 en suivant le [guide d'installation](docs/INSTALL_FR.md). Fournir une bibliothèque signée `libc.prx` adaptée au firmware dans `PPSA99611/sce_module/libc.prx` ([comment l'obtenir depuis sa propre console](docs/LIBC_EXTRACTION.md#obtenir-son-propre-libcprx)) ; les bibliothèques Sony et les sauvegardes personnelles ne sont pas incluses.

L'exécutable est issu d'une recompilation statique et conserve du code/des données intégrés provenant du jeu. Exclure les fichiers ROM séparés ne supprime pas ce contenu : le binaire n'est pas présenté comme dépourvu de données du jeu. Le paquet nettoyé reste en préversion en attendant un nouvel essai sur console.

Ne pas joindre d'image disque, de fichiers extraits, de bibliothèques Sony ou de NAND personnelle aux issues ou aux pull requests.
## Installation

Suivre le [guide complet en français](docs/INSTALL_FR.md) ou le [guide en anglais](docs/INSTALL_EN.md).

1. Extraire la partition de données du disque avec Dolphin dans un dossier `DATA`.
2. Placer `DATA` dans le dossier de l'application `PPSA99611`.
3. Copier l'application dans `/data/PPSA99611` par FTP.
4. Envoyer `Outils/droits-kart-ps5.elf` au chargeur d'ELF pour appliquer les droits d'exécution.
5. Configurer la pause de kstuff avec ShadowMountPlus, puis lancer **Kart PS5** depuis l'écran d'accueil.

Le délai utilisé pendant les essais se règle dans `/data/shadowmount/config.ini` :

```ini
kstuff_delay=PPSA99611:5
```

Le guide explique les réglages de bascule automatique et la correction du délai si ShadowMountPlus l'augmente après un plantage.

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
## Organisation de l'application

```text
PPSA99611/
├── eboot.bin
├── DATA/                  Données de votre disque PAL
├── sce_module/libc.prx    Bibliothèque adaptée au firmware
├── sce_sys/               Métadonnées et icône du titre
├── shaders/               Shaders AGC
├── runtime/               Ressources du moteur
├── wii_bootstrap/         Initialisation Wii au premier lancement
└── UserData/
    ├── Config.toml
    ├── NAND/              Sauvegardes à conserver lors des mises à jour
    └── Logs/              Créé pendant l'exécution
```

## Signaler un problème

Préciser le modèle de console, le firmware, la variante installée, le circuit et les étapes pour reproduire le problème. Les journaux sont dans `/data/PPSA99611/UserData/Logs/base_<date>_pid<N>/`. Relire les journaux avant de les partager ; ne pas joindre les données du disque ni toute la NAND.

## Crédits

- [WiiCompiled](https://github.com/patchzyy/Wiicompiled) : recompilation statique et moteur du jeu.
- [ps5link-sdk](https://github.com/Rufidj/ps5link-sdk) : lien PS5 et base AGC.
- [SharpProspero](https://github.com/SvenGDK/SharpProspero) : signature et conteneurs de shaders.
- Les contributeurs de Dolphin : extraction du disque et structure d'initialisation Wii de référence.
- Les contributeurs de kstuff et ShadowMountPlus : environnement de lancement sur console.

## État du dépôt

Les sources PS5 et les modifications des dépendances sont incluses. Voir les [notes de compilation](docs/BUILDING.md), les [notes de version](docs/RELEASE-NOTES.md) et les [licences](THIRD-PARTY-NOTICES.md). La compilation depuis un environnement vierge reste à vérifier.







