/* Reset expression frozen from both 6eabf4d call sites; LGPL-2.1-or-later. */
static void reference_reset(LibJob *job)
{
    memset(job, 0, sizeof(*job));
}
