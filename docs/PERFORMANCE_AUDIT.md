# Audit de performance du portage PS5

Audit statique du dépôt (≈ 39 000 lignes : `ps5/`, `patches/`, `dependency-additions/`) complété par des mesures sur PC avec les bancs d'essai déjà présents. Aucune mesure n'a été faite sur console. Chaque piste indique donc le compteur `[mkw-frame-stats]` ou le micro-benchmark qui permet de la confirmer sur PS5 avant de la retenir.

## Résumé

| # | Piste | Zone | Gain attendu | Risque | État |
|---|---|---|---|---|---|
| 1 | Accès mémoire invité toujours « checked » | CPU jeu | Très élevé | Élevé | Piste |
| 2 | Attente GPU complète à chaque `end_frame` | CPU/GPU | Élevé | Moyen | Piste |
| 3 | Données de vertex copiées quatre fois, environ six allocations par draw | CPU rendu | Élevé | Moyen | Piste |
| 4 | Flush de cache avec `CLFLUSH` en série | CPU rendu | Moyen | Faible | **Implémenté** |
| 5 | Registres de contexte réémis à chaque draw | GPU/CPU | Moyen | Moyen | Piste |
| 6 | Une allocation noyau par texture | Textures | Moyen (saccades) | Faible | Piste |
| 7 | Éviction O(n), purge brutale du mémo de hachage | Textures | Faible à moyen (saccades) | Faible | Piste |
| 8 | Détuilage pixel par pixel des copies EFB | Copies EFB | Moyen (courses) | Faible | Piste |
| 9 | Résolution des sources GX sur six banques à chaque appel | CPU rendu | Faible | Faible | Piste |
| 10 | Options de compilation (`-mtune`, LTO, SLP) | Build | Faible à moyen | Faible | Piste |
| 11 | Compteurs de performance atomiques actifs en release | CPU rendu | Faible | Faible | Piste |

Outils à utiliser pour mesurer : la ligne `[mkw-frame-stats]` (build de développement), `tools/compare_frame_stats.py` pour comparer deux exécutions, le fichier-drapeau `UserData/microbench.txt` (`perf_microbench_ps5.cpp`), le profil de fonctions (`Compile-Game.ps1 -FunctionProfile`), `tests/bench_snapshot_segments.cpp` et `Test-SnapshotEquivalence.ps1`.

---

## 1. Accès mémoire invité toujours « checked » (priorité 1)

`patches/Wiicompiled.patch` (≈ ligne 3563) force `GuestFlat::RequiresCheckedAccess()` à `true` sur PS5. La raison : les pages natives font 16 Kio, alors que les politiques Wii (lectures EFB différées, écriture dans du code exécutable) sont suivies à 4 Kio. Conséquence : **chaque lecture et chaque écriture du code PowerPC recompilé** passe par le chemin vérifié `Memory::*` (consultation de tables) au lieu d'un accès direct `base + adresse` suivi d'un `bswap`.

C'est très probablement le premier poste CPU de la logique de jeu.

- **Piste :** conserver l'accès direct et n'utiliser le chemin vérifié que pour les pages de 16 Kio qui contiennent au moins une sous-page de 4 Kio « spéciale ». Deux options :
  - une table de bits par page native, testée en ligne (une lecture et un test par accès) ;
  - une protection mémoire des seules pages spéciales, avec repli dans le gestionnaire de fautes.
- **Mesure :** `microbench.txt` affiche déjà `Memory::Read32 (checked)` et `host bswap load 32`. Leur écart, multiplié par le nombre d'accès par image (profil de fonctions), borne le gain.
- **Risque :** élevé (lectures EFB différées, code auto-modifiant). Il faut valider avec `tests/memory_access_equivalence.cpp` et `tests/guest_flat_ps5_test.cpp`.

## 2. Pas de recouvrement CPU/GPU entre deux images (priorité 1)

À la ligne 224 de `ps5/gpu/gx_frame_renderer.cpp`, `end()` appelle `core->renderer.finish()`, une attente bornée de **tout** le travail GPU de l'image, avant la présentation. Le CPU reste donc inactif pendant que le GPU termine l'image N, et le GPU attend ensuite que le CPU produise l'image N+1.

- **Piste :** remplacer cette attente par `submit_draws()` quand la copie d'affichage a déjà attendu (voir `GxDisplayCopyBackend`). La file GPU ordonne déjà les soumissions : les blits suivent les draws, et `Core::submit` s'appuie sur cet ordre. Le recyclage des lots (`AgcGxDraw::begin_batch` → `wait_until`) protège les ressources. Garder `finish()` pour les relectures CPU et pour `close()`.
- **Mesure :** `fifo-drain` (qui inclut cette attente), `gpu-wait`, `frame`, `interval-p95`.
- **Risque :** moyen. Le présentateur lit la cible, et le `begin_frame` suivant efface l'EFB par blit. Il faut vérifier sur console que ces deux opérations sont ordonnées sur la même file.

