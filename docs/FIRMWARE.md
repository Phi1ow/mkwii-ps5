# Package variants / Variantes du paquet

Compatible with **PS5 and PS5 Pro** (jailbroken consoles).

The backport archive provides replacement `eboot.bin` and `sce_sys/param.json` files. It is an overlay, not a standalone application: keep the common files from the main package. Select the variant matching your console and provide your own matching signed `sce_module/libc.prx`; Sony libraries are not distributed.

Compatible avec **PS5 et PS5 Pro** jailbreakées.

L'archive de backports fournit les fichiers de remplacement `eboot.bin` et `sce_sys/param.json`. Ce n'est pas une application complète : conserver les fichiers communs du paquet principal. Choisir la variante adaptée à sa console et fournir sa propre bibliothèque signée `sce_module/libc.prx` correspondante ; les bibliothèques Sony ne sont pas distribuées.

No separate disc files or personal saves are included. Embedded code/data remains in the recompiled executable. Aucun fichier disque séparé ni sauvegarde personnelle ne sont inclus ; le binaire recompilé conserve son code et ses données intégrés.
