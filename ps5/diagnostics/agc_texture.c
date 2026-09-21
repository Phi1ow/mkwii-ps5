/* SPDX-License-Identifier: GPL-3.0-only
 * Derived from ps5link-sdk/examples/gpu_cube/main.c (see sources.lock.json).
 * First single-texture AGC draw; no GX/TEV rendering is claimed.
 * Shared helpers and built-in VS are included by gpu_probe.c.
 */
#include "texture_fixture.h"
#if defined(MKW_GX_FILTERED_COPY) || defined(MKW_GX_DISPLAY_COPY)
#include "copy_shader_sb.h"
#endif
#ifdef MKW_GX_DISPLAY_COPY
#include "display_mesh_sb.h"
#endif
#ifdef MKW_GX_LIGHTING
#include "../gpu/gx_lighting.h"
#endif
#ifdef MKW_GX_TEXGEN
#include "../gpu/gx_texgen.h"
#endif
#ifdef MKW_TEV_ARITHMETIC
#define fixture_checks fixture_tev_checks
#endif
extern int sceVideoOutGetFlipStatus(int handle, void* status);
#ifdef MKW_OWNED_COLOR_TARGETS
extern int mkw_color_target_create(unsigned int,void**,unsigned long*);
extern int mkw_color_target_clear(unsigned int,unsigned int);
extern int mkw_color_target_context(unsigned int,void*);
extern int mkw_color_targets_release(int);
#endif
#ifdef MKW_INSPECT_LINKAGE
#include "agc_linkage.c"
#endif
static int check_texture_frame(int vhandle,void* framebuffer,long long frameArg,int mipTest) {
    /* Flip retirement follows this draw in its command buffer. Read only
     * after count>0, arg==1 and an empty pending queue, with a finite timeout. */
    struct { unsigned long long count, time, reserved0; long long arg;
        unsigned long long reserved1, counter; int graphics, pending, current;
        unsigned char padding[68]; } flip;
    _Static_assert(sizeof(flip)==128, "VideoOut flip ABI");
    int retired=0;
    for(int attempt=0;attempt<500;++attempt) {
        int rc=sceVideoOutGetFlipStatus(vhandle,&flip);
        if(rc<0) return 3;
        if(flip.count>0 && flip.arg==frameArg && flip.pending==0) {retired=1;break;}
        sceKernelUsleep(10000);
    }
    if(!retired) { mkw_diagnostic_log("[mkw-texture] FAIL flip timeout\n"); return 4; }
#ifdef MKW_FRAMEBUFFER_SAMPLE
    if(frameArg==1) {
        // Do not read or invalidate any source pixel on the CPU between the
        // first draw and its use as a sampled texture. The second draw's
        // readback validates the complete GPU render -> sample path.
        mkw_diagnostic_log("[mkw-framebuffer] source draw retired; source pixels not read back\n");
        return 0;
    }
#endif
    mkw_diagnostic_log(mipTest?"[mkw-texture] validating forced mip 3\n":"[mkw-texture] validating base frame\n");
    int failures=0;
    for(unsigned int i=0;i<sizeof(fixture_checks)/sizeof(fixture_checks[0]);++i) {
        volatile unsigned int* pixel=(volatile unsigned int*)((unsigned char*)framebuffer+fixture_checks[i].offset);
        __builtin_ia32_clflush((const void*)pixel);
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        unsigned int actual=*pixel;
        unsigned int expected=(mipTest && i<16)?0xff00ffffu:fixture_checks[i].expected;
#ifdef MKW_GX_VARYINGS
        if(i<16) {
            unsigned int coordinate=
#ifdef MKW_GX_FRAGMENT_OUTPUT
                0;
#else
                ((unsigned int)frameArg-1)&7;
#endif
            unsigned int right=(i%4)>=2,bottom=(i/4)>=2;
            if(coordinate&4){unsigned int t=right;right=bottom;bottom=t;}
            if(coordinate&1)right^=1;
            if(coordinate&2)bottom^=1;
            expected=bottom?(right?0xffffffffu:0xff0000ffu):(right?0xff00ff00u:0xffff0000u);
        }
#endif
#ifdef MKW_GX_LIGHTING
        /* Aurora's lighting output is integer material*(L+(L>>7))>>8:
         * material=255 gives 128 for L=128, but 63 for L=64. The TEV
         * texture multiplication preserves those resulting byte values. */
        if(i<16)expected=0xff000000u|((expected&0x00ff0000u)?0x00800000u:0)|
            ((expected&0x0000ff00u)?0x00003f00u:0)|((expected&0x000000ffu)?0x0000003fu:0);
#if defined(MKW_GX_VARYINGS) && !defined(MKW_GX_FRAGMENT_OUTPUT)
        if(i<16 && frameArg>8)expected=0xff000000u|((expected&0x00ff0000u)?0x00400000u:0)|
            ((expected&0x0000ff00u)?0x00008000u:0)|((expected&0x000000ffu)?0x000000a0u:0);
#endif
#endif
#ifdef MKW_GX_FRAGMENT_OUTPUT
        if(i<16) {
            // Independent truth tables for alpha=109/219 compared with 128.
            // Compare 1 is EQUAL 109: true on the top, false on the bottom.
            unsigned int frame=(unsigned int)frameArg-1,bottom=i/4>=2;
            unsigned int first=((bottom?0xf0u:0xaau)>>(frame&7))&1,second=!bottom;
            unsigned int operation=frame/8;
            unsigned int pass=operation==0?(first&second):operation==1?(first|second):operation==2?(first^second):!(first^second);
            expected=pass?(expected&0xffffffu)|((bottom?219u:109u)<<24):0xff201828u;
        }
#endif
#ifdef MKW_GX_BLEND
        extern int mkw_blend_expected(unsigned int,unsigned int,unsigned int*,unsigned int*);
        unsigned int writeMask=0;
        if(mkw_blend_expected((unsigned int)frameArg-1,i,&expected,&writeMask))return 12;
#endif
#ifdef MKW_GX_VIEWPORT
        // Independent pixel-side expectation for left/right/top/bottom scissor.
        if(i<16) {
            unsigned side=((unsigned int)frameArg-1)%4;
            unsigned right=(i%4)>=2,bottom=(i/4)>=2;
            unsigned keep=side==0?!right:side==1?right:side==2?!bottom:bottom;
            if(!keep)expected=0x80201828u;
        }
#endif
#ifdef MKW_FRAMEBUFFER_SAMPLE
        if(i<16)expected=0xff000000u|(expected&0x00808080u);
#endif
#ifdef MKW_GPU_BLIT
        if(i<16)expected=i<8?0xff800000u:0xff000080u;
#endif
#ifdef MKW_GX_FILTERED_COPY
        // Independent integer oracle: (16*red+32*red+16*blue)/64
        // gives (96,0,32); the lower copy gives (32,0,96), whose
        // BT.601 limited-range intensity rounds to 34.
        if(i<16)expected=i<8?0xff600020u:0xff222222u;
#endif
#ifdef MKW_GX_COPY_FORMATS
        static const unsigned int color_copy_expected[16]={
            0x77777777,0x74747474,0x55777777,0x50747474,0xff4080c0,0x504080c0,0x504080c0,
            0x44444444,0x55444444,0x50404040,0x50505050,0x40404040,0x80808080,0xc0c0c0c0,0x80404040,0xc0808080};
        if(i<16)expected=color_copy_expected[i];
#endif
#ifdef MKW_GX_COPY_READBACK
        if(i<16)expected=0x50404040;
#endif
        char line[96]; int n=0;
        const char* prefix="[mkw-texture] pixel ";
        for(int j=0;prefix[j];++j) line[n++]=prefix[j];
        n=hex_append(line,n,i,4); line[n++]=' ';
        n=hex_append(line,n,actual,8); line[n++]=' ';
        n=hex_append(line,n,expected,8); line[n++]='\n';line[n]=0;
        mkw_diagnostic_log(line);
#ifdef MKW_GX_BLEND
        static const unsigned int shifts[4]={16,8,0,24};
        for(unsigned int channel=0;channel<4;++channel){
            int delta=(int)((actual>>shifts[channel])&255)-(int)((expected>>shifts[channel])&255);
            // Compare byte output exactly for every candidate. A failed format
            // is retained as evidence, never accepted by widening tolerance.
            int tolerance=0;
            if(delta < -tolerance || delta > tolerance)++failures;
        }
#else
        if(actual!=expected) ++failures;
#endif
    }
#ifdef MKW_GX_BLEND
    mkw_diagnostic_log(failures?"[mkw-blend] FAIL exact blend pixels differ\n":"[mkw-blend] PASS exact blend, masks and background pixels\n");
#elif defined(MKW_TEV_DIRECT)
    mkw_diagnostic_log(failures?"[mkw-tev-direct] FAIL sampled program pixels differ\n":"[mkw-tev-direct] PASS two sampled TEV stages: 16 exact texture pixels and 4 background pixels after retired flip\n");
#elif defined(MKW_TEV_PROGRAM)
    mkw_diagnostic_log(failures?"[mkw-tev-program] FAIL GPU program pixels differ\n":"[mkw-tev-program] PASS 2048 TEV programs and 4 background pixels after retired flip\n");
#elif defined(MKW_TEV_ARITHMETIC)
    mkw_diagnostic_log(failures?"[mkw-tev] FAIL GPU arithmetic pixels differ\n":"[mkw-tev] PASS 2048 TEV arithmetic pixels and 4 background pixels after retired flip\n");
#else
    mkw_diagnostic_log(failures?"[mkw-texture] FAIL GPU pixels differ\n":"[mkw-texture] PASS 16 textured pixels and 4 background pixels after retired flip\n");
#endif
    return failures?5:0;
}
static int mkw_agc_texture_probe(void) {
    notify("texture_test: arrancando...");

    static unsigned long long agc_state;
    { int r = sceAgcInit(&agc_state, 8); if (r < 0) { notify_rc("texture_test: sceAgcInit fallo", r); return 1; } }

    int vhandle = sceVideoOutOpen(SCE_USER_SYSTEM, VIDEOOUT_BUS_MAIN, 0, 0);
    if (vhandle < 0) { notify_rc("texture_test: sceVideoOutOpen fallo", vhandle); return 1; }
    sceVideoOutSetFlipRate(vhandle, 0);

    const unsigned int W = 1920, H = 1080;
    const int BUF_COUNT = 2;
    unsigned int padded_w = (W + 127u) & ~127u;      /* 64KB tiles are 128x128 elements at 32bpp */
    unsigned int padded_h = (H + 127u) & ~127u;
    unsigned long frame_bytes = (unsigned long)padded_w * padded_h * 4;
    frame_bytes = (frame_bytes + (2*1024*1024 - 1)) & ~(unsigned long)(2*1024*1024 - 1);

    #undef ALLOC_OR_FAIL
    #define ALLOC_OR_FAIL(region, bytes, align, what) \
        do { if (direct_alloc(&(region), (bytes), (align)) < 0) { notify("texture_test: alloc " what " fallo"); return 1; } } while (0)

    DirectMem fb[2];
    for (int i = 0; i < 2; i++) {
#ifdef MKW_OWNED_COLOR_TARGETS
        fb[i].phys = -1; /* Borrowed; the C++ target owns this allocation. */
        if(mkw_color_target_create(i,&fb[i].ptr,&fb[i].size))return 16;
#else
        ALLOC_OR_FAIL(fb[i], frame_bytes, 2*1024*1024, "framebuffer");
#endif
    }

#ifdef MKW_GPU_FENCE
    DirectMem display_spare;
    display_spare.phys=-1;
    if(mkw_color_target_create(2,&display_spare.ptr,&display_spare.size))return 16;
#endif
    SceVideoOutBuffers addresses[2];
    for (int i = 0; i < BUF_COUNT; i++) {
#ifdef MKW_GPU_FENCE
        addresses[i].data = i==0?fb[1].ptr:display_spare.ptr;
#else
        addresses[i].data = fb[i].ptr;
#endif
        addresses[i].metadata = 0; addresses[i].reserved0 = 0; addresses[i].reserved1 = 0;
    }
    /* SharpProspero's SceVideoOutBufferAttribute2 is 80 bytes, including reserves. */
    unsigned char attr[80]; for (int i = 0; i < 80; i++) attr[i] = 0;
    sceVideoOutSetBufferAttribute2(attr, VIDEOOUT_PIXELFORMAT_BGRA8_SRGB, VIDEOOUT_TILING_TILED, W, H, 0ULL, 0u, 0ULL);
    { int r = sceVideoOutRegisterBuffers2(vhandle, 0, 0, addresses, BUF_COUNT, attr, 0, 0);
#ifdef MKW_GPU_FENCE
      char line[80];int n=0;const char* prefix="[mkw-gpu-fence] RegisterBuffers2=";
      for(int j=0;prefix[j];++j)line[n++]=prefix[j];
      n=hex_append(line,n,(unsigned int)r,8);line[n++]='\n';line[n]=0;mkw_diagnostic_log(line);
#endif
      if (r < 0) { notify_rc("texture_test: RegisterBuffers2 fallo", r); return 1; } }
#ifdef MKW_GPU_FENCE
    DirectMem completion;
    if(direct_alloc(&completion,65536,65536)<0){mkw_diagnostic_log("[mkw-gpu-fence] FAIL label allocation\n");return 17;}
    *(unsigned long long*)completion.ptr=0;
    __builtin_ia32_clflush(completion.ptr);__atomic_thread_fence(__ATOMIC_SEQ_CST);
    mkw_diagnostic_log("[mkw-gpu-fence] destination and spare registered with VideoOut; source is offscreen\n");
#endif

    /* Shaders */
    ShaderParts vs_parts, ps_parts;
    if (shader_parts_from_elf(mesh_vs_sb, mesh_vs_sb_len, &vs_parts) < 0 ||
        shader_parts_from_elf(pixel_shader_sb, pixel_shader_sb_len, &ps_parts) < 0) {
        notify("texture_test: parseo de shader fallo"); return 1;
    }
    DirectMem vs_hdr, vs_code, ps_hdr, ps_code;
    ALLOC_OR_FAIL(vs_hdr,  vs_parts.header_len, DIRECT_MEM_MIN_ALIGN, "vs_hdr");
    ALLOC_OR_FAIL(vs_code, vs_parts.code_len,   DIRECT_MEM_MIN_ALIGN, "vs_code");
    ALLOC_OR_FAIL(ps_hdr,  ps_parts.header_len, DIRECT_MEM_MIN_ALIGN, "ps_hdr");
    ALLOC_OR_FAIL(ps_code, ps_parts.code_len,   DIRECT_MEM_MIN_ALIGN, "ps_code");
    { unsigned char *d = vs_hdr.ptr;  for (unsigned int i=0;i<vs_parts.header_len;i++) d[i]=vs_parts.header[i]; }
    { unsigned char *d = vs_code.ptr; for (unsigned int i=0;i<vs_parts.code_len;i++)   d[i]=vs_parts.code[i]; }
    { unsigned char *d = ps_hdr.ptr;  for (unsigned int i=0;i<ps_parts.header_len;i++) d[i]=ps_parts.header[i]; }
    { unsigned char *d = ps_code.ptr; for (unsigned int i=0;i<ps_parts.code_len;i++)   d[i]=ps_parts.code[i]; }

    void *vs_handle = 0, *ps_handle = 0;
    { int r = sceAgcCreateShader(&vs_handle, vs_hdr.ptr, vs_code.ptr); if (r < 0) { notify_rc("texture_test: CreateShader VS fallo", r); return 1; } }
    { int r = sceAgcCreateShader(&ps_handle, ps_hdr.ptr, ps_code.ptr); if (r < 0) { notify_rc("texture_test: CreateShader PS fallo", r); return 1; } }
#if defined(MKW_GX_FILTERED_COPY) || defined(MKW_GX_DISPLAY_COPY)
    ShaderParts copy_parts;DirectMem copy_hdr,copy_code;void* copy_handle=0;
    if(shader_parts_from_elf(copy_shader_sb,copy_shader_sb_len,&copy_parts)<0)return 19;
    ALLOC_OR_FAIL(copy_hdr,copy_parts.header_len,DIRECT_MEM_MIN_ALIGN,"copy_hdr");
    ALLOC_OR_FAIL(copy_code,copy_parts.code_len,DIRECT_MEM_MIN_ALIGN,"copy_code");
    for(unsigned i=0;i<copy_parts.header_len;++i)((unsigned char*)copy_hdr.ptr)[i]=copy_parts.header[i];
    for(unsigned i=0;i<copy_parts.code_len;++i)((unsigned char*)copy_code.ptr)[i]=copy_parts.code[i];
    if(sceAgcCreateShader(&copy_handle,copy_hdr.ptr,copy_code.ptr)<0)return 19;
    for(unsigned long off=0;off<copy_hdr.size;off+=64)__builtin_ia32_clflush((char*)copy_hdr.ptr+off);
    for(unsigned long off=0;off<copy_code.size;off+=64)__builtin_ia32_clflush((char*)copy_code.ptr+off);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
#endif
#ifdef MKW_GX_DISPLAY_COPY
    ShaderParts display_mesh_parts;DirectMem display_mesh_hdr,display_mesh_code;void* display_mesh_handle=0;
    if(shader_parts_from_elf(display_mesh_sb,display_mesh_sb_len,&display_mesh_parts)<0)return 22;
    ALLOC_OR_FAIL(display_mesh_hdr,display_mesh_parts.header_len,DIRECT_MEM_MIN_ALIGN,"display_mesh_hdr");
    ALLOC_OR_FAIL(display_mesh_code,display_mesh_parts.code_len,DIRECT_MEM_MIN_ALIGN,"display_mesh_code");
    for(unsigned i=0;i<display_mesh_parts.header_len;++i)((unsigned char*)display_mesh_hdr.ptr)[i]=display_mesh_parts.header[i];
    for(unsigned i=0;i<display_mesh_parts.code_len;++i)((unsigned char*)display_mesh_code.ptr)[i]=display_mesh_parts.code[i];
    if(sceAgcCreateShader(&display_mesh_handle,display_mesh_hdr.ptr,display_mesh_code.ptr)<0)return 22;
    for(unsigned long off=0;off<display_mesh_hdr.size;off+=64)__builtin_ia32_clflush((char*)display_mesh_hdr.ptr+off);
    for(unsigned long off=0;off<display_mesh_code.size;off+=64)__builtin_ia32_clflush((char*)display_mesh_code.ptr+off);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
#endif
#ifdef MKW_INSPECT_LINKAGE
    if(inspect_agc_linkage(vs_handle,ps_handle))return 9;
#endif

    /* Geometry */
    DirectMem vbuf, ibuf;
#ifdef MKW_RAW_VERTEX
    unsigned long raw_geometry_size=0;
    ALLOC_OR_FAIL(vbuf, 65536, DIRECT_MEM_MIN_ALIGN, "raw GX vbuf");
#else
    ALLOC_OR_FAIL(vbuf, 4 * VERTEX_STRIDE, DIRECT_MEM_MIN_ALIGN, "vbuf");
#endif
    ALLOC_OR_FAIL(ibuf, 6 * 4, DIRECT_MEM_MIN_ALIGN, "ibuf");
#ifdef MKW_RAW_VERTEX
    extern int mkw_geometry_raw(void*,unsigned long,unsigned long*,unsigned int*);
    if(mkw_geometry_raw(vbuf.ptr,vbuf.size,&raw_geometry_size,(unsigned int*)ibuf.ptr))return 8;
#elif defined(MKW_GEOMETRY_PROBE)
    extern int mkw_geometry_quad(float*,float*,unsigned int*,unsigned int*);
    float geometry_positions[12],geometry_uv[8];unsigned int geometry_colors[4],geometry_indices[6];
    if(mkw_geometry_quad(geometry_positions,geometry_uv,geometry_colors,geometry_indices))return 8;
    for(int i=0;i<4;++i)put_vertex(vbuf.ptr,i,geometry_positions[i*3],geometry_positions[i*3+1],geometry_positions[i*3+2],
        geometry_uv[i*2],geometry_uv[i*2+1],0.f,geometry_colors[i]);
    for(int i=0;i<6;++i)((unsigned int*)ibuf.ptr)[i]=geometry_indices[i];
#else
    /* The supplied VS exports normal.xyz as TEXCOORD0 without normalization.
     * With identity model, normal.xy carries UV exactly. This diagnostic
     * deliberately uses that documented interface; it is not a GX vertex shader. */
    static const float xy[4][2] = {{-.75f,.75f},{.75f,.75f},{.75f,-.75f},{-.75f,-.75f}};
    for (int i=0;i<4;++i)
        put_vertex(vbuf.ptr, i, xy[i][0], xy[i][1], .5f,
                   (i==1 || i==2)?1.f:0.f, i>=2?1.f:0.f, 0.f,
#if defined(MKW_FRAMEBUFFER_SAMPLE) && !defined(MKW_GX_COPY_FORMATS) && !defined(MKW_GX_COPY_READBACK)
                   0xff808080u); // Distinguishes rendered source from original texture.
#else
                   0xffffffffu);
