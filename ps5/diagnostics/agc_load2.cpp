/* The CRT already resolved sceKernelLoadStartModule: __sp_dlfcn_sce_load_mod. */
extern "C" void __prospero_klog(const char*);
extern "C" long __sp_dlfcn_sce_load_mod(const char* path, void* a, void* b, int c, void* d, void** e);
extern "C" void* __prospero_get_payload_args(void);

typedef int (*dlsym_t)(int handle, const char* name, void** out);

static char* put(char* p, const char* s) { while (*s) *p++ = *s++; return p; }
static char* puthex(char* p, unsigned long v, int w)
{ for (int i = 0; i < w; ++i) *p++ = "0123456789abcdef"[(v >> (4 * (w - 1 - i))) & 15]; return p; }
static void line(const char* name, unsigned long a)
{ char l[192]; char* p = put(l, "[ld] "); p = put(p, name); *p++ = 32;
  p = puthex(p, a, 16); *p++ = 10; *p = 0; __prospero_klog(l); }

extern "C" int main(void)
{
    __prospero_klog("[ld] begin\n");
    line("load_mod_ptr", (unsigned long)__sp_dlfcn_sce_load_mod);
    const char* libs[4] = {
        "/system/common/lib/libSceAgc.sprx",
        "/system/common/lib/libSceAgcDriver.sprx",
        "libSceAgc.sprx",
        "libSceAgcDriver.sprx" };
    unsigned char* args = (unsigned char*)__prospero_get_payload_args();
    dlsym_t dl = 0; if (args) __builtin_memcpy(&dl, args + 0, 8);
    for (int i = 0; i < 4; ++i) {
        long h = __sp_dlfcn_sce_load_mod(libs[i], 0, 0, 0, 0, 0);
        line(libs[i], (unsigned long)h);
        if (h > 0 && dl) {
            void* fn = 0;
            int rc = dl((int)h, "sceAgcAcbDmaData", &fn);
            line("  dlsym DmaData", ((unsigned long)(unsigned)rc << 32) | (unsigned long)fn);
        }
    }
    __prospero_klog("[ld] end\n");
    return 0;
}
