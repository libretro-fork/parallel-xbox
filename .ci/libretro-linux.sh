#!/bin/bash
# The libretro core for Linux, as the buildbot's job builds it (.gitlab-ci.yml)
# in ubuntu:24.04; the GitLab preflight workflow runs the same script.
#
# The static Makefile, as the GitHub workflow builds it: GLib is the core's
# own (libretro/glib) and libslirp is built from deps/ and linked in, so the
# one file the buildbot ships is the whole core.
set -euxo pipefail

NUMPROC=${NUMPROC:-$(nproc)}
[ "$NUMPROC" -ge 1 ] || NUMPROC=1
export DEBIAN_FRONTEND=noninteractive

apt-get -qy update
apt-get -qy install --no-install-recommends \
  build-essential git ca-certificates pkg-config libepoxy-dev libvulkan-dev
git config --global --add safe.directory '*'

make -j "$NUMPROC"
strip --strip-unneeded parallel_xbox_libretro.so
ls -la parallel_xbox_libretro.so
readelf -d parallel_xbox_libretro.so | grep NEEDED
# libslirp and GLib are inside: no NEEDED entry for either
! readelf -d parallel_xbox_libretro.so | grep -q 'NEEDED.*\(libslirp\|libglib\)'
# and no window system's library: the frontend owns the window
! readelf -d parallel_xbox_libretro.so | grep -q 'NEEDED.*libX11'
