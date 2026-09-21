/* SPDX-License-Identifier: GPL-3.0-only
 * Exercises the actual shared stage interpreter and draw-time GX snapshots.
 * Texture samples here are fixture inputs, not image_sample instructions.
 * Future renderer supplies samples from its direct/indirect coordinate path.
 */
#include "../tev_stage.h"
typedef unsigned u4 __attribute__((ext_vector_type(4)));
typedef float f4 __attribute__((ext_vector_type(4)));
typedef __fp16 h2 __attribute__((ext_vector_type(2)));
extern u4 load_case(u4, unsigned, unsigned, unsigned) __asm("llvm.amdgcn.raw.buffer.load.v4i32");
extern void export_pixel(unsigned,unsigned,h2,h2,_Bool,_Bool) __asm("llvm.amdgcn.exp.compr.v2f16");
static inline MkwTevValue load_value(u4 data,unsigned offset) {
    u4 bits=load_case(data,offset,0,0);
    f4 v=__builtin_bit_cast(f4,bits);
    MkwTevValue out={v.x,v.y,v.z,v.w};return out;
}
void tev_probe(u4 data,u4 reserved0,u4 reserved1,unsigned mask,float i,float j) {
    float x=__builtin_amdgcn_interp_p1(i,0,0,mask),y=__builtin_amdgcn_interp_p1(i,1,0,mask);
    x=__builtin_amdgcn_interp_p2(x,j,0,0,mask);y=__builtin_amdgcn_interp_p2(y,j,1,0,mask);
    unsigned col=(unsigned)(x*64.0f),row=(unsigned)(y*32.0f);
    col=col<64?col:63;row=row<32?row:31;
    // Each fixture record is 2048 bytes: 1360-byte program plus inputs.
    unsigned base=(row*64+col)*2048;
    u4 header=load_case(data,base,0,0);
    MkwTevRegisters regs={load_value(data,base+16),load_value(data,base+32),
        load_value(data,base+48),load_value(data,base+64)};
    MkwTevValue raster0=load_value(data,base+1360),raster1=load_value(data,base+1376);
    MkwTevValue indirect=load_value(data,base+1392);
    MkwTevValue result=regs.prev;
    unsigned count=header.x<16?header.x:16;
    #pragma clang loop unroll(disable)
    for(unsigned n=0;n<count;++n) {
        unsigned at=base+80+n*80;
        u4 a=load_case(data,at,0,0),b=load_case(data,at+16,0,0);
        u4 c=load_case(data,at+32,0,0),d=load_case(data,at+48,0,0);
        MkwTevStage s={a.x,a.y,a.z,a.w,b.x,b.y,b.z,b.w,
            c.x,c.y,c.z,c.w,d.x,d.y,d.z,d.w,load_value(data,at+64)};
        MkwTevValue tex=s.sample_enabled?load_value(data,base+1408+n*16):mkw_tev_splat((float)header.z);
        MkwTevValue ras=mkw_tev_raster(s,raster0,raster1,indirect);
        result=mkw_tev_stage(&regs,s,tex,ras);
    }
    /* Mark alpha rejection as transparent black in this arithmetic probe.
     * The production pixel shader must use discard; no discard is claimed.
     */
    int pass=mkw_tev_alpha_test(result.a,header.y);
    result=mkw_tev_wrap4(result);
    const float unorm=0x1.010102p-8f;
    float r=pass?result.r*unorm:0,g=pass?result.g*unorm:0,b=pass?result.b*unorm:0;
    export_pixel(0,15,__builtin_amdgcn_cvt_pkrtz(r,g),__builtin_amdgcn_cvt_pkrtz(b,pass?1.0f:0.0f),1,1);
}
