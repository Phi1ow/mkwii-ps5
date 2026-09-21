# Mario Kart Wii pour PS5

[English](README.md) · **Français**

Portage natif de **Mario Kart Wii PAL (RMCP01)** sur PlayStation 5, basé sur [WiiCompiled](https://github.com/patchzyy/Wiicompiled).

- Lancement depuis l'écran d'accueil sous le nom **Kart PS5**.
- Recompilation statique du jeu Wii et rendu sur le GPU PS5 avec AGC.
- Commandes DualSense correspondant à une manette GameCube.

> Il faut fournir les données de sa propre copie de Mario Kart Wii PAL. Cet exécutable ne prend pas en charge les autres régions.
>
> La documentation de la version fournie indique des essais sur **PS5 Pro, firmware 9.40**. La PS5 standard et les autres firmwares ne sont pas confirmés.

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
- La PS5 standard n'a pas été testée selon la documentation fournie.
- La présence d'un paquet par firmware ne confirme pas son fonctionnement : voir les [notes sur les firmwares](docs/FIRMWARE.md).

## Prérequis

- Une PS5 jailbreakée, un serveur FTP et un chargeur d'ELF.
- kstuff ; la version indiquée dans les essais est EchoStretch v1.6.7.
- ShadowMountPlus 1.6beta16 ; les versions 1.7alpha essayées ont refusé les informations du titre.
- Une extraction personnelle de Mario Kart Wii PAL contenant `DATA/sys/main.dol` et `DATA/files/rel/StaticR.rel`.
- Environ 3 Go disponibles sur la console.

## Téléchargement

[Télécharger Kart PS5 et les variantes de firmware](https://github.com/Phi1ow/mkwii-ps5/releases/tag/v1.0.0-rc1).

Cette distribution est en préversion : le paquet nettoyé n'a pas été retesté sur console. Ajouter sa propre bibliothèque signée **libc.prx**, adaptée au firmware, dans **PPSA99611/sce_module/libc.prx** avant de suivre le guide. Les données du jeu et les sauvegardes personnelles ne sont pas incluses. Les variantes contiennent uniquement l'exécutable et les métadonnées de remplacement.

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

## Commandes

| DualSense | GameCube |
| --- | --- |
| Joystick gauche | Stick |
| Croix directionnelle | Croix directionnelle |
| Croix | A |
| Rond | B |
| Carré | X |
| Triangle | Y |
| R1 | Z |
| L2 / R2 | L / R |
| Options | Start |

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



