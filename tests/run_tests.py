"""Run the PPL regression tests in tests/lang.

Each .ppl file is a program; lines of the form
    //> EXPR => RESULT
are evaluated on Home after loading it (`ppl --ascii run FILE EXPR...`) and
the printed result compared with RESULT.  RESULT `*error*` expects an error;
`*error* TEXT` an error whose message contains TEXT (e.g. `(line 12)`).
A line `//+ NAME.ppl` loads tests/lang/lib/NAME.ppl as a second program.
"""
import glob, os, subprocess, sys

here = os.path.dirname(os.path.abspath(__file__))
ppl = os.path.join(here, '..', 'build', 'ppl')
fails = total = 0
for path in sorted(glob.glob(os.path.join(here, 'lang', '*.ppl'))):
    cases, libs = [], []
    for line in open(path, encoding='utf-8'):
        if line.startswith('//+'):
            libs.append(os.path.join(here, 'lang', 'lib', line[3:].strip()))
        if line.startswith('//>'):
            expr, want = line[3:].rsplit('=>', 1)
            cases.append((expr.strip(), want.strip()))
    for expr, want in cases:
        total += 1
        p = subprocess.run([ppl, '--ascii', 'run', path] + libs + [expr], capture_output=True, text=True)
        got = p.stdout.strip() if p.returncode == 0 else '*error*'
        if want.startswith('*error*') and want != '*error*' and got == '*error*':
            # `*error* (line N)`: the error message must contain the rest
            got = '*error* ' + want[7:].strip() if want[7:].strip() in p.stderr else '*error* ' + p.stderr.strip()
        if got != want:
            fails += 1
            err = p.stderr.strip().splitlines()[-1] if p.stderr.strip() else ''
            print('FAIL %s: %s => %s (want %s) %s' % (os.path.basename(path), expr, got, want, err))
# golden images: //# EXPR => CRC in tests/gfx/*.ppl
for path in sorted(glob.glob(os.path.join(here, 'gfx', '*.ppl'))):
    for line in open(path, encoding='utf-8'):
        if not line.startswith('//#'):
            continue
        expr, want = line[3:].rsplit('=>', 1)
        expr, want = expr.strip(), want.split()[0]
        total += 1
        p = subprocess.run([ppl, '--crc', 'run', path, expr], capture_output=True, text=True)
        got = p.stdout.strip().splitlines()[-1].split()[-1] if p.returncode == 0 and p.stdout.strip() else '*error*'
        if got != want:
            fails += 1
            print('FAIL %s: %s => crc %s (want %s)' % (os.path.basename(path), expr, got, want))
print('%d/%d language tests pass' % (total - fails, total))
sys.exit(1 if fails else 0)
