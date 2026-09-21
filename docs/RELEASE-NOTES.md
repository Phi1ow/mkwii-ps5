# Release preparation

Target repository: `Phi1ow/mkwii-ps5`.

The author identifies the supplied build as the final build adapted for PS5 firmware variants. The base executable has SHA-256:

```text
0ade4a361478121e49b6c9273845c0ed62bbfb5210abbb62d96fbcc0539e1a5e
```

The local candidate archive preserves this executable, shaders, icon, configuration, runtime bootstrap and permissions payload. It omits the supplied player NAND and `libc.prx`. Users must provide their own matching signed runtime library before installing it. This is a candidate, not a complete ready-to-run public release.

Firmware overlays are prepared separately, preserving each supplied executable and metadata but omitting `libc.prx`. The 41 folder names are listed in firmware-variants.json. Only PS5 Pro 9.40 is documented as tested. No console execution was performed during repository preparation.

Before public release, establish source-to-binary correspondence, complete the binary third-party notice bundle and validate installation with a locally supplied runtime library. Repository: https://github.com/Phi1ow/mkwii-ps5. Release assets are local candidates until installation validation is complete.