#endif
    static const unsigned int indices[6]={0,1,2,0,2,3};
    for(int i=0;i<6;++i) ((unsigned int*)ibuf.ptr)[i]=indices[i];
#endif
    DirectMem texture;
    unsigned int texture_words[8];
#ifdef MKW_TEV_DIRECT
    ALLOC_OR_FAIL(texture, 2000, 65536, "direct TEV uniforms");
    extern int mkw_tev_direct_create(void*,unsigned long);
    if(mkw_tev_direct_create(texture.ptr,texture.size))return 6;
    for(int i=0;i<8;++i)texture_words[i]=0;
    desc_structured(texture_words,(unsigned long long)texture.ptr,0,2000);
    texture_words[3] |= 3u << 28;
#elif defined(MKW_TEV_ARITHMETIC)
    ALLOC_OR_FAIL(texture, sizeof(fixture_tev_records), 65536, "TEV cases");
    for(unsigned int i=0;i<sizeof(fixture_tev_records)/4;++i)
        ((unsigned int*)texture.ptr)[i]=((const unsigned int*)fixture_tev_records)[i];
    for(int i=0;i<8;++i)texture_words[i]=0;
    desc_structured(texture_words,(unsigned long long)texture.ptr,0,sizeof(fixture_tev_records));
    // SharpProspero Structured(stride=0) uses byte bounds, kOffsetOrSwizzle.
    texture_words[3] |= 3u << 28;
