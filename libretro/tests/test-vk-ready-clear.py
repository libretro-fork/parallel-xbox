# SPDX-License-Identifier: LGPL-2.1-or-later
"""Execute the production masked-clear builder and bounded native-owner table.

Vulkan calls are intercepted to inspect create-info and inject owner failures.
This is an API/ownership contract, not a rendered-image or latency test.
"""
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
header = (repo / 'hw/xbox/nv2a/pgraph/vk/renderer.h').read_text(encoding='utf-8')
start = draw.index('static VkResult create_clear_native(')
end = draw.index('\n#ifdef XEMU_EXPERIMENTAL_READY_CLEAR', start)
native = draw[start:end]
state = re.search(r'typedef struct RenderPassState \{.*?\} RenderPassState;', header, re.S)[0]
owners = re.search(r'    struct \{\s+RenderPassState state;.*?PipelineBinding ready_clear_binding;[^\n]*', header, re.S)[0]
production = native + '\n' + (repo / 'hw/xbox/nv2a/pgraph/vk/ready-clear.inc').read_text(encoding='utf-8')
constants = (repo / 'hw/xbox/nv2a/pgraph/vk/constants.h').read_text(encoding='utf-8')
formats = re.search(r'static const SurfaceFormatInfo kelvin_surface_color_format_vk_map\[\] = \{.*?\n\};', constants, re.S)[0]
code = Path(__file__).with_suffix('.c').read_text(encoding='utf-8')
code = code.replace('/* STATE */', state).replace('/* OWNERS */', owners).replace('/* PRODUCTION */', production)
code = code.replace('/* FORMAT MAP */', formats)
env = os.environ.copy()
env['PATH'] = str(Path(cc).resolve().parent) + os.pathsep + env.get('PATH', '')
with tempfile.TemporaryDirectory(prefix='vk-ready-clear-') as temp:
    deadline = time.monotonic() + 300
    def remaining():
        value = deadline - time.monotonic()
        assert value > 0, 'masked-clear owners shared 300-second watchdog expired'
        return value
    folder = Path(temp)
    source = folder / 'test.c'
    exe = folder / ('test.exe' if os.name == 'nt' else 'test')
    command = [cc, '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
               '-fsanitize=undefined', '-fsanitize-undefined-trap-on-error',
               '-I' + str(repo / 'deps/vulkan-headers'), '-I' + str(repo),
               str(source), '-o', str(exe)]
    source.write_text(code, encoding='utf-8')
    subprocess.run(command, env=env, check=True, timeout=remaining())
    subprocess.run([str(exe)], env=env, check=True, timeout=remaining())
    faults = [
        ('capability gate', 'if (!r->dyn_blend) return;', 'if (false) return;'),
        ('partial returned owner', 'if (pipeline) vkDestroyPipeline(r->device, pipeline, NULL);', '(void)pipeline;'),
        ('rollback owners', 'vkDestroyPipeline(r->device, r->ready_clear[i].pipeline, NULL);', '(void)i;'),
        ('same-view native transition', 'view->pipeline != r->ready_clear[i].pipeline', 'false'),
        ('last-consumer retirement', 'pgraph_vk_trash_push(r, &entry);', '(void)pgraph_vk_trash_push; vkDestroyPipeline(r->device, entry.pipeline, NULL);'),
        ('depth writes disabled', '.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,', '.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, .depthWriteEnable = VK_TRUE,'),
        ('dynamic mask declaration', '.dynamicStateCount = r->dyn_blend ? 4 : 3,', '.dynamicStateCount = 3,'),
        ('unsupported format', 'if ((properties.optimalTilingFeatures & required) != required) continue;', '(void)properties; (void)required;'),
        ('null native owner', 'pipeline == VK_NULL_HANDLE', 'false'),
    ]
    for name, before, after in faults:
        assert code.count(before) == 1, name
        source.write_text(code.replace(before, after, 1), encoding='utf-8')
        subprocess.run(command, env=env, check=True, timeout=remaining())
        result = subprocess.run([str(exe)], env=env, capture_output=True, timeout=remaining())
        assert result.returncode != 0 and b'FAIL ' in result.stderr, (name, result)
        print('Required fault rejected:', name)
