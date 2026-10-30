#include "qemu/osdep.h"
#include "system/system.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-ui.h"
#include "trace.h"
#include "ui/input.h"
#include "ui/console.h"
#include "system/runstate.h"

struct QemuInputHandlerState {
    DeviceState       *dev;
    const QemuInputHandler *handler;
    int               id;
    int               events;
    QemuConsole       *con;
    QTAILQ_ENTRY(QemuInputHandlerState) node;
};

typedef struct QemuInputEventQueue QemuInputEventQueue;
typedef QTAILQ_HEAD(QemuInputEventQueueHead, QemuInputEventQueue)
    QemuInputEventQueueHead;

struct QemuInputEventQueue {
    enum {
        QEMU_INPUT_QUEUE_DELAY = 1,
        QEMU_INPUT_QUEUE_EVENT,
        QEMU_INPUT_QUEUE_SYNC,
    } type;
    QEMUTimer *timer;
    uint32_t delay_ms;
    QemuConsole *src;
    InputEvent *evt;
    QTAILQ_ENTRY(QemuInputEventQueue) node;
};

static QTAILQ_HEAD(, QemuInputHandlerState) handlers =
    QTAILQ_HEAD_INITIALIZER(handlers);

static QemuInputEventQueueHead kbd_queue = QTAILQ_HEAD_INITIALIZER(kbd_queue);