#elif defined(MKW_GENERAL_TEXTURE)
    extern int mkw_texture_upload_create(void**,unsigned long*,unsigned int*);
    texture.phys=0;
    if(mkw_texture_upload_create(&texture.ptr,&texture.size,texture_words))return 6;
#else
    ALLOC_OR_FAIL(texture, sizeof(fixture_pixels), 65536, "texture");
    for(unsigned int i=0;i<sizeof(fixture_pixels);++i)
#ifdef MKW_WII_TEXTURE_PROBE
        ((unsigned char*)texture.ptr)[i]=0xcd; // A missing/incorrect upload must fail readback.
#else
        ((unsigned char*)texture.ptr)[i]=fixture_pixels[i];
#endif
#if defined(MKW_GX_COPY_FORMATS) || defined(MKW_GX_COPY_READBACK)
    for(unsigned i=0;i<64*64;++i){unsigned offset=fixture_pixel_offsets[i];
        ((unsigned char*)texture.ptr)[offset]=64;((unsigned char*)texture.ptr)[offset+1]=128;
        ((unsigned char*)texture.ptr)[offset+2]=192;((unsigned char*)texture.ptr)[offset+3]=80;}
#endif
#ifdef MKW_WII_TEXTURE_PROBE
    extern int mkw_wii_texture_decode(unsigned char*, unsigned long);
    unsigned char decoded[64*64*4];
    if (mkw_wii_texture_decode(decoded, sizeof(decoded))) return 6;
    // Upload real Aurora conversion output. Host-generated SharpProspero
    // offsets tile this fixed-size diagnostic image; general upload comes next.
    for(unsigned int i=0;i<64*64;++i)
        for(unsigned int c=0;c<4;++c)
            ((unsigned char*)texture.ptr)[fixture_pixel_offsets[i]+c]=decoded[i*4+c];
