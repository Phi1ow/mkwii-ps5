#!/usr/bin/env python3
"""Compile the WiiCompiled color-copy equations for the validated AGC PS ABI."""
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct
import subprocess
from agc_header import set_register_bits

ROOT=Path(__file__).resolve().parents[2]
BIN=ROOT/'.tools/clang+llvm-18.1.8-x86_64-pc-windows-msvc/bin'
OUT=ROOT/'artifacts/gx-copy-shader'
SOURCE=ROOT/'ps5/gpu/shaders/gx_copy.c'
OUT.mkdir(parents=True,exist_ok=True)
def run(tool,*args):subprocess.run([str(BIN/tool),*map(str,args)],check=True)
for tool in ('clang.exe','llc.exe'):
    if not re.search(r'version 18\.1\.8\b',subprocess.check_output([str(BIN/tool),'--version'],text=True)):
        raise RuntimeError('Expected pinned LLVM 18.1.8')
run('clang.exe','-target','amdgcn-amd-unknown','-mcpu=gfx1010','-mwavefrontsize64','-nogpulib',
    '-ffreestanding','-O2','-fno-fast-math','-ffp-contract=off','-S','-emit-llvm',SOURCE,'-o',OUT/'copy.ll')
ir=(OUT/'copy.ll').read_text()
entries=re.findall(r'^define .*$',ir,re.M)
if len(entries)!=1 or '@gx_copy(' not in entries[0]:raise RuntimeError('Expected one inlined copy entry')
old=entries[0];new=old.replace('define dso_local void','define amdgpu_ps void')
new,n=re.subn(r'<4 x i32> noundef','<4 x i32> inreg noundef',new)
new,m=re.subn(r'(?<!x )i32 noundef %3','i32 inreg noundef %3',new)
if n!=3 or m!=1 or new==old:raise RuntimeError('Unexpected copy entry ABI')
(OUT/'copy-ps.ll').write_text(ir.replace(old,new),newline='\n')
for kind,suffix in [('asm','s'),('obj','o')]:
    run('llc.exe','-mtriple=amdgcn-amd-unknown','-mcpu=gfx1010','-verify-machineinstrs',
        '-filetype='+kind,OUT/'copy-ps.ll','-o',OUT/('copy.'+suffix))
asm=(OUT/'copy.s').read_text()
stats={k:int(re.search(r'; '+k+r': (\d+)',asm)[1]) for k in ['NumSgprs','NumVgprs','ScratchSize','SGPRBlocks','VGPRBlocks']}
if stats['ScratchSize'] or stats['NumSgprs']>104 or stats['NumVgprs']>256:raise RuntimeError('Unsupported allocation')
for pattern in [r's_mov_b32 m0, s12',r'image_sample(?:_b)?\b',r's_buffer_load_dwordx8\b',r's_wqm_b64',r'buffer_load_dword\b']:
    if not re.search(pattern,asm):raise RuntimeError('Missing expected instruction '+pattern)
spec=importlib.util.spec_from_file_location('agcpack',ROOT/'ps5link-sdk/shaders/tools/agcpack.py')
packer=importlib.util.module_from_spec(spec);spec.loader.exec_module(packer)
sections,_,_=packer.read_sections((OUT/'copy.o').read_bytes())
if any(s['type'] in (4,9) and s['size'] for s in sections):raise RuntimeError('Unresolved shader relocation')
code=next(s['data'] for s in sections if s['name']=='.text')
base=(ROOT/'ps5link-sdk/shaders/third_party/sharpprospero/mesh_ps.sb').read_bytes()
if packer.pack(base,packer.own_program(base))!=base:raise RuntimeError('Base container roundtrip differs')
container=packer.texture_container(base);sections,entsize,strndx=packer.read_sections(container)
header=next(s for s in sections if s['name']=='.shader_header');h=bytearray(header['data'])
struct.pack_into('<H',h,0x160,0x8000) # one small read-only buffer at s0..3
struct.pack_into('<H',h,0x142,0) # no direct sampler binding
set_register_bits(h,'shader',0xa,0x3ff,stats['VGPRBlocks']|(stats['SGPRBlocks']<<6))
header['data']=bytes(h)
container=packer.pack(packer.write_elf(container,sections,entsize,strndx),code)
if packer.own_program(container)!=code:raise RuntimeError('Packed code differs')
(OUT/'copy.sb').write_bytes(container)
report={'compiler':'LLVM 18.1.8','cpu':'gfx1010','registers':stats,'codeBytes':len(code),'nativeTested':False,
    'containerSha256':hashlib.sha256(container).hexdigest(),'sources':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest()
    for p in [SOURCE,ROOT/'ps5/gpu/gx_copy_color.h',ROOT/'ps5/gpu/gx_depth_offset.h',ROOT/'ps5/gpu/gx_copy_options.h',Path(__file__),ROOT/'ps5/tools/agc_header.py']}}
(OUT/'build.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
