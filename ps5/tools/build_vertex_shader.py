#!/usr/bin/env python3
"""Compile an AGC vertex body using the supplied mesh VS entry/interface.

Only this known container/input ABI is supported. The prefix comes from its
own program, and must roundtrip exactly through LLVM's gfx1010 assembler.
No Mesa/other renderer code is imported. LLVM calling convention reference:
https://releases.llvm.org/18.1.8/docs/AMDGPUUsage.html
"""
import hashlib
import argparse
import importlib.util
import json
from pathlib import Path
import re
import struct
import subprocess
from agc_header import set_register_bits
from agc_varyings import set_varying_count

ROOT=Path(__file__).resolve().parents[2]
BIN=ROOT/'.tools/clang+llvm-18.1.8-x86_64-pc-windows-msvc/bin'
OUT=ROOT/'artifacts/gx-vertex'
SOURCE=ROOT/'ps5/gpu/shaders/gx_vertex_probe.c'
parser=argparse.ArgumentParser();parser.add_argument('--raw',action='store_true')
parser.add_argument('--transforms',action='store_true');parser.add_argument('--lighting',action='store_true')
parser.add_argument('--texgen',action='store_true');parser.add_argument('--varyings',action='store_true');options=parser.parse_args()
if options.varyings:options.texgen=True
if options.texgen:options.lighting=True
if options.lighting:options.transforms=True
if options.transforms:options.raw=True
if options.raw:
    SOURCE=ROOT/'ps5/gpu/shaders/gx_vertex_raw.c'
    OUT=ROOT/'artifacts/gx-vertex-raw'
if options.transforms:OUT=ROOT/'artifacts/gx-transforms'
if options.lighting:OUT=ROOT/'artifacts/gx-lighting'
if options.texgen:OUT=ROOT/'artifacts/gx-texgen'
if options.varyings:OUT=ROOT/'artifacts/gx-varyings'
OUT.mkdir(parents=True,exist_ok=True)
spec=importlib.util.spec_from_file_location('agcpack',ROOT/'ps5link-sdk/shaders/tools/agcpack.py')
pack=importlib.util.module_from_spec(spec);spec.loader.exec_module(pack)
def run(tool,*args):subprocess.run([str(BIN/tool),*map(str,args)],check=True)
def code_of_obj(file):
    sections,_,_=pack.read_sections(file.read_bytes())
    if any(s['type'] in (4,9) and s['size'] for s in sections):raise RuntimeError('Shader has relocations')
    return next(s['data'] for s in sections if s['name']=='.text')
def assemble(text,name):
    source=OUT/(name+'.s');obj=OUT/(name+'.o');source.write_text(text,encoding='utf-8',newline='\n')
    run('llvm-mc.exe','--triple=amdgcn-amd-amdhsa','--mcpu=gfx1010',
        '-mattr=+wavefrontsize64,-wavefrontsize32','-filetype=obj',source,'-o',obj)
    return code_of_obj(obj)
for tool in ('clang.exe','llc.exe','llvm-mc.exe'):
    if not re.search(r'version 18\.1\.8\b',subprocess.check_output([str(BIN/tool),'--version'],text=True)):
        raise RuntimeError('Expected LLVM 18.1.8')
base=(ROOT/'ps5link-sdk/shaders/third_party/sharpprospero/mesh_vs.sb').read_bytes()
original=pack.own_program(base)
if pack.pack(base,original)!=base:raise RuntimeError('Supplied vertex container does not roundtrip')
# Allocation request uses s3: [7:0] vertex lanes, [15:8] primitive lanes.
# v0 holds packed primitive indices, v5 the vertex ID. User data begins at s8.
prefix='''\t.text
    s_inst_prefetch 0x3
    s_bfe_u32 vcc_hi, s3, 0x80008
    s_lshl_b32 vcc_lo, vcc_hi, 12
    s_and_b32 s0, s3, 0xff
    s_or_b32 m0, s0, vcc_lo
    s_sendmsg sendmsg(MSG_GS_ALLOC_REQ)
    s_sub_i32 vcc_lo, 64, vcc_hi
    s_lshr_b64 exec, -1, vcc_lo
    exp prim v0, off, off, off done
    s_sub_i32 vcc_lo, 64, s0
    s_waitcnt expcnt(0)
    s_lshr_b64 exec, -1, vcc_lo
'''
prefix_code=assemble(prefix,'entry-prefix')
if prefix_code!=original[:len(prefix_code)]:raise RuntimeError('AGC primitive entry differs from supplied shader')
run('clang.exe','-target','amdgcn-amd-unknown','-mcpu=gfx1010','-mwavefrontsize64','-nogpulib',
    '-ffreestanding','-O2','-fno-fast-math','-ffp-contract=off','-S','-emit-llvm',
    *(['-DMKW_GX_TRANSFORMS'] if options.transforms else []),*(['-DMKW_GX_LIGHTING'] if options.lighting else []),
    *(['-DMKW_GX_TEXGEN'] if options.texgen else []),*(['-DMKW_GX_VARYINGS'] if options.varyings else []),SOURCE,'-o',OUT/'vertex.ll')
