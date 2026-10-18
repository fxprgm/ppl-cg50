"""Build tests/conformance/hpkit_cases.tsv from a clone of hp-prime-kit.

    python3 -I tools/mkconform.py ref/hp-prime-kit

Rows come from docs/commands/results.tsv (answers recorded on HP's Virtual
Calculator) plus the documentation's examples measured on a G2 or stated by
HP's help that have no recorded row.  Each row: entry, call, answer, TYPE
code, source, group.  The kit is MIT-licensed (see LICENSE-hp-prime-kit).
"""
import os, re, sys

root = sys.argv[1]
cmd = os.path.join(root, 'docs', 'commands')
group = {}
for g in sorted(os.listdir(cmd)):
    p = os.path.join(cmd, g)
    if os.path.isdir(p):
        for f in os.listdir(p):
            if f.endswith('.md'):
                group[f[:-3]] = g

rows, seen = [], set()
with open(os.path.join(cmd, 'results.tsv'), encoding='utf-8') as fh:
    for line in fh:
        if line.startswith('#') or not line.strip():
            continue
        f = line.rstrip('\n').split('\t')
        entry, call, answer, typ = f[0], f[1], f[2], f[3] if len(f) > 3 else ''
        seen.add((entry, call))
        g = group.get(entry, 'topics' if '.' in entry else '?')
        rows.append((entry, call, answer, typ, 'emulator', g))

cell = re.compile(r'^\|\s*`(.*)`\s*\|\s*(`(.*)`|\*error\*)\s*\|\s*(G2|HP help)\b')
for name, g in sorted(group.items()):
    with open(os.path.join(cmd, g, name + '.md'), encoding='utf-8') as fh:
        for line in fh:
            m = cell.match(line.strip())
            if not m:
                continue
            call = m.group(1).replace('\\|', '|')
            ans = '*error*' if m.group(2) == '*error*' else m.group(3)
            if (name, call) in seen:
                continue
            seen.add((name, call))
            rows.append((name, call, ans, '', m.group(4), g))

out = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tests', 'conformance', 'hpkit_cases.tsv')
with open(out, 'w', encoding='utf-8', newline='\n') as fh:
    fh.write('# Conformance cases from hp-prime-kit (https://github.com/JordiRigau/hp-prime-kit, MIT,\n')
    fh.write('# see LICENSE-hp-prime-kit). Answers recorded on HP\'s Virtual Calculator 2.4\n')
    fh.write('# (source "emulator"), measured on a G2, or stated by HP\'s built-in help.\n')
    fh.write('# entry\tcall\tanswer\ttype\tsource\tgroup\n')
    for r in rows:
        fh.write('\t'.join(x.replace('\t', ' ') for x in r) + '\n')
print(len(rows), 'rows;', sum(1 for r in rows if r[4] != 'emulator'), 'from the documentation')
