#ifndef MKW_DIAGNOSTIC_TITLE
#error "Provide the project title to launch"
#endif
/* Layout and functions from SharpProspero Interop/SystemService and UserService.
 * No check-bypass flags, title removal or changes to another application. */
typedef struct {
    unsigned size; int user;
    unsigned appOpt, padding;
    unsigned long long crashReport;
    unsigned checkFlag, tailPadding;
} LncAppParam;
_Static_assert(sizeof(LncAppParam) == 32, "Launch parameter ABI");
extern int sceUserServiceGetForegroundUser(int*);
extern int sceUserServiceInitialize(void*);
extern int sceUserServiceTerminate(void);
extern int sceLncUtilLaunchApp(const char*, const char* const*, LncAppParam*);
extern void __prospero_klog(const char*);
#ifdef MKW_CLOSE_PROBE
#include "../runtime/title_lifecycle.h"
#endif
static void report(unsigned rc) {
    char value[12] = {'0','x'};
    for (unsigned i=0; i<8; ++i) value[i+2] = "0123456789abcdef"[(rc >> (28-i*4)) & 15];
    value[10]='\n'; value[11]=0; __prospero_klog(value);
}
int main(void) {
#ifdef MKW_CLOSE_PROBE
    __prospero_klog("[mkw-launch] close " MKW_DIAGNOSTIC_TITLE "\n");
    int app = sceSystemServiceGetAppId(MKW_DIAGNOSTIC_TITLE);
    __prospero_klog("[mkw-launch] lookup "); report((unsigned)app);
    int rc = mkw_request_title_close(MKW_DIAGNOSTIC_TITLE);
    __prospero_klog("[mkw-launch] close result "); report((unsigned)rc);
    return rc != 0;
#else
    int user = -1;
    int rc = sceUserServiceInitialize((void*)0);
    if (rc < 0) { __prospero_klog("[mkw-launch] user service initialization failed "); report((unsigned)rc); return 1; }
    rc = sceUserServiceGetForegroundUser(&user);
    if (rc < 0) { __prospero_klog("[mkw-launch] foreground user unavailable "); report((unsigned)rc); sceUserServiceTerminate(); return 1; }
    LncAppParam param = {sizeof(LncAppParam), user, 0, 0, 0, 0, 0};
    __prospero_klog("[mkw-launch] launch " MKW_DIAGNOSTIC_TITLE "\n");
    rc = sceLncUtilLaunchApp(MKW_DIAGNOSTIC_TITLE, (const char* const*)0, &param);
    __prospero_klog("[mkw-launch] result "); report((unsigned)rc);
    sceUserServiceTerminate();
    return rc < 0;
#endif
}
