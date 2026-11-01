# SPDX-License-Identifier: GPL-2.0-or-later
"""Extract the known common GLSL helper definitions from test shader sources."""
import re

COMMON_HELPERS=("sign1","sign2","sign3","sign3_to_0_to_1","remapCubeTo2D","remap2DToCube")
start=re.compile(r"(?:float|vec2|vec3) ("+"|".join(COMMON_HELPERS)+r")\([^\n]*\) \{")
def extract_common_helpers(source):
    found={};spans=[]
    for match in start.finditer(source):
        i=source.index("{",match.start())+1;depth=1
        while depth:
            depth+=(source[i]=="{")-(source[i]=="}");i+=1
        assert source[i]=="\n";i+=1
        assert match.group(1) not in found
        found[match.group(1)]=source[match.start():i];spans.append((match.start(),i))
    for a,b in reversed(spans):source=source[:a]+source[b:]
    return found,source
