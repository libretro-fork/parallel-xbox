/* SPDX-License-Identifier: GPL-2.0-or-later */
/* CPU-only synthetic manifests exercise the runner, never shader correctness. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
int main(int argc, char **argv)
{
    assert(argc == 4 && !strcmp(argv[1], "--range"));
    unsigned first = strtoul(argv[2], NULL, 10);
    unsigned limit = strtoul(argv[3], NULL, 10);
    unsigned cases = limit - first, checked = cases * 320;
    const char *fault = getenv("READY_RUNNER_FAULT");
    if (!fault) fault = "";
    if (!strcmp(fault, "timeout")) {
#ifdef _WIN32
        Sleep(60000);
#else
        sleep(60);
#endif
    }
    if (!first && strcmp(fault, "oracle")) {
        puts("PASS independent half-plane/window/volume oracle:");
    }
    if (first && !strcmp(fault, "exit")) return 17;
    if (first && !strcmp(fault, "checks")) checked -= 320;
    if (first && !strcmp(fault, "range")) first++;
    if (first && !strcmp(fault, "cases")) cases++;
    unsigned written = strcmp(fault, "pixels") ? 1 : 0;
    for (unsigned i = 0; i < (first && !strcmp(fault, "duplicate") ? 2u : 1u); i++)
        printf("PASS ready fragment: range=%u:%u cases=%u checked=%u written=%u discarded=1,\n",
               first, limit, cases, checked, written);
    return 0;
}
