#include "qemu/osdep.h"
#include "qemu/notify.h"
#include "net/colo-compare.h"
#include "migration/colo.h"
#include "qemu/error-report.h"
#include "qapi/qapi-commands-migration.h"

bool migration_in_colo_state(void)
{
    return false;
}

bool migration_incoming_in_colo_state(void)
{
    return false;
}
