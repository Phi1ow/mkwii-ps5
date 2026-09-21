"""Read register tables in the supplied, unprepared AGC shader headers.

The on-disk pointers are offsets relative to the pointer field itself. After
sceAgcCreateShader, SharpProspero's AgcShader reads absolute pointers at these
same fields. Never use this reader on a prepared header.
"""
import struct


def register_records(header, kind):
    """Return (byte offset, register, value) tuples after checking all bounds."""
    if len(header) < 96 or struct.unpack_from('<II', header) != (0x34333231, 24):
        raise ValueError('Expected the supplied AGC shader header version 24')
    if struct.unpack_from('<I', header, 0x40)[0] != len(header):
        raise ValueError('Shader header size does not match its section')
    try:
        pointer, count_offset = {'context': (24, 91), 'shader': (32, 92)}[kind]
    except KeyError:
        raise ValueError('Register kind must be context or shader') from None
    count = header[count_offset]
    relative = struct.unpack_from('<q', header, pointer)[0]
    if count == 0:
        return []
    start = pointer + relative
    if relative == 0 or start < 96 or start % 8 or start + count * 8 > len(header):
        raise ValueError('Shader register table points outside its aligned payload')
    records = []
    for at in range(start, start + count * 8, 8):
        register, reserved, value = struct.unpack_from('<HHI', header, at)
        if reserved:
            raise ValueError('Unexpected reserved bits in a shader register record')
        records.append((at, register, value))
    return records


def set_register_bits(header, kind, register, mask, bits):
    """Update one unambiguous register, preserving every bit outside mask."""
    if not 0 <= mask <= 0xffffffff or not 0 <= bits <= 0xffffffff or bits & ~mask:
        raise ValueError('Register bits exceed the supplied mask')
    matches = [r for r in register_records(header, kind) if r[1] == register]
    if len(matches) != 1:
        raise ValueError(f'Expected one {kind} register {register:#x}, found {len(matches)}')
    at, _, value = matches[0]
    struct.pack_into('<I', header, at + 4, (value & ~mask) | bits)
