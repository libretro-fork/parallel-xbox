#ifndef QEMU_THREAD_WIN32_H
#define QEMU_THREAD_WIN32_H

#include <windows.h>

struct QemuMutex {
    SRWLOCK lock;
    bool initialized;
};

typedef struct QemuRecMutex QemuRecMutex;
struct QemuRecMutex {
    CRITICAL_SECTION lock;
    bool initialized;
};

struct QemuCond {
    CONDITION_VARIABLE var;
    bool initialized;
};




#endif
