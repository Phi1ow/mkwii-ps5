/* SPDX-License-Identifier: GPL-3.0-only
 * Make one run's WiiCompiled log folder readable over FTP.
 *
 * The game creates Logs/<run> with its own application credentials. After a
 * console restart the FTP payload can run under different credentials and then
 * gets 550/451 on those files even once the game has exited. This payload runs
 * through the ELF loader, like file_access.c, and only relaxes the modes of the
 * named run folder and its three known log files. Nothing is read, moved or
 * deleted, and no other path is touched.
 */
#ifndef MKW_DIAGNOSTIC_TITLE
#error "Provide the project title"
#endif
#ifndef MKW_LOG_RUN
#error "Provide the run folder name, e.g. base_1789476011_pid109"
#endif
#define RUN_PATH "/data/" MKW_DIAGNOSTIC_TITLE "/UserData/Logs/" MKW_LOG_RUN

extern int chmod(const char*, unsigned short);
extern int stat(const char*, void*);
extern void __prospero_klog(const char*);

static char line[512];
static unsigned pos;
static void text(const char* s) { while (*s && pos < sizeof(line) - 1) line[pos++] = *s++; }
static void hex(unsigned long v, unsigned width) {
    const char* digits = "0123456789abcdef";
    for (unsigned i = width; i > 0; --i)
        if (pos < sizeof(line) - 1) line[pos++] = digits[(v >> ((i - 1) * 4)) & 15];
}
static unsigned load16(const unsigned char* b) { return (unsigned)b[0] | ((unsigned)b[1] << 8); }

static void relax(const char* path, unsigned short mode) {
    unsigned char status[4096] __attribute__((aligned(16)));
    for (unsigned i = 0; i < sizeof(status); ++i) status[i] = 0;
    const int present = stat(path, status);
    int rc = -1;
    if (present == 0) rc = chmod(path, mode);
    unsigned after = 0;
    if (present == 0 && stat(path, status) == 0) after = load16(status + 8);
    pos = 0;
    text("[mkw-log-access] "); text(path);
    text(" stat="); hex((unsigned)present, 8);
    text(" chmod="); hex((unsigned)rc, 8);
    text(" mode=0x"); hex(after, 4);
    text("\n"); line[pos] = 0; __prospero_klog(line);
}

int main(void) {
    __prospero_klog("[mkw-log-access] begin " RUN_PATH "\n");
    relax(RUN_PATH, 0777);
    relax(RUN_PATH "/console.log", 0666);
    relax(RUN_PATH "/stderr.log", 0666);
    relax(RUN_PATH "/crash_exception.txt", 0666);
    __prospero_klog("[mkw-log-access] end\n");
    return 0;
}