#endif
    for(int i=0;i<8;++i) texture_words[i]=fixture_texture[i];
    unsigned long long address=(unsigned long long)texture.ptr;
    texture_words[0]=(unsigned int)(address>>8);
    texture_words[1]=(texture_words[1]&0xffffff00u)|(unsigned int)((address>>40)&255u);
#endif
    int tex_dwo=resource_dword_offset(ps_handle,KIND_READONLY,0);
    int sam_dwo=resource_dword_offset(ps_handle,KIND_SAMPLER,0);
#if defined(MKW_TEV_ARITHMETIC) || defined(MKW_TEV_DIRECT)
    if(tex_dwo!=0 || sam_dwo!=-1) return 2;
    mkw_diagnostic_log("[mkw-tev] shader creation and read-only buffer slot passed\n");
#else
    if(tex_dwo!=0 || sam_dwo!=8) return 2;
    mkw_diagnostic_log("[mkw-texture] shader creation and texture/sampler slots passed\n");
#endif

    /* Where the vertex program wants its two resources. */
    int cb_dwo = resource_dword_offset(vs_handle, KIND_CONSTANTBUFFER, 0);
    int vb_dwo = resource_dword_offset(vs_handle, KIND_READONLY, 0);
    notify_vals("chk1", "cbdwo", (unsigned long long)(int)cb_dwo, "vbdwo", (unsigned long long)(int)vb_dwo,
                "vscx", SHDR_CXCOUNT(vs_handle), "pscx", SHDR_CXCOUNT(ps_handle));
    if (cb_dwo < 0 || vb_dwo < 0) { notify("texture_test: el VS no declara sus recursos"); return 1; }

    unsigned int vb_words[4];
