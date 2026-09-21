/* __sp_dlfcn_sce_load_mod is a BSS SLOT holding the resolved pointer. Declare it as a variable. */
extern "C" void __prospero_klog(const char*);
extern "C" void* __prospero_get_payload_args(void);
typedef long (*load_t)(const char* path, void* a, void* b, int c, void* d, void** e);
extern "C" load_t __sp_dlfcn_sce_load_mod;

typedef int (*dlsym_t)(int handle, const char* name, void** out);

static char* put(char* p, const char* s) { while (*s) *p++ = *s++; return p; }
static char* puthex(char* p, unsigned long v, int w)
{ for (int i = 0; i < w; ++i) *p++ = "0123456789abcdef"[(v >> (4 * (w - 1 - i))) & 15]; return p; }
static void line(const char* name, unsigned long a)
{ char l[192]; char* p = put(l, "[ld2] "); p = put(p, name); *p++ = 32;
  p = puthex(p, a, 16); *p++ = 10; *p = 0; __prospero_klog(l); }

extern "C" int main(void)
{
    __prospero_klog("[ld2] begin\n");
    load_t ld = __sp_dlfcn_sce_load_mod;
    line("slot_addr", (unsigned long)&__sp_dlfcn_sce_load_mod);
    line("fn_ptr", (unsigned long)ld);
    if (!ld) { __prospero_klog("[ld2] null\n"); return 1; }
    unsigned char* args = (unsigned char*)__prospero_get_payload_args();
    dlsym_t dl = 0; if (args) __builtin_memcpy(&dl, args + 0, 8);
    const char* libs[2] = { "/system/common/lib/libSceAgcDriver.sprx",
                            "/system/common/lib/libSceAgc.sprx" };
    for (int i = 0; i < 2; ++i) {
        long h = 0;
        long rc = ld(libs[i], 0, 0, 0, 0, (void**)&h);
        line(libs[i], (unsigned long)rc);
        line("  handle", (unsigned long)h);
        if (dl) { void* fn = 0; int r = dl((int)h, "sceAgcAcbDmaData", &fn);
                  line("  dlsym", ((unsigned long)(unsigned)r << 40) ^ (unsigned long)fn); }
    }
    __prospero_klog("[ld2] end\n");
    return 0;
}
