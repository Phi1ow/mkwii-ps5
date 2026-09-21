"""Register-table regressions using the two supplied SharpProspero binaries."""
import importlib.util
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'ps5/tools'))
from agc_header import register_records, set_register_bits
from agc_varyings import set_varying_count

spec = importlib.util.spec_from_file_location('agcpack', ROOT / 'ps5link-sdk/shaders/tools/agcpack.py')
packer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packer)


def supplied_header(stage):
    container = (ROOT / f'ps5link-sdk/shaders/third_party/sharpprospero/mesh_{stage}.sb').read_bytes()
    sections, _, _ = packer.read_sections(container)
    return bytearray(next(s['data'] for s in sections if s['name'] == '.shader_header'))


class HeaderTests(unittest.TestCase):
    def test_ten_varyings_preserve_resources_and_move_semantics(self):
        for stage, pointer, count_at, register, expected_value in [
                ('vs',56,0x56,0x1b1,18),('ps',48,0x50,0x1b6,10)]:
            with self.subTest(stage=stage):
                h=supplied_header(stage);old=bytes(h)
                registers=register_records(h,'shader')
                set_varying_count(h,10)
                self.assertEqual(register_records(h,'shader'),registers)
                self.assertEqual(struct.unpack_from('<H',h,count_at)[0],10)
                at=pointer+struct.unpack_from('<q',h,pointer)[0]
                self.assertEqual(at,len(old))
                self.assertEqual(list(struct.unpack_from('<10I',h,at)),
                    [(15+i)|(i<<8 if stage=='vs' else 0) for i in range(10)])
                self.assertEqual(next(v for _,r,v in register_records(h,'context') if r==register),expected_value)
                self.assertEqual(h[0x118:len(old)],old[0x118:])
                with self.assertRaises(ValueError):set_varying_count(h,10)

    def test_reject_invalid_varying_counts(self):
        for count in [0,1,33]:
            with self.subTest(count=count):
                with self.assertRaises(ValueError):set_varying_count(supplied_header('vs'),count)

    def test_supplied_tables(self):
        # These sequences include the tail entries omitted by the old reader.
        expected = {
            ('vs', 'context'): (0xc8, [0x1b1, 0x1c3, 0x1c2, 0x207, 0x2d3, 0x2e4, 0x291, 0x1ff, 0x2ab, 0x2ce]),
            ('vs', 'shader'): (0x98, [0xc8, 0xc9, 0x80, 0x80, 0x8a, 0x8b]),
            ('ps', 'context'): (0xc8, [0x1c4, 0x1c5, 0x1b3, 0x1b4, 0x1b6, 0x1b8, 0x203, 0x8f, 0x310]),
            ('ps', 'shader'): (0x98, [0x8, 0x9, 0x6, 0x6, 0xa, 0xb]),
        }
        for (stage, kind), (start, ids) in expected.items():
            with self.subTest(stage=stage, kind=kind):
                records = register_records(supplied_header(stage), kind)
                self.assertEqual([r[0] for r in records], list(range(start, start + 8 * len(ids), 8)))
                self.assertEqual([r[1] for r in records], ids)

    def test_register_update_preserves_unrelated_bytes(self):
        for stage, register in [('vs', 0x8a), ('ps', 0xa)]:
            with self.subTest(stage=stage):
                h = supplied_header(stage)
                expected = bytearray(h)
                value = struct.unpack_from('<I', expected, 0xbc)[0]
                struct.pack_into('<I', expected, 0xbc, (value & ~0x3ff) | 0x14a)
                set_register_bits(h, 'shader', register, 0x3ff, 0x14a)
                self.assertEqual(h, expected)

    def test_relocated_table(self):
        h = supplied_header('vs')
        table = bytes(h[0xc8:0x118])
        start = len(h)
        h.extend(table)
        struct.pack_into('<I', h, 0x40, len(h))
        struct.pack_into('<q', h, 24, start - 24)
        records = register_records(h, 'context')
        self.assertEqual(records[-1], (start + 72, 0x2ce, 0))

    def test_reject_bad_pointers(self):
        for offset in [0, -24, 0x100000000, 0xb1, 0x160]:
            with self.subTest(offset=offset):
                h = supplied_header('vs')
                struct.pack_into('<q', h, 24, offset)
                with self.assertRaises(ValueError):
                    register_records(h, 'context')

    def test_reject_bad_header_or_count(self):
        for field, value in [(0, 0), (4, 0), (0x40, 0), (91, 255)]:
            with self.subTest(field=field):
                h = supplied_header('vs')
                h[field] = value
                with self.assertRaises(ValueError):
                    register_records(h, 'context')
        with self.assertRaises(ValueError):
            register_records(b'', 'shader')

    def test_reject_ambiguous_or_missing_update(self):
        h = supplied_header('vs')
        for kind, register in [('shader', 0x80), ('shader', 0xffff), ('context', 0x8a)]:
            with self.subTest(kind=kind, register=register):
                with self.assertRaises(ValueError):
                    set_register_bits(h, kind, register, 0x3ff, 1)
        with self.assertRaises(ValueError):
            set_register_bits(h, 'shader', 0x8a, 0x3ff, 0x400)


if __name__ == '__main__':
    unittest.main(verbosity=2)
