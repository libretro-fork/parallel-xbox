#!/bin/bash
# The libretro core for Windows x64, as the buildbot's job builds it
# (.gitlab-ci.yml): the static Makefile, cross-compiled in the MXE toolchain
# image upstream xemu uses for its Windows releases. Every library the core
# links is built from source in deps/ (Makefile.deps); the GitLab preflight
# workflow runs the same script.
set -euxo pipefail

NUMPROC=${NUMPROC:-$(nproc)}
[ "$NUMPROC" -ge 1 ] || NUMPROC=1
CROSSPREFIX=x86_64-w64-mingw32.static-
git config --global --add safe.directory '*'

make -j "$NUMPROC" platform=win CROSS_PREFIX=$CROSSPREFIX
${CROSSPREFIX}strip --strip-unneeded xemu_libretro.dll
ls -la xemu_libretro.dll
# What it needs at load time: Windows' own DLLs and nothing else.
${CROSSPREFIX}objdump -p xemu_libretro.dll | grep 'DLL Name' | sort -u
