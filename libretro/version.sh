#!/bin/sh
# xemu-version-macro.h for the Makefile: the version and commit the core
# logs at start and shows RetroArch, as scripts/xemu-version.py writes it
# for meson. The file is rewritten only when it would change.
#
#   version.sh <core dir> <output>
#
# SPDX-License-Identifier: GPL-2.0-or-later
dir=$1
out=$2

commit=$(git -C "$dir" rev-parse HEAD 2>/dev/null)
if [ -z "$commit" ] && [ -f "$dir/XEMU_COMMIT" ]; then
    commit=$(cat "$dir/XEMU_COMMIT")
fi

# upstream.version: "<version>-<commits since> <upstream commit>"
version=0.0.0
upstream=
if [ -f "$dir/upstream.version" ]; then
    read -r version upstream < "$dir/upstream.version"
fi
upstream=$(printf '%.7s' "$upstream")

base=${version%%-*}
case $version in *-*) since=${version#*-} ;; *) since=0 ;; esac
major=${base%%.*}
rest=${base#*.}
minor=${rest%%.*}
patch=${rest#*.}

libretro=$version
[ -n "$upstream" ] && libretro="$libretro $upstream"
[ -n "$commit" ] && libretro="$libretro (libretro core $(printf '%.7s' "$commit"))"

tmp=$out.tmp
{
    printf '#define XEMU_VERSION       "%s"\n' "$version"
    printf '#define XEMU_VERSION_MAJOR %s\n' "$major"
    printf '#define XEMU_VERSION_MINOR %s\n' "$minor"
    printf '#define XEMU_VERSION_PATCH %s\n' "$patch"
    printf '#define XEMU_VERSION_COMMIT %s\n' "$since"
    printf '#define XEMU_COMMIT        "%s"\n' "$commit"
    printf '#define XEMU_DATE          "%s"\n' "$(date -u +%Y-%m-%d)"
    printf '#define XEMU_LIBRETRO_VERSION "%s"\n' "$libretro"
} > "$tmp"
if cmp -s "$tmp" "$out"; then
    rm -f "$tmp"
else
    mv -f "$tmp" "$out"
fi
