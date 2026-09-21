// SPDX-License-Identifier: GPL-3.0-only
#include "SDL_internal.h"
#include "filesystem/SDL_sysfilesystem.h"

// RuntimePlatform supplies explicit paths. SDL's generic platform discovery
// has no PS5 counterpart. Current-directory and file operations are supplied
// separately by SDL's POSIX backend; getcwd reports EOPNOTSUPP on this target.
char* SDL_SYS_GetBasePath(void) { SDL_Unsupported(); return NULL; }
char* SDL_SYS_GetPrefPath(const char* org, const char* app) {
    (void)org; (void)app; SDL_Unsupported(); return NULL;
}
char* SDL_SYS_GetUserFolder(SDL_Folder folder) {
    (void)folder; SDL_Unsupported(); return NULL;
}
