#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile production global/label allocation against real TCG structures."""
from pathlib import Path
import os
import shlex
import subprocess
import sys
import tempfile

r = Path(sys.argv[1]).resolve()
cc = shlex.split(sys.argv[2] if len(sys.argv) > 2 else 'cc')
source = (r / 'tcg/tcg.c').read_text()


def function(key, source=source):
    start = source.rindex(key)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


keys = ['static void tcg_temp_free_context(', 'void tcg_temp_free_internal(',
        'static void tcg_temp_free_handle(', 'void tcg_temp_free_i32(',
        'void tcg_temp_free_i64(', 'void tcg_temp_free_i128(',
        'void tcg_temp_free_ptr(', 'void tcg_temp_free_vec(',
        'static void remove_label_use(', 'void tcg_op_remove(',
        'void tcg_remove_ops_after(', 'static TCGOp *tcg_op_alloc(', 'TCGOp *tcg_emit_op(',
        'TCGOp *tcg_op_insert_before(', 'TCGOp *tcg_op_insert_after(',
        'TCGLabel *gen_new_label(',
        'static TCGTemp *tcg_temp_alloc(', 'static TCGTemp *tcg_global_alloc(',
        'static TCGTemp *tcg_global_reg_new_internal(', 'void tcg_set_frame(',
        'static TCGTemp *tcg_global_mem_new_internal(',
        'TCGv_i32 tcg_global_mem_new_i32(', 'TCGv_i64 tcg_global_mem_new_i64(']
keys += ['static TCGTemp *tcg_temp_new_context(', 'TCGTemp *tcg_temp_new_internal(',
         'static uintptr_t tcg_temp_new_handle(', 'TCGv_i32 tcg_temp_new_i32(',
         'TCGv_i32 tcg_temp_ebb_new_i32(', 'TCGv_i64 tcg_temp_new_i64(',
         'TCGv_i64 tcg_temp_ebb_new_i64(', 'TCGv_f32 tcg_temp_new_f32(',
         'TCGv_f64 tcg_temp_new_f64(', 'TCGv_ptr tcg_temp_new_ptr(',
         'TCGv_ptr tcg_temp_ebb_new_ptr(', 'TCGv_i128 tcg_temp_new_i128(',
         'TCGv_i128 tcg_temp_ebb_new_i128(', 'TCGv_vec tcg_temp_new_vec(',
         'TCGv_vec tcg_temp_new_vec_matching(']
keys += ['static TCGTemp *tcg_constant_context(', 'TCGTemp *tcg_constant_internal(',
         'static uintptr_t tcg_constant_handle(', 'TCGv_i32 tcg_constant_i32(',
         'TCGv_i64 tcg_constant_i64(', 'TCGv_ptr tcg_constant_ptr_int(',
         'TCGv_vec tcg_constant_vec(', 'TCGv_vec tcg_constant_vec_matching(']
code = Path(__file__).with_suffix('.c').read_text()
code = code.replace('/* PRODUCTION */', '\n'.join(function(key) for key in keys) + '\n' +
                    function('static void gen_tb_end(',
                             (r / 'accel/tcg/translator.c').read_text()).replace(
                                 'tcg_constant_internal(', 'fixture_count_constant(') + '\n' +
                    function('uint64_t (dup_const)(',
                             (r / 'tcg/tcg-op-gvec.c').read_text()) + '\n' +
                    '\n'.join(function(key, (r / 'tcg/tcg-op-vec.c').read_text())
                              for key in ['void vec_gen_2(', 'void vec_gen_3(', 'void vec_gen_4(',
                                          'static void vec_gen_op2(', 'static void vec_gen_op3(',
                                          'void tcg_gen_mov_vec(']) + '\n' +
                    '\n'.join(function(key, (r / 'tcg/tcg-op-vec.c').read_text())
                              .replace('tcg_can_emit_vec_op(', 'fixture_vec_capability(')
                              .replace('tcg_expand_vec_op(', 'fixture_vec_expand(')
                              for key in ['static bool do_op2(', 'static bool do_op3(',
                                          'static void do_shifti(', 'static void do_shifts(']))
comparison = function('void tcg_gen_cmp_vec(', (r / 'tcg/tcg-op-vec.c').read_text())
for original, replacement in [('tcg_can_emit_vec_op', 'fixture_vec_capability'),
                              ('tcg_expand_vec_op', 'fixture_vec_expand'),
                              ('TCG_TARGET_HAS_tst_vec', 'fixture_has_tst'),
                              ('tcg_temp_new_internal', 'fixture_cmp_temp_new'),
                              ('tcg_constant_internal', 'fixture_cmp_zero'),
                              ('tcg_temp_free_internal', 'fixture_cmp_temp_free')]:
    comparison = comparison.replace(original, replacement)
code = code.replace('/* COMPARISON */', comparison)
with tempfile.TemporaryDirectory(prefix='tcg-global-layout-') as temporary:
    folder = Path(temporary)
    (folder / 'test.c').write_text(code)
    binary = Path(os.environ.get('TCG_GLOBAL_LAYOUT_BINARY', folder / 'test'))
    flags = ['-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-D_GNU_SOURCE', '-DHAVE_THREAD_STORAGE']
    if os.environ.get('TCG_GLOBAL_LAYOUT_TEST_32BIT'):
        # Test real 32-bit types/constructors without changing the production
        # host restrictions. A full 32-bit core needs a separate port/config.
        config = (r / 'libretro/gen/config-host-win32.h').read_text()
        for macro in ('CONFIG_INT128', 'CONFIG_INT128_TYPE'):
            config = config.replace('#define ' + macro + '\n', '#undef ' + macro + '\n')
        guard = '#error "the core\'s TCG runs on x86-64 and AArch64 hosts"'
        if guard not in config:
            raise ValueError('host config guard changed; review the test-only adaptation')
        config = config.replace(guard, '#if !defined(__i386__)\n'
                                '#error "32-bit fixture requires an i386 compiler"\n'
                                '#endif\n#define HOST_I386 1')
        (folder / 'config-host.h').write_text(config)
        flags += ['-isystem', str(folder)]
    for include in ['libretro/gen', 'libretro/glib', '.', 'include', 'tcg/i386',
                    'host/include/generic', 'libretro-common/include']:
        flags += ['-isystem', str(r / include)]
    flags += shlex.split(os.environ.get('TCG_GLOBAL_LAYOUT_CFLAGS', ''))
    subprocess.run(cc + flags + [str(folder / 'test.c'), '-o', str(binary)], check=True)
    if not os.environ.get('TCG_GLOBAL_LAYOUT_SKIP_RUN'):
        subprocess.run([str(binary)], check=True, timeout=30)