#ifdef MKW_RAW_VERTEX
    desc_structured(vb_words, (unsigned long long)(unsigned long)vbuf.ptr, 0, (unsigned int)raw_geometry_size);
    vb_words[3] |= 3u << 28;
#else
    desc_structured(vb_words, (unsigned long long)(unsigned long)vbuf.ptr, VERTEX_STRIDE, 4);
#endif

    /* Per-frame-in-flight state, so recording one frame never overwrites what
     * an earlier frame's draw is still reading. */
    DirectMem dcb_m[2], ctx_m[2], sh_m[2], prim_m[2], cons_m[2];
    DcbState dcb_state[2];
#ifdef MKW_GX_TRANSFORMS
    #include "../gpu/gx_transform_format.h"
#ifdef MKW_GX_LIGHTING
    #ifdef MKW_GX_TEXGEN
    #define MKW_PROBE_VERTEX_UNIFORMS (MKW_GX_TEXGEN_OFFSET+MKW_GX_TEXGEN_BYTES)
    #else
    #define MKW_PROBE_VERTEX_UNIFORMS (MKW_GX_TRANSFORM_BYTES+MKW_GX_LIGHTING_BYTES)
    #endif
#else
    #define MKW_PROBE_VERTEX_UNIFORMS MKW_GX_TRANSFORM_BYTES
#endif
    extern int mkw_geometry_transforms(void*,unsigned long);
#endif
    for (int i = 0; i < BUF_COUNT; i++) {
        ALLOC_OR_FAIL(dcb_m[i],  64*1024, DIRECT_MEM_MIN_ALIGN, "dcb");
        ALLOC_OR_FAIL(ctx_m[i],  8192,    DIRECT_MEM_MIN_ALIGN, "ctx");
        ALLOC_OR_FAIL(sh_m[i],   4096,    DIRECT_MEM_MIN_ALIGN, "sh");
        ALLOC_OR_FAIL(prim_m[i], 4096,    DIRECT_MEM_MIN_ALIGN, "prim");
#ifdef MKW_GX_TRANSFORMS
        ALLOC_OR_FAIL(cons_m[i], MKW_PROBE_VERTEX_UNIFORMS, DIRECT_MEM_MIN_ALIGN, "GX vertex uniforms");
#else
        ALLOC_OR_FAIL(cons_m[i], 256,     DIRECT_MEM_MIN_ALIGN, "cons");
#endif
    }

    void *defaults = sceAgcGetRegisterDefaults();
    CxRegister **cx_blocks = *(CxRegister***)((unsigned char*)defaults + 0x00);
    unsigned int cx_record_count = *(unsigned int*)((unsigned char*)defaults + 0x20);
    CxRegister *cx_records = cx_blocks ? cx_blocks[0] : 0;
    static const unsigned short kRenderTargetOffsets[16] = {
        0x0318,0x031B,0x031C,0x031D,0x031E,0x031F,0x0321,0x0323,
        0x0324,0x0325,0x0390,0x0398,0x03A0,0x03A8,0x03B0,0x03B8,
    };

    notify("WiiCompiled: AGC texture readback");
#ifdef MKW_GX_BLEND
    const int FRAMES = 72;
#elif defined(MKW_GX_FRAGMENT_OUTPUT)
    const int FRAMES = 32;
#elif defined(MKW_GX_VARYINGS)
    const int FRAMES = 16;
#elif defined(MKW_GENERAL_TEXTURE) || defined(MKW_FRAMEBUFFER_SAMPLE)
    const int FRAMES = 2;
#else
    const int FRAMES = 1;
