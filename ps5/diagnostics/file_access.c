/* Probe of this project's package and mounted view. Read-only by default;
 * MKW_REPAIR_EXECUTE opts into chmod on its two executable files. Stat offsets
 * match SharpProspero/Interop/Kernel/KernelFile.cs. Reserve a full page so a
 * differing firmware structure cannot overrun the local buffer. No game data
 * or unrelated directory is read. */
#ifndef MKW_DIAGNOSTIC_TITLE
#define MKW_DIAGNOSTIC_TITLE "PPSA99504"
#endif
#define PACKAGE_PATH "/data/" MKW_DIAGNOSTIC_TITLE
#define MOUNT_PATH "/system_ex/app/" MKW_DIAGNOSTIC_TITLE
extern int stat(const char*, void*);
extern int sceKernelOpen(const char*, int, unsigned short);
extern long sceKernelRead(int, void*, unsigned long);
extern int sceKernelClose(int);
extern void __prospero_klog(const char*);
#ifdef MKW_REPAIR_EXECUTE
extern int chmod(const char*, unsigned short);
#endif

static char line[512];
static unsigned pos;
static void text(const char* s) { while (*s && pos < sizeof(line) - 1) line[pos++] = *s++; }
static void hex(unsigned long v, unsigned width) {
    const char* digits = "0123456789abcdef";
    for (unsigned i = width; i > 0; --i) {
        if (pos < sizeof(line) - 1) line[pos++] = digits[(v >> ((i - 1) * 4)) & 15];
    }
}
static unsigned long load(const unsigned char* b, unsigned n) {
    unsigned long v = 0;
    for (unsigned i = 0; i < n; ++i) v |= (unsigned long)b[i] << (8 * i);
    return v;
}
static void inspect(const char* path, int is_file) {
    unsigned char status[4096] __attribute__((aligned(16)));
    for (unsigned i = 0; i < sizeof(status); ++i) status[i] = 0;
    int rc = stat(path, status);
    pos = 0; text("[mkw-file-access] "); text(path); text(" stat="); hex((unsigned)rc, 8);
    if (rc == 0) {
        text(" mode=0x"); hex(load(status + 8, 2), 4);
        text(" uid=0x"); hex(load(status + 12, 4), 8);
        text(" size=0x"); hex(load(status + 72, 8), 16);
    }
    if (is_file) {
        int fd = sceKernelOpen(path, 0, 0);
        text(" open="); hex((unsigned)fd, 8);
        if (fd >= 0) {
            unsigned char magic[4];
            long bytes = sceKernelRead(fd, magic, 4);
            text(" read="); hex((unsigned long)bytes, 16);
            if (bytes == 4) { text(" magic="); for (unsigned i = 0; i < 4; ++i) hex(magic[i], 2); }
            sceKernelClose(fd);
        }
    }
    text("\n"); line[pos] = 0; __prospero_klog(line);
}
int main(void) {
    __prospero_klog("[mkw-file-access] begin\n");
#ifdef MKW_REPAIR_EXECUTE
    /* Opt-in repair limited to the two executable files of this test. */
    const char* files[] = {
        PACKAGE_PATH "/eboot.bin", PACKAGE_PATH "/sce_module/libc.prx"
    };
    for (unsigned i = 0; i < 2; ++i) {
        int rc = chmod(files[i], 0755);
        pos = 0; text("[mkw-file-access] chmod "); text(files[i]);
        text(" rc="); hex((unsigned)rc, 8); text("\n");
        line[pos] = 0; __prospero_klog(line);
    }
#endif
    inspect(PACKAGE_PATH, 0);
    inspect(PACKAGE_PATH "/eboot.bin", 1);
    inspect(PACKAGE_PATH "/sce_module/libc.prx", 1);
    inspect(MOUNT_PATH, 0);
    inspect(MOUNT_PATH "/eboot.bin", 1);
    inspect(MOUNT_PATH "/sce_module/libc.prx", 1);
    __prospero_klog("[mkw-file-access] end\n");
    return 0;
}
