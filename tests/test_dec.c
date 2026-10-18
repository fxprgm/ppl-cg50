/* test_dec.c -- unit tests of the decimal layer against values recorded on
 * HP's Virtual Calculator (hp-prime-kit results) and exact identities. */
#include "../src/dec.h"
#include <stdio.h>
#include <string.h>

static int fails, total;

static void num(real *r, const char *s) { dec_from_str(r, s, NULL); }

static void check(const char *what, const real *r, const char *want)
{
    char buf[64];
    dec_format(buf, r, FMT_STD, 0, DECF_ASCII);
    total++;
    if (strcmp(buf, want)) { fails++; printf("FAIL %-28s got %-22s want %s\n", what, buf, want); }
}

typedef int (*f1)(real *, const real *);
typedef int (*f1a)(real *, const real *, int);

static void t1(const char *name, f1 f, const char *x, const char *want)
{
    real a, r; char what[64];
    num(&a, x);
    dec_zero(&r);
    if (f(&r, &a) && !DEC_SOFT(0)) {}
    snprintf(what, sizeof what, "%s(%s)", name, x);
    check(what, &r, want);
}

static void ta(const char *name, f1a f, const char *x, int ang, const char *want)
{
    real a, r; char what[64];
    num(&a, x);
    dec_zero(&r);
    f(&r, &a, ang);
    snprintf(what, sizeof what, "%s(%s)[%d]", name, x, ang);
    check(what, &r, want);
}

typedef int (*f2)(real *, const real *, const real *);
static void t2(const char *name, f2 f, const char *x, const char *y, const char *want)
{
    real a, b, r; char what[80];
    num(&a, x); num(&b, y);
    dec_zero(&r);
    f(&r, &a, &b);
    snprintf(what, sizeof what, "%s(%s,%s)", name, x, y);
    check(what, &r, want);
}

static void tfmt(const char *x, int fmt, int dg, const char *want)
{
    real a; char buf[64];
    num(&a, x);
    dec_format(buf, &a, fmt, dg, DECF_ASCII);
    total++;
    if (strcmp(buf, want)) { fails++; printf("FAIL fmt(%s,%d,%d) got %s want %s\n", x, fmt, dg, buf, want); }
}

