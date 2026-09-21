/* Load libSceAgc / libSceAgcDriver, then resolve the DMA primitive on those handles. */
extern "C" void __prospero_klog(const char*);
extern "C" void* __prospero_get_payload_args(void);

typedef int (*dlsym_t)(int handle, const char* name, void** out);
typedef int (*loadmod_t)(const char* path, void* args, void* argp, int flags, void* opt, void** res);
typedef int (*kdlsym_t)(int handle, const char* symbol, void** out);

static char* put(char* p, const char* s) { while (*s) *p++ = *s++; return p; }
static char* puthex(char* p, unsigned long v, int w)
{ for (int i = 0; i < w; ++i) *p++ = "0123456789abcdef"[(v >> (4 * (w - 1 - i))) & 15]; return p; }
static void line(const char* name, unsigned long a, unsigned long b)
{ char l[192]; char* p = put(l, "[agc] "); p = put(p, name); *p++ = 32;
  p = puthex(p, a, 16); *p++ = 32; p = puthex(p, b, 16); *p++ = 10; *p = 0; __prospero_klog(l); }

extern "C" int main(void)
{
    __prospero_klog("[agc] begin\n");
    unsigned char* args = (unsigned char*)__prospero_get_payload_args();
    if (!args) return 1;
    dlsym_t dl = 0; __builtin_memcpy(&dl, args + 0, 8);
    if (!dl) return 2;
    void* lsm = 0; int r1 = dl(0x2001, "sceKernelLoadStartModule", &lsm);
    line("resolve_lsm", (unsigned long)(unsigned)r1, (unsigned long)lsm);
    void* kdl = 0; int r2 = dl(0x2001, "sceKernelDlsym", &kdl);
    line("resolve_kdlsym", (unsigned long)(unsigned)r2, (unsigned long)kdl);
    if (!lsm) { __prospero_klog("[agc] no loadmodule\n"); return 3; }
    loadmod_t load = (loadmod_t)lsm;
    const char* paths[4] = { "libSceAgc.sprx", "libSceAgcDriver.sprx",
                             "/system/common/lib/libSceAgc.sprx",
                             "/system/common/lib/libSceAgcDriver.sprx" };
    for (int i = 0; i < 4; ++i) {
        void* h = 0;
        int rc = load(paths[i], 0, 0, 0, 0, &h);
        line(paths[i], (unsigned long)(unsigned)rc, (unsigned long)h);
        if (h && kdl) {
            kdlsym_t kd = (kdlsym_t)kdl;
            void* fn = 0;
            int r = kd((int)(long)h, "sceAgcAcbDmaData", &fn);
            line("  DmaData", (unsigned long)(unsigned)r, (unsigned long)fn);
        }
    }
    __prospero_klog("[agc] end\n");
    return 0;
}
