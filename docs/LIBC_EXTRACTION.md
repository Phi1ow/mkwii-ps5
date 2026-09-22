# Getting your own `libc.prx`

**English** · [Français](#obtenir-son-propre-libcprx)

Kart PS5 needs a **signed** `libc.prx` at `PPSA99611/sce_module/libc.prx`.

The system does not provide `libc.prx` to applications: every title ships its own copy in its `sce_module/` folder. It belongs to Sony, so it is **not distributed** here, in the releases or in the firmware overlays. You take it from a game you already own.

> Never commit it, attach it to an issue or pull request, or upload it anywhere. `*.prx` is already in `.gitignore`.

This is the same requirement as in [ps5link-sdk](https://github.com/Rufidj/ps5link-sdk#packaging-and-installing) and [sm64-ps5](https://github.com/Rufidj/sm64-ps5), which this port builds on.

## In short

1. Pick an installed game whose `sce_module/` folder contains `libc.prx`, made for **your firmware or an older one**.
2. Copy that `libc.prx` to your PC over FTP.
3. Check that it is signed (first bytes `54 14 F5 EE` or `4F 15 3D 1D`). If it is a plain ELF (`7F 45 4C 46`), sign it first.
4. Put it at `PPSA99611/sce_module/libc.prx`, copy the package to `/data/PPSA99611`, and send `Outils/droits-kart-ps5.elf`.

## 1. Choose the source

Take the file from the `sce_module/` folder of a **game dump** on your console:

- The module must match your firmware **or come from an older one**. A module from a newer firmware is rejected.
- A file you copy off a *running* title is a decrypted ELF, and the console rejects it as it is (see step 3).
- Any game that contains `sce_module/libc.prx` works as a source. This project has not tested which titles are best.

## 2. Copy it to your PC

Start an FTP server on the console (the port depends on your payload) and browse it with WinSCP, FileZilla or `curl`:

```powershell
curl.exe ftp://<console-ip>:<ftp-port>/data/
```

Installed titles are folders such as `/data/<TITLE>-app/` or `/data/<TITLE>/`. Download the file:

```powershell
curl.exe --fail --connect-timeout 4 --max-time 30 `
  "ftp://<console-ip>:<ftp-port>/data/<TITLE>-app/sce_module/LIBC.PRX" `
  -o libc.prx
```

Building from source? `ps5/Get-ConsoleLibc.ps1` does the same download and inspects the module. Its default `-RemotePath` points at a title on the author's console, so **always pass your own**:

```powershell
./ps5/Get-ConsoleLibc.ps1 -ConsoleIp <console-ip> -FtpPort <ftp-port> -RemotePath '/data/<TITLE>-app/sce_module/LIBC.PRX'
```

It writes `.tools/console/libc.prx` and `.tools/console/libc-source.json`, and needs the SharpProspero tools and .NET SDK from [BUILDING.md](BUILDING.md).

## 3. Check it, and sign it if needed

```powershell
[BitConverter]::ToString([IO.File]::ReadAllBytes('libc.prx'), 0, 4)
```

| First bytes | Meaning | What to do |
| --- | --- | --- |
| `54-14-F5-EE` or `4F-15-3D-1D` | Signed container | Use it as it is |
| `7F-45-4C-46` | Plain, unsigned ELF | The console rejects it (`sceSblAuthMgrAuthHeader:readHeader ... invalid state`). Sign it, below |
| anything else | Wrong file or truncated download | Download again, or pick another game |

To sign a plain ELF, use SharpProspero's `self` command (needs the .NET SDK and a SharpProspero checkout, see [BUILDING.md](BUILDING.md)):

```powershell
$gen = "SharpProspero/tools/SharpProspero.Bindings.Generator/SharpProspero.Bindings.Generator.csproj"
dotnet run --project $gen -c Release -- self --sign --in libc-plain.prx --out libc.prx
dotnet run --project $gen -c Release -- self --inspect --file libc.prx
```

This is what `ps5/Build-GpuProbe.ps1` runs. Note the hash to know later which file a package contains:

```powershell
Get-FileHash libc.prx -Algorithm SHA256
```

## 4. Install it

```text
PPSA99611/
├── eboot.bin
├── DATA/
├── sce_module/
│   └── libc.prx        <- your file
└── sce_sys/
```

For a firmware overlay (`Backport/FW_x.xx`), only `eboot.bin` and `sce_sys/param.json` are replaced; `libc.prx` is still yours ([FIRMWARE.md](FIRMWARE.md)).

Copy the package to `/data/PPSA99611`, then send `Outils/droits-kart-ps5.elf` to the ELF loader. It makes `eboot.bin` and `sce_module/libc.prx` executable. Full steps: [INSTALL_EN.md](INSTALL_EN.md).

## Troubleshooting

| Symptom | Likely cause | Fix |
| --- | --- | --- |
| Error **CE-107750-0** at launch | `eboot.bin` / `libc.prx` not executable | Send `Outils/droits-kart-ps5.elf` again |
| Kernel log: `sceSblAuthMgrAuthHeader:readHeader ... invalid state` | `libc.prx` is an unsigned ELF | Sign it (step 3) |
| Kernel log: `Lack of a .prx file in /app0/sce_module is detected!!!` | `libc.prx` missing or in the wrong folder | Check the path `PPSA99611/sce_module/libc.prx` |
| Rejected or crashes at start | Module from a newer firmware than yours | Use one from your firmware or an older one |

**Reading the kernel log:** send klogsrv with the ELF loader, then run `nc <console-ip> 3232` before launching the game. The messages are literal and worth reading in full.

Compatibility of a given `libc.prx` with a given firmware is not verified by this project (`Get-ConsoleLibc.ps1` records `firmwareCompatibilityVerified = false`).

## Sources

- [ps5link-sdk: Packaging and installing / Debugging](https://github.com/Rufidj/ps5link-sdk)
- [sm64-ps5](https://github.com/Rufidj/sm64-ps5): same `libc.prx` requirement
- [SharpProspero](https://github.com/SvenGDK/SharpProspero): the `self` sign / inspect command

---

# Obtenir son propre `libc.prx`

Kart PS5 a besoin d'un `libc.prx` **signé** à l'emplacement `PPSA99611/sce_module/libc.prx`.

Le système ne fournit pas `libc.prx` aux applications : chaque titre embarque sa propre copie dans son dossier `sce_module/`. Ce fichier appartient à Sony : il n'est **pas distribué** ici, ni dans les releases, ni dans les overlays de firmware. On le récupère depuis un jeu qu'on possède déjà.

> Ne le commitez jamais, ne le joignez pas à une issue ou une pull request, ne le publiez nulle part. `*.prx` est déjà dans `.gitignore`.

C'est la même exigence que dans [ps5link-sdk](https://github.com/Rufidj/ps5link-sdk#packaging-and-installing) et [sm64-ps5](https://github.com/Rufidj/sm64-ps5), sur lesquels ce portage s'appuie.

## En bref

1. Choisir un jeu installé dont le dossier `sce_module/` contient `libc.prx`, conçu pour **votre firmware ou un plus ancien**.
2. Copier ce `libc.prx` sur le PC par FTP.
3. Vérifier qu'il est signé (premiers octets `54 14 F5 EE` ou `4F 15 3D 1D`). Si c'est un ELF brut (`7F 45 4C 46`), le signer d'abord.
4. Le placer dans `PPSA99611/sce_module/libc.prx`, copier le paquet vers `/data/PPSA99611` et envoyer `Outils/droits-kart-ps5.elf`.

## 1. Choisir la source

Prenez le fichier dans le dossier `sce_module/` d'un **dump de jeu** de votre console :

- Le module doit correspondre à votre firmware **ou venir d'un plus ancien**. Un module d'un firmware plus récent est rejeté.
- Un fichier copié depuis un titre *en cours d'exécution* est un ELF déchiffré, que la console rejette tel quel (voir étape 3).
- Tout jeu contenant `sce_module/libc.prx` convient comme source. Ce projet n'a pas testé quels titres sont les meilleurs.

## 2. Le copier sur le PC

Lancez un serveur FTP sur la console (le port dépend de votre payload) et parcourez-la avec WinSCP, FileZilla ou `curl` :

```powershell
curl.exe ftp://<ip-console>:<port-ftp>/data/
```

Les titres installés sont des dossiers comme `/data/<TITRE>-app/` ou `/data/<TITRE>/`. Téléchargez le fichier :

```powershell
curl.exe --fail --connect-timeout 4 --max-time 30 `
  "ftp://<ip-console>:<port-ftp>/data/<TITRE>-app/sce_module/LIBC.PRX" `
  -o libc.prx
```

Vous compilez depuis les sources ? `ps5/Get-ConsoleLibc.ps1` fait le même téléchargement et inspecte le module. Son `-RemotePath` par défaut vise un titre de la console de l'auteur : **donnez toujours le vôtre**.

```powershell
./ps5/Get-ConsoleLibc.ps1 -ConsoleIp <ip-console> -FtpPort <port-ftp> -RemotePath '/data/<TITRE>-app/sce_module/LIBC.PRX'
```

Il écrit `.tools/console/libc.prx` et `.tools/console/libc-source.json`, et nécessite les outils SharpProspero et le SDK .NET décrits dans [BUILDING.md](BUILDING.md).

## 3. Vérifier, et signer si besoin

```powershell
[BitConverter]::ToString([IO.File]::ReadAllBytes('libc.prx'), 0, 4)
```

| Premiers octets | Signification | Action |
| --- | --- | --- |
| `54-14-F5-EE` ou `4F-15-3D-1D` | Conteneur signé | Utilisable tel quel |
| `7F-45-4C-46` | ELF brut, non signé | La console le rejette (`sceSblAuthMgrAuthHeader:readHeader ... invalid state`). Le signer, ci-dessous |
| autre chose | Mauvais fichier ou téléchargement tronqué | Retélécharger, ou choisir un autre jeu |

Pour signer un ELF brut, utilisez la commande `self` de SharpProspero (SDK .NET et copie de SharpProspero nécessaires, voir [BUILDING.md](BUILDING.md)) :

```powershell
$gen = "SharpProspero/tools/SharpProspero.Bindings.Generator/SharpProspero.Bindings.Generator.csproj"
dotnet run --project $gen -c Release -- self --sign --in libc-brut.prx --out libc.prx
dotnet run --project $gen -c Release -- self --inspect --file libc.prx
```

C'est ce que lance `ps5/Build-GpuProbe.ps1`. Notez l'empreinte pour savoir plus tard quel fichier contient un paquet :

```powershell
Get-FileHash libc.prx -Algorithm SHA256
```

## 4. L'installer

```text
PPSA99611/
├── eboot.bin
├── DATA/
├── sce_module/
│   └── libc.prx        <- votre fichier
└── sce_sys/
```

Pour un overlay de firmware (`Backport/FW_x.xx`), seuls `eboot.bin` et `sce_sys/param.json` sont remplacés ; `libc.prx` reste le vôtre ([FIRMWARE.md](FIRMWARE.md)).

Copiez le paquet vers `/data/PPSA99611`, puis envoyez `Outils/droits-kart-ps5.elf` au chargeur d'ELF. Il rend `eboot.bin` et `sce_module/libc.prx` exécutables. Étapes complètes : [INSTALL_FR.md](INSTALL_FR.md).

## Dépannage

| Symptôme | Cause probable | Solution |
| --- | --- | --- |
| Erreur **CE-107750-0** au lancement | `eboot.bin` / `libc.prx` non exécutables | Renvoyer `Outils/droits-kart-ps5.elf` |
| Journal noyau : `sceSblAuthMgrAuthHeader:readHeader ... invalid state` | `libc.prx` est un ELF non signé | Le signer (étape 3) |
| Journal noyau : `Lack of a .prx file in /app0/sce_module is detected!!!` | `libc.prx` absent ou mal placé | Vérifier le chemin `PPSA99611/sce_module/libc.prx` |
| Refusé ou plantage au démarrage | Module d'un firmware plus récent que le vôtre | En prendre un de votre firmware ou plus ancien |

**Lire le journal du noyau :** envoyez klogsrv avec le chargeur d'ELF, puis lancez `nc <ip-console> 3232` avant de démarrer le jeu. Les messages sont explicites et méritent d'être lus en entier.

La compatibilité d'un `libc.prx` donné avec un firmware donné n'est pas vérifiée par ce projet (`Get-ConsoleLibc.ps1` note `firmwareCompatibilityVerified = false`).

## Sources

- [ps5link-sdk : Packaging and installing / Debugging](https://github.com/Rufidj/ps5link-sdk)
- [sm64-ps5](https://github.com/Rufidj/sm64-ps5) : même exigence pour `libc.prx`
- [SharpProspero](https://github.com/SvenGDK/SharpProspero) : commande `self` de signature et d'inspection
