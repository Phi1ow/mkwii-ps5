// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#undef SDL_FILESYSTEM_UNIX
#undef SDL_FILESYSTEM_DUMMY
#undef SDL_FSOPS_DUMMY
#define SDL_FSOPS_POSIX 1
// Capabilities are configured by ps5/sdl/CMakeLists.txt before SDL's checks.
