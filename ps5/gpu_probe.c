/* SPDX-License-Identifier: GPL-3.0-only
 * First hardware gate for the WiiCompiled PS5 port. This intentionally runs
 * the upstream AGC cube unchanged; it is not the Mario Kart renderer.
 */
_Static_assert(sizeof(void *) == 8 && sizeof(long) == 8,
               "PS5 requires the x86-64 LP64 ABI, not Windows LLP64");
#define main ps5link_gpu_cube_main
#include "../ps5link-sdk/examples/gpu_cube/main.c"
#undef main
#ifdef MKW_PIXEL_SHADER_PROBE
#include "pixel_shader_sb.h"
#endif

#include "runtime/title_lifecycle.h"
#ifndef MKW_PROBE_TITLE
#error "Build must supply the exact project title"
#endif

#ifdef MKW_MEMORY_PROBE
extern int mkw_memory_test(void);
extern int mkw_cpp_runtime_smoke(void);
#ifdef MKW_CPP_ABI_PROBE
extern int mkw_cpp_abi_test(void);
#endif
#ifdef MKW_ENGINE_MEMORY_PROBE
extern int mkw_engine_memory_test(void);
extern int mkw_cxx_library_test(void);
extern int mkw_cpu_features_test(void);
#endif
extern int open(const char*, int, ...);
extern long write(int, const void*, unsigned long);
extern int close(int);
static int diagnostic_fd = -1;
void mkw_diagnostic_log(const char* text) {
    unsigned long length = 0;
    while (text[length]) ++length;
    if (diagnostic_fd >= 0) {
        unsigned long sent = 0;
        while (sent < length) {
            long n = write(diagnostic_fd, text + sent, length - sent);
            if (n <= 0) break;
            sent += (unsigned long)n;
        }
    }
}
#ifdef MKW_PIXEL_SHADER_PROBE
// Validate a newly assembled container after the cube has initialized AGC.
// This checks shader creation and descriptor layout, not textured rendering.
static int validate_pixel_shader(void) {
    ShaderParts parts;
    if (shader_parts_from_elf(pixel_shader_sb, pixel_shader_sb_len, &parts) < 0) return 1;
    DirectMem header, code;
    if (direct_alloc(&header, parts.header_len, DIRECT_MEM_MIN_ALIGN) < 0) return 2;
    if (direct_alloc(&code, parts.code_len, DIRECT_MEM_MIN_ALIGN) < 0) return 3;
    for (unsigned i = 0; i < parts.header_len; ++i) ((unsigned char*)header.ptr)[i] = parts.header[i];
    for (unsigned i = 0; i < parts.code_len; ++i) ((unsigned char*)code.ptr)[i] = parts.code[i];
    void* shader = 0;
    int rc = sceAgcCreateShader(&shader, header.ptr, code.ptr);
    if (rc < 0 || !shader) return 4;
#if defined(MKW_TEV_ARITHMETIC) || defined(MKW_TEV_DIRECT)
    if (resource_dword_offset(shader, KIND_READONLY, 0) != 0 || resource_dword_offset(shader, KIND_SAMPLER, 0) != -1) return 5;
    mkw_diagnostic_log("[mkw-shader] PASS TEV shader creation and buffer binding 0\n");
#else
    if (resource_dword_offset(shader, KIND_READONLY, 0) != 0 || resource_dword_offset(shader, KIND_SAMPLER, 0) != 8) return 5;
    mkw_diagnostic_log("[mkw-shader] PASS custom shader creation and texture/sampler bindings 0/8\n");
#endif
    return 0;
}
#endif
#endif

#ifdef MKW_TEXTURE_DRAW_PROBE
#include "diagnostics/agc_texture.c"
#endif

