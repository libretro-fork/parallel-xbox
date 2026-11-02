/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Frozen d99a0a5 method body for state/publication comparison. */
{
    PG_SET_MASK(NV_PGRAPH_CSV0_C, NV_PGRAPH_CSV0_C_EMISSION,
             (parameter >> 0) & 3);
    PG_SET_MASK(NV_PGRAPH_CSV0_C, NV_PGRAPH_CSV0_C_AMBIENT,
             (parameter >> 2) & 3);
    PG_SET_MASK(NV_PGRAPH_CSV0_C, NV_PGRAPH_CSV0_C_DIFFUSE,
             (parameter >> 4) & 3);
    PG_SET_MASK(NV_PGRAPH_CSV0_C, NV_PGRAPH_CSV0_C_SPECULAR,
             (parameter >> 6) & 3);
}
