#!/usr/bin/env python3
"""Exercise image preparation with synthetic images, without game/BIOS files."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "prepare_xiso", Path(__file__).parents[1] / "tools/prepare-xiso.py")
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)


def image():
    data = bytearray(128 * 1024)
    data[65536:65556] = b"MICROSOFT*XBOX*MEDIA"
    struct.pack_into("<II", data, 65556, 33, 64)
    directory = 33 * 2048
    struct.pack_into("<II", data, directory + 4, 34, 4096)
    data[directory + 13] = 11
    data[directory + 14:directory + 25] = b"default.xbe"
    xbe = 34 * 2048
    data[xbe:xbe + 4] = b"XBEH"
    struct.pack_into("<II", data, xbe + 0x104, 0x10000, 4096)
    struct.pack_into("<I", data, xbe + 0x118, 0x10200)
    struct.pack_into("<I", data, xbe + 0x200, 0x1ec)
    struct.pack_into("<I", data, xbe + 0x3d8, 0xa5a50003)
    return data, xbe + 0x3d8


class PrepareImageTests(unittest.TestCase):
    def test_copy_and_minimal_flag_change(self):
        for prefix in (0, 405798912):
            with self.subTest(prefix=prefix), tempfile.TemporaryDirectory() as directory:
                source, output = Path(directory) / "in.iso", Path(directory) / "out.iso"
                original, flag = image()
                with source.open("wb") as stream:
                    stream.seek(prefix)
                    stream.write(original)
                self.assertEqual(tool.prepare(source, output, True), (prefix, 0xa5a50003))
                expected = bytearray(original)
                struct.pack_into("<I", expected, flag, 0xa5a50002)
                self.assertEqual(output.read_bytes(), expected)
                with source.open("rb") as stream:
                    stream.seek(prefix)
                    self.assertEqual(stream.read(), original)
                with self.assertRaises(FileExistsError):
                    tool.prepare(source, output, True)
                self.assertEqual(output.read_bytes(), expected)

    def test_unmodified_copy_and_invalid_certificate(self):
        with tempfile.TemporaryDirectory() as directory:
            source, output = Path(directory) / "in.iso", Path(directory) / "out.iso"
            original, _ = image()
            source.write_bytes(original)
            tool.prepare(source, output)
            self.assertEqual(output.read_bytes(), original)
            struct.pack_into("<I", original, 34 * 2048 + 0x118, 0xffffffff)
            source.write_bytes(original)
            invalid_output = Path(directory) / "invalid.iso"
            with self.assertRaisesRegex(ValueError, "certificate out of bounds"):
                tool.prepare(source, invalid_output, True)
            self.assertFalse(invalid_output.exists())
            with self.assertRaises(FileExistsError):
                tool.prepare(source, source)


if __name__ == "__main__":
    unittest.main()
