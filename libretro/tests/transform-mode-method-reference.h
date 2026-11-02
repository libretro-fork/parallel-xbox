/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Frozen e052e45 method body for state/publication comparison. */
{
    PG_SET_MASK(NV_PGRAPH_CSV0_D, NV_PGRAPH_CSV0_D_MODE,
             GET_MASK(parameter,
                      NV097_SET_TRANSFORM_EXECUTION_MODE_MODE));
    PG_SET_MASK(NV_PGRAPH_CSV0_D, NV_PGRAPH_CSV0_D_RANGE_MODE,
             GET_MASK(parameter,
                      NV097_SET_TRANSFORM_EXECUTION_MODE_RANGE_MODE));
}
