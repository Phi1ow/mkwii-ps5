// SPDX-License-Identifier: GPL-3.0-only
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <initializer_list>
extern "C" int stat(const char*, void*);
extern "C" int __sp_device_chdir(const char*);
extern "C" int __sp_device_lstat(const char*, void*);
extern "C" int __sp_device_access(const char*, int);
extern "C" void mkw_diagnostic_log(const char*);
namespace {
int inspect(const char* path) {
    alignas(16) unsigned char status[4096]{};
    errno = 0;
    const int result = stat(path, status);
    const int error = errno;
    unsigned mode = status[8] | unsigned(status[9]) << 8;
    char line[240];
    std::snprintf(line, sizeof(line), "[mkw-files] stat path=%s rc=0x%08x errno=%d mode=0x%x\n", path, unsigned(result), error, mode);
    mkw_diagnostic_log(line);
    return result;
}
}
extern "C" int mkw_test_filesystem() {
    mkw_diagnostic_log("[mkw-files] begin real kernel chdir/lstat/access aliases\n");
    inspect(".");
    inspect("/app0");
    errno = 0;
    const int changed = __sp_device_chdir("/app0");
    const int error = errno;
    char line[240];
    std::snprintf(line, sizeof(line), "[mkw-files] native chdir /app0 rc=0x%08x errno=%d\n", unsigned(changed), error);
    mkw_diagnostic_log(line);
    const int relative = inspect(".");
    const int parent = inspect("..");
    const int nested = inspect("sce_sys/param.json");
    alignas(16) unsigned char status[4096]{};
    errno = 0;
    const int lst = __sp_device_lstat("/app0/sce_sys/param.json", status);
    const int lstError = errno;
    errno = 0;
    const int access = __sp_device_access("/app0/sce_sys/param.json", 0);
    const int accessError = errno;
    std::snprintf(line, sizeof(line), "[mkw-files] native lstat rc=0x%08x errno=%d access rc=0x%08x errno=%d\n", unsigned(lst), lstError, unsigned(access), accessError);
    mkw_diagnostic_log(line);
    const bool passed = changed == 0 && relative == 0 && parent == 0 && nested == 0 && lst == 0 && access == 0;
    mkw_diagnostic_log(passed ? "[mkw-files] PASS native cwd initialization, relative stat, lstat and access\n" : "[mkw-files] FAIL native file behavior; inspect individual results\n");
    return passed ? 0 : 1;
}
