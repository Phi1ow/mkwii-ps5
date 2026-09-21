/* Modules are loaded now -> the kernel dynlib list contains AGC. Resolve from it. */
extern "C" void __prospero_klog(const char*);
extern "C" void* __prospero_get_payload_args(void);
typedef long (*load_t)(const char* path, void* a, void* b, int c, void* d, void** e);
extern "C" load_t __sp_dlfcn_sce_load_mod;
extern "C" unsigned long __sp_kernel_dynlib_resolve(const char* name, void** out);

static char* put(char* p, const char* s) { while (*s) *p++ = *s++; return p; }
static char* puthex(char* p, unsigned long v, int w)
{ for (int i = 0; i < w; ++i) *p++ = "0123456789abcdef"[(v >> (4 * (w - 1 - i))) & 15]; return p; }
static void line(const char* name, unsigned long a, unsigned long b)
{ char l[200]; char* p = put(l, "[dy] "); p = put(p, name); *p++ = 32;
  p = puthex(p, a, 16); *p++ = 32; p = puthex(p, b, 16); *p++ = 10; *p = 0; __prospero_klog(l); }

extern "C" int main(void)
{
    __prospero_klog("[dy] begin\n");
    load_t ld = __sp_dlfcn_sce_load_mod;
    if (!ld) { __prospero_klog("[dy] no loader\n"); return 1; }
    long h1 = ld("/system/common/lib/libSceAgcDriver.sprx", 0, 0, 0, 0, 0);
    long h2 = ld("/system/common/lib/libSceAgc.sprx", 0, 0, 0, 0, 0);
    line("handles", (unsigned long)h1, (unsigned long)h2);
    const char* syms[3] = { "sceAgcAcbDmaData", "sceAgcDriverAgrSubmitDcb", "sceAgcDriverCreateQueue" };
    for (int i = 0; i < 3; ++i) {
        void* out = 0;
        unsigned long rax = __sp_kernel_dynlib_resolve(syms[i], &out);
        line(syms[i], rax, (unsigned long)out);
    }
    __prospero_klog("[dy] end\n");
    return 0;
}
