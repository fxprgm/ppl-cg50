/* dec.h -- HP Prime Home real numbers: 12-digit decimal floating point.
 *
 * A real is a 12-significant-digit decimal with an exponent in -499..499,
 * like the Prime's Home reals (see notes/ppl.md, "Number semantics").
 * Every operation is computed exactly (or with enough guard digits) and then
 * rounded once to 12 digits, half away from zero.
 *
 * Portable C11: no floating point, no 64-bit integers on the hot paths, no
 * endianness or alignment assumptions.  The coefficient is held in base-10000
 * limbs so that every limb product fits in 32 bits (the SH-4 has a fast
 * 32-bit multiply and no 64-bit divide).
 */
#ifndef PPL_DEC_H
#define PPL_DEC_H

#include <stdint.h>
#include <stddef.h>

#define DEC_DIGITS 12          /* significant digits of a stored real */
#define DEC_EMAX   499         /* largest exponent: 9.99999999999E499 */
#define DEC_EMIN   (-499)      /* smallest normal exponent: 1E-499 */

enum { DEC_FINITE = 0, DEC_INF = 1, DEC_NAN = 2 };

/* |x| = d1.d2d3...d12 * 10^e with m[0] = d1d2d3d4 (m[0] >= 1000 unless the
 * value is zero), m[1] = d5..d8, m[2] = d9..d12.  Zero is all-zero with
 * e = 0 and s = 0. */
typedef struct {
    uint16_t m[3];
    int16_t  e;
    uint8_t  s;     /* 1 = negative */
    uint8_t  k;     /* DEC_FINITE, DEC_INF, DEC_NAN */
} real;

/* Status codes returned by operations (0 = fine). */
enum {
    DEC_OK = 0,
    DEC_EOVERFLOW,    /* result above MAXREAL */
    DEC_EUNDERFLOW,   /* result below MINREAL (the result is set to 0) */
    DEC_EDIVZERO,     /* division by zero */
    DEC_EDOMAIN,      /* argument outside the function's domain */
    DEC_ESYNTAX,      /* not a number (parsing) */
    DEC_ERANGE        /* does not fit the requested integer type */
};

/* Angle measure, as Home's HAngle: 0 radians, 1 degrees, 2 gradians. */
enum { ANG_RAD = 0, ANG_DEG = 1, ANG_GRAD = 2 };

/* Overflow and underflow are not errors on the Prime: a result above MAXREAL
 * becomes +-MAXREAL and one below MINREAL becomes 0.  Operations still
 * report DEC_EOVERFLOW / DEC_EUNDERFLOW (with the clamped result stored) so
 * that a caller can tell; the interpreter ignores both. */
#define DEC_SOFT(st) ((st) == DEC_EOVERFLOW || (st) == DEC_EUNDERFLOW)

extern int dec_round_mode;   /* 1 (default): ties to even, as the Prime; 0: ties away from zero */

/* ---- construction and conversion ---- */
int    dec_maxreal(real *r, int neg);   /* sets +-MAXREAL, returns DEC_EOVERFLOW */
void   dec_zero(real *r);
void   dec_from_int(real *r, int32_t v);
int    dec_from_str(real *r, const char *s, const char **end); /* ASCII literal */
int    dec_to_int32(const real *x, int32_t *v);   /* exact integers only */
int    dec_to_int32_trunc(const real *x, int32_t *v); /* truncates toward 0 */
int    dec_is_int(const real *x);
int    dec_is_zero(const real *x);
/* Unsigned 64-bit value (as two 32-bit halves) of a non-negative integer real
 * below 10^12, or of its truncation. */
int    dec_to_u64(const real *x, uint32_t *hi, uint32_t *lo);
void   dec_from_u64(real *r, uint32_t hi, uint32_t lo); /* rounds to 12 digits */

/* ---- arithmetic (exactly rounded to 12 digits) ---- */
int dec_add(real *r, const real *a, const real *b);
int dec_sub(real *r, const real *a, const real *b);
int dec_mul(real *r, const real *a, const real *b);
int dec_div(real *r, const real *a, const real *b);
void dec_neg(real *r, const real *a);
void dec_abs(real *r, const real *a);
int dec_cmp(const real *a, const real *b);       /* -1, 0, 1 (NaN: 2) */

/* ---- rounding / parts ---- */
int dec_ip(real *r, const real *x);       /* integer part (truncate) */
int dec_fp(real *r, const real *x);       /* fractional part, sign of x */
int dec_floor(real *r, const real *x);
int dec_ceil(real *r, const real *x);
int dec_round_places(real *r, const real *x, int places, int truncate);
            /* places >= 0: decimal places; < 0: -places significant digits */
int dec_mod(real *r, const real *a, const real *b);  /* a - b*floor(a/b) */
int dec_xpon(const real *x);              /* decimal exponent */
void dec_mant(real *r, const real *x);    /* mantissa, 1 <= |m| < 10 */

/* ---- elementary functions (decfn.c) ---- */
int dec_sqrt(real *r, const real *x);
int dec_exp(real *r, const real *x);
int dec_expm1(real *r, const real *x);
int dec_ln(real *r, const real *x);
int dec_lnp1(real *r, const real *x);
int dec_log10(real *r, const real *x);
int dec_alog(real *r, const real *x);     /* 10^x */
int dec_logb(real *r, const real *x, const real *b);
int dec_pow(real *r, const real *x, const real *y);
int dec_nthroot(real *r, const real *n, const real *x);  /* real n-th root */
int dec_sin(real *r, const real *x, int ang);
int dec_cos(real *r, const real *x, int ang);
int dec_tan(real *r, const real *x, int ang);
int dec_asin(real *r, const real *x, int ang);
int dec_acos(real *r, const real *x, int ang);
int dec_atan(real *r, const real *x, int ang);
int dec_atan2(real *r, const real *y, const real *x, int ang);
int dec_sinh(real *r, const real *x);
int dec_cosh(real *r, const real *x);
int dec_tanh(real *r, const real *x);
int dec_asinh(real *r, const real *x);
int dec_acosh(real *r, const real *x);
int dec_atanh(real *r, const real *x);
int dec_gamma(real *r, const real *x);
int dec_lgamma(real *r, const real *x);
void dec_pi(real *r);
void dec_e(real *r);
/* angle conversion of a real angle between measures */
int dec_angle_to_rad(real *r, const real *x, int ang);
int dec_angle_from_rad(real *r, const real *x, int ang);

/* ---- formatting ---- */
enum { FMT_STD = 0, FMT_FIX = 1, FMT_SCI = 2, FMT_ENG = 3 };
/* Format flags */
#define DECF_ASCII 1   /* write '-' and 'E' instead of the Prime's glyphs */
/* Writes UTF-8 into buf (size >= 40); returns the length. */
int dec_format(char *buf, const real *x, int fmt, int digits, int flags);

#endif
