/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Test-only compiler timing. Input is a read-only mapped, NUL-terminated
 * scratch corpus. The mapping lends its bytes through each synchronous compile.
 * Output, mapping and filesystem work are outside the timed interval. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "glslc.h"
#ifdef _WIN32
#include <windows.h>
static uint64_t now_ns(void)
{
    LARGE_INTEGER value, frequency;
    QueryPerformanceCounter(&value); QueryPerformanceFrequency(&frequency);
    return (uint64_t)((double)value.QuadPart * 1e9 / frequency.QuadPart);
}
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
static uint64_t now_ns(void)
{
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000 + t.tv_nsec;
}
#endif

int main(int argc, char **argv)
{
    if (argc != 4 && argc != 5) return 2;
    unsigned iterations = (unsigned)strtoul(argv[2], NULL, 10);
    if (!iterations || iterations > 1000) return 2;
    FILE *manifest = fopen(argv[1], "r"), *out = fopen(argv[3], "w");
    if (!manifest || !out) return 2;
    char line[8192]; unsigned id = 0;
    while (fgets(line, sizeof(line), manifest)) {
        char *path = strchr(line, '\t');
        if (!path || !strchr(path, '\n')) return 2;
        *path++ = 0; path[strcspn(path, "\r\n")] = 0;
        unsigned stage = (unsigned)strtoul(line, NULL, 10);
        if (stage > GLSLC_STAGE_COMPUTE) return 2;
        size_t size; const char *source;
#ifdef _WIN32
        HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file == INVALID_HANDLE_VALUE) return 2;
        LARGE_INTEGER length; if (!GetFileSizeEx(file, &length) || length.QuadPart <= 0 ||
                                  length.QuadPart > (64LL << 20)) return 2;
        size = (size_t)length.QuadPart;
        HANDLE mapping = CreateFileMappingA(file, NULL, PAGE_READONLY, 0, 0, NULL);
        source = mapping ? MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0) : NULL;
#else
        int file = open(path, O_RDONLY); struct stat st;
        if (file < 0 || fstat(file, &st) || st.st_size <= 0 || st.st_size > (64LL << 20)) return 2;
        size = (size_t)st.st_size;
        source = mmap(NULL, size, PROT_READ, MAP_PRIVATE, file, 0);
        if (source == MAP_FAILED) source = NULL;
#endif
        if (!source || source[size-1]) return 2;
        for (unsigned target = 0; target < 2; target++) {
            for (unsigned iteration = 0; iteration < iterations; iteration++) {
                GlslcOptions options = {0}; GlslcResult result = {0};
                options.spirv_1_6 = target != 0; options.emit_layout = true;
                uint64_t start = now_ns();
                bool ok = glslc_compile((GlslcStage)stage, source, &options, &result);
#ifdef BENCH_INJECT_DELAY
                /* Deliberately bad test-only control; never core code. */
#ifdef _WIN32
                Sleep(1);
#else
                struct timespec delay = {0, 1000000}; nanosleep(&delay, NULL);
#endif
#endif
                uint64_t duration = now_ns()-start;
                if (!ok) { fprintf(stderr, "%s: %s\n", path, result.error); return 1; }
                fprintf(out, "%u,%u,%u,%u,%llu,%zu,%zu\n", id, stage, target,
                        iteration, (unsigned long long)duration, result.num_words, size-1);
                if (argc == 5 && !iteration) {
                    char name[8192];
                    int length = snprintf(name, sizeof(name), "%s/%u-%u.spv", argv[4], id, target);
                    if (length < 0 || (size_t)length >= sizeof(name)) return 2;
                    FILE *words = fopen(name, "wb");
                    if (!words || fwrite(result.words, 4, result.num_words, words) != result.num_words || fclose(words)) return 2;
                }
#ifdef BENCH_LEGACY_FREE
                free(result.words);
#else
                glslc_result_free_words(&result);
#endif
                free(result.uniforms.members); free(result.push_constants.members);
            }
        }
#ifdef _WIN32
        UnmapViewOfFile(source); CloseHandle(mapping); CloseHandle(file);
#else
        munmap((void *)source, size); close(file);
#endif
        id++;
    }
    if (!id || ferror(manifest) || fclose(manifest) || fclose(out)) return 2;
    return 0;
}
