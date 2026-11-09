"""Compare actual compilers on identical mapped generated shaders, in ABBA order.
Not driver PSO or gameplay timing. Exit 2 is unqualified, never a passing sample.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import csv
import ctypes
import hashlib
import importlib.util
import json
import mmap
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys


def digest(path):
    with path.open('rb') as source:
        with mmap.mmap(source.fileno(), 0, access=mmap.ACCESS_READ) as view:
            return hashlib.sha256(view).hexdigest()


def module_growth(before, after):
    if before.keys() != after.keys() or any(n <= 0 for n in (*before.values(), *after.values())):
        raise ValueError('incomplete or invalid emitted-module comparison')
    return [dict(module=k[0], target=k[1], baseline=n, candidate=after[k])
            for k, n in before.items() if after[k] > n]


def comparison_status(statuses, *, word_growth=False, instruction_growth=False,
                      shader_regressions=False, noisy_shaders=False):
    statuses = tuple(statuses)
    if not statuses or any(s not in ('pass', 'inconclusive', 'regression') for s in statuses):
        raise ValueError('missing or invalid compiler comparison status')
    # An established regression remains a regression even if another stage
    # is unqualified. Noise never hides emitted-code or timing failures.
    if word_growth or instruction_growth or shader_regressions or 'regression' in statuses:
        return 'regression'
    if noisy_shaders or 'inconclusive' in statuses:
        return 'inconclusive'
    return 'pass'


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('baseline', 'candidate', 'corpus', 'output'):
        p.add_argument('--'+name, type=Path, required=True)
    p.add_argument('--cc', default='cc')
    p.add_argument('--cpu', type=int, help='pin comparison processes to one allowed logical CPU')
    p.add_argument('--validator', help='spirv-val executable; required by GitHub CI')
    p.add_argument('--inject-candidate-delay', action='store_true', help='test-only regression control; must fail the gate')
    p.add_argument('--prepare-corpus', action='store_true', help='append a sentinel to scratch GLSL exports in place')
    p.add_argument('--per-stage', type=int, default=64)
    p.add_argument('--iterations', type=int, default=5)
    p.add_argument('--blocks', type=int, default=2)
    p.add_argument('--max-slowdown', type=float, default=0.05)
    p.add_argument('--max-noise', type=float, default=0.05)
    a = p.parse_args()
    if (a.per_stage < 2 or a.iterations < 1 or a.blocks < 2 or
            not 0 < a.max_slowdown < 1 or not 0 < a.max_noise < 1):
        p.error('require >=2 modules/stage, >=1 iteration and >=2 ABBA blocks')
    a.output = a.output.resolve(); a.output.mkdir(parents=True, exist_ok=False)
    if a.cpu is not None:
        if a.cpu < 0:
            raise ValueError('invalid CPU')
        if os.name == 'nt':
            if a.cpu >= 64:
                raise ValueError('Windows benchmark CPU must be within one processor group')
            kernel = ctypes.WinDLL('kernel32', use_last_error=True)
            kernel.GetCurrentProcess.restype = ctypes.c_void_p
            kernel.SetProcessAffinityMask.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
            kernel.SetProcessAffinityMask.restype = ctypes.c_int
            if not kernel.SetProcessAffinityMask(kernel.GetCurrentProcess(), 1 << a.cpu):
                raise OSError(ctypes.get_last_error(), 'benchmark affinity rejected')
        else:
            os.sched_setaffinity(0, {a.cpu})
    roots = {'baseline': a.baseline.resolve(), 'candidate': a.candidate.resolve()}
    candidates = {0: [], 1: [], 2: []}
    seen = {0: set(), 1: set(), 2: set()}
    for path in sorted(a.corpus.resolve().rglob('*.glsl')):
        match = re.search(r'_(vsh(?:-pc)?|geom|psh)\.glsl$', path.name)
        if match:
            with path.open('rb') as source:
                source.seek(-1, 2); terminated = source.read(1) == b'\0'
            if not terminated and a.prepare_corpus:
                with path.open('ab') as source:
                    source.write(b'\0')
            stage = {'vsh': 0, 'vsh-pc': 0, 'geom': 1, 'psh': 2}[match[1]]
            sha = digest(path)
            if sha not in seen[stage]:
                candidates[stage].append((path, sha, path.stat().st_size))
                seen[stage].add(sha)
    chosen = []
    for stage, modules in candidates.items():
        if not modules:
            raise ValueError('corpus missing shader stage '+str(stage))
        # Largest modules plus a fixed hash-ranked spread; no hand-picked wins.
        large = sorted(modules, key=lambda x: (-x[2], x[1]))[:a.per_stage//2]
        spread = [x for x in sorted(modules, key=lambda x: x[1]) if x not in large]
        chosen += [(stage, x) for x in large+spread[:max(0, a.per_stage-len(large))]]
    manifest = a.output/'manifest.tsv'
    manifest_lines = []
    for stage, (path, sha, size) in chosen:
        with path.open('rb') as source:
            source.seek(-1, 2); terminated = source.read(1) == b'\0'
        if not terminated:
            if not a.prepare_corpus:
                raise ValueError('scratch corpus must have NUL sentinels; use --prepare-corpus')
            with path.open('ab') as source:
                source.write(b'\0')  # append one byte, never clone the shader payload
        manifest_lines.append(f'{stage}\t{path}\n')
    manifest.write_text(''.join(manifest_lines))
    runner = Path(__file__).with_name('shader-compile-bench.c')
    env = os.environ.copy()
    if Path(a.cc).is_absolute():
        env['PATH'] = str(Path(a.cc).parent)+os.pathsep+env.get('PATH', '')
    executables = {}
    hashes = {}
    for name, root in roots.items():
        folder = root/'hw/xbox/nv2a/pgraph/spv'
        exe = a.output/(name+('.exe' if os.name == 'nt' else ''))
        header = folder/'glslc.h'
        command = [a.cc, '-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                   '-Wno-missing-field-initializers',
                   '-I'+str(folder), '-I'+str(root/'deps/spirv-headers/include'),
                   str(runner), str(folder/'glslc.c'), str(folder/'glslc_lex.c'), '-lm', '-o', str(exe)]
        if 'glslc_result_free_words' not in header.read_text():
            command += ['-DBENCH_LEGACY_FREE']
        if name == 'candidate' and a.inject_candidate_delay:
            command += ['-DBENCH_INJECT_DELAY']
        subprocess.run(command, check=True, env=env)
        executables[name] = exe
        hashes[name] = {f: digest(folder/f) for f in ('glslc.c', 'glslc_lex.c', 'glslc.h')}
        # Validation is a separate process, outside all timing runs.
        if a.validator:
            words = a.output/(name+'-validation'); words.mkdir()
            subprocess.run([str(exe), str(manifest), '1', str(words/'modules.csv'), str(words)],
                           check=True, env=env, timeout=180)
            for module in sorted(words.glob('*.spv')):
                target = 'vulkan1.3' if module.stem.endswith('-1') else 'vulkan1.2'
                subprocess.run([a.validator, '--target-env', target, str(module)], check=True, env=env)
    spec = importlib.util.spec_from_file_location('gate', Path(__file__).with_name('benchmark-core-throughput.py'))
    gate = importlib.util.module_from_spec(spec); spec.loader.exec_module(gate)
    totals = {'baseline': [], 'candidate': []}; stage_totals = {i: {'baseline': [], 'candidate': []} for i in candidates}
    per_shader = {'baseline': {}, 'candidate': {}}
    shader_runs = {'baseline': {}, 'candidate': {}}
    durations = {'baseline': [], 'candidate': []}
    module_words = {'baseline': {}, 'candidate': {}}
    module_instructions = {'baseline': {}, 'candidate': {}}
    for block in range(a.blocks):
        for slot, name in enumerate(('baseline', 'candidate', 'candidate', 'baseline')):
            output = a.output/f'{block}-{slot}-{name}.csv'
            subprocess.run([str(executables[name]), str(manifest), str(a.iterations), str(output)],
                           check=True, env=env, timeout=180)
            with output.open() as source:
                rows = [[int(x) for x in row] for row in csv.reader(source)]
            expected = {(i, stage, target, iteration)
                        for i, (stage, _) in enumerate(chosen) for target in range(2)
                        for iteration in range(a.iterations)}
            if (len(rows) != len(expected) or any(len(r) != 8 for r in rows) or
                    {tuple(r[:4]) for r in rows} != expected or
                    any(r[4] <= 0 or r[5] <= 0 or r[7] <= 0 for r in rows)):
                raise ValueError('incomplete or invalid compiler sample')
            totals[name].append(sum(r[4] for r in rows))
            for stage in candidates:
                stage_totals[stage][name].append(sum(r[4] for r in rows if r[1] == stage))
            for row in rows:
                per_shader[name].setdefault((row[0], row[2]), []).append(row[4])
                durations[name].append(row[4])
                key = (row[0], row[2])
                if key in module_words[name] and module_words[name][key] != row[5]:
                    raise ValueError('nondeterministic emitted module size')
                module_words[name][key] = row[5]
                if key in module_instructions[name] and module_instructions[name][key] != row[7]:
                    raise ValueError('nondeterministic emitted instruction count')
                module_instructions[name][key] = row[7]
            for key in per_shader[name]:
                shader_runs[name].setdefault(key, []).append(statistics.median(
                    r[4] for r in rows if (r[0], r[2]) == key))
            print(f'{block}-{slot} {name}: {totals[name][-1]/1e6:.3f} ms compilation', flush=True)
    result = gate.assess(totals['baseline'], totals['candidate'], a.max_slowdown, a.max_noise)
    # These are aggregate nanoseconds, not callback throughput. Never label FPS.
    result.pop('baseline_fps'); result.pop('candidate_fps')
    stages = {}
    for stage, samples in stage_totals.items():
        stages[str(stage)] = gate.assess(samples['baseline'], samples['candidate'], a.max_slowdown, a.max_noise)
        stages[str(stage)].pop('baseline_fps'); stages[str(stage)].pop('candidate_fps')
    # A large isolated shader regression must not disappear in the corpus total.
    outliers = []
    noisy_outliers = []
    for key, before in per_shader['baseline'].items():
        after = per_shader['candidate'][key]
        ratio = statistics.median(after)/statistics.median(before)-1
        if ratio > 0.10 and statistics.median(after)-statistics.median(before) > 50000:
            # A single noisy process must not become a claimed regression.
            # Keep the same budget, but require agreement across ABBA blocks.
            check = gate.assess(shader_runs['baseline'][key],
                                shader_runs['candidate'][key], 0.10, a.max_noise)
            check.pop('baseline_fps'); check.pop('candidate_fps')
            entry = dict(module=key[0], target=key[1], slowdown=ratio,
                         comparison=check)
            (outliers if check['status'] == 'regression' else noisy_outliers).append(entry)
    aggregate_status = result['status']
    states = [aggregate_status]+[x['status'] for x in stages.values()]
    word_growth = module_growth(module_words['baseline'], module_words['candidate'])
    instruction_growth = module_growth(module_instructions['baseline'], module_instructions['candidate'])
    result['status'] = comparison_status(
        states, word_growth=bool(word_growth), instruction_growth=bool(instruction_growth),
        shader_regressions=bool(outliers), noisy_shaders=bool(noisy_outliers))
    result.update(aggregate_status=aggregate_status, stages=stages, shader_outliers=outliers,
                  noisy_shader_outliers=noisy_outliers, compiler_hashes=hashes,
                  corpus=[dict(stage=s, file=p.relative_to(a.corpus.resolve()).as_posix(), sha256=h, bytes=n) for s, (p, h, n) in chosen],
                  iterations=a.iterations, blocks=a.blocks, compiler=a.cc,
                  cpu=a.cpu,
                  max_slowdown=a.max_slowdown, max_noise=a.max_noise,
                  emitted_word_totals={k: sum(v.values()) for k, v in module_words.items()},
                  emitted_word_regressions=word_growth,
                  emitted_instruction_totals={k: sum(v.values()) for k, v in module_instructions.items()},
                  emitted_instruction_regressions=instruction_growth,
                  injected_negative_control=a.inject_candidate_delay,
                  distributions_ns={name: dict(p50=statistics.median(values),
                    p95=sorted(values)[min(len(values)-1, int(len(values)*.95))],
                    p99=sorted(values)[min(len(values)-1, int(len(values)*.99))], maximum=max(values))
                    for name, values in durations.items()},
                  scope='CPU compilation only; no generator, Vulkan PSO, GPU execution or gameplay qualification')
    (a.output/'report.json').write_text(json.dumps(result, indent=2)+'\n')
    print(result['status'], 'shader comparison; aggregate compile time', f"{result['slowdown']:+.1%}")
    print('Aggregate timing qualification:', aggregate_status)
    for stage, check in stages.items():
        print(f"Stage {stage}: {check['status']} ({check['slowdown']:+.1%})")
    print(f'Per-shader regressions: {len(outliers)}; noisy shader comparisons: {len(noisy_outliers)}; '
          f'word growth: {len(word_growth)}; instruction growth: {len(instruction_growth)}')
    return {'pass': 0, 'regression': 1, 'inconclusive': 2}[result['status']]


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        print('unqualified:', error)
        sys.exit(2)
