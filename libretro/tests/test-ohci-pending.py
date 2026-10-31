# SPDX-License-Identifier: GPL-2.0-or-later
"""Production OHCI TD service with controlled USB completion and guest DMA."""
from pathlib import Path
import os, re, shlex, subprocess, sys, tempfile

repo = Path(sys.argv[1]).resolve()
cc = shlex.split(sys.argv[2] if len(sys.argv) > 2 else 'cc')
source = Path(os.environ.get('OHCI_PENDING_SOURCE', repo / 'hw/usb/hcd-ohci.c')).read_text()
usb = (repo / 'include/hw/usb.h').read_text()
start = source.index('static int ohci_service_td(')
end = source.index('\n/* Service an endpoint list.', start)
function = source[start:end]
definitions = source[source.index('/* Bitfields for the first word of an Endpoint Descriptor.'):source.index('#define USB_HZ')]
definitions += '\n' + '\n'.join(re.findall(r'^#define OHCI_(?:CC|TD_DIR)_.*', source, re.M))
definitions += '\n' + '\n'.join(re.findall(r'^#define USB_(?:TOKEN|RET)_.*', usb, re.M))
traces = '\n'.join('#define ' + n + '(...) ((void)0)' for n in sorted(set(re.findall(r'\b(trace_usb_\w+)\(', function))))
code = Path(__file__).with_suffix('.c').read_text().replace('/* DEFINITIONS */', definitions + '\n' + traces).replace('/* PRODUCTION */', function)
with tempfile.TemporaryDirectory(prefix='ohci-pending-') as directory:
    work = Path(directory)
    c = work / 'test.c'
    c.write_text(code)
    binary = os.environ.get('OHCI_PENDING_BINARY', str(work / 'test'))
    flags = shlex.split(os.environ.get('OHCI_PENDING_CFLAGS', '-fsanitize=address,undefined'))
    subprocess.run(cc + ['-O2', '-std=gnu11', '-fwrapv', '-Wall', '-Wextra', '-Werror', '-fno-sanitize-recover=all',
                        '-Wno-unused-parameter', '-Wno-unused-but-set-variable',
                        '-Wno-sign-compare'] + flags + [str(c), '-o', binary], check=True)
    if not os.environ.get('OHCI_PENDING_SKIP_RUN'):
        subprocess.run([binary], check=True, timeout=30)
