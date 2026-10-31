/*
 * Xen HVM emulation support in KVM
 *
 * Copyright © 2019 Oracle and/or its affiliates. All rights reserved.
 * Copyright © 2022 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_SYSTEM_KVM_XEN_H
#define QEMU_SYSTEM_KVM_XEN_H

/* The KVM API uses these to indicate "no GPA" or "no GFN" */
#define INVALID_GPA UINT64_MAX
#define INVALID_GFN UINT64_MAX

/* QEMU plays the rôle of dom0 for "interdomain" communication. */
#define DOMID_QEMU  0

uint32_t kvm_xen_get_caps(void);

#define kvm_xen_has_cap(cap) (!!(kvm_xen_get_caps() &           \
                                 KVM_XEN_HVM_CONFIG_ ## cap))

#define XEN_SPECIAL_AREA_ADDR 0xfeff8000UL
#define XEN_SPECIAL_AREA_SIZE 0x4000UL

#define XEN_SPECIALPAGE_CONSOLE     0
#define XEN_SPECIALPAGE_XENSTORE    1

#define XEN_SPECIAL_PFN(x) ((XEN_SPECIAL_AREA_ADDR >> TARGET_PAGE_BITS) + \
                            XEN_SPECIALPAGE_##x)

#endif /* QEMU_SYSTEM_KVM_XEN_H */
