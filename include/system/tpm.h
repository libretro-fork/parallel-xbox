/*
 * Public TPM functions
 *
 * Copyright (C) 2011-2013 IBM Corporation
 *
 * Authors:
 *  Stefan Berger    <stefanb@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef QEMU_TPM_H
#define QEMU_TPM_H

#include "qapi/qapi-types-tpm.h"
#include "qom/object.h"


#define tpm_init()  (0)
#define tpm_cleanup()

/* needed for an alignment check in non-tpm code */
static inline Object *TPM_IS_CRB(Object *obj)
{
     return NULL;
}


#endif /* QEMU_TPM_H */
