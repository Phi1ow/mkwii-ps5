"""Extend the supplied shader pair's parameter interface, retaining its ABI.

PPSA99533 measured the prepared pointers/counts, exact low-byte semantic
matching and output slot bits 8..12. IDs 15 and 16 are the original pair.
Register field reference (older AMD hardware; verify on this console):
https://www.amd.com/content/dam/amd/en/documents/radeon-tech-docs/programmer-references/SI_3D_registers.pdf
SPI_VS_OUT_CONFIG VS_EXPORT_COUNT[5:1] is count-1;
SPI_PS_IN_CONTROL NUM_INTERP[5:0] is count.
"""
import struct
from agc_header import register_records, set_register_bits


def set_varying_count(header, count):
    if not 2 <= count <= 32:
        raise ValueError('Expected 2..32 parameter vectors')
    register_records(header, 'context')  # Validate the unprepared header first.
    stage = header[90]
    if stage == 2:
        pointer, count_at, original = 56, 0x56, (0x0f, 0x110)
        register, mask, value = 0x1b1, 0x3e, (count - 1) << 1
    elif stage == 1:
        pointer, count_at, original = 48, 0x50, (0x0f, 0x10)
        register, mask, value = 0x1b6, 0x3f, count
    else:
        raise ValueError('Only the supplied geometry/pixel containers are supported')
    start = pointer + struct.unpack_from('<q', header, pointer)[0]
    if start != 0x90 or struct.unpack_from('<H', header, count_at)[0] != 2:
        raise ValueError('Expected the original two-parameter interface')
    if struct.unpack_from('<II', header, start) != original or len(header) % 8:
        raise ValueError('Unexpected original semantic table or alignment')
    appended = len(header)
    for slot in range(count):
        header.extend(struct.pack('<I', (15 + slot) | (slot << 8 if stage == 2 else 0)))
    header.extend(bytes((-len(header)) % 8))
    struct.pack_into('<I', header, 0x40, len(header))
    struct.pack_into('<q', header, pointer, appended - pointer)
    struct.pack_into('<H', header, count_at, count)
    set_register_bits(header, 'context', register, mask, value)
