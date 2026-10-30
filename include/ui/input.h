#ifndef INPUT_H
#define INPUT_H

#include "qapi/qapi-types-ui.h"
#include "qemu/notify.h"

#define INPUT_EVENT_MASK_KEY   (1<<INPUT_EVENT_KIND_KEY)
#define INPUT_EVENT_MASK_BTN   (1<<INPUT_EVENT_KIND_BTN)
#define INPUT_EVENT_MASK_REL   (1<<INPUT_EVENT_KIND_REL)
#define INPUT_EVENT_MASK_ABS   (1<<INPUT_EVENT_KIND_ABS)
#define INPUT_EVENT_MASK_MTT   (1<<INPUT_EVENT_KIND_MTT)

#define INPUT_EVENT_ABS_MIN    0x0000
#define INPUT_EVENT_ABS_MAX    0x7FFF
#define INPUT_EVENT_SLOTS_MIN  0x0
#define INPUT_EVENT_SLOTS_MAX  0xa

typedef struct QemuInputHandler QemuInputHandler;
typedef struct QemuInputHandlerState QemuInputHandlerState;

typedef void (*QemuInputHandlerEvent)(DeviceState *dev, QemuConsole *src,
                                      InputEvent *evt);
typedef void (*QemuInputHandlerSync)(DeviceState *dev);

struct QemuInputHandler {
    const char             *name;
    uint32_t               mask;
    QemuInputHandlerEvent  event;
    QemuInputHandlerSync   sync;
};

void qemu_input_check_mode_change(void);

extern const guint qemu_input_map_atset1_to_qcode_len;
extern const guint16 qemu_input_map_atset1_to_qcode[];

extern const guint qemu_input_map_linux_to_qcode_len;
extern const guint16 qemu_input_map_linux_to_qcode[];

extern const guint qemu_input_map_qcode_to_atset1_len;
extern const guint16 qemu_input_map_qcode_to_atset1[];

extern const guint qemu_input_map_qcode_to_atset2_len;
extern const guint16 qemu_input_map_qcode_to_atset2[];

extern const guint qemu_input_map_qcode_to_atset3_len;
extern const guint16 qemu_input_map_qcode_to_atset3[];

extern const guint qemu_input_map_qcode_to_linux_len;
extern const guint16 qemu_input_map_qcode_to_linux[];

extern const guint qemu_input_map_qcode_to_qnum_len;
extern const guint16 qemu_input_map_qcode_to_qnum[];

extern const guint qemu_input_map_qcode_to_sun_len;
extern const guint16 qemu_input_map_qcode_to_sun[];

extern const guint qemu_input_map_qnum_to_qcode_len;
extern const guint16 qemu_input_map_qnum_to_qcode[];

extern const guint qemu_input_map_usb_to_qcode_len;
extern const guint16 qemu_input_map_usb_to_qcode[];

extern const guint qemu_input_map_win32_to_qcode_len;
extern const guint16 qemu_input_map_win32_to_qcode[];

extern const guint qemu_input_map_x11_to_qcode_len;
extern const guint16 qemu_input_map_x11_to_qcode[];

extern const guint qemu_input_map_xorgevdev_to_qcode_len;
extern const guint16 qemu_input_map_xorgevdev_to_qcode[];

extern const guint qemu_input_map_xorgkbd_to_qcode_len;
extern const guint16 qemu_input_map_xorgkbd_to_qcode[];

extern const guint qemu_input_map_xorgxquartz_to_qcode_len;
extern const guint16 qemu_input_map_xorgxquartz_to_qcode[];

extern const guint qemu_input_map_xorgxwin_to_qcode_len;
extern const guint16 qemu_input_map_xorgxwin_to_qcode[];

extern const guint qemu_input_map_osx_to_qcode_len;
extern const guint16 qemu_input_map_osx_to_qcode[];

#endif /* INPUT_H */
