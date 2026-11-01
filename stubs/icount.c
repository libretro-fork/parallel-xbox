#include "qemu/osdep.h"
#include "exec/icount.h"

/* icount - Instruction Counter API */

ICountMode use_icount = ICOUNT_DISABLED;

void libretro_instruction_clock_init(void)
{
    abort();
}
int64_t icount_get_raw(void)
{
    abort();
    return 0;
}
void icount_notify_exit(void)
{
    abort();
}
