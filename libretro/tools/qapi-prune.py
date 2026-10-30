#!/usr/bin/env python3
"""Prune the generated QAPI code in libretro/gen/qapi to what the core uses.

QAPI generates a type, a visitor and an event emitter for everything in
every schema; the core reaches about a quarter of them. A definition in
libretro/gen/qapi/*.c stays when

  - the linked core has it (the Makefile links with --gc-sections, so what
    the core never calls is not in it), or
  - hand-written code names it: any compiled source outside libretro/gen,
    any header (Windows-only and macOS-only code included, which a Linux
    build does not link), and the names the QAPI macros build by pasting
    (QAPI_CLONE, QAPI_CLONE_MEMBERS, g_autoptr and the X_str() lookups), or
  - a definition that stays names it.

The rest goes, with its declarations in the generated headers. A source
left with nothing goes from Makefile.common and the tree, and so does a
generated header no hand-written file (compiled or not, any platform) nor
a remaining generated file reaches by #include.

Run it from the top of the tree on a freshly built Linux core, after
copying regenerated QAPI files here (libretro/gen/README.md):

    make -j$(nproc) && python3 libretro/tools/qapi-prune.py parallel_xbox_libretro.so
    make -j$(nproc)    # and the Windows build: both have to link

It is idempotent: on an already pruned tree it changes nothing.
"""
import collections
import os
import re
import subprocess
import sys

GEN = 'libretro/gen/qapi/'
ident = re.compile(r'[A-Za-z_]\w*')


def lookups(names):
    """the X_lookup tables the X_str() macros read"""
    return {n[:-4] + '_lookup' for n in names if n.endswith('_str')}


def hand_written_names(sources):
    names = set()
    files = [s for s in sources if not s.startswith('libretro/gen/')]
    for root, dirs, fs in os.walk('.'):
        dirs[:] = [d for d in dirs if d not in ('.git', 'subprojects')]
        if root.startswith('./libretro/gen'):
            continue
        files += [os.path.join(root, f)[2:] for f in fs
                  if f.endswith(('.h', '.inc'))]
    for f in set(files):
        try:
            t = open(f, errors='replace').read()
        except FileNotFoundError:
            continue
        names.update(ident.findall(t))
        names.update('visit_type_' + m
                     for m in re.findall(r'QAPI_CLONE\(\s*(\w+)', t))
        names.update('visit_type_' + m + '_members'
                     for m in re.findall(r'QAPI_CLONE_MEMBERS\(\s*(\w+)', t))
        names.update('qapi_free_' + m for m in
                     re.findall(r'g_auto(?:ptr|list|slist|queue)\(\s*(\w+)', t))
    return names | lookups(names)


def definitions(path):
    """the top-level definitions of a generated source:
    (first line, line after the last, name, static)"""
    L = open(path).read().split('\n')
    out = []
    i, n = 0, len(L)
    while i < n:
        line = L[i]
        if not line or line[0] in ' \t#/*}' or line.startswith('typedef'):
            i += 1
            continue
        if line.rstrip().endswith(';') and '{' not in line:
            end = i + 1
        else:
            j = i
            while j < n and L[j] not in ('}', '};'):
                j += 1
            end = j + 1
        m = re.match(r'(.*?)\b([A-Za-z_]\w*)\s*(\(|=|;|\[)',
                     ' '.join(L[i:min(end, i + 3)]))
        out.append((i, end, m.group(2) if m else None,
                    line.startswith('static')))
        i = end
    return L, out


def drop_empty_conditionals(s):
    prev = None
    while prev != s:
        prev = s
        s = re.sub(r'(?m)^#if[^\n]*\n\n*#endif[^\n]*\n\n?', '', s)
    return re.sub(r'\n{3,}', '\n\n', s)


def prune_sources(gen_c, linked, hand):
    parsed = {p: definitions(p) for p in gen_c}
    by_name = collections.defaultdict(list)
    statics = collections.defaultdict(dict)
    keep, work = set(), []
    for p, (L, defs) in parsed.items():
        for k, (a, b, name, static) in enumerate(defs):
            if static:
                statics[p][name] = k
            elif name:
                by_name[name].append((p, k))
            if (name is None or name.startswith('qapi_dummy') or
                    (not static and (name in linked or name in hand))):
                keep.add((p, k))
                work.append((p, k))
    while work:
        p, k = work.pop()
        L, defs = parsed[p]
        a, b = defs[k][:2]
        names = set(ident.findall('\n'.join(L[a:b])))
        for nm in names | lookups(names):
            targets = list(by_name.get(nm, []))
            if nm in statics[p]:
                targets.append((p, statics[p][nm]))
            for t in targets:
                if t not in keep:
                    keep.add(t)
                    work.append(t)

    removed = set()
    for p, (L, defs) in parsed.items():
        kill = set()
        for k, (a, b, name, static) in enumerate(defs):
            if (p, k) in keep:
                continue
            if not static:
                removed.add(name)
            kill.update(range(a, b))
            if b < len(L) and L[b] == '':
                kill.add(b)
        if kill:
            s = '\n'.join(l for i, l in enumerate(L) if i not in kill)
            open(p, 'w').write(drop_empty_conditionals(s))
    return removed


