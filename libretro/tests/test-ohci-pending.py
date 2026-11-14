# SPDX-License-Identifier: GPL-2.0-or-later
"""Production OHCI TD service with controlled USB completion and guest DMA."""
from pathlib import Path
import os, re, shlex, subprocess, sys, tempfile

repo = Path(sys.argv[1]).resolve()
cc = shlex.split(sys.argv[2] if len(sys.argv) > 2 else 'cc')
source = Path(os.environ.get('OHCI_PENDING_SOURCE', repo / 'hw/usb/hcd-ohci.c')).read_text()
usb = (repo / 'include/hw/usb.h').read_text()
def function(name, text=source):
    match = re.search(r'^(?:static )?(?:OHCIEndpointPacket \*|void |int )' + name + r'\([^;{]*\)\s*\{', text, re.M)
    assert match, name
    end = text.index('{', match.start()) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[match.start():end]

names = ['ohci_pending_td', 'ohci_endpoint_packet', 'ohci_packet_reserve',
         'ohci_packet_cancel', 'ohci_packet_remove', 'ohci_clear_packets',
         'ohci_drop_device_packets', 'ohci_stop_endpoints', 'ohci_service_td',
         'ohci_service_ed_list_inner', 'ohci_service_ed_list',
         'ohci_async_complete_packet', 'ohci_pre_load']
production = '\n'.join(function(n) for n in names)
definitions = source[source.index('/* Bitfields for the first word of an Endpoint Descriptor.'):source.index('#define USB_HZ')]
definitions += '\n' + '\n'.join(re.findall(r'^#define OHCI_(?:CC|TD_DIR)_.*', source, re.M))
definitions += '\n' + re.search(r'^#define OHCI_INTR_UE\s+.*', source, re.M)[0]
definitions += '\n' + re.search(r'^#define TD_SERVICE_LIMIT\s+.*', source, re.M)[0]
definitions += '\n' + '\n'.join(re.findall(r'^#define USB_(?:TOKEN|RET)_.*', usb, re.M))
traces = '\n'.join('#define ' + n + '(...) ((void)0)' for n in sorted(set(re.findall(r'\b(trace_usb_\w+)\(', production))))
header = (repo / 'hw/usb/hcd-ohci.h').read_text()
packet_types = header[header.index('typedef struct OHCIEndpointPacket'):header.index('struct OHCIState {')]
# Keep the old wire fields, and explicitly cancel requests before a rewind.
vmstate = source[source.index('const VMStateDescription vmstate_ohci_state ='):]
for member in ['VMSTATE_UNUSED(8192 + 4 + 1)', '.pre_load = ohci_pre_load']:
    assert member in vmstate
soft_reset = function('ohci_soft_reset')
assert soft_reset.index('ohci_stop_endpoints(ohci)') < soft_reset.index('ohci->ctl =')
port_reset = function('ohci_port_set_status')
port_reset = port_reset[port_reset.index('val & OHCI_PORT_PRS'):]
assert port_reset.index('ohci_drop_device_packets') < port_reset.index('usb_device_reset')
code = Path(__file__).with_suffix('.c').read_text().replace('/* DEFINITIONS */', definitions + '\n' + traces).replace('/* PACKET TYPES */', packet_types).replace('/* PRODUCTION */', production)
iov_source = (repo / 'util/iov.c').read_text()
code = code.replace('/* IOVEC */', '\n'.join(function(n, iov_source) for n in ['qemu_iovec_init', 'qemu_iovec_add', 'qemu_iovec_reset', 'qemu_iovec_destroy']))
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
