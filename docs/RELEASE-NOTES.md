# Sources only / Sources uniquement

The previous binary archives were withdrawn from the release on 2026-09-21. The executable embeds game-derived DOL/REL sections. The initial no-game-data statement was too broad: absence of a disc image does not mean absence of embedded game bytes.

Les anciennes archives binaires ont été retirées de la release le 21 septembre 2026. L'exécutable intègre des sections DOL/REL extraites du jeu. La mention initiale « aucune donnée du jeu » était trop large : l'absence d'image disque ne garantit pas l'absence de données intégrées.

The repository provides port sources, dependency patches and documentation. Extract your own PAL RMCP01 disc locally; see INSTALL_EN.md or INSTALL_FR.md. Do not upload extracted data, generated game sources, generated blobs or compiled game executables. No Sony runtime libraries or personal saves are offered.

To offer a binary without these raw game sections, the runtime/build must first be adapted to load and validate the necessary sections from the user's local files. That change has not been implemented or tested. The existing firmware variants are not offered for download.
