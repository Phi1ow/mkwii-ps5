#pragma once
#ifdef __cplusplus
extern "C" {
#endif
int sceSystemServiceGetAppId(const char*);
int sceSystemServiceGetAppTitleId(int, char*);
unsigned sceLncUtilKillApp(unsigned);
#ifdef __cplusplus
}
#endif
/* Close only the exact title compiled into this application's build. Verify
 * the reverse lookup before issuing the service call. This is not libc exit
 * or LoadExec, which both failed with this console's application environment. */
static int mkw_request_title_close(const char* expectedTitle) {
    int app = sceSystemServiceGetAppId(expectedTitle);
    if (app < 0) return app;
    char actual[16] = {0};
    int rc = sceSystemServiceGetAppTitleId(app, actual);
    if (rc < 0) return rc;
    for (unsigned i = 0; i < 10; ++i)
        if (actual[i] != expectedTitle[i]) return -1;
    return (int)sceLncUtilKillApp((unsigned)app);
}
