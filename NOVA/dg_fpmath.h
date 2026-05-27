#ifndef DG_FPMATH_H_
#define DG_FPMATH_H_ 0

#include "nova_defs.h"

typedef struct _SHORT_FLOAT {
        int32   short_fract;                            /* Fraction                  */
        short   expo;                                   /* Exponent + 64             */
        uint8   sign;                                   /* Sign                      */
} SHORT_FLOAT;

typedef struct _LONG_FLOAT {
        t_int64 long_fract;                             /* Fraction                  */
        short   expo;                                   /* Exponent + 64             */
        uint8   sign;                                   /* Sign                      */
} LONG_FLOAT;

extern void get_sf(SHORT_FLOAT *fl, t_int64 *fpr);
extern void store_sf(SHORT_FLOAT *fl, t_int64 *fpr);
extern void get_lf(LONG_FLOAT *fl, t_int64 *fpr);
extern void store_lf(LONG_FLOAT *fl, t_int64 *fpr);
extern int normal_sf (SHORT_FLOAT *fl);
extern int normal_lf (LONG_FLOAT *fl);
extern int overflow_sf(SHORT_FLOAT *fl);
extern int overflow_lf(LONG_FLOAT *fl);
extern int underflow_sf(SHORT_FLOAT *fl);
extern int underflow_lf(LONG_FLOAT *fl);
extern int significance_sf(SHORT_FLOAT *fl);
extern int significance_lf(LONG_FLOAT *fl);
extern int add_sf(SHORT_FLOAT *fl, SHORT_FLOAT *add_f1, int normal);
extern int add_lf(LONG_FLOAT *fl, LONG_FLOAT *add_fl, int normal);
extern int mul_sf(SHORT_FLOAT *fl, SHORT_FLOAT *mul_fl);
extern int mul_lf(LONG_FLOAT *fl, LONG_FLOAT *mul_fl);
extern int div_sf(SHORT_FLOAT *fl, SHORT_FLOAT *div_fl);
extern int div_lf(LONG_FLOAT *fl, LONG_FLOAT *div_fl);

#define DGF_OK     0
#define DGF_OVF    1
#define DGF_UNF    2
#define DGF_DVZ    3

#endif
