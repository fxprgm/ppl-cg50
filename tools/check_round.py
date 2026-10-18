"""Check that the engine's elementary functions are correctly rounded.

    python3 tools/check_round.py [N]

Random 12-digit arguments; the reference is Python's decimal module at 60
digits (exp, ln, log10, sqrt, power), rounded to 12 digits half away from
zero.  Prints every mismatch and a summary.
"""
import decimal as D, random, subprocess, sys, os

D.getcontext().prec = 60
N = int(sys.argv[1]) if len(sys.argv) > 1 else 300
ppl = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'build', 'ppl')
rnd = random.Random(1)

def r12(x):
    if x == 0:
        return D.Decimal(0)
    e = x.adjusted()
    q = D.Decimal(1).scaleb(e - 11)
    return x.quantize(q, rounding=D.ROUND_HALF_UP)

def arg(lo, hi):
    e = rnd.randint(lo, hi)
    m = rnd.randint(10**11, 10**12 - 1)
    return D.Decimal(m).scaleb(e - 11)

def fmt(x):
    return format(x.normalize(), 'E').replace('E+', 'E')

cases = []
for _ in range(N):
    a = arg(-6, 2)
    if rnd.random() < 0.5: a = -a
    cases.append(('EXP', 'EXP(%s)' % fmt(a), a.exp()))
    b = arg(-30, 30); cases.append(('LN', 'LN(%s)' % fmt(b), b.ln()))
    cases.append(('LOG', 'LOG(%s)' % fmt(b), b.log10()))
    cases.append(('SQRT', '√(%s)' % fmt(b), b.sqrt()))
    c = arg(-3, 2); p = arg(-2, 1)
    cases.append(('POW', '(%s)^(%s)' % (fmt(c), fmt(p)), c ** p))
try:
    import mpmath
    mpmath.mp.dps = 60
    for _ in range(N):
        a = arg(-4, 2)
        for name, f in (('SIN', mpmath.sin), ('COS', mpmath.cos), ('TAN', mpmath.tan), ('ATAN', mpmath.atan)):
            cases.append((name, '%s(%s)' % (name, fmt(a)), D.Decimal(mpmath.nstr(f(mpmath.mpf(str(a))), 50))))
        s = arg(-6, -1)
        cases.append(('ASIN', 'ASIN(%s)' % fmt(s), D.Decimal(mpmath.nstr(mpmath.asin(mpmath.mpf(str(s))), 50))))
        cases.append(('ACOS', 'ACOS(%s)' % fmt(s), D.Decimal(mpmath.nstr(mpmath.acos(mpmath.mpf(str(s))), 50))))
except ImportError:
    print('(mpmath not installed: trig not checked)')

bad = {}
tot = {}
chunk = 200
for i in range(0, len(cases), chunk):
    part = cases[i:i + chunk]
    out = subprocess.run([ppl, '--ascii', 'eval'] + [c[1] for c in part], capture_output=True, text=True).stdout.split('\n')
    for c, got in zip(part, out):
        want = r12(c[2])
        tot[c[0]] = tot.get(c[0], 0) + 1
        try:
            g = D.Decimal(got.strip())
        except Exception:
            g = None
        if g is None or g != want:
            bad[c[0]] = bad.get(c[0], 0) + 1
            print('MISMATCH %s -> %s want %s' % (c[1], got.strip(), fmt(want)))
for k in tot:
    print('%-5s %d/%d correctly rounded' % (k, tot[k] - bad.get(k, 0), tot[k]))
