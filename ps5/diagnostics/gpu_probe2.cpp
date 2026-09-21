/* Resolve AGC symbols by every available route. */
extern "C" void __prospero_klog(const char*);
extern "C" void* __prospero_get_payload_args(void);
extern "C" int __sp_kernel_dynlib_resolve(const char* name, void** out);

typedef int (*dlsym_t)(int handle, const char* name, void** out);

static char* put(char* p, const char* s) { while (*s) *p++ = *s++; return p; }
static char* puthex(char* p, unsigned long v, int w)
{ for (int i = 0; i < w; ++i) *p++ = "0123456789abcdef"[(v >> (4 * (w - 1 - i))) & 15]; return p; }
static void line(const char* name, unsigned long rc, unsigned long fn)
{ char l[192]; char* p = put(l, "[gpu2] "); p = put(p, name); *p++ = 32;
  p = puthex(p, rc, 8); *p++ = 32; p = puthex(p, fn, 16); *p++ = 10; *p = 0; __prospero_klog(l); }

extern "C" int main(void)
{
    __prospero_klog("[gpu2] begin\n");
    unsigned char* args = (unsigned char*)__prospero_get_payload_args();
    if (!args) return 1;
    dlsym_t dl = 0; __builtin_memcpy(&dl, args + 0, 8);
    const char* nm = "sceAgcAcbDmaData";
    for (int h = 0; h < 3; ++h) {
        int handles[3] = { 1, 0x2001, 0x2002 };
        void* fn = 0;
        int rc = dl ? dl(handles[h], nm, &fn) : -999;
        line("dlsym", (unsigned long)(unsigned)rc | ((unsigned long)(unsigned)handles[h] << 32), (unsigned long)fn);
    }
    void* fn2 = 0;
    int rc2 = __sp_kernel_dynlib_resolve(nm, &fn2);
    line("dynlib_resolve", (unsigned long)(unsigned)rc2, (unsigned long)fn2);
    __prospero_klog("[gpu2] end\n");
    return 0;
}
