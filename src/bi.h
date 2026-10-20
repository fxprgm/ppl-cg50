/* bi.h -- shared helpers of the built-in function files. */
#ifndef PPL_BI_H
#define PPL_BI_H

#include "vm.h"

#define BI(name) int name(ppl_vm *V, int argc, value *a, value *out)

int  bi_fin(value *out, const real *r, int st);          /* status -> error */
int  bi_real(const value *v, real *r);                    /* E_BADTYPE */
int  bi_int(const value *v, int32_t *n);                  /* integer-valued */
int  bi_map2(ppl_vm *V, const value *a, const value *b,
             int (*f)(ppl_vm *, const value *, const value *, value *), value *out);
int  bi_str_new(value *out, const uint16_t *s, uint32_t n);
int  bi_list_from(value *out, uint32_t n);                /* list of n zeros */
void bi_text(ppl_vm *V, value *out, const value *v, int quote);  /* STRING */

/* list and string built-ins (bi_list.c) */
BI(b_size); BI(b_dim); BI(b_concat); BI(b_sort); BI(b_reverse); BI(b_pos);
BI(b_insert); BI(b_suppress); BI(b_sigmalist); BI(b_pilist); BI(b_deltalist);
BI(b_difference); BI(b_intersect); BI(b_union); BI(b_eq); BI(b_makelist);
BI(b_asc); BI(b_char); BI(b_left); BI(b_right); BI(b_mid); BI(b_instring);
BI(b_lower); BI(b_upper); BI(b_rotate); BI(b_replace); BI(b_string);
BI(b_expr); BI(b_strfromid); BI(b_evallist); BI(b_execon); BI(b_sigma);
BI(b_iterate); BI(b_listtomat); BI(b_mattolist); BI(b_listfn);
BI(b_list2mat); BI(b_mat2list); BI(b_remove);

/* matrix built-ins (bi_mat.c) */
BI(b_makemat); BI(b_idenmat); BI(b_randmat); BI(b_trn); BI(b_det); BI(b_rref);
BI(b_addrow); BI(b_addcol); BI(b_delrow); BI(b_delcol); BI(b_redim);
BI(b_scale); BI(b_scaleadd); BI(b_sub); BI(b_swaprow); BI(b_swapcol);
BI(b_dot); BI(b_cross); BI(b_trace); BI(b_rownorm); BI(b_colnorm);
BI(b_rank); BI(b_inverse); BI(b_lsq); BI(b_lu); BI(b_qr); BI(b_cond);
BI(b_specnorm); BI(b_specrad); BI(b_eigenval); BI(b_eigenvv); BI(b_cholesky);
BI(b_diag); BI(b_hilbert); BI(b_jordanblock); BI(b_vandermonde); BI(b_polyeval);
BI(b_polycoef); BI(b_polyroot); BI(b_l1norm); BI(b_maxnorm); BI(b_l2norm);
BI(b_mkisom); BI(b_pivot); BI(b_svl); BI(b_svd); BI(b_schur); BI(b_lq);
BI(b_hessenberg); BI(b_lucas); BI(b_rowdim); BI(b_coldim); BI(b_eigvc); BI(b_eigvl); BI(b_randperm);

/* probability and statistics (bi_prob.c) */
BI(b_normald); BI(b_normald_cdf); BI(b_normald_icdf);
BI(b_binomial); BI(b_binomial_cdf); BI(b_binomial_icdf);
BI(b_poisson); BI(b_poisson_cdf); BI(b_poisson_icdf);
BI(b_geometric); BI(b_geometric_cdf); BI(b_geometric_icdf);
BI(b_chisquare); BI(b_chisquare_cdf); BI(b_chisquare_icdf);
BI(b_student); BI(b_student_cdf); BI(b_student_icdf);
BI(b_fisher); BI(b_fisher_cdf); BI(b_fisher_icdf);
BI(b_beta); BI(b_erf); BI(b_erfc); BI(b_psi); BI(b_zeta);
BI(b_mean); BI(b_median); BI(b_stddev); BI(b_variance); BI(b_stddevp);

#endif
