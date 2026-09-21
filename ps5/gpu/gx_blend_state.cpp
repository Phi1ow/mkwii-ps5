// SPDX-License-Identifier: GPL-3.0-only
#include "gx_blend_state.h"
#include <stdexcept>
namespace mkw::agc {
namespace {
// Aurora gx.cpp::to_blend_factor. GX aliases SRC/DSTCLR; the operand's
// source/destination role resolves the alias. Values come from CxBlend.cs.
unsigned factor(unsigned gx,bool destination,bool alpha) {
    if(gx>7)throw std::invalid_argument("Invalid GX blend factor");
    if(gx==2)return alpha?(destination?4:6):(destination?2:8);
    if(gx==3)return alpha?(destination?5:7):(destination?3:9);
    return gx;
}
}
GxBlendState snapshot_gx_blend(const aurora::gx::GXRegisterState& s,uint32_t defaults) {
    if(unsigned(s.blendMode)>3 || unsigned(s.pixelFmt)>7 ||
       (s.dstAlpha!=UINT32_MAX && s.dstAlpha>255))throw std::invalid_argument("Invalid GX blend state");
    const bool hasAlpha=aurora::gx::render_target_has_alpha(s.pixelFmt);
    unsigned src=unsigned(s.blendFacSrc),dst=unsigned(s.blendFacDst);
    unsigned cs=1,cd=0,operation=0;
    switch(s.blendMode) {
    case GX_BM_NONE:break;
    case GX_BM_BLEND:
        if(!hasAlpha){if(src==6)src=1;else if(src==7)src=0;if(dst==6)dst=1;else if(dst==7)dst=0;}
        cs=factor(src,false,false);cd=factor(dst,true,false);break;
    case GX_BM_SUBTRACT:cs=cd=1;operation=4;break;
    case GX_BM_LOGIC:
        // Implement the blend-expressible Aurora cases. Integer logic needs
        // a separate hardware state; never silently replace it with COPY.
        switch(s.blendOp){
        case GX_LO_CLEAR:cs=cd=0;break;
        case GX_LO_COPY:break;
        case GX_LO_NOOP:cs=0;cd=1;break;
        case GX_LO_INVAND:cs=0;cd=3;break;
        case GX_LO_OR:cs=cd=1;operation=3;break;
        default:throw std::invalid_argument("GX integer logic operation is not implemented");
        }
        break;
    }
    unsigned as=cs,ad=cd,alphaOperation=operation;
    const bool alphaWrite=hasAlpha && s.alphaUpdate;
    const bool constant=alphaWrite && s.dstAlpha!=UINT32_MAX;
    if(constant){as=19;ad=0;alphaOperation=0;}
    else if(s.blendMode==GX_BM_BLEND){as=factor(src,false,true);ad=factor(dst,true,true);}
    // Aurora uses a constant-alpha source multiplier for dstAlpha. Matching
    // that policy is not a claim of Wii EFB precision or forced-alpha fidelity.
    const unsigned bits=0x60000000u|cs|(operation<<5)|(cd<<8)|(as<<16)|(alphaOperation<<21)|(ad<<24);
    return {(defaults&~0x7fff1fffu)|bits,
        (s.colorUpdate?7u:0u)|(alphaWrite?8u:0u),{0,0,0,constant?float(s.dstAlpha)/255.f:0.f}};
}
}