#endif
    for (int frame = 0; frame < FRAMES; frame++) {
#ifdef MKW_GX_VARYINGS
        extern int mkw_tev_direct_select_frame(void*,unsigned int);
        if(mkw_tev_direct_select_frame(texture.ptr,(unsigned int)frame))return 10;
        char frame_line[64];int n=0;const char* prefix="[mkw-varyings] frame ";
        for(int j=0;prefix[j];++j)frame_line[n++]=prefix[j];
        n=hex_append(frame_line,n,(unsigned int)frame,2);frame_line[n++]='\n';frame_line[n]=0;
        mkw_diagnostic_log(frame_line);
#endif
#ifdef MKW_FRAMEBUFFER_SAMPLE
        if(frame==1) {
#ifdef MKW_GPU_BLIT
            extern int mkw_framebuffer_blit(void*,void*);
#ifdef MKW_GX_FILTERED_COPY
            if(mkw_framebuffer_blit(vs_handle,copy_handle))return 18;
#else
            if(mkw_framebuffer_blit(vs_handle,ps_handle))return 18;
#endif
#endif
            // Frame zero has retired without CPU pixel readback before this loop
            // iteration. Only the other target is cleared and written below.
            extern int mkw_framebuffer_view(void*,unsigned long,unsigned int*);
            if(mkw_framebuffer_view(fb[0].ptr,fb[0].size,texture_words))return 15;
            for(int i=0;i<4;++i)put_vertex(vbuf.ptr,i,xy[i][0],xy[i][1],.5f,
                (i==1||i==2)?.875f:.125f,i>=2?.875f:.125f,0.f,0xffffffffu);
            mkw_diagnostic_log("[mkw-framebuffer] sampling retired GPU color target; no CPU pixel conversion\n");
        }
#endif
        int slot = frame & 1;
        DirectMem *ctx = &ctx_m[slot], *shm = &sh_m[slot], *prm = &prim_m[slot], *cns = &cons_m[slot];
        DcbState *dcb = &dcb_state[slot];

        unsigned int cb_words[4];
#ifdef MKW_GX_TRANSFORMS
        if(mkw_geometry_transforms(cns->ptr,cns->size))return 9;
        desc_structured(cb_words, (unsigned long long)(unsigned long)cns->ptr,0,MKW_PROBE_VERTEX_UNIFORMS);
        cb_words[3] |= 3u << 28;
#else
        float* constants=(float*)cns->ptr;
        mat_identity(constants); mat_identity(constants+16);
        desc_constant(cb_words, (unsigned long long)(unsigned long)cns->ptr, 128);
#endif

        /* Clear this frame's target to a dark background from the CPU: these
         * shaders draw a cube, not a background, and without a depth buffer
         * there is nothing else to wipe last frame's cube away. */
#ifdef MKW_OWNED_COLOR_TARGETS
        if(mkw_color_target_clear(slot,0xFF201828u))return 16;
#else
        { unsigned int *p = (unsigned int*)fb[slot].ptr;
          unsigned long n = fb[slot].size / 4;
          for (unsigned long i = 0; i < n; i++) p[i] =
#ifdef MKW_GX_BLEND
              0x80201828u;
#else
              0xFF201828u;
#endif
        }
#endif

        dcb_reset(dcb, (unsigned int*)dcb_m[slot].ptr, (unsigned int)(dcb_m[slot].size / 4));

        /* Render target -> this frame's framebuffer. */
        CxRegister rt[16];
        for (int i = 0; i < 16; i++) {
            rt[i].offset = kRenderTargetOffsets[i]; rt[i].pad = 0; rt[i].value = 0;
            for (unsigned int r = 0; r < cx_record_count; r++)
                if (cx_records[r].offset == kRenderTargetOffsets[i]) { rt[i].value = cx_records[r].value; break; }
        }
#ifdef MKW_OWNED_COLOR_TARGETS
        if(mkw_color_target_context(slot,rt))return 16;
#else
        #define RT_SET(idx, mask, val) rt[idx].value = (rt[idx].value & ~(unsigned int)(mask)) | (unsigned int)(val)
        RT_SET(1, 0x03ffe000u, 0u);
        RT_SET(2, 0x0000007cu, 0x00000028u); /* Format = k8_8_8_8 */
        RT_SET(2, 0x00000700u, 0x00000000u); /* ChannelType = kUNorm */
        RT_SET(2, 0x00001800u, 0x00000800u); /* ChannelOrder = kAlt */
        RT_SET(2, 0x00010000u, 0u);          /* BlendBypass off */
        RT_SET(2, 0x00008000u, 0x00008000u); /* BlendClamp on */
        RT_SET(2, 0x00040000u, 0u);          /* RoundMode = by half */
        RT_SET(2, 0x10000000u, 0u);          /* DccCompression off */
        RT_SET(2, 0x00004000u, 0u);          /* FmaskCompression off */
        RT_SET(3, 0x00007000u, 0u);          /* 1 sample */
        RT_SET(3, 0x00018000u, 0u);          /* 1 fragment */
        RT_SET(4, 0x00000008u, 0x00000008u);
        RT_SET(4, 0x00000040u, 0x00000040u);
        RT_SET(4, 0x00100200u, 0u);
        RT_SET(4, 0x00080000u, 0u);
        rt[14].value = (rt[14].value & 0xffffc000u) | ((H - 1) & 0x00003fffu);
        rt[14].value = (rt[14].value & 0xf0003fffu) | (((W - 1) << 14) & 0x0fffc000u);
        rt[14].value = (rt[14].value & 0x0fffffffu) | 0u;
        rt[15].value = (rt[15].value & 0xffffe000u) | 0u;
        RT_SET(15, 0x0007c000u, 0x0006c000u); /* TileMode = render target */
        RT_SET(15, 0x03000000u, 0x01000000u); /* 2D */
        RT_SET(15, 0x44000000u, 0x44000000u); /* metadata pipe alignment */
        { unsigned long long a = (unsigned long long)(unsigned long)fb[slot].ptr;
          rt[0].value = (unsigned int)((a >> 8) & 0xffffffffu);
          rt[10].value = (rt[10].value & 0xffffff00u) | (unsigned int)((a >> 40) & 0xffu);
          rt[5].value = 0; rt[11].value &= 0xffffff00u;
          rt[6].value = 0; rt[12].value &= 0xffffff00u;
          rt[9].value = 0; rt[13].value &= 0xffffff00u; }
#endif

        /* Viewport */
        CxRegister vp[14];
        { float xs = W * 0.5f, xo = xs, ys = -(float)H * 0.5f, yo = H * 0.5f;
          union { float f; unsigned int u; } cv;
          int i = 0;
          #define VPF(off, fval) { cv.f = (fval); vp[i].offset=(off); vp[i].pad=0; vp[i].value=cv.u; i++; }
          VPF(0x10F, xs); VPF(0x110, xo); VPF(0x111, ys); VPF(0x112, yo);
          VPF(0x113, 1.0f); VPF(0x114, 0.0f); VPF(0x0B4, 0.0f); VPF(0x0B5, 1.0f);
          VPF(0x2FA, 8.0f); VPF(0x2FB, 8.0f); VPF(0x2FC, 8.0f); VPF(0x2FD, 8.0f);
          #undef VPF
          vp[i].offset=0x090; vp[i].pad=0; vp[i].value=0x80000000u; i++;
          vp[i].offset=0x091; vp[i].pad=0; vp[i].value=(0x4000u) | (0x4000u << 16); i++; }

#ifdef MKW_GX_VIEWPORT
        extern int mkw_viewport_frame(unsigned int,void*);
        if(mkw_viewport_frame((unsigned int)frame,vp))return 14;
#endif
        CxRegister linkage[34], primitive_state[3];
        { int r = sceAgcLinkShaders(linkage, primitive_state, 0, vs_handle, ps_handle, 4);
          if (r < 0) { notify_rc("texture_test: LinkShaders fallo", r); return 1; } }
#ifdef MKW_GX_VARYINGS
        for(unsigned int i=0;i<10;++i)if(linkage[i].offset!=0x191+i || linkage[i].value!=i) {
            mkw_diagnostic_log("[mkw-varyings] FAIL ten-parameter linkage; draw not submitted\n");return 11;
        }
        if(frame==0)mkw_diagnostic_log("[mkw-varyings] PASS ten AGC parameters mapped before draw\n");
#endif

        CxRegister context[16 + 14 + 1 + 34 + 32 + 32];
        int cx = 0;
        for (int i = 0; i < 16; i++) context[cx++] = rt[i];
        for (int i = 0; i < 14; i++) context[cx++] = vp[i];
        context[cx].offset = 0x008E; context[cx].pad = 0; context[cx].value = 0xF; cx++;  /* CB_TARGET_MASK */
#ifdef MKW_GX_BLEND
        extern int mkw_blend_frame(unsigned int,unsigned int,unsigned int*,unsigned int*,float*);
        unsigned int blendDefault=0,blendControl=0,blendMask=0;int foundBlend=0;
        for(unsigned int r=0;r<cx_record_count;++r)if(cx_records[r].offset==0x1e0){blendDefault=cx_records[r].value;foundBlend=1;break;}
        float blendConstant[4];
        if(!foundBlend || mkw_blend_frame((unsigned int)frame,blendDefault,&blendControl,&blendMask,blendConstant))return 13;
        context[cx-1].value=blendMask;
        context[cx].offset=0x1e0;context[cx].pad=0;context[cx].value=blendControl;++cx;
        for(unsigned int i=0;i<4;++i){union{float f;unsigned int u;}v;v.f=blendConstant[i];
            context[cx].offset=0x105+i;context[cx].pad=0;context[cx].value=v.u;++cx;}
#endif

        {
            unsigned int mode = 0;
            for (unsigned int r = 0; r < cx_record_count; r++)
                if (cx_records[r].offset == 0x205) { mode = cx_records[r].value; break; }
            mode &= ~0x7u; /* Both faces: this test has only one flat quad. */
            context[cx].offset = 0x205; context[cx].pad = 0; context[cx].value = mode; cx++;
        }
        for (int i = 0; i < 34; i++) context[cx++] = linkage[i];
        for (int i = 0; i < SHDR_CXCOUNT(vs_handle); i++) context[cx++] = SHDR_CXREGS(vs_handle)[i];
        for (int i = 0; i < SHDR_CXCOUNT(ps_handle); i++) context[cx++] = SHDR_CXREGS(ps_handle)[i];

        CxRegister shr[32];
        int sh = 0;
        for (int i = 0; i < SHDR_SHCOUNT(vs_handle); i++) shr[sh++] = SHDR_SHREGS(vs_handle)[i];
        for (int i = 0; i < SHDR_SHCOUNT(ps_handle); i++) shr[sh++] = SHDR_SHREGS(ps_handle)[i];

        for (int i = 0; i < cx; i++) ((CxRegister*)ctx->ptr)[i] = context[i];
        for (int i = 0; i < sh; i++) ((CxRegister*)shm->ptr)[i] = shr[i];
        for (int i = 0; i < 3;  i++) ((CxRegister*)prm->ptr)[i] = primitive_state[i];

        sceAgcDcbSetCxRegistersIndirect(dcb, ctx->ptr, (unsigned int)cx);
        sceAgcDcbSetShRegistersIndirect(dcb, shm->ptr, (unsigned int)sh);
        sceAgcDcbSetUcRegistersIndirect(dcb, prm->ptr, 3);

        /* Both resources belong to the vertex stage. */
        sceAgcCbSetShRegisterRangeDirect(dcb, GS_USER_DATA_BASE + (unsigned int)cb_dwo, cb_words, 4);
        sceAgcCbSetShRegisterRangeDirect(dcb, GS_USER_DATA_BASE + (unsigned int)vb_dwo, vb_words, 4);

        sceAgcCbSetShRegisterRangeDirect(dcb, PS_USER_DATA_BASE+(unsigned int)tex_dwo, texture_words, 8);
        unsigned int sampler[4];for(int i=0;i<4;++i)sampler[i]=fixture_sampler[i];
#ifdef MKW_GENERAL_TEXTURE
        if(frame==1) {
            sampler[1]=768u|(768u<<12); // min=max=3.0 LOD (unsigned 4.8)
            sampler[2]=(sampler[2]&~(3u<<26))|(1u<<26); // nearest mip
        }
#endif
        if(sam_dwo>=0) sceAgcCbSetShRegisterRangeDirect(dcb, PS_USER_DATA_BASE+(unsigned int)sam_dwo, sampler, 4);
        sceAgcDcbSetIndexSize(dcb, 1, 0);
        sceAgcDcbSetIndexBuffer(dcb, ibuf.ptr);
        sceAgcDcbSetIndexCount(dcb, 6);
        sceAgcDcbDrawIndex(dcb, 6, ibuf.ptr, 0);
#ifdef MKW_INSPECT_FENCE
        unsigned int* flip_begin=dcb->up_cursor;
#endif
#ifdef MKW_GPU_FENCE
        if(frame==0) {
            extern int mkw_gpu_completion_packet(unsigned long long,unsigned long long,unsigned int*);
            if(dcb->down_cursor-dcb->up_cursor<8)return 17;
            if(mkw_gpu_completion_packet((unsigned long long)(unsigned long)completion.ptr,
                0x12345678abcdef01ULL,dcb->up_cursor))return 17;
            dcb->up_cursor+=8;
        } else sceAgcDcbSetFlip(dcb,(unsigned int)vhandle,0,1,(long long)frame+1);
#else
        sceAgcDcbSetFlip(dcb, (unsigned int)vhandle, slot, 1 /* VSync */, (long long)frame + 1);
#endif
#ifdef MKW_INSPECT_FENCE
        for(unsigned int* p=flip_begin;p<dcb->up_cursor;++p) {
            char line[96];int n=0;const char* prefix="[mkw-fence-packet] ";
            for(int j=0;prefix[j];++j)line[n++]=prefix[j];
            n=hex_append(line,n,(unsigned int)frame,2);line[n++]=' ';
            n=hex_append(line,n,(unsigned int)(p-flip_begin),4);line[n++]=' ';
            n=hex_append(line,n,*p,8);line[n++]='\n';line[n]=0;mkw_diagnostic_log(line);
        }
#endif

        struct { void *words; unsigned int wordCount; unsigned char flag; } submit;
        submit.words = dcb->bottom;
        submit.wordCount = (unsigned int)(dcb->up_cursor - dcb->bottom);
        submit.flag = 0;
        /* Publish CPU writes before the GPU reads these buffers, including
         * the clear. Each frame is retired before another is submitted. */
        DirectMem* writes[] = {&fb[slot], &vbuf, &ibuf, &texture, &vs_hdr, &vs_code,
            &ps_hdr, &ps_code, ctx, shm, prm, cns, &dcb_m[slot]};
        for(unsigned int j=0;j<sizeof(writes)/sizeof(writes[0]);++j)
            for(unsigned long off=0;off<writes[j]->size;off+=64)
                __builtin_ia32_clflush((const char*)writes[j]->ptr+off);
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        { int r = sceAgcDriverSubmitDcb(&submit);
          if (r < 0) { notify_rc("texture_test: SubmitDcb fallo", r); return 1; } }
        sceAgcSuspendPoint();

#ifdef MKW_GPU_FENCE
        if(frame==0) {
            int done=0;
            for(unsigned int tries=0;tries<5000;++tries) {
                __builtin_ia32_clflush(completion.ptr);__atomic_thread_fence(__ATOMIC_SEQ_CST);
                if(*(volatile unsigned long long*)completion.ptr==0x12345678abcdef01ULL){done=1;break;}
                sceKernelUsleep(1000);
            }
            if(!done){mkw_diagnostic_log("[mkw-gpu-fence] FAIL completion timeout; allocations retained\n");return 17;}
            // The first draw must not have queued or completed any display flip.
            unsigned long long flip[16]={0};
            if(sceVideoOutGetFlipStatus(vhandle,flip)<0||flip[0]!=0) {
                mkw_diagnostic_log("[mkw-gpu-fence] FAIL unexpected source display flip\n");return 17;
            }
            mkw_diagnostic_log("[mkw-gpu-fence] offscreen 64-bit completion observed; zero source flips and no source pixel readback\n");
            continue;
        }
#endif
        sceVideoOutWaitVblank(vhandle);
        int validation=check_texture_frame(vhandle,fb[slot].ptr,frame+1,
#if defined(MKW_GX_VARYINGS) || defined(MKW_FRAMEBUFFER_SAMPLE)
            0
#else
            frame==1
#endif
        );
#ifdef MKW_GPU_BLIT
        extern int mkw_framebuffer_blit_inspect(void);
        if((validation==0||validation==5)&&mkw_framebuffer_blit_inspect())return 20;
#endif
        if(validation)return validation;
    }

