# Firmware variants / Variantes de firmware

The supplied package contains 41 folders under `Backport/`, enumerated in [firmware-variants.json](firmware-variants.json). Each holds an `eboot.bin`, `sce_module/libc.prx` and `sce_sys/param.json`; it is an overlay, not a complete application.

Le paquet fourni contient 41 dossiers dans `Backport/`, listés dans [firmware-variants.json](firmware-variants.json). Chacun contient `eboot.bin`, `sce_module/libc.prx` et `sce_sys/param.json` : il s'agit de fichiers de remplacement, pas d'une application complète.

Only PS5 Pro firmware 9.40 is documented as tested. Folder names and patched version metadata do not demonstrate compatibility. Preserve matching executable, runtime and metadata together; installation instructions for these variants still need validation.

Seule la PS5 Pro en firmware 9.40 est documentée comme testée. Le nom du dossier et la modification des métadonnées de version ne démontrent pas la compatibilité. Conserver ensemble l'exécutable, la bibliothèque et les métadonnées correspondantes ; la procédure d'installation de ces variantes reste à valider.