int main(void)
{
    /* parsing and Standard display */
    tfmt("100000000000", FMT_STD, 0, "100000000000");
    tfmt("1E12", FMT_STD, 0, "1E12");
    tfmt("1E-12", FMT_STD, 0, "0.000000000001");
    tfmt("1E-13", FMT_STD, 0, "1E-13");
    tfmt("1.234E-9", FMT_STD, 0, "0.000000001234");
    /* below 1: fixed only if everything fits in 12 decimal places (HP's Virtual Calculator) */
    tfmt("1.2345E-9", FMT_STD, 0, "1.2345E-9");
    tfmt("1.234E-9", FMT_STD, 0, "0.000000001234");
    tfmt("0.0213116411288", FMT_STD, 0, "2.13116411288E-2");
    tfmt("0.021311641129", FMT_STD, 0, "0.021311641129");
    tfmt("0.000012345", FMT_STD, 0, "0.000012345");
    tfmt("-0.0000000000004", FMT_STD, 0, "-4E-13");
    tfmt("9.99999999999E499", FMT_STD, 0, "9.99999999999E499");
    tfmt("1E-499", FMT_STD, 0, "1E-499");
    tfmt("3.14159265358979", FMT_STD, 0, "3.14159265359");
    tfmt("-0.5", FMT_STD, 0, "-0.5");
    tfmt("0.333333333333", FMT_FIX, 8, "0.33333333");
    tfmt("0.333333333333", FMT_FIX, 2, "0.33");
    tfmt("123.456", FMT_SCI, 2, "1.23E2");
    tfmt("1.23456E9", FMT_ENG, 2, "1.23E9");
    tfmt("123.456789", FMT_FIX, 2, "123.46");

    /* arithmetic */
    t2("div", dec_div, "8", "3", "2.66666666667");
    t2("div", dec_div, "1", "3", "0.333333333333");
    t2("div", dec_div, "2", "3", "0.666666666667");
    t2("mul", dec_mul, "0.333333333333", "3", "0.999999999999");
    t2("add", dec_add, "0.1", "0.2", "0.3");
    t2("sub", dec_sub, "1", "1E-12", "0.999999999999");
    t2("add", dec_add, "1E499", "9E499", "9.99999999999E499");
    t2("mul", dec_mul, "1E250", "1E250", "9.99999999999E499");
    t2("div", dec_div, "1", "1E500", "0");        /* 1/MAXREAL underflows */
    t2("div", dec_div, "10", "1E500", "1E-499");
    t2("mod", dec_mod, "9", "4", "1");
    t2("mod", dec_mod, "-9", "4", "3");
    t2("mod", dec_mod, "9", "-4", "-3");
    t2("mod", dec_mod, "5.5", "2", "1.5");

    /* oracle values (hp-prime-kit results.tsv, Virtual Calculator 2.4) */
    ta("SIN", dec_sin, "1", 0, "0.841470984808");
    ta("COS", dec_cos, "1", 0, "0.540302305868");
    ta("TAN", dec_tan, "1", 0, "1.55740772465");
    ta("ASIN", dec_asin, "0.5", 0, "0.523598775598");
    ta("ACOS", dec_acos, "0.5", 0, "1.0471975512");
    ta("ATAN", dec_atan, "1", 0, "0.785398163397");
    ta("SIN", dec_sin, "90", 1, "1");
    ta("SIN", dec_sin, "100", 2, "1");
    ta("SIN", dec_sin, "180", 1, "0");
    ta("COS", dec_cos, "90", 1, "0");
    ta("SIN", dec_sin, "3.14159265359", 0, "-2.06761537357E-13");
    ta("ASIN", dec_asin, "1", 1, "90");
    ta("ACOS", dec_acos, "0.5", 1, "60");
    t1("EXP", dec_exp, "5", "148.413159103");
    t1("EXP", dec_exp, "-2.3", "0.100258843723");
    t1("EXP", dec_exp, "1", "2.71828182846");
    t1("EXPM1", dec_expm1, "0.5", "0.6487212707");
    t1("LN", dec_ln, "0.1", "-2.30258509299");
    t1("LN", dec_ln, "2", "0.69314718056");
    t1("LN", dec_ln, "1", "0");
    t1("LNP1", dec_lnp1, "0.5", "0.405465108108");
    t1("LOG", dec_log10, "8", "0.903089986992");
    t1("LOG", dec_log10, "100", "2");
    t1("ALOG", dec_alog, "3", "1000");
    t1("SQRT", dec_sqrt, "2", "1.41421356237");
    t1("SQRT", dec_sqrt, "30", "5.47722557505");
    t1("SQRT", dec_sqrt, "1.44", "1.2");
    t1("SINH", dec_sinh, "1", "1.17520119364");
    t1("COSH", dec_cosh, "1", "1.54308063482");
    t1("TANH", dec_tanh, "1", "0.761594155956");
    t1("ASINH", dec_asinh, "1", "0.88137358702");
    t1("ACOSH", dec_acosh, "2", "1.31695789692");
    t1("ATANH", dec_atanh, "0.5", "0.549306144334");
    t1("GAMMA", dec_gamma, "5", "24");
    t1("GAMMA", dec_gamma, "0.5", "1.77245385091");
    t2("POW", dec_pow, "3", "0.5", "1.73205080757");
    t2("POW", dec_pow, "2", "10", "1024");
    t2("POW", dec_pow, "2", "-1", "0.5");
    t2("POW", dec_pow, "-2", "3", "-8");
    t2("POW", dec_pow, "1E250", "2", "9.99999999999E499");
    t2("NTHROOT", dec_nthroot, "3", "8", "2");
    t2("NTHROOT", dec_nthroot, "3", "-8", "-2");
    t2("NTHROOT", dec_nthroot, "1.73205080757", "64", "11.0356646359");

    printf("%d/%d passed\n", total - fails, total);
    return fails != 0;
}
