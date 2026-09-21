/* Removes the home-screen registration of this project's test titles
 * (sceAppInstUtilAppUnInstall), one klog line per title with the registration
 * state before and after. The title list is compiled in by
 * Build-TitleUninstall.ps1, which only accepts project test identifiers.
 * This payload deletes no file itself; ShadowMount titles keep their /data
 * source folder (checked on PPSA99612 before any batch, PORTAGE.md §83). */
#ifndef MKW_UNINSTALL_TITLES
#error "Provide the comma-separated title list"
#endif
extern int sceAppInstUtilInitialize(void);
extern int sceAppInstUtilTerminate(void);
extern int sceAppInstUtilAppExists(const char*, int*);
extern int sceAppInstUtilAppUnInstall(const char*);
extern void __prospero_klog(const char*);

static char line[160];
static unsigned pos;
static void text(const char* s) { while (*s && pos < sizeof(line) - 2) line[pos++] = *s++; }
static void hex(unsigned v) {
    text("0x");
    for (unsigned i = 0; i < 8; ++i) line[pos < sizeof(line) - 2 ? pos++ : pos] = "0123456789abcdef"[(v >> (28 - i * 4)) & 15];
}
static void flush(void) { line[pos++] = '\n'; line[pos] = 0; __prospero_klog(line); pos = 0; }
// The service writes a single byte (SharpProspero PackageInstaller.AppExists).
static int exists(const char* title, int* state) {
    unsigned char flag[8] = {0};
    int rc = sceAppInstUtilAppExists(title, (int*)(void*)flag);
    *state = rc < 0 ? -1 : flag[0];
    return rc;
}

int main(void) {
    const char* list = MKW_UNINSTALL_TITLES;
    __prospero_klog("[mkw-uninstall] initializing AppInstUtil\n");
    int rc = sceAppInstUtilInitialize();
    text("[mkw-uninstall] begin init="); hex((unsigned)rc); flush();
    if (rc < 0) return 1;
    char title[16];
    unsigned failures = 0;
    for (const char* p = list; *p;) {
        unsigned n = 0;
        while (*p && *p != ',' && n < sizeof(title) - 1) title[n++] = *p++;
        title[n] = 0;
        if (*p == ',') ++p;
        if (n != 9) { text("[mkw-uninstall] skipped malformed entry"); flush(); ++failures; continue; }
        int before = -1, after = -1;
        int erc = exists(title, &before);
#ifdef MKW_CHECK_ONLY
        int urc = 0;  // report the registration state only
#else
        int urc = before > 0 ? sceAppInstUtilAppUnInstall(title) : 0;
#endif
        int arc = exists(title, &after);
        text("[mkw-uninstall] "); text(title);
        text(" exists-rc="); hex((unsigned)erc); text(" before="); hex((unsigned)before);
        text(" uninstall="); hex((unsigned)urc);
        text(" exists-rc="); hex((unsigned)arc); text(" after="); hex((unsigned)after);
        flush();
#ifndef MKW_CHECK_ONLY
        failures += urc < 0 || after > 0;
#endif
    }
    sceAppInstUtilTerminate();
    text("[mkw-uninstall] end failures="); hex(failures); flush();
    return failures != 0;
}