ir=(OUT/'vertex.ll').read_text(encoding='utf-8');entries=re.findall(r'^define .*$',ir,re.M)
if len(entries)!=1 or '@gx_vertex(' not in entries[0]:raise RuntimeError('Expected one fully inlined vertex body')
old=entries[0];new=old.replace('define dso_local void','define amdgpu_vs void')
new,n=re.subn(r'<4 x i32> noundef','<4 x i32> inreg noundef',new)
if n!=2 or new==old:raise RuntimeError('Unexpected Clang entry ABI')
ir=ir.replace(old,new);(OUT/'vertex-vs.ll').write_text(ir,encoding='utf-8',newline='\n')
run('llc.exe','-mtriple=amdgcn-amd-unknown','-mcpu=gfx1010','-verify-machineinstrs','-filetype=asm',OUT/'vertex-vs.ll','-o',OUT/'body.s')
assembly=(OUT/'body.s').read_text(encoding='utf-8')
stats={k:int(re.search(r'; '+k+r': (\d+)',assembly)[1]) for k in ['NumSgprs','NumVgprs','ScratchSize']}
if stats['ScratchSize']:raise RuntimeError('Vertex shader requires scratch')
start=assembly.index('gx_vertex:');end=assembly.index('.Lfunc_end',start)
body=assembly[start:end]
if any(s in body for s in ['s_setpc','s_swappc','s_getpc','flat_','scratch_']):
    raise RuntimeError('Vertex body contains unsupported addressing/control flow')
# Ordinary LLVM VS ABI: descriptors s0..7, vertex ID v0. AGC reserves system
# registers s0..7 and v0..4. Shift ALL named physical registers consistently,
# including temporary ranges; special registers (exec/vcc/m0) stay unchanged.
def shift(match):
    kind,one,lo,hi=match.groups();delta=8 if kind=='s' else 5
    if one is not None:return kind+str(int(one)+delta)
    return f'{kind}[{int(lo)+delta}:{int(hi)+delta}]'
body=re.sub(r'\b([sv])(?:(\d+)\b|\[(\d+):(\d+)\])',shift,body)
sgprs=stats['NumSgprs']+8;vgprs=stats['NumVgprs']+5
if sgprs>104 or vgprs>256:raise RuntimeError('Shifted register allocation exceeds supported range')
padding_word=bytes.fromhex('00009fbf')
padding_words=0
while original[-4*(padding_words+1):len(original)-4*padding_words if padding_words else None]==padding_word:
    padding_words+=1
if padding_words!=24:raise RuntimeError('Unexpected supplied shader padding')
program=assemble(prefix+body+'\n'+'    s_code_end\n'*padding_words,'vertex')
if not program.startswith(prefix_code):raise RuntimeError('Final entry prefix changed')
container=pack.pack(base,program)
sections,entsize,strndx=pack.read_sections(container)
header=next(s for s in sections if s['name']=='.shader_header');h=bytearray(header['data'])
# wave64: VGPRS in groups of four; SGPRS in groups of eight.
set_register_bits(h,'shader',0x8a,0x3ff,((vgprs-1)//4)|(((sgprs-1)//8)<<6))
if options.varyings:set_varying_count(h,11)
header['data']=bytes(h);container=pack.write_elf(container,sections,entsize,strndx)
if pack.own_program(container)!=program:raise RuntimeError('Container program mismatch')
(OUT/'vertex.bin').write_bytes(program);(OUT/'vertex.sb').write_bytes(container)
sha=lambda b:hashlib.sha256(b).hexdigest()
report=dict(compiler='LLVM 18.1.8',cpu='gfx1010',waveSize=64,sourceSha256=sha(SOURCE.read_bytes()),
    headerReaderSha256=sha((ROOT/'ps5/tools/agc_header.py').read_bytes()),
    gxVaryings=options.varyings,varyingCount=11 if options.varyings else 2,
    varyingHeaderSha256=sha((ROOT/'ps5/tools/agc_varyings.py').read_bytes()) if options.varyings else None,
    rawGeometry=options.raw,geometryAbiSha256=sha((ROOT/'ps5/gpu/gx_vertex_format.h').read_bytes()) if options.raw else None,
    gxTransforms=options.transforms,transformAbiSha256=sha((ROOT/'ps5/gpu/gx_transform_format.h').read_bytes()) if options.transforms else None,
    gxLighting=options.lighting,lightingSha256=sha((ROOT/'ps5/gpu/gx_lighting.h').read_bytes()) if options.lighting else None,
    gxTexgen=options.texgen,texgenSha256=sha((ROOT/'ps5/gpu/gx_texgen.h').read_bytes()) if options.texgen else None,
    originalContainerSha256=sha(base),prefixBytes=len(prefix_code),prefixMatchesOriginal=True,codeBytes=len(program),paddingWords=padding_words,
    compilerRegisters=stats,physicalSgprs=sgprs,physicalVgprs=vgprs,containerSha256=sha(container),
    nativeTested=False,fullGxVertexShader=False,resourceAbi='AGC: read-only s8..11, constants s12..15; vertex ID v5; primitive v0, lane counts s3')
(OUT/'build.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps(report,indent=2))