int main(void) {
#ifdef MKW_MEMORY_PROBE
    diagnostic_fd = open("/app0/memory-result.log", 0x601 /* WRONLY|CREAT|TRUNC */, 0666);
    if (diagnostic_fd < 0) notify("WiiCompiled: memory log open failed");
    int cpp_result = mkw_cpp_runtime_smoke();
    mkw_diagnostic_log(cpp_result ? "[mkw-cpp] FAIL constructor or main-thread TLS\n" : "[mkw-cpp] PASS constructor and main-thread TLS\n");
#ifdef MKW_CPP_ABI_PROBE
    mkw_diagnostic_log("[mkw-cpp-abi] begin exceptions, new/delete and worker TLS\n");
    int abi_result = mkw_cpp_abi_test();
    mkw_diagnostic_log(abi_result ? "[mkw-cpp-abi] FAIL\n" : "[mkw-cpp-abi] PASS exception catch, stack destructors, new/delete and independent worker TLS\n");
    if (abi_result) notify_rc("WiiCompiled C++ ABI failed", abi_result);
#endif
#ifdef MKW_ENGINE_MEMORY_PROBE
    int cpu_result = mkw_cpu_features_test();
    mkw_diagnostic_log(cpu_result ? "[mkw-cpu] FAIL\n" : "[mkw-cpu] PASS all CPU checks\n");
    if (cpu_result) notify_rc("WiiCompiled CPU features failed", cpu_result);
    int library_result = mkw_cxx_library_test();
    mkw_diagnostic_log(library_result ? "[mkw-libcxx] FAIL\n" : "[mkw-libcxx] PASS all library checks\n");
    if (library_result) notify_rc("WiiCompiled C++ library failed", library_result);
    int memory_result = mkw_engine_memory_test();
#else
    int memory_result = mkw_memory_test();
#endif
    if (memory_result) notify_rc("WiiCompiled memory FAIL", memory_result);
    else notify("WiiCompiled memory PASS");
#endif
    #ifdef MKW_PLATFORM_SERVICES_PROBE
    extern int mkw_test_platform_services(void);
    int status = mkw_test_platform_services();
    #elif defined(MKW_FILESYSTEM_PROBE)
    extern int mkw_test_filesystem(void);
    int status = mkw_test_filesystem();
    #elif defined(MKW_SDL_AUDIO)
    extern int mkw_test_sdl_audio(void);
    int status = mkw_test_sdl_audio();
    #elif defined(MKW_SDL_INPUT)
    extern int mkw_test_sdl_input(void);
    int status = mkw_test_sdl_input();
    #elif defined(MKW_NATIVE_INPUT)
    notify("WiiCompiled PS5: native controller diagnostic");
    extern int mkw_test_native_input(void);
    int status = mkw_test_native_input();
    #elif defined(MKW_AURORA_BOOTSTRAP)
    extern int mkw_test_aurora_bootstrap(void);
    int status = mkw_test_aurora_bootstrap();
    #elif defined(MKW_TEXTURE_DRAW_PROBE)
    int status = mkw_agc_texture_probe();
    #else
    int status = ps5link_gpu_cube_main();
    #endif
#if defined(MKW_PIXEL_SHADER_PROBE) && !defined(MKW_AURORA_BOOTSTRAP)
    if (!status) {
        int shader_result = validate_pixel_shader();
        if (shader_result) {
            mkw_diagnostic_log("[mkw-shader] FAIL custom shader creation/bindings\n");
            notify_rc("WiiCompiled custom shader failed", shader_result);
        }
    }
#endif
    /* Both returning from main and libc exit(0) report a fatal termination with
     * the console's libc. Ask the system service to close the application.
     * A full game must flush saves and release resources before this point. */
    if (status) notify_rc("WiiCompiled AGC probe failed", status);
    int rc = mkw_request_title_close(MKW_PROBE_TITLE);
#ifdef MKW_MEMORY_PROBE
    char rc_text[12] = {'0', 'x'};
    for (unsigned i = 0; i < 8; ++i) rc_text[2+i] = "0123456789abcdef"[((unsigned)rc >> (28-i*4)) & 15];
    rc_text[10] = '\n';
    mkw_diagnostic_log("[mkw-close] returned ");
    mkw_diagnostic_log(rc_text);
    if (diagnostic_fd >= 0) close(diagnostic_fd);
    diagnostic_fd = -1;
#endif
    if (rc < 0) notify_rc("WiiCompiled: close request failed", rc);
    for (;;) sceKernelUsleep(100000);
}
