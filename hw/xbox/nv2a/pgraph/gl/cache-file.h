/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NV2A_GL_CACHE_FILE_H
#define NV2A_GL_CACHE_FILE_H
#include <inttypes.h>
#include <stdio.h>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

/* Disk-consumer only. Never truncate the published inode: a loader may lend
 * its mapping to glProgramBinary until that call returns, in another process
 * as well as this one. Each active process/slot owns its sibling temporary. */
static FILE *gl_cache_file_begin(GLDiskSlot *slot, const char *family,
                                 uint64_t hash)
{
    int n = snprintf(slot->path, slot->path_capacity, "%s/%s",
                     slot->base_path, family);
    if (n < 0 || (size_t)n >= slot->path_capacity) return NULL;
    qemu_mkdir(slot->path);
    size_t parent = (size_t)n;
    n = snprintf(slot->path + parent, slot->path_capacity - parent,
                 "/%04x", (uint32_t)(hash >> 48));
    if (n < 0 || (size_t)n >= slot->path_capacity - parent) return NULL;
    parent += (size_t)n;
    qemu_mkdir(slot->path);
    n = snprintf(slot->path + parent, slot->path_capacity - parent,
                 "/%012" PRIx64, hash & UINT64_C(0xffffffffffff));
    if (n < 0 || (size_t)n >= slot->path_capacity - parent) return NULL;
#ifdef _WIN32
    uintmax_t process = (uintmax_t)_getpid();
#else
    uintmax_t process = (uintmax_t)getpid();
#endif
    n = snprintf(slot->temporary_path, slot->path_capacity,
                 "%s.%" PRIuMAX ".%" PRIxPTR ".tmp", slot->path,
                 process, (uintptr_t)slot);
    if (n < 0 || (size_t)n >= slot->path_capacity) return NULL;
    return fopen_utf8(slot->temporary_path, "wb");
}

/* Failed writes/close/replace discard only this unpublished temporary. A
 * complete old cache entry and all of its readers remain untouched. */
static bool gl_cache_file_finish(GLDiskSlot *slot, FILE *file, bool ok)
{
    if (fclose(file) != 0) ok = false;
    if (ok && g_rename(slot->temporary_path, slot->path) == 0) return true;
    qemu_unlink(slot->temporary_path);
    return false;
}
#endif
