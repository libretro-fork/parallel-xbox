#ifndef QEMU_MSIX_H
#define QEMU_MSIX_H

#include "hw/pci/pci.h"

#define MSIX_CAP_LENGTH 12

MSIMessage msix_get_message(PCIDevice *dev, unsigned int vector);

void msix_write_config(PCIDevice *dev, uint32_t address, uint32_t val, int len);

void msix_save(PCIDevice *dev, QEMUFile *f);
void msix_load(PCIDevice *dev, QEMUFile *f);

int msix_enabled(PCIDevice *dev);
int msix_present(PCIDevice *dev);

bool msix_is_masked(PCIDevice *dev, unsigned vector);
void msix_set_pending(PCIDevice *dev, unsigned vector);
void msix_clr_pending(PCIDevice *dev, int vector);
int msix_is_pending(PCIDevice *dev, unsigned vector);

void msix_notify(PCIDevice *dev, unsigned vector);

void msix_reset(PCIDevice *dev);

extern const VMStateDescription vmstate_msix;

#define VMSTATE_MSIX_TEST(_field, _state, _test) {                   \
    .name         = (stringify(_field)),                             \
    .size         = sizeof(PCIDevice),                               \
    .vmsd         = &vmstate_msix,                                   \
    .flags        = VMS_STRUCT,                                      \
    .offset       = vmstate_offset_value(_state, _field, PCIDevice), \
    .field_exists = (_test)                                          \
}

#define VMSTATE_MSIX(_f, _s)                                         \
    VMSTATE_MSIX_TEST(_f, _s, NULL)

#endif