## 3. Chaîne de copies des vertex et allocations par draw (priorité 2)

Pour un draw fusionné, les enregistrements de vertex sont copiés quatre fois :

1. du FIFO vers `pending.records` (`ps5/gpu/gx_draw.cpp:124`) ;
2. vers `GxGeometry::records_` (`ps5/gpu/gx_geometry.cpp:141`) ;
3. vers le vecteur produit par `serialize_gx_geometry` (`ps5/gpu/gx_geometry_buffer.cpp`, qui fait en plus un `resize` avec remplissage à zéro avant chaque `memcpy`) ;
4. vers l'arène d'upload GPU (`place()`, `ps5/gpu/agc_gx_draw.cpp:218`).

Les tableaux indexés sont copiés trois fois (`gx_geometry.cpp:180`, puis la sérialisation, puis l'upload). Chaque draw alloue aussi `records_`, un vecteur par attribut indexé, `indices_`, le vecteur sérialisé et le vecteur de matériaux, soit environ six `malloc`/`free`. `microbench.txt` mesure le coût de `malloc` sur console.

Le commentaire de `tests/bench_snapshot_segments.cpp` indique environ 3 ms par image de course pour `snapshot_segments` sur PS5. Sur PC, le même banc donne environ 0,85 µs par groupe, soit environ 0,4 ms pour 430 groupes. L'écart suggère que les allocations et les copies dominent sur console.

- **Piste :** sérialiser directement dans l'arène d'upload du lot ouvert. Il suffit de réserver la taille maximale connue (en-tête + enregistrements + intervalles de tableaux), puis d'y écrire l'en-tête, les enregistrements et les tableaux, ainsi que les indices. `GxGeometry` ne transporterait alors que des vues (spans) et la disposition des données. À défaut, il est possible de recycler les vecteurs (pool par thread) pour supprimer les allocations.
- **Mesure :** `snapshot`, `build-serialize`, `submit`, `snap-record-kb` et `snap-array-kb`. Vérifier l'équivalence des sorties avec `Test-SnapshotEquivalence.ps1`.

## 4. Flush de cache CPU→GPU : `CLFLUSH` en série — **implémenté**

Le code vide le cache ligne par ligne avec `CLFLUSH` sur de gros volumes :

- l'arène d'upload de chaque lot de draws (`agc_gx_draw.cpp:173`, souvent plusieurs Mio par image) ;
- chaque texture envoyée au GPU ;
- chaque effacement de cible couleur ou profondeur ;
- chaque relecture de copie EFB.

`CLFLUSH` s'exécute dans l'ordre, une ligne après l'autre. `CLFLUSHOPT`, disponible sur Zen 2, laisse ces écritures se recouvrir. Le `MFENCE` final conserve la même garantie d'ordre.

- **Changement :** nouveau `ps5/gpu/cpu_cache_flush.h` (`flush_cpu_cache_lines`). Il détecte `CLFLUSHOPT` une seule fois via CPUID et revient à `CLFLUSH` sinon. Il remplace les boucles de `gpu_buffer.cpp`, `gpu_texture.cpp`, `gpu_color_target.cpp`, `gpu_depth_target.cpp` et `gx_copy_readback.cpp`. Les petits flushs de `agc_blit.cpp` et le sondage des labels sont laissés tels quels.
- **Mesure sur PC :** écrire puis vider 4 Kio passe de 13,5 µs à 2,2 µs.
- **Sur console :** `microbench.txt` affiche maintenant `write+clflush 4KiB (dirty)` et `write+clflushopt 4KiB (dirty)`. Vérifier aussi `submit`, `tex-uploads` et `target-create`.
- **Piste suivante :** allouer l'arène d'upload dans un type de mémoire cohérent avec le GPU pour ne plus avoir à vider le cache (le type 12 est utilisé aujourd'hui). **Attention :** le dédoublonnage par `memcmp` (`agc_gx_draw.cpp:225-227`) relit l'arène. Avec une mémoire write-combined, cette relecture deviendrait très lente : il faudrait garder une copie côté CPU pour la comparaison.

## 5. Registres de contexte réémis à chaque draw

Pour chaque draw, `agc_gx_draw.cpp:238` et les lignes suivantes réécrivent environ 100 registres CX : 34 de liaison, les registres du shader, 16 pour la cible couleur, 16 pour la profondeur, puis le viewport, le blend et le cull. Pourtant, seuls le viewport, la profondeur, le blend et le cull changent d'un draw à l'autre dans un même lot.

- **Piste :** émettre la liaison, le shader et les cibles une fois par lot, ou quand la cible change, puis seulement l'état variable à chaque draw. Cela réduit les écritures CPU dans le DCB et le nombre de roulements de contexte côté GPU.
- **Risque :** moyen. Il faut vérifier qu'AGC n'exige pas un contexte complet par `SetCxRegistersIndirect`.

## 6. Une allocation noyau par texture

Chaque texture absente du cache crée un `GpuTexture` : `sceKernelAllocateDirectMemory` et `sceKernelMapDirectMemory` à la création (`gpu_texture.cpp:23`), puis `Munmap` et `ReleaseDirectMemory` à l'éviction. Les appels système coûtent environ 22 µs ou plus sur cette console (mesure citée dans le code).

- **Piste :** sous-allouer les textures dans un tas de mémoire directe réservé une seule fois, par blocs de 64 Kio avec listes libres par taille, et recycler les blocs retirés.
- **Mesure :** `tex-uploads/f` multiplié par le temps d'allocation ; les saccades à l'entrée en course (`frames>34ms`).

## 7. Cache de textures : éviction et mémo

- `gx_texture_cache.cpp:120` : chaque éviction parcourt toute la table (`min_element`). Une liste LRU chaînée rendrait l'éviction O(1).
- `gx_texture_cache.cpp:35` : à 4096 entrées, le mémo de hachage est vidé d'un coup. Toutes les textures suivies sont alors rehachées dans la même image, ce qui peut causer une saccade. Mieux vaut purger les entrées progressivement (LRU ou horloge).
- Les textures non suivies ou `no_cache` sont rehachées en entier (xxh3) à chaque draw. Suivre `tex-digests` contre `tex-digest-skips`.

## 8. Relecture des copies EFB

`gx_copy_readback.cpp:27` copie la surface tuilée pixel par pixel, avec un appel à `layout.pixel_offset(x,y)` et un `memcpy` de 4 octets pour chaque pixel, après avoir vidé le cache de toute la cible.

- **Piste :** détuiler par micro-tuile (base calculée une seule fois, copie de lignes contiguës), ou faire produire une cible linéaire par le GPU (blit) puis la copier d'un bloc.
- **Mesure :** `materialize` et `materializations/f`.

## 9. Résolution des sources mémoire GX

`GxMemorySources::read` (`gx_memory_sources.cpp:14`) parcourt six banques. Pour chacune, il appelle `Memory::Contains` et `Memory::GetPointer` sur **toute** la banque, puis `HostRangeToGuest`. Cela se produit pour chaque attribut indexé et chaque texture, à chaque draw.

- **Piste :** mettre en cache les intervalles hôte des banques, qui sont fixes après l'initialisation, et ne faire qu'une comparaison de pointeurs.

## 10. Options de compilation

- `-march=x86-64-v3` sans `-mtune=znver2` : l'ordonnancement des instructions reste générique. Ajouter `-mtune=znver2` ne change pas la sémantique (la PS5 est une Zen 2). Attention : Zen 2 exécute `PDEP`/`PEXT` en microcode, très lentement. Il faut les éviter dans le code écrit à la main.
- Pas de LTO. Le chemin des draws traverse beaucoup de petites bibliothèques statiques (`mkw_ps5_gx_*`) et des pointeurs de fonction. Un ThinLTO limité au runtime et aux bibliothèques GX, sans les shards traduits, trop volumineux, permettrait de l'inliner.
- Le `-O2` explicite l'emporte sur le `-O3` de `Release`. À essayer : `-O3` pour les bibliothèques GX.
- Les shards traduits sont compilés avec `-fno-slp-vectorize` (`GameObjects.cmake:59`). Sans `-ffast-math`, la vectorisation SLP ne réassocie pas les calculs flottants. Il serait intéressant de mesurer son effet sur l'émulation des paired singles, avec `tests/ppc_vectors_native.cpp` comme garde-fou.

## 11. Instrumentation active en release

Même avec `MKW_PS5_RELEASE`, chaque draw exécute plusieurs `fetch_add` atomiques sur `gx_perf_stats()` (`gx_draw.cpp:89`, `gx_renderer.cpp:22`, `count_snapshot`…). Le coût unitaire est faible, mais il est payé des milliers de fois par image. Dans le paquet joueur, ces compteurs pourraient être compilés comme des opérations vides.

---

## Ordre conseillé

1. Mesurer le correctif 4 sur console (`microbench.txt` et `[mkw-frame-stats]`).
2. Prototyper la piste 2, qui touche peu de code, et mesurer `fifo-drain` et `interval-p95`.
3. Piste 3 : sérialisation directe dans l'arène d'upload, avec `Test-SnapshotEquivalence.ps1` comme filet de sécurité.
4. Piste 1 : le plus gros gain potentiel, mais aussi le plus gros chantier. À faire avec les tests d'équivalence mémoire.
