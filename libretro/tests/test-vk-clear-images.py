# SPDX-License-Identifier: LGPL-2.1-or-later
"""Actual masked-clear pipeline and command block, exact color/depth/stencil GPU oracle."""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile
import time

repo = Path(sys.argv[1]).resolve()
cc = sys.argv[2] if len(sys.argv) > 2 else 'cc'
draw = (repo / 'hw/xbox/nv2a/pgraph/vk/draw.c').read_text(encoding='utf-8')
a = draw.index('static VkResult create_clear_native(')
b = draw.index('\n#ifdef XEMU_EXPERIMENTAL_READY_CLEAR', a)
native = draw[a:b]
a = draw.index('    int num_attachments = 0;', draw.index('void pgraph_vk_clear_surface('))
b = draw.index('    end_draw(pg);', a)
commands = draw[a:b]
helpers = Path(__file__).with_name('spv-clear-vulkan.h').read_text(encoding='utf-8')
helpers = helpers[helpers.index('#define CHECK'):helpers.index('static VkPipeline pipeline(')]
old = 'CHECK(vkCreateDevice(physical,&di,NULL,&device));'
new = '''VkPhysicalDeviceExtendedDynamicState3FeaturesEXT dynamic = {
 .sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT};
 VkPhysicalDeviceFeatures2 query={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,.pNext=&dynamic};
 vkGetPhysicalDeviceFeatures2(physical,&query);assert(dynamic.extendedDynamicState3ColorWriteMask);
 memset(&dynamic,0,sizeof(dynamic));dynamic.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT;
 dynamic.extendedDynamicState3ColorWriteMask=VK_TRUE;
 const char *dynamic_extension=VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME;
 di.pNext=&dynamic;di.enabledExtensionCount=1;di.ppEnabledExtensionNames=&dynamic_extension;
 CHECK(vkCreateDevice(physical,&di,NULL,&device));'''
assert helpers.count(old) == 1
helpers = helpers.replace(old, new)
code = Path(__file__).with_suffix('.c').read_text(encoding='utf-8')
code = code.replace('/* HELPERS */', helpers).replace('/* PRODUCTION */', native).replace('/* COMMANDS */', commands)
env = os.environ.copy()
env['PATH'] = str(Path(cc).resolve().parent) + os.pathsep + env.get('PATH', '')
with tempfile.TemporaryDirectory(prefix='vk-clear-images-') as temp:
    deadline = time.monotonic() + 300
    def remaining():
        value = deadline - time.monotonic()
        assert value > 0, 'masked-clear suite shared 300-second watchdog expired'
        return value
    folder = Path(temp)
    source = folder / 'test.c'
    source.write_text(code, encoding='utf-8')
    exe = folder / ('test.exe' if os.name == 'nt' else 'test')
    command = [cc, '-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
               '-Wno-unused-function', '-Wno-misleading-indentation',
               '-I' + str(repo), '-I' + str(repo / 'libretro'),
               '-I' + str(Path(__file__).resolve().parent),
               '-I' + str(repo / 'deps/spirv-headers/include'),
               '-I' + str(repo / 'deps/vulkan-headers'), str(source),
               str(repo / 'hw/xbox/nv2a/pgraph/spv/clear.c'),
               str(repo / 'libretro/vk_dispatch.c'), str(repo / 'libretro/tests/vk-test-loader.c'),
               '-o', str(exe)]
    if os.name != 'nt':
        command += ['-ldl']
    subprocess.run(command, env=env, check=True, timeout=remaining())
    subprocess.run([str(exe)], env=env, check=True, timeout=remaining())
    faults = [
        ('missing dynamic mask', 'vkCmdSetColorWriteMaskEXT(r->command_buffer, 0, 1, &mask);', '(void)mask;'),
        ('missing attachment clear', 'if (num_attachments) {', 'if (false && num_attachments) {'),
        ('missing red channel', 'write_mask |= VK_COLOR_COMPONENT_R_BIT;', 'write_mask |= 0;'),
        ('unrequested depth write', '.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,',
         '.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, .depthTestEnable=VK_TRUE, .depthWriteEnable=VK_TRUE, .depthCompareOp=VK_COMPARE_OP_ALWAYS,'),
    ]
    for name, before, after in faults:
        assert code.count(before) == 1, name
        source.write_text(code.replace(before, after, 1), encoding='utf-8')
        subprocess.run(command, env=env, check=True, timeout=remaining())
        result = subprocess.run([str(exe)], env=env, capture_output=True, timeout=remaining())
        assert result.returncode == 1 and b'FAIL ' in result.stderr, (name, result)
        print('Required GPU fault rejected:', name)
