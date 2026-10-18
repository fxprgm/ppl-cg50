/* builtins.c -- the table of built-in names.  Each family lives in its own
 * file (bi_math.c, bi_misc.c, bi_list.c, bi_mat.c, bi_prob.c) with its own
 * table; this file joins them and looks names up.  Graphics, input and the
 * CAS are entries like any other, so a front end can replace them. */
#include "bi.h"
#include <string.h>

extern const builtin bi_math_table[];
extern const builtin bi_misc_table[];
extern const builtin bi_gfx_table[];
extern const builtin bi_cas_table[];
extern const builtin bi_cas2_table[];

static const builtin bi_core_table[] = {
    /* lists */
    { "SIZE", b_size, 1, 1, 0 }, { "DIM", b_dim, 1, 1, 0 },
    { "CONCAT", b_concat, 1, -1, 0 }, { "SORT", b_sort, 1, 2, 0 },
    { "REVERSE", b_reverse, 1, 1, 0 }, { "POS", b_pos, 2, 2, 0 },
    { "INSERT", b_insert, 3, 3, 0 }, { "SUPPRESS", b_suppress, 1, 2, 0 },
    { "suppress", b_suppress, 1, 2, 0 },
    { "\xCE\xA3LIST", b_sigmalist, 1, 1, 0 },      /* ΣLIST */
    { "SUM", b_sigmalist, 1, 1, 0 }, { "AVERAGE", b_mean, 1, 2, 0 },
    { "\xCE\xA0LIST", b_pilist, 1, 1, 0 },         /* ΠLIST */
    { "\xCF\x80LIST", b_pilist, 1, 1, 0 },         /* πLIST */
    { "\xCE\x94LIST", b_deltalist, 1, 1, 0 },      /* ΔLIST */
    { "DIFFERENCE", b_difference, 2, -1, 0 }, { "INTERSECT", b_intersect, 2, 2, 0 },
    { "UNION", b_union, 2, 2, 0 }, { "EQ", b_eq, 2, 2, 0 },
    { "MAKELIST", b_makelist, 2, 5, BF_QUOTE1 | BF_QUOTE2 },
    { "\xCE\xA3", b_sigma, 4, 4, BF_QUOTE1 | BF_QUOTE2 },          /* Σ */
    { "\xE2\x88\x91", b_sigma, 4, 4, BF_QUOTE1 | BF_QUOTE2 },      /* ∑ */
    { "ITERATE", b_iterate, 4, 4, BF_QUOTE1 | BF_QUOTE2 },
    { "EVALLIST", b_evallist, 1, 1, 0 }, { "EXECON", b_execon, 2, -1, 0 },
    { "ListToMat", b_listtomat, 1, 1, 0 }, { "list2mat", b_listtomat, 1, 1, 0 },
    { "MatToList", b_mattolist, 1, 1, 0 }, { "mat2list", b_mattolist, 1, 1, 0 },
    /* strings */
    { "ASC", b_asc, 1, 1, 0 }, { "CHAR", b_char, 1, 1, 0 },
    { "LEFT", b_left, 2, 2, 0 }, { "RIGHT", b_right, 2, 2, 0 }, { "MID", b_mid, 2, 3, 0 },
    { "INSTRING", b_instring, 2, 2, 0 }, { "LOWER", b_lower, 1, 1, 0 }, { "UPPER", b_upper, 1, 1, 0 },
    { "ROTATE", b_rotate, 1, 2, 0 }, { "REPLACE", b_replace, 3, 3, 0 },
    { "STRING", b_string, 1, 4, 0 }, { "EXPR", b_expr, 1, 1, 0 },
    { "STRINGFROMID", b_strfromid, 1, 1, 0 },
    /* matrices */
    { "MAKEMAT", b_makemat, 3, 3, BF_QUOTE1 }, { "IDENMAT", b_idenmat, 1, 1, 0 },
    { "RANDMAT", b_randmat, 1, 4, 0 }, { "TRN", b_trn, 1, 1, 0 }, { "DET", b_det, 1, 1, 0 },
    { "RREF", b_rref, 1, 1, 0 }, { "ADDROW", b_addrow, 3, 3, BF_INPLACE }, { "ADDCOL", b_addcol, 3, 3, BF_INPLACE },
    { "DELROW", b_delrow, 2, 2, BF_INPLACE }, { "DELCOL", b_delcol, 2, 2, BF_INPLACE }, { "REDIM", b_redim, 2, 2, BF_INPLACE },
    { "SCALE", b_scale, 3, 3, BF_INPLACE }, { "SCALEADD", b_scaleadd, 4, 4, BF_INPLACE }, { "SUB", b_sub, 3, 3, 0 },
    { "SWAPROW", b_swaprow, 3, 3, BF_INPLACE }, { "SWAPCOL", b_swapcol, 3, 3, BF_INPLACE },
    { "DOT", b_dot, 2, 2, 0 }, { "dot", b_dot, 2, 2, 0 }, { "CROSS", b_cross, 2, 2, 0 },
    { "TRACE", b_trace, 1, 1, 0 }, { "ROWNORM", b_rownorm, 1, 1, 0 }, { "COLNORM", b_colnorm, 1, 1, 0 },
    { "RANK", b_rank, 1, 1, 0 }, { "INVERSE", b_inverse, 1, 1, 0 },
    { "LSQ", b_lsq, 2, 2, 0 }, { "LU", b_lu, 1, 1, 0 }, { "QR", b_qr, 1, 1, 0 },
    { "COND", b_cond, 1, 1, 0 }, { "SPECNORM", b_specnorm, 1, 1, 0 }, { "SPECRAD", b_specrad, 1, 1, 0 },
    { "EIGENVAL", b_eigenval, 1, 1, 0 }, { "EIGENVV", b_eigenvv, 1, 1, 0 },
    { "cholesky", b_cholesky, 1, 1, 0 }, { "diag", b_diag, 1, 1, 0 }, { "hilbert", b_hilbert, 1, 1, 0 },
    { "JordanBlock", b_jordanblock, 2, 2, 0 }, { "vandermonde", b_vandermonde, 1, 1, 0 },
    { "POLYEVAL", b_polyeval, 2, 2, 0 }, { "POLYCOEF", b_polycoef, 1, 1, 0 },
    { "POLYROOT", b_polyroot, 1, 1, 0 },
    { "l1norm", b_l1norm, 1, 1, 0 }, { "maxnorm", b_maxnorm, 1, 1, 0 }, { "l2norm", b_l2norm, 1, 1, 0 },
    { "mkisom", b_mkisom, 2, 2, 0 }, { "pivot", b_pivot, 3, 3, 0 }, { "SVL", b_svl, 1, 1, 0 },
    { "SVD", b_svd, 1, 1, 0 }, { "SCHUR", b_schur, 1, 1, 0 }, { "LQ", b_lq, 1, 1, 0 },
    { "hessenberg", b_hessenberg, 1, 1, 0 },
    /* probability and statistics */
    { "NORMALD", b_normald, 1, 3, 0 }, { "NORMALD_CDF", b_normald_cdf, 1, 4, 0 },
    { "NORMALD_ICDF", b_normald_icdf, 1, 3, 0 },
    { "BINOMIAL", b_binomial, 2, 3, 0 }, { "BINOMIAL_CDF", b_binomial_cdf, 3, 4, 0 },
    { "BINOMIAL_ICDF", b_binomial_icdf, 3, 3, 0 },
    { "POISSON", b_poisson, 2, 2, 0 }, { "POISSON_CDF", b_poisson_cdf, 2, 3, 0 },
    { "POISSON_ICDF", b_poisson_icdf, 2, 2, 0 },
    { "GEOMETRIC", b_geometric, 2, 2, 0 }, { "GEOMETRIC_CDF", b_geometric_cdf, 2, 3, 0 },
    { "GEOMETRIC_ICDF", b_geometric_icdf, 2, 2, 0 },
    { "geometric_cdf", b_geometric_cdf, 2, 3, 0 }, { "geometric_icdf", b_geometric_icdf, 2, 2, 0 },
    { "CHISQUARE", b_chisquare, 2, 2, 0 }, { "CHISQUARE_CDF", b_chisquare_cdf, 2, 3, 0 },
    { "CHISQUARE_ICDF", b_chisquare_icdf, 2, 2, 0 },
    { "STUDENT", b_student, 2, 2, 0 }, { "STUDENT_CDF", b_student_cdf, 2, 3, 0 },
    { "STUDENT_ICDF", b_student_icdf, 2, 2, 0 },
    { "FISHER", b_fisher, 3, 3, 0 }, { "FISHER_CDF", b_fisher_cdf, 3, 4, 0 },
    { "FISHER_ICDF", b_fisher_icdf, 3, 3, 0 },
    { "Beta", b_beta, 2, 2, 0 }, { "erf", b_erf, 1, 1, 0 }, { "erfc", b_erfc, 1, 1, 0 },
    { "Psi", b_psi, 1, 2, 0 }, { "Zeta", b_zeta, 1, 1, 0 },
    { "mean", b_mean, 1, 2, 0 }, { "median", b_median, 1, 2, 0 },
    { "stddev", b_stddev, 1, 2, 0 }, { "variance", b_variance, 1, 2, 0 },
    { NULL, NULL, 0, 0, 0 }
};

#define MAXB 600
static const builtin *ALL[MAXB];
static int nall;

static void add_table(const builtin *t)
{
    for (; t->name && nall < MAXB; t++) ALL[nall++] = t;
}

static void init_table(void)
{
    if (nall) return;
    add_table(bi_math_table);
    add_table(bi_misc_table);
    add_table(bi_gfx_table);
    add_table(bi_core_table);
    add_table(bi_cas_table);
    add_table(bi_cas2_table);
}

const builtin *bi_lookup(const char *name)
{
    int i;
    init_table();
    for (i = 0; i < nall; i++) if (!strcmp(ALL[i]->name, name)) return ALL[i];
    return NULL;
}

const builtin *bi_get(int id) { init_table(); return (id >= 0 && id < nall) ? ALL[id] : NULL; }

int bi_id(const builtin *b)
{
    int i;
    init_table();
    for (i = 0; i < nall; i++) if (ALL[i] == b) return i;
    return -1;
}

int bi_count(void) { init_table(); return nall; }