#ifdef MKW_GX_DRAW_PROBE
#ifdef MKW_GX_PRESENT
    if(sceVideoOutClose(vhandle)<0)return 23;
    mkw_diagnostic_log("[mkw-present] legacy VideoOut detached after its last retired flip\n");
#endif
#ifdef MKW_GX_DISPLAY_COPY
    extern void mkw_display_copy_shaders(void*,void*);
    mkw_display_copy_shaders(display_mesh_handle,copy_handle);
#endif
    extern int mkw_test_gx_draw(void*,void*);
    if(mkw_test_gx_draw(vs_handle,ps_handle))return 21;
#ifdef MKW_GX_LIFECYCLE
#ifdef MKW_OWNED_SHADERS
    extern int mkw_test_owned_shader_frames(const unsigned char*,unsigned long,const unsigned char*,unsigned long,const unsigned char*,unsigned long,const unsigned char*,unsigned long);
    if(mkw_test_owned_shader_frames(mesh_vs_sb,mesh_vs_sb_len,pixel_shader_sb,pixel_shader_sb_len,display_mesh_sb,display_mesh_sb_len,copy_shader_sb,copy_shader_sb_len))return 24;
#else
    extern int mkw_test_gx_frame(void*,void*,void*,void*);
    if(mkw_test_gx_frame(vs_handle,ps_handle,display_mesh_handle,copy_handle))return 24;
#endif
#endif
#endif
// Only release texture handles once the GPU has retired their draw.
#ifdef MKW_OWNED_COLOR_TARGETS
    if(mkw_color_targets_release(vhandle))return 16;
#endif
#ifdef MKW_TEV_DIRECT
    extern int mkw_tev_direct_release(void);
    if(mkw_tev_direct_release())return 7;
#endif
#ifdef MKW_GENERAL_TEXTURE
    extern int mkw_texture_upload_release(void);
    if(mkw_texture_upload_release())return 7;
#endif
#ifdef MKW_TEV_ARITHMETIC
    notify("WiiCompiled: TEV arithmetic readback PASS");
#else
    notify("WiiCompiled: texture readback PASS");
#endif
    sceKernelUsleep(5000000);
    return 0;
}
