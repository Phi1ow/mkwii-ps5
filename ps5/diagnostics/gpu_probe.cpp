/* Can a payload reach the AGC (GPU) driver? Resolve the symbols via the loader Dlsym. */
extern "C" void __prospero_klog(const char*);
extern "C" void* __prospero_get_payload_args(void);

typedef int (*dlsym_t)(int handle, const char* name, void** out);

static char* put(char* p, const char* s) { while (*s) *p++ = *s++; return p; }
static char* puthex(char* p, unsigned long v, int w)
{ for (int i = 0; i < w; ++i) *p++ = "0123456789abcdef"[(v >> (4 * (w - 1 - i))) & 15]; return p; }
static void line(const char* name, unsigned long a)
{ char l[176]; char* p = put(l, "[gpu] "); p = put(p, name); *p++ = 32;
  p = puthex(p, a, 16); *p++ = 10; *p = 0; __prospero_klog(l); }

static const char* names[] = {
    "sceAgcDriverCreateQueue",
    "sceAgcDriverAgrSubmitDcb",
    "sceAgcDriverGetResourceName",
    "sceAgcAcbDmaData",
    "sceAgcAcbAcquireMem",
    "sceAgcDriverAgrSubmitMultiDcbs",
};

extern "C" int main(void)
{
    __prospero_klog("[gpu] begin\n");
    unsigned char* args = (unsigned char*)__prospero_get_payload_args();
    if (!args) { __prospero_klog("[gpu] no args\n"); return 1; }
    dlsym_t dl = 0; __builtin_memcpy(&dl, args + 0, 8);
    line("dlsym_ptr", (unsigned long)dl);
    if (!dl) return 2;
    for (int i = 0; i < 6; ++i) {
        void* fn = 0;
        int rc = dl(0x2001, names[i], &fn);
        line(names[i], ((unsigned long)(unsigned)rc << 40) ^ (unsigned long)fn);
    }
    __prospero_klog("[gpu] end\n");
    return 0;
}
