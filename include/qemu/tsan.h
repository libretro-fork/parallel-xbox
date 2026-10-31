#ifndef QEMU_TSAN_H
#define QEMU_TSAN_H
/*
 * tsan.h
 *
 * This file defines macros used to give ThreadSanitizer
 * additional information to help suppress warnings.
 * This is necessary since TSan does not provide a header file
 * for these annotations.  The standard way to include these
 * is via the below macros.
 *
 * Annotation examples can be found here:
 *  https://github.com/llvm/llvm-project/tree/master/compiler-rt/test/tsan
 * annotate_happens_before.cpp or ignore_race.cpp are good places to start.
 *
 * The full set of annotations can be found here in tsan_interface_ann.cpp.
 *  https://github.com/llvm/llvm-project/blob/master/compiler-rt/lib/tsan/rtl/
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#define QEMU_TSAN_ANNOTATE_HAPPENS_BEFORE(addr)
#define QEMU_TSAN_ANNOTATE_HAPPENS_AFTER(addr)
#define QEMU_TSAN_ANNOTATE_THREAD_NAME(name)
#define QEMU_TSAN_ANNOTATE_IGNORE_READS_BEGIN()
#define QEMU_TSAN_ANNOTATE_IGNORE_READS_END()
#define QEMU_TSAN_ANNOTATE_IGNORE_WRITES_BEGIN()
#define QEMU_TSAN_ANNOTATE_IGNORE_WRITES_END()

void AnnotateHappensBefore(const char *f, int l, void *addr);
void AnnotateHappensAfter(const char *f, int l, void *addr);
void AnnotateThreadName(const char *f, int l, char *name);
void AnnotateIgnoreReadsBegin(const char *f, int l);
void AnnotateIgnoreReadsEnd(const char *f, int l);
void AnnotateIgnoreWritesBegin(const char *f, int l);
void AnnotateIgnoreWritesEnd(const char *f, int l);
#endif
