"""Fits the song render cost model (LoadableFx::SONG_*_COST, fx_effect.h) to
song_cost.py's measurements: least squares of the measured share of core 0 on
the base, each kind's sounding voices, filtered voices, voices in their attack
and the kinds sounding together beyond the first, all per sample over the
measured window (`fxc songwork`). Prints the terms in hundredths of a percent,
as fx_effect.h writes them, and every song's fit.

  python3 firmware/tools/hil/song_cost_fit.py firmware/evidence/song-cost-20261024/all-with-work.json
"""
import json
import math
import sys

TERMS = ['tone', 'noise', 'stepped', 'pulse', 'wave', 'fm', 'drum', 'filter', 'attack', 'mix']


def row(work):
    n = work['samples']
    return [1.0] + [work[t] / n for t in TERMS]


def solve(a, b):
    """Gaussian elimination with partial pivoting."""
    n = len(b)
    m = [r[:] + [v] for r, v in zip(a, b)]
    for c in range(n):
        p = max(range(c, n), key=lambda r: abs(m[r][c]))
        m[c], m[p] = m[p], m[c]
        for r in range(n):
            if r != c and m[c][c]:
                f = m[r][c] / m[c][c]
                m[r] = [x - f * y for x, y in zip(m[r], m[c])]
    return [m[i][n] / m[i][i] for i in range(n)]


def fit(songs):
    x = [row(s['work']) for s in songs.values()]
    y = [s['share'] * 100 for s in songs.values()]
    k = len(x[0])
    ata = [[sum(r[i] * r[j] for r in x) for j in range(k)] for i in range(k)]
    aty = [sum(r[i] * v for r, v in zip(x, y)) for i in range(k)]
    return solve(ata, aty)


def main():
    songs = json.load(open(sys.argv[1]))
    c = fit(songs)
    print('base', round(c[0] * 100), ' '.join(f'{t} {round(v * 100)}' for t, v in zip(TERMS, c[1:])))
    errors = []
    for name, s in songs.items():
        model = sum(a * b for a, b in zip(row(s['work']), c))
        device = s['share'] * 100
        errors.append((model - device) / device)
        print(f'{name:24s} device {device:6.2f}% fit {model:6.2f}% ({errors[-1]:+.1%})')
    print(f'max |error| {max(map(abs, errors)):.1%}, rms {math.sqrt(sum(e * e for e in errors) / len(errors)):.1%}')


if __name__ == '__main__':
    main()