def prune_headers(removed):
    for h in sorted(os.listdir(GEN)):
        if not h.endswith('.h'):
            continue
        h = GEN + h
        L = open(h).read().split('\n')
        out, i, n, depth, changed = [], 0, len(L), 0, False
        while i < n:
            line = L[i]
            skip = 0
            if (depth == 0 and line and line[0] not in ' \t#/*}' and
                    not re.match(r'(typedef|struct|union|enum)\b', line)):
                j = i
                while j < n and not L[j].rstrip().endswith((';', ')')):
                    j += 1
                stmt = ' '.join(L[i:j + 1])
                m = (re.match(r'G_DEFINE_AUTOPTR_CLEANUP_FUNC\(\w+,\s*(\w+)\)',
                              stmt) or
                     re.match(r'[^(;]*?\b(\w+)\s*(?:\(|;)', stmt))
                if m and m.group(1) in removed:
                    skip = j + 1 - i
            m = re.match(r'#define (\w+)_str\(val\) \\$', line)
            if m and m.group(1) + '_lookup' in removed:
                skip = 2
            if skip:
                i += skip
                changed = True
                if i < n and L[i] == '' and out and out[-1] == '':
                    i += 1
                continue
            depth += line.count('{') - line.count('}')
            out.append(line)
            i += 1
        if changed:
            open(h, 'w').write(drop_empty_conditionals('\n'.join(out)))


def drop_empty_sources(gen_c):
    empty = []
    for c in gen_c:
        body = [l for l in open(c).read().split('\n')
                if l.strip() and not re.match(r'#|/\*| \*|char qapi_dummy', l)]
        if not body:
            empty.append(c)
    if empty:
        mk = open('Makefile.common').read().split('\n')
        mk = [l for l in mk
              if not any(l.strip().startswith('$(CORE_DIR)/' + c) for c in empty)]
        open('Makefile.common', 'w').write('\n'.join(mk))
        for c in empty:
            os.remove(c)
    return empty


def drop_unincluded_headers(sources):
    """generated headers no hand-written file reaches by #include"""
    inc = re.compile(r'(?m)^\s*#\s*include\s*[<"](?:qapi/)?(qapi-[\w-]+\.h)[>"]')
    seeds = [s for s in sources if not s.startswith('libretro/gen/')]
    for root, dirs, fs in os.walk('.'):
        dirs[:] = [d for d in dirs if d not in ('.git', 'subprojects')]
        if root.startswith('./libretro/gen'):
            continue
        seeds += [os.path.join(root, f)[2:] for f in fs
                  if f.endswith(('.h', '.inc'))]
    seeds += [s for s in sources if s.startswith(GEN)]
    reached, work = set(), []
    for f in seeds:
        try:
            work += inc.findall(open(f, errors='replace').read())
        except FileNotFoundError:
            pass
    while work:
        h = work.pop()
        if h in reached or not os.path.exists(GEN + h):
            continue
        reached.add(h)
        work += inc.findall(open(GEN + h).read())
    gone = [h for h in os.listdir(GEN) if h.endswith('.h') and h not in reached]
    for h in gone:
        os.remove(GEN + h)
    return gone


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    nm = subprocess.run(['nm', sys.argv[1]], capture_output=True, text=True,
                        check=True).stdout
    linked = {l.split()[-1] for l in nm.split('\n') if l.strip()}
    mk = open('Makefile.common').read()
    sources = re.findall(r'\$\(CORE_DIR\)/(\S+\.(?:c|cc|cpp|m))', mk)
    gen_c = sorted({s for s in sources if s.startswith(GEN)})
    removed = prune_sources(gen_c, linked, hand_written_names(sources))
    prune_headers(removed)
    empty = drop_empty_sources(gen_c)
    gen_c = [c for c in gen_c if c not in empty]
    headers = drop_unincluded_headers([s for s in sources if s not in empty])
    print('%d definitions removed, %d sources and %d headers gone' %
          (len(removed), len(empty), len(headers)))


if __name__ == '__main__':
    main()
