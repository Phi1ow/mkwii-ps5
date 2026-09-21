// SPDX-License-Identifier: GPL-3.0-only
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <new>
#include <string>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace {
// PS5 application file APIs require absolute paths. The private lstat/chdir
// exports return EPERM. Validate directory entries before using native stat;
// refuse symbolic links rather than falsely treating their targets as links.
// This is a pathname walk, with the same rename race limitation as libc++'s
// portable remove_all implementation on platforms without openat support.
int entry_type(const std::string& parent, const std::string& name, unsigned& type) {
    struct Owner { DIR* value; ~Owner() { if (value) closedir(value); } } dir{opendir(parent.c_str())};
    if (!dir.value) return errno ? errno : EIO;
    errno = 0;
    while (const dirent* entry = readdir(dir.value)) {
        if (name == entry->d_name) { type = entry->d_type; return 0; }
    }
    return errno ? errno : ENOENT;
}
int checked_absolute(const char* input, std::string& result) {
    if (!input) return EINVAL;
    if (!*input) return ENOENT;
    if (*input != '/') return EOPNOTSUPP;
    if (std::strlen(input) >= PATH_MAX) return ENAMETOOLONG;
    result = "/";
    unsigned type = DT_DIR;
    const char* cursor = input;
    while (*cursor) {
        if (type != DT_DIR) return ENOTDIR;
        while (*cursor == '/') ++cursor;
        if (!*cursor) break;
        const char* end = std::strchr(cursor, '/');
        if (!end) end = cursor + std::strlen(cursor);
        std::string component(cursor, end);
        cursor = end;
        if (component == ".") continue;
        if (component == "..") {
            const auto separator = result.find_last_of('/');
            result.resize(separator == 0 ? 1 : separator);
            continue;
        }
        const int error = entry_type(result, component, type);
        if (error) return error;
        if (type == DT_LNK || type == DT_UNKNOWN) return EOPNOTSUPP;
        if (result.size() != 1) result += '/';
        result += component;
    }
    return 0;
}
}
extern "C" {
char* getcwd(char*, size_t) {
    // Runtime roots are explicit (/app0 and its configured data directory).
    // Never invent a process cwd for code that accidentally requests one.
    errno = EOPNOTSUPP;
    return nullptr;
}
int lstat(const char* path, struct stat* output) {
    if (!output) { errno = EFAULT; return -1; }
    try {
        std::string absolute;
        const int error = checked_absolute(path, absolute);
        if (error) { errno = error; return -1; }
        return stat(absolute.c_str(), output);
    } catch (const std::bad_alloc&) { errno = ENOMEM; return -1; }
      catch (...) { errno = EIO; return -1; }
}
char* realpath(const char* path, char* resolved) {
    try {
        std::string absolute;
        const int error = checked_absolute(path, absolute);
        if (error) { errno = error; return nullptr; }
        struct stat status{};
        if (stat(absolute.c_str(), &status) != 0) return nullptr;
        if (!resolved) {
            resolved = static_cast<char*>(std::malloc(absolute.size() + 1));
            if (!resolved) { errno = ENOMEM; return nullptr; }
        }
        std::memcpy(resolved, absolute.c_str(), absolute.size() + 1);
        return resolved;
    } catch (const std::bad_alloc&) { errno = ENOMEM; return nullptr; }
      catch (...) { errno = EIO; return nullptr; }
}
int fchmodat(int directory, const char* path, mode_t mode, int flags) {
    if (flags & ~AT_SYMLINK_NOFOLLOW) { errno = EINVAL; return -1; }
    if (!path || !*path) { errno = path ? ENOENT : EINVAL; return -1; }
    if (directory != AT_FDCWD || *path != '/' || flags) { errno = EOPNOTSUPP; return -1; }
    return chmod(path, mode);
}
int statvfs(const char*, struct statvfs*) {
    // No application export supplies capacity information. std::filesystem
    // must return an error, never a fabricated amount of free save space.
    errno = EOPNOTSUPP;
    return -1;
}
}
