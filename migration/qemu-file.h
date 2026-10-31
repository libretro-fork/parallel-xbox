/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef MIGRATION_QEMU_FILE_H
#define MIGRATION_QEMU_FILE_H

#include "exec/cpu-common.h"

/* A saved-state cursor borrows frontend memory. It owns no channel, staging
 * allocation or backing data. Finish only releases a captured Error object. */
struct QEMUFile {
    uint8_t *buf;
    size_t buf_size;
    size_t buf_index;
    bool is_writable;
    int last_error;
    Error *last_error_obj;
};

void qemu_file_init_input(QEMUFile *f, const void *data, size_t size);
void qemu_file_init_output(QEMUFile *f, void *data, size_t size);
int qemu_fclose(QEMUFile *f);

/* Current byte position in the bounded state stream. */
uint64_t qemu_file_transferred(QEMUFile *f);

#include "migration/qemu-file-types.h"

size_t qemu_peek_buffer(QEMUFile *f, uint8_t **buf, size_t size, size_t offset);

/* Borrow a view at an offset from the cursor without advancing it. */
int qemu_peek_byte(QEMUFile *f, int offset);
void qemu_file_skip(QEMUFile *f, int size);
int qemu_file_get_error_obj_any(QEMUFile *f1, QEMUFile *f2, Error **errp);
void qemu_file_set_error_obj(QEMUFile *f, int ret, Error *err);
int qemu_file_get_error_obj(QEMUFile *f, Error **errp);
void qemu_file_set_error(QEMUFile *f, int ret);
int qemu_fflush(QEMUFile *f);

#endif
