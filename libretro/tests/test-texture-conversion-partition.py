# SPDX-License-Identifier: GPL-2.0-or-later
"""Guard the conversion-format partition used by swizzled upload callers."""
from pathlib import Path
import re
import sys

root = Path(sys.argv[1]).resolve()
text = (root / 'hw/xbox/nv2a/pgraph/texture.c').read_text()
size_function = text.split('size_t pgraph_converted_texture_size(', 1)[1].split('\n}', 1)[0]
converted = set(re.findall(r'case (NV097_SET_TEXTURE_FORMAT_COLOR_\w+):', size_function))
linear = dict(re.findall(r'\[(NV097_SET_TEXTURE_FORMAT_COLOR_\w+)\]\s*=\s*\{\s*\d+,\s*(true|false)\s*\}', text))
fused = {
    'NV097_SET_TEXTURE_FORMAT_COLOR_SZ_I8_A8R8G8B8',
    'NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R6G5B5',
}
assert fused <= converted
for fmt in converted:
    assert fmt in linear, ('missing format classification', fmt)
    assert (linear[fmt] == 'false') == (fmt in fused), ('unhandled conversion path', fmt)
print(f'{len(converted)} converted formats: two fused Morton formats and two linear YUV formats PASS')
