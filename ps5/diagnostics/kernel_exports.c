/* SPDX-License-Identifier: GPL-3.0-only
 * Read-only lookup of unresolved platform names. No resolved function is called.
 * Use the same read-only module lookup/NID resolver as SharpProspero's CRT.
 */
extern int __sp_kernel_dynlib_handle(int, const char*, int*);
extern void* __sp_kernel_dynlib_dlsym(int, int, const char*);
extern int __sp_kernel_dynlib_obj(int, int, void*);
extern int __sp_kernel_copyout(unsigned long, void*, unsigned long);
extern void __sp_nid_encode(const char*, char*);
extern void __prospero_klog(const char*);
static unsigned char symbols[262144], strings[262144];
static unsigned long symbol_size, string_size;
#ifndef MKW_EXPORT_PID
#define MKW_EXPORT_PID -1
#endif
#if MKW_EXPORT_PID < 0
#define MKW_EXPORT_MODULE "libkernel_sys.sprx"
#else
#define MKW_EXPORT_MODULE "libkernel.sprx"
#endif
static unsigned long load(const unsigned char* data, unsigned size) {
    unsigned long value = 0;
    for (unsigned i = 0; i < size; ++i) value |= (unsigned long)data[i] << (8*i);
    return value;
}
static int read_tables(int handle) {
    unsigned char object[0x180] = {0}, metadata[0x120] = {0};
    if (__sp_kernel_dynlib_obj(MKW_EXPORT_PID, handle, object) < 0) return -1;
    if (__sp_kernel_copyout(load(object + 0x148, 8), metadata, sizeof(metadata)) < 0) return -1;
    symbol_size = load(metadata + 0x30, 8); string_size = load(metadata + 0x40, 8);
    if (!symbol_size || symbol_size > sizeof(symbols) || symbol_size % 24 || !string_size || string_size > sizeof(strings)) return -1;
    if (__sp_kernel_copyout(load(metadata + 0x28, 8), symbols, symbol_size) < 0) return -1;
    return __sp_kernel_copyout(load(metadata + 0x38, 8), strings, string_size);
}
static void print_scopes(const char* name) {
    char nid[12] = {0}; __sp_nid_encode(name, nid);
    for (unsigned long offset = 0; offset < symbol_size; offset += 24) {
        if (!load(symbols + offset + 8, 8)) continue;
        unsigned long index = load(symbols + offset, 4);
        if (index >= string_size || string_size - index < 12) continue;
        unsigned i = 0;
        while (i < 11 && nid[i] == strings[index + i]) ++i;
        if (i != 11) continue;
        char line[160]; unsigned p = 0;
        const char* prefix = "[mkw-kernel-scope] ";
        while (*prefix) line[p++] = *prefix++;
        for (const char* text = name; *text; ++text) line[p++] = *text;
        line[p++] = ' ';
        for (i = 0; i < 48 && index + i < string_size && strings[index + i]; ++i) line[p++] = (char)strings[index + i];
        line[p++] = '\n'; line[p] = 0; __prospero_klog(line);
    }
}
static const char* names[] = {
    "sceKernelDlsym", "stat", "clock_gettime", "mkw_symbol_that_does_not_exist",
    "pthread_getschedparam", "pthread_setschedparam", "pthread_key_delete",
    "pthread_rwlock_init", "pthread_rwlock_destroy", "pthread_rwlock_tryrdlock", "pthread_rwlock_trywrlock",
    "pthread_set_name_np", "pthread_rename_np", "pthread_setcanceltype",
    "getgid", "getegid", "getresuid", "getresgid", "gethostname", "getcwd",
    "setenv", "unsetenv", "wcslcat", "wcslcpy", "wcsnlen",
    "dup", "execvp", "fchmodat", "fdopendir", "openat", "unlinkat", "truncate", "statvfs", "times", "sleep",
    "getpeername", "getsockname", "getaddrinfo", "freeaddrinfo", "recvfrom", "sendto",
    "__inet_addr", "__inet_ntop", "__inet_pton", "arc4random_buf", "_exit", "sigsetjmp",
    "__getcwd", "sceKernelGetcwd", "realpath", "syscall", "__syscall",
    "lstat", "readlink", "chdir", "statfs", "fstatfs", "sceKernelStat", "access", "remove",
    "sceKernelMkdir", "mkdir", "rmdir", "rename", "unlink", "fchmod", "chmod",
    "link", "linkat", "sceKernelLink", "renameat", "renameatx_np", "renameat2"
};
int main(void) {
    __prospero_klog("[mkw-kernel-exports] begin\n");
    int handle = -1;
    if (__sp_kernel_dynlib_handle(MKW_EXPORT_PID, MKW_EXPORT_MODULE, &handle) < 0 || handle < 0) {
        __prospero_klog("[mkw-kernel-exports] FAIL find loaded " MKW_EXPORT_MODULE "\n"); return 1;
    }
    if (!__sp_kernel_dynlib_dlsym(MKW_EXPORT_PID, handle, "sceKernelDlsym")) {
        __prospero_klog("[mkw-kernel-exports] FAIL resolver positive control\n"); return 1;
    }
    __prospero_klog("[mkw-kernel-exports] module=" MKW_EXPORT_MODULE " control=PASS\n");
    if (read_tables(handle) < 0) { __prospero_klog("[mkw-kernel-exports] FAIL reading bounded symbol metadata\n"); return 1; }
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        void* address = __sp_kernel_dynlib_dlsym(MKW_EXPORT_PID, handle, names[i]);
        int rc = address ? 0 : -1;
        char line[160]; unsigned p = 0;
        const char* prefix = "[mkw-kernel-exports] ";
        while (*prefix) line[p++] = *prefix++;
        for (const char* name = names[i]; *name; ++name) line[p++] = *name;
        const char* label = " rc=";
        while (*label) line[p++] = *label++;
        for (unsigned b = 0; b < 8; ++b) line[p++] = "0123456789abcdef"[((unsigned)rc >> (28 - 4*b)) & 15];
        label = address ? " address=present\n" : " address=absent\n";
        while (*label) line[p++] = *label++;
        line[p] = 0; __prospero_klog(line);
        if (address) print_scopes(names[i]);
    }
    __prospero_klog("[mkw-kernel-exports] end\n");
    return 0;
}
