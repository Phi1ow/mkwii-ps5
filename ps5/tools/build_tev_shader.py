#!/usr/bin/env python3
"""Compile the TEV ALU probe with LLVM, then pack using the requested SDK.

LLVM shader ABI: https://releases.llvm.org/18.1.8/docs/AMDGPUUsage.html
AGC slot metadata: SharpProspero/Graphics/Agc/AgcShader.cs.
The container retains 12 user dwords so the validated s12 interpolation ABI
does not change. Only dwords 0..3 are one read-only SMALL buffer descriptor.
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

ROOT = Path(__file__).resolve().parents[2]
args = argparse.ArgumentParser()
args.add_argument('--program', action='store_true', help='Compile the full stage-program diagnostic')
args.add_argument('--direct', action='store_true', help='Compile the sampled direct-material diagnostic')
args.add_argument('--projected', action='store_true', help='Interpolate/project the diagnostic GX STQ coordinate')
args.add_argument('--varyings', action='store_true', help='Eight STQ coordinates and two raster colors')
args.add_argument('--fragment-output', action='store_true', help='Real GX alpha discard and RGBA output')
args.add_argument('--fp32-output', action='store_true', help='Preserve color precision through fragment export')
args.add_argument('--unorm16-output', action='store_true', help='Represent every GX color byte exactly at export')
options = args.parse_args()
if options.fp32_output and options.unorm16_output:args.error('Choose one fragment export format')
if options.unorm16_output:options.fragment_output=True
if options.fp32_output:options.fragment_output=True
if options.fragment_output:options.varyings=True
if options.varyings:options.projected=True
if options.projected:options.direct=True
if options.direct and options.program:
    args.error('--direct and --program are mutually exclusive')
BIN = ROOT / '.tools/clang+llvm-18.1.8-x86_64-pc-windows-msvc/bin'
OUT = ROOT / ('artifacts/tev-program' if options.program else 'artifacts/tev-shaders')
SOURCE = ROOT / ('ps5/gpu/shaders/tev_program_probe.c' if options.program else 'ps5/gpu/shaders/tev_probe.c')
if options.direct:
    OUT = ROOT / 'artifacts/tev-direct'
    SOURCE = ROOT / 'ps5/gpu/shaders/tev_direct_probe.c'
if options.projected:OUT=ROOT/'artifacts/tev-projected'
if options.varyings:OUT=ROOT/'artifacts/tev-varyings'
if options.fragment_output:OUT=ROOT/'artifacts/tev-fragment'
if options.fp32_output:OUT=ROOT/'artifacts/tev-fragment-fp32'
if options.unorm16_output:OUT=ROOT/'artifacts/tev-fragment-unorm16'
CPU='gfx1010' if options.projected else 'gfx1030'
OUT.mkdir(parents=True, exist_ok=True)


def run(tool, *args):
    subprocess.run([str(BIN / tool), *map(str, args)], check=True)


for tool in ('clang.exe', 'llc.exe'):
    version = subprocess.check_output([str(BIN / tool), '--version'], text=True)
    if not re.search(r'version 18\.1\.8\b', version):
        raise RuntimeError('The shader ABI pipeline requires LLVM 18.1.8: ' + tool)
run('clang.exe', '-target', 'amdgcn-amd-unknown', '-mcpu='+CPU,
    '-mwavefrontsize64', '-nogpulib', '-ffreestanding', '-O2',
    '-fno-fast-math', '-ffp-contract=off', '-S', '-emit-llvm',
    *(['-DMKW_GX_PROJECTED'] if options.projected else []),*(['-DMKW_GX_VARYINGS'] if options.varyings else []),
    *(['-DMKW_GX_FRAGMENT_OUTPUT'] if options.fragment_output else []),
    *(['-DMKW_GX_FP32_OUTPUT'] if options.fp32_output else []),
    *(['-DMKW_GX_UNORM16_OUTPUT'] if options.unorm16_output else []),SOURCE, '-o', OUT / 'tev_probe.ll')
ir = (OUT / 'tev_probe.ll').read_text(encoding='utf-8')
if options.fragment_output and 'call void @llvm.amdgcn.kill(' not in ir:
    raise RuntimeError('Fragment output shader lacks the pixel kill intrinsic')
if options.fp32_output and '@llvm.amdgcn.exp.f32(' not in ir:
    raise RuntimeError('Expected uncompressed FP32 color export')
entries = re.findall(r'^define .*$', ir, flags=re.M)
if len(entries) != 1 or '@tev_probe(' not in entries[0]:
    raise RuntimeError('Expected one fully inlined shader entry')
old = entries[0]
new = old.replace('define dso_local void', 'define amdgpu_ps void')
new, vectors = re.subn(r'<4 x i32> noundef', '<4 x i32> inreg noundef', new)
new, masks = re.subn(r'(?<!x )i32 noundef %3', 'i32 inreg noundef %3', new)
if vectors != 3 or masks != 1 or new == old:
    raise RuntimeError('Clang entry ABI changed; do not guess register mapping')
ir = ir.replace(old, new)
(OUT / 'tev_probe-ps.ll').write_text(ir, encoding='utf-8', newline='\n')
for kind, suffix in [('asm', 's'), ('obj', 'o')]:
    run('llc.exe', '-mtriple=amdgcn-amd-unknown', '-mcpu='+CPU,
        '-verify-machineinstrs', '-filetype=' + kind,
        OUT / 'tev_probe-ps.ll', '-o', OUT / ('tev_probe.' + suffix))
assembly = (OUT / 'tev_probe.s').read_text(encoding='utf-8')
stats = {key: int(re.search(r'; ' + key + r': (\d+)', assembly)[1])
         for key in ['NumSgprs', 'NumVgprs', 'ScratchSize', 'SGPRBlocks', 'VGPRBlocks']}
if stats['ScratchSize'] or stats['NumSgprs'] > 104 or stats['NumVgprs'] > 256:
    raise RuntimeError('Shader requires scratch or unsupported register allocation')
if not re.search(r's_mov_b32 m0, s12', assembly):
    raise RuntimeError('Interpolation mask no longer arrives in s12')
if options.direct:
    if not re.search(r'image_sample_b\b', assembly) or not re.search(r's_buffer_load_dwordx8\b', assembly):
        raise RuntimeError('Direct shader lacks scalar texture descriptor loads or biased sampling')
    if 's_wqm_b64' not in assembly:
        raise RuntimeError('Implicit derivatives require whole-quad execution')

spec = importlib.util.spec_from_file_location('agcpack', ROOT / 'ps5link-sdk/shaders/tools/agcpack.py')
packer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packer)
obj = (OUT / 'tev_probe.o').read_bytes()
sections, _, _ = packer.read_sections(obj)
if any(s['type'] in (4, 9) and s['size'] for s in sections):
    raise RuntimeError('Shader has unresolved relocations')
program = next(s['data'] for s in sections if s['name'] == '.text')
base = (ROOT / 'ps5link-sdk/shaders/third_party/sharpprospero/mesh_ps.sb').read_bytes()
if packer.pack(base, packer.own_program(base)) != base:
    raise RuntimeError('Original shader container roundtrip differs')
container = packer.texture_container(base)
sections, entsize, strndx = packer.read_sections(container)
header = next(s for s in sections if s['name'] == '.shader_header')
h = bytearray(header['data'])
struct.pack_into('<H', h, 0x160, 0x8000)  # ReadOnly slot 0: four dwords at s0
struct.pack_into('<H', h, 0x142, 0)       # No sampler resource
set_register_bits(h, 'shader', 0xA, 0x3ff,
                  stats['VGPRBlocks'] | (stats['SGPRBlocks'] << 6))
if options.varyings:set_varying_count(h,11)
if options.fragment_output:
    # DB_SHADER_CONTROL.KILL_ENABLE, AMD register reference. Keep the supplied
    # Z-order preference; interaction with real depth writes needs its own test.
    set_register_bits(h,'context',0x203,0x40,0x40)
if options.fp32_output:
    # SPI_SHADER_COL_FORMAT.COL0_EXPORT_FORMAT = SPI_SHADER_32_ABGR (9).
    # AMD CIK register reference; console validation is tracked separately.
    set_register_bits(h,'context',0x1c5,0xf,9)
if options.unorm16_output:set_register_bits(h,'context',0x1c5,0xf,5)
header['data'] = bytes(h)
container = packer.write_elf(container, sections, entsize, strndx)
container = packer.pack(container, program)
if packer.own_program(container) != program:
    raise RuntimeError('Packed program differs from LLVM machine code')
(OUT / 'tev_probe.bin').write_bytes(program)
(OUT / 'tev_probe.sb').write_bytes(container)
report = dict(compiler='LLVM 18.1.8', cpu=CPU, waveSize=64, projectedCoordinates=options.projected,
    fragmentOutput=options.fragment_output,alphaDiscard=options.fragment_output,
    fp32Output=options.fp32_output,
    unorm16Output=options.unorm16_output,
    headerReaderSha256=hashlib.sha256((ROOT/'ps5/tools/agc_header.py').read_bytes()).hexdigest(),
    gxVaryings=options.varyings,varyingCount=11 if options.varyings else 2,
    varyingHeaderSha256=hashlib.sha256((ROOT/'ps5/tools/agc_varyings.py').read_bytes()).hexdigest() if options.varyings else None,
    stageProgram=options.program, directMaterial=options.direct,
    stageSha256=hashlib.sha256((ROOT / 'ps5/gpu/tev_stage.h').read_bytes()).hexdigest() if options.program or options.direct else None,
    sourceSha256=hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
    arithmeticSha256=hashlib.sha256((ROOT / 'ps5/gpu/tev_integer.h').read_bytes()).hexdigest(),
    containerSha256=hashlib.sha256(container).hexdigest(), codeBytes=len(program),
    registers=stats, userDwords=12, readOnlySmallBufferDword=0,
    interpolationMaskSgpr=12, nativeTested=False, fullGxRenderer=False)
(OUT / 'build.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
print(json.dumps(report, indent=2))
