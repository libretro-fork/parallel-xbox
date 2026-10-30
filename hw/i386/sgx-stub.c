#include "qemu/osdep.h"
#include "monitor/monitor.h"
#include "monitor/hmp-target.h"
#include "hw/i386/pc.h"
#include "hw/i386/sgx-epc.h"
#include "qapi/qapi-commands-misc-i386.h"
#include "qapi/error.h"

SgxInfo *qmp_query_sgx(Error **errp)
{
    error_setg(errp, "SGX support is not compiled in");
    return NULL;
}

SgxInfo *qmp_query_sgx_capabilities(Error **errp)
{
    error_setg(errp, "SGX support is not compiled in");
    return NULL;
}

bool check_sgx_support(void)
{
    return false;
}

