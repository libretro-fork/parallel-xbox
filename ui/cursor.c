#include "qemu/osdep.h"
#include "ui/console.h"

QEMUCursor *cursor_ref(QEMUCursor *c)
{
    c->refcount++;
    return c;
}

void cursor_unref(QEMUCursor *c)
{
    if (c == NULL)
        return;
    c->refcount--;
    if (c->refcount)
        return;
    g_free(c);
}

