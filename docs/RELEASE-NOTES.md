# Recompiled application with user-supplied disc files

The release provides the recompiled executables and 41 firmware overlays. Separate disc images, extracted main.dol / StaticR.rel, the game DATA tree, Sony libc.prx and personal NAND/save data are excluded.

The executable retains embedded game-derived code/data from static recompilation. Excluding separate ROM files does not remove that content. The brief binary withdrawal was reversed at the author's request to retain the recompilation and exclude only separate ROM-related files.

Follow INSTALL_FR.md or INSTALL_EN.md to extract your own PAL RMCP01 disc and supply the required files locally. Compatible with PS5 and PS5 Pro, as confirmed by the maintainer. A clean install of the public firmware 9.40 variant reached the menu on a PS5 Pro after adding the missing `PPSA99611/portable.txt` marker. The original `v1.0.0-rc1` archive omitted this file and otherwise stayed on a black screen; the corrected ZIP includes it. Other firmware variants remain untested by this clean-install check; folder names do not prove universal compatibility.
