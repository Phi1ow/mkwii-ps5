#!/usr/bin/env python3
"""Differential stage/register oracle, independent of the C stage evaluator.

Based on Aurora shader.cpp selector tables, tev_* equations and final alpha
comparison. All floating operations explicitly round to f32. WGSL round uses
ties to even: https://www.w3.org/TR/WGSL/#round-builtin
"""
import ctypes as c
import hashlib
import json
import os
from pathlib import Path
import struct

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/tev-program'
def f32(v):return struct.unpack('<f',struct.pack('<f',v))[0]
class Value(c.Structure):_fields_=[(k,c.c_float) for k in ('r','g','b','a')]
class Registers(c.Structure):_fields_=[(k,Value) for k in ('prev','reg0','reg1','reg2')]
class Stage(c.Structure):_fields_=[('words',c.c_uint*16),('konst',Value)]
assert c.sizeof(Stage)==80 and c.sizeof(Registers)==64
runtime_directory=os.add_dll_directory(str(ROOT/'.tools/llvm-mingw-20260908-ucrt-x86_64/bin'))
lib=c.CDLL(str(OUT/'stage.dll'))
lib.tev_step.argtypes=[c.POINTER(Registers),c.POINTER(Stage),c.POINTER(Value),c.POINTER(Value),c.POINTER(Value)]
lib.tev_step.restype=None
lib.tev_alpha.argtypes=[c.c_float,c.c_uint];lib.tev_alpha.restype=c.c_int
lib.tev_raster.argtypes=[c.POINTER(Stage),c.POINTER(Value),c.POINTER(Value),c.POINTER(Value),c.POINTER(Value)]
lib.tev_raster.restype=None
def vals(v):return [v.r,v.g,v.b,v.a]
def wrap(v):return f32(v-(v//256)*256)
def splat(v):return [v]*4
def swap(v,flags):return [v[(flags>>(i*2))&3] for i in range(4)]
def choose_color(regs,sel,tex,ras,konst):
    if sel<8:return splat(regs[sel//2][3]) if sel%2 else regs[sel//2][:]
    return {8:tex,9:splat(tex[3]),10:ras,11:splat(ras[3]),12:splat(255),13:splat(127.5),14:konst,15:splat(0)}[sel][:]
def choose_alpha(regs,sel,tex,ras,konst):return regs[sel][3] if sel<4 else [tex[3],ras[3],konst[3],0][sel-4]
def packed(v,op):
    x=v[0]
    if op>=10:x=f32(x+f32(v[1]*256))
    if op>=12:x=f32(x+f32(v[2]*65536))
    return round(x)
def evaluate(a,b,cc,d,flags,color_a,color_b):
    op=flags&15
    if op<2:
        a,b,cc,d=map(round,(a,b,cc,d))
        scale=(flags>>4)&3;factor=(1,2,4,1)[scale]
        bias=(((flags>>8)&3)-1)*128
        weight=cc+cc//128
        numerator=(a*(256-weight)+b*weight)*factor
        delta=(numerator+(0 if scale==3 else 127 if op else 128))//256
        result=(d+bias)*factor+(-delta if op else delta)
        if scale==3:result//=2
    else:
        lhs,rhs=(packed(color_a,op),packed(color_b,op)) if op<14 else (round(a),round(b))
        passed=lhs==rhs if op%2 else lhs>rhs
        result=f32(d+(cc if passed else 0))
    return f32(max(0 if flags&64 else -1024,min(255 if flags&64 else 1023,result)))
def stage_ref(regs,words,konst,tex,ras):
    tex,ras=swap(tex,words[4]),swap(ras,words[5])
    color=[choose_color(regs,(words[0]>>(i*4))&15,tex,ras,konst) for i in range(4)]
    alpha=[choose_alpha(regs,(words[1]>>(i*3))&7,tex,ras,konst) for i in range(4)]
    for i in range(3):color[i]=list(map(wrap,color[i]));alpha[i]=wrap(alpha[i])
    result=[evaluate(*(v[lane] for v in color),words[2],color[0],color[1]) for lane in range(3)]
    result.append(evaluate(*alpha,words[3],color[0],color[1]))
    regs[(words[2]>>10)&3][:3]=result[:3]
    regs[(words[3]>>10)&3][3]=result[3]
    return result
def raster_ref(words,r0,r1,ind):
    chan=words[8]
    if chan<6:return [r0,r1][chan%2]
    if chan in (7,8) and words[10]<4 and words[12]:
        value=int(ind[words[12]-1])&[0xf8,0xe0,0xf0,0xf8][words[11]]
        return splat(f32(value*f32(255/248)) if chan==8 else value)
    return splat(0)
def alpha_ref(alpha,flags):
    value=round(min(wrap(alpha),255))
    def cmp(op,ref):return [False,value<ref,value==ref,value<=ref,value>ref,value!=ref,value>=ref,True][op]
    a=cmp(flags&7,(flags>>8)&255);b=cmp((flags>>3)&7,(flags>>16)&255)
    return [a and b,a or b,a!=b,a==b][(flags>>6)&3]
def unorm_export(byte):
    # cvt_pkrtz f32 -> positive f16 (toward zero), then AGC round-by-half UNorm.
    normalized=f32(wrap(byte)*f32(1/255))
    bits=struct.unpack('<H',struct.pack('<e',normalized))[0]
    half=struct.unpack('<e',struct.pack('<H',bits))[0]
    if half>normalized:half=struct.unpack('<e',struct.pack('<H',bits-1))[0]
    return max(0,min(255,int(half*255+0.5)))

def corner(regs,words,konst,expected):
    host=Registers(*(Value(*v) for v in regs));s=Stage((c.c_uint*16)(*words),Value(*konst))
    value=Value();zero=Value(0,0,0,0)
    lib.tev_step(c.byref(host),c.byref(s),c.byref(zero),c.byref(zero),c.byref(value))
    assert vals(value)==expected,('corner',vals(value),expected)
    assert stage_ref([r[:] for r in regs],words,konst,[0]*4,[0]*4)==expected
    return host
identity=0xe4
words=[0|(15<<4)|(15<<8)|(15<<12),7|(7<<3)|(7<<6)|(7<<9),0x140,0x140,identity,identity]+[0]*10
corner([[-0.25,-0.25,-0.25,0],[0]*4,[0]*4,[0]*4],words,[0]*4,[255,255,255,0])
words[0]=13|(14<<4)|(12<<8)|(15<<12);words[2]=11|0x140
corner([[0]*4 for _ in range(4)],words,[0,128,0,0],[255,255,255,0])
words[0]=0|(2<<4)|(15<<8)|(15<<12);words[2]=1|0x140
words[1]=7|(7<<3)|(6<<6)|(7<<9);words[3]=8|0x140
corner([[10,20,30,40],[5,6,7,8],[0]*4,[0]*4],words,[0,0,0,123],[0,0,0,123])
# Different output destinations preserve the opposite channels in both banks.
words[2]|=1<<10;words[3]|=2<<10
bank=corner([[10,20,30,40],[5,6,7,8],[91,92,93,94],[0]*4],words,[0,0,0,123],[0,0,0,123])
assert vals(bank.reg0)==[0,0,0,8] and vals(bank.reg1)==[91,92,93,123] and vals(bank.prev)==[10,20,30,40]

data=(OUT/'programs.bin').read_bytes()
assert len(data)==2048*2048
cases=[];stage_checks=0;raster_checks=0;alpha_checks=0
observed_counts=set();observed_color=set();observed_alpha=set();fractional_compares=0
for n in range(2048):
    record=data[n*2048:(n+1)*2048]
    count,af,disabled,_=struct.unpack_from('<4I',record)
    assert 1<=count<=16;observed_counts.add(count)
    host=Registers.from_buffer_copy(record,16)
    initial=struct.unpack_from('<16f',record,16);regs=[list(initial[i:i+4]) for i in range(0,16,4)]
    rast0=list(struct.unpack_from('<4f',record,1360));rast1=list(struct.unpack_from('<4f',record,1376))
    indirect=list(struct.unpack_from('<4f',record,1392))
    for k in range(count):
        s=Stage.from_buffer_copy(record,80+k*80);w=list(s.words);konst=vals(s.konst)
        observed_color.update((w[0]>>(i*4))&15 for i in range(4));observed_alpha.update((w[1]>>(i*3))&7 for i in range(4))
        tex=list(struct.unpack_from('<4f',record,1408+k*16)) if w[9] else splat(disabled)
        ras=raster_ref(w,rast0,rast1,indirect)
        actual_ras=Value();lib.tev_raster(c.byref(s),c.byref(Value(*rast0)),c.byref(Value(*rast1)),c.byref(Value(*indirect)),c.byref(actual_ras))
        assert vals(actual_ras)==ras,(n,k,'raster',vals(actual_ras),ras);raster_checks+=1
        expected=stage_ref(regs,w,konst,tex,ras)
        actual=Value();lib.tev_step(c.byref(host),c.byref(s),c.byref(Value(*tex)),c.byref(Value(*ras)),c.byref(actual))
        assert vals(actual)==expected,(n,k,'stage',vals(actual),expected)
        flat=[v for reg in regs for v in reg]
        assert struct.unpack('<16f',bytes(host))==tuple(flat),(n,k,'register write')
        stage_checks+=1
        if (w[2]&15)>=8 and any(v!=int(v) for v in expected):fractional_compares+=1
    passed=alpha_ref(expected[3],af)
    assert bool(lib.tev_alpha(expected[3],af))==passed;alpha_checks+=1
    rgb=list(map(unorm_export,expected[:3])) if passed else [0,0,0]
    pixel=((255 if passed else 0)<<24)|(rgb[0]<<16)|(rgb[1]<<8)|rgb[2]
    cases.append({'expected':pixel})
assert observed_counts==set(range(1,17)) and observed_color==set(range(16)) and observed_alpha==set(range(8))
assert fractional_compares>0
# Every alpha comparator/operator/ref endpoint, including equality ties.
for op in range(4):
    for a in range(8):
        for b in range(8):
            for ref in (0,1,127,128,254,255):
                flags=a|(b<<3)|(op<<6)|(ref<<8)|((255-ref)<<16)
                for value in (-1,0,0.5,1,126.5,127,127.5,128,254,254.5,255,255.5,256):
                    assert bool(lib.tev_alpha(value,flags))==alpha_ref(value,flags),(value,flags)
                    alpha_checks+=1
(OUT/'cases.json').write_text(json.dumps(cases)+'\n',encoding='utf-8')
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
report=dict(programs=2048,stageAndRegisterChecks=stage_checks,rasterChecks=raster_checks,
    precisionAndWriteCorners=4,
    alphaChecks=alpha_checks,fractionalComparisonResults=fractional_compares,
    allStageCounts=True,allColorAndAlphaSelectors=True,passed=True,nativeTested=False,
    stageSha256=sha(ROOT/'ps5/gpu/tev_stage.h'),arithmeticSha256=sha(ROOT/'ps5/gpu/tev_integer.h'),
    programEncoderSha256=sha(ROOT/'ps5/gpu/gx_tev_program.cpp'),programsSha256=sha(OUT/'programs.bin'),
    casesSha256=sha(OUT/'cases.json'),fullGxRenderer=False)
(OUT/'host-result.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
message=f'PASS TEV programs: {stage_checks} stages and complete register banks, {raster_checks} raster selections, {alpha_checks} alpha checks; 2048 native cases prepared'
(OUT/'host.log').write_text(message+'\n',encoding='utf-8');print(message)
