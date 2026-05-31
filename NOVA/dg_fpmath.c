#include "dg_fpmath.h"

/* ------------------------------------------------------------------- */
/*                     Floating Point Arithmetic                       */
/* ------------------------------------------------------------------- */


/* Get short float from FPAC */

void get_sf (SHORT_FLOAT *fl, t_int64 *fpr)
{
    fl->sign = (uint8)(*fpr >> 63) & 1;
    fl->expo = (short)(*fpr >> 56) & 0x007F;
    fl->short_fract = (int32)(*fpr >> 32) & 0x00FFFFFF;
} 

void print_sf (SHORT_FLOAT *fl)
{
    printf(" %d:%d:%08X",fl->sign,fl->expo, fl->short_fract);
}

/* Store short float to FPAC */

void store_sf (SHORT_FLOAT *fl, t_int64 *fpr)
{
    *fpr = 0;
    *fpr = ((t_int64)fl->sign << 63)
         | ((t_int64)(fl->expo &0x7f) << 56)
         | ((t_int64)(fl->short_fract & 0x00ffffff) <<32);
} 

/* Get long float from FPAC */

void get_lf (LONG_FLOAT *fl, t_int64 *fpr)
{
    fl->sign = (uint8)(*fpr >> 63) & 1;
    fl->expo = (short)(*fpr >> 56) & 0x007F;
    fl->long_fract = (t_int64)*fpr & 0x00FFFFFFFFFFFFFF;

} 

void print_lf (LONG_FLOAT *fl)
{
    printf(" %d:%d:%016llX",fl->sign,fl->expo, fl->long_fract);
}

/* Store long float to FPAC */

void store_lf (LONG_FLOAT *fl, t_int64 *fpr)
{
    *fpr = 0;
    *fpr = (t_int64)fl->sign << 63;
    *fpr |= (t_int64)(fl->expo & 0x7f) << 56;
    *fpr |= fl->long_fract & 0x00FFFFFFFFFFFFFF;
}


/* Check short for Overflow */

int overflow_sf (SHORT_FLOAT *fl)
{
    if (fl->expo > 127) {
        fl->expo &= 0x007F;
        return(1);
    }
    return(0);

}

/* Normalize Short Float */

int normal_sf(SHORT_FLOAT *fl)
{
    if (fl->short_fract) {
        if ((fl->short_fract & 0x00FFFF00) == 0) {
            fl->short_fract <<= 16;
            fl->expo -= 4;
        }
        if ((fl->short_fract & 0x00FF0000) == 0) {
            fl->short_fract <<= 8;
            fl->expo -= 2;
        }
        if ((fl->short_fract & 0x00F00000) == 0) {
            fl->short_fract <<= 4;
            (fl->expo)--;
        }
    } else {
        fl->sign = 0;
        fl->expo = 0;
    }
    if (fl->expo < 0)
        return (2);
    return(0);
}

/* Normalize long float */

int normal_lf (LONG_FLOAT *fl)
{
    if (fl->long_fract) {
        if ((fl->long_fract & 0x00FFFFFFFF000000) == 0) {
            fl->long_fract <<= 32;
            fl->expo -= 8;
        }
        if ((fl->long_fract & 0x00FFFF0000000000) == 0) {
            fl->long_fract <<= 16;
            fl->expo -= 4;
        }
        if ((fl->long_fract & 0x00FF000000000000) == 0) {
            fl->long_fract <<= 8;
            fl->expo -= 2;
        }
        if ((fl->long_fract & 0x00F0000000000000) == 0) {
            fl->long_fract <<= 4;
            (fl->expo)--;
        }
    } else {
        fl->sign = 0;
        fl->expo = 0;
    }
    if (fl->expo < 0)
        return (2);
    return(0);
}

/* Check Long for Overflow */

int overflow_lf(LONG_FLOAT *fl)
{
    if (fl->expo > 127) {
        fl->expo &= 0x007F;
        return(1);
    }
    return(0);

}

int underflow_sf(SHORT_FLOAT *fl)
{
    if (fl->expo < 0) {
        fl->short_fract = 0;
        fl->expo = 0;
        fl->sign = 0;
    }
    return(0);

}


int underflow_lf(LONG_FLOAT *fl)
{
    if (fl->expo < 0) {
        fl->long_fract = 0;
        fl->expo = 0;
        fl->sign = 0;
    }
    return(0);
}

/* Check Short for Over/Under flow */

int over_under_flow_sf(SHORT_FLOAT *fl)
{
    if (fl->expo > 127) {
        fl->expo &= 0x007F;
        return(1);
    } else {
        if (fl->expo < 0) {
            /* set true 0 */
            fl->short_fract = 0;
            fl->expo = 0;
            fl->sign = 0;
        }
    }
    return(0);

}

/* Check Long for Over/Under flow */

int over_under_flow_lf(LONG_FLOAT *fl)
{
    if (fl->expo > 127) {
        fl->expo &= 0x007F;
        return(1);
    } else {
        if (fl->expo < 0) {
            /* set true 0 */
            fl->long_fract = 0;
            fl->expo = 0;
            fl->sign = 0;
        }
    }
    return(0);
}

int significance_sf (SHORT_FLOAT *fl)
{
    fl->sign = 0;
    fl->expo = 0;
    return(0);

}

int significance_lf (LONG_FLOAT *fl)
{
    fl->sign = 0;
    fl->expo = 0;
    return(0);

}


/*-------------------------------------------------------------------*/
/* Add short float                                                   */
/*                                                                   */
/* Input:                                                            */
/*      fl      Float                                                */
/*      add_fl  Float to be added                                    */
/*      normal  Normalize if true                                    */
/* Value:                                                            */
/*              exeption                                             */
/*-------------------------------------------------------------------*/
int add_sf (SHORT_FLOAT *fl, SHORT_FLOAT *add_fl, int normal)
{
int     pgm_check;
int    shift;

    pgm_check = 0;
    if (add_fl->short_fract
    || add_fl->expo) {                                  /* add_fl not 0 */
        if (fl->short_fract
        || fl->expo) {                                  /* fl not 0 */
            /* both not 0 */

            if (fl->expo == add_fl->expo) {
                /* expo equal */

                /* both guard digits */
                fl->short_fract <<= 4;
                add_fl->short_fract <<= 4;
            } else {
                /* expo not equal, denormalize */

                if (fl->expo < add_fl->expo) {
                    /* shift minus guard digit */
                    shift = add_fl->expo - fl->expo - 1;
                    fl->expo = add_fl->expo;

                    if (shift) {
                        if (shift >= 6
                        || ((fl->short_fract >>= (shift * 4)) == 0)) {
                            /* 0, copy summand */

                            fl->sign = add_fl->sign;
                            fl->short_fract = add_fl->short_fract;

                            if (fl->short_fract == 0) {
                                pgm_check = significance_sf(fl);
                            } else {
                                if (normal) {
                                    normal_sf(fl);
                                    pgm_check = underflow_sf(fl);
                                }
                            }
                            return(pgm_check);
                        }
                    }
                    /* guard digit */
                    add_fl->short_fract <<= 4;
                } else {
                    /* shift minus guard digit */
                    shift = fl->expo - add_fl->expo - 1;

                    if (shift) {
                        if (shift >= 6
                        || ((add_fl->short_fract >>= (shift * 4)) == 0)) {
                            /* 0, nothing to add */

                            if (fl->short_fract == 0) {
                                pgm_check = significance_sf(fl);
                            } else {
                                if (normal) {
                                    normal_sf(fl);
                                    pgm_check = underflow_sf(fl);
                                }
                            }
                            return(pgm_check);
                        }
                    }
                    /* guard digit */
                    fl->short_fract <<= 4;
                }
            }

            /* compute with guard digit */
            if (fl->sign == add_fl->sign) {
                fl->short_fract += add_fl->short_fract;
            } else {
                if (fl->short_fract == add_fl->short_fract) {
                    /* true 0 */

                    fl->short_fract = 0;
                    return( significance_sf(fl) );

                } else if (fl->short_fract > add_fl->short_fract) {
                    fl->short_fract -= add_fl->short_fract;
                } else {
                    fl->short_fract = add_fl->short_fract - fl->short_fract;
                    fl->sign = add_fl->sign;
                }
            }

            /* handle overflow with guard digit */
            if (fl->short_fract & 0xF0000000) {
                fl->short_fract >>= 8;
                (fl->expo)++;
                pgm_check = overflow_sf(fl);
            } else {

                if (normal) {
                    /* normalize with guard digit */
                    if (fl->short_fract) {
                        /* not 0 */

                        if (fl->short_fract & 0x0F000000) {
                         /* not normalize, just guard digit */
                            fl->short_fract >>= 4;
                        } else {
                            (fl->expo)--;
                            normal_sf(fl);
                            pgm_check = underflow_sf(fl);
                        }
                    } else {
                        /* true 0 */

                        pgm_check = significance_sf(fl);
                    }
                } else {
                    /* not normalize, just guard digit */
                    fl->short_fract >>= 4;
                    if (fl->short_fract == 0) {
                        pgm_check = significance_sf(fl);
                    }
                }
            }
            return(pgm_check);
        } else {                                        /* fl 0, add_fl not 0 */
            /* copy summand */

            fl->expo = add_fl->expo;
            fl->sign = add_fl->sign;
            fl->short_fract = add_fl->short_fract;
            if (fl->short_fract == 0) {
                return( significance_sf(fl) );
            }
        }
    } else {                                            /* add_fl 0 */
        if (fl->short_fract == 0) {                     /* fl 0 */
            /* both 0 */

            return( significance_sf(fl) );
        }
    }
    if (normal) {
        normal_sf(fl);
        pgm_check = underflow_sf(fl);
    }
    return(pgm_check);

}


/*-------------------------------------------------------------------*/
/* Add long float                                                    */
/*                                                                   */
/* Input:                                                            */
/*      fl      Float                                                */
/*      add_fl  Float to be added                                    */
/*      normal  Normalize if true                                    */
/* Value:                                                            */
/*              exeption                                             */
/*-------------------------------------------------------------------*/
int add_lf (LONG_FLOAT *fl, LONG_FLOAT *add_fl, int normal)
{
int     pgm_check;
int    shift;

    pgm_check = 0;
    if (add_fl->long_fract
    || add_fl->expo) {                                  /* add_fl not 0 */
        if (fl->long_fract
        || fl->expo) {                                  /* fl not 0 */
            /* both not 0 */

            if (fl->expo == add_fl->expo) {
                /* expo equal */

                /* both guard digits */
                fl->long_fract <<= 4;
                add_fl->long_fract <<= 4;
            } else {
                /* expo not equal, denormalize */

                if (fl->expo < add_fl->expo) {
                    /* shift minus guard digit */
                    shift = add_fl->expo - fl->expo - 1;
                    fl->expo = add_fl->expo;

                    if (shift) {
                        if (shift >= 14
                        || ((fl->long_fract >>= (shift * 4)) == 0)) {
                            /* 0, copy summand */

                            fl->sign = add_fl->sign;
                            fl->long_fract = add_fl->long_fract;

                            if (fl->long_fract == 0) {
                                pgm_check = significance_lf(fl);
                            } else {
                                if (normal) {
                                    normal_lf(fl);
                                    pgm_check = underflow_lf(fl);
                                }
                            }
                            return(pgm_check);
                        }
                    }
                    /* guard digit */
                    add_fl->long_fract <<= 4;
                } else {
                    /* shift minus guard digit */
                    shift = fl->expo - add_fl->expo - 1;

                    if (shift) {
                        if (shift >= 14
                        || ((add_fl->long_fract >>= (shift * 4)) == 0)) {
                            /* 0, nothing to add */

                            if (fl->long_fract == 0) {
                                pgm_check = significance_lf(fl);
                            } else {
                                if (normal) {
                                    normal_lf(fl);
                                    pgm_check = underflow_lf(fl);
                                }
                            }
                            return(pgm_check);
                        }
                    }
                    /* guard digit */
                    fl->long_fract <<= 4;
                }
            }

            /* compute with guard digit */
            if (fl->sign == add_fl->sign) {
                fl->long_fract += add_fl->long_fract;
            } else {
                if (fl->long_fract == add_fl->long_fract) {
                    /* true 0 */

                    fl->long_fract = 0;
                    return( significance_lf(fl) );

                } else if (fl->long_fract > add_fl->long_fract) {
                    fl->long_fract -= add_fl->long_fract;
                } else {
                    fl->long_fract = add_fl->long_fract - fl->long_fract;
                    fl->sign = add_fl->sign;
                }
            }

            /* handle overflow with guard digit */
            if (fl->long_fract & 0xF000000000000000) {
                fl->long_fract >>= 8;
                (fl->expo)++;
                pgm_check = overflow_lf(fl);
            } else {

                if (normal) {
                    /* normalize with guard digit */
                    if (fl->long_fract) {
                        /* not 0 */

                        if (fl->long_fract & 0x0F00000000000000) {
                            /* not normalize, just guard digit */
                            fl->long_fract >>= 4;
                        } else {
                            (fl->expo)--;
                            normal_lf(fl);
                            pgm_check = underflow_lf(fl);
                        }
                    } else {
                        /* true 0 */

                        pgm_check = significance_lf(fl);
                    }
                } else {
                    /* not normalize, just guard digit */
                    fl->long_fract >>= 4;
                    if (fl->long_fract == 0) {
                        pgm_check = significance_lf(fl);
                    }
                }
            }
            return(pgm_check);
        } else {                                        /* fl 0, add_fl not 0 */
            /* copy summand */

            fl->expo = add_fl->expo;
            fl->sign = add_fl->sign;
            fl->long_fract = add_fl->long_fract;
            if (fl->long_fract == 0) {
                return( significance_lf(fl) );
            }
        }
    } else {                                            /* add_fl 0 */
        if (fl->long_fract == 0) {                      /* fl 0 */
            /* both 0 */

            return( significance_lf(fl) );
        }
    }
    if (normal) {
        normal_lf(fl);
        pgm_check = underflow_lf(fl);
    }
    return(pgm_check);

}

/*-------------------------------------------------------------------*/
/* Multiply short float                                              */
/*                                                                   */
/* Input:                                                            */
/*      fl      Multiplicand short float                             */
/*      mul_fl  Multiplicator short float                            */
/* Value:                                                            */
/*              exeption                                             */
/*-------------------------------------------------------------------*/

int mul_sf(SHORT_FLOAT *fl, SHORT_FLOAT *mul_fl)
{
t_int64     wk;

    if (fl->short_fract
    && mul_fl->short_fract) {
        /* normalize operands */
        normal_sf( fl );
        normal_sf( mul_fl );

        /* multiply fracts */
        wk = (t_int64) fl->short_fract * mul_fl->short_fract;

        /* normalize result and compute expo */
        if (wk & 0x0000F00000000000) {
            fl->short_fract = (int32)wk >> 24;
            fl->expo = (short)fl->expo + mul_fl->expo - 64;
        } else {
            fl->short_fract = (int32)wk >> 20;
            fl->expo = (short)fl->expo + mul_fl->expo - 65;
        }

        /* determine sign */
        fl->sign = (fl->sign == mul_fl->sign) ? 0 : 1;

        /* handle overflow and underflow */
        return( over_under_flow_sf(fl) );
    } else {
        /* set true 0 */

        fl->short_fract = 0;
        fl->expo = 0;
        fl->sign = 0;
        return(0);
    }

}


/*-------------------------------------------------------------------*/
/* Multiply long float                                               */
/*                                                                   */
/* Input:                                                            */
/*      fl      Multiplicand long float                              */
/*      mul_fl  Multiplicator long float                             */
/* Value:                                                            */
/*              exeption                                             */
/*-------------------------------------------------------------------*/
int mul_lf(LONG_FLOAT *fl, LONG_FLOAT *mul_fl)
{
t_int64   wk;
int32     v;

    if (fl->long_fract
    && mul_fl->long_fract) {
        /* normalize operands */
        normal_lf( fl );
        normal_lf( mul_fl );

        /* multiply fracts by sum of partial multiplications */
        wk = ((fl->long_fract & 0x00000000FFFFFFFF) * (mul_fl->long_fract & 0x00000000FFFFFFFF)) >> 32;

        wk += ((fl->long_fract & 0x00000000FFFFFFFF) * (mul_fl->long_fract >> 32));
        wk += ((fl->long_fract >> 32) * (mul_fl->long_fract & 0x00000000FFFFFFFF));
        v = (int32)wk;

        fl->long_fract = (wk >> 32) + ((fl->long_fract >> 32) * (mul_fl->long_fract >> 32));

        /* normalize result and compute expo */
        if (fl->long_fract & 0x0000F00000000000) {
            fl->long_fract = (fl->long_fract << 8)
                           | ((uint32)v >> 24);
            fl->expo = fl->expo + mul_fl->expo - 64;
        } else {
            fl->long_fract = (fl->long_fract << 12)
                           | ((uint32)v >> 20);
            fl->expo = fl->expo + mul_fl->expo - 65;
        }

        /* determine sign */
        fl->sign = (fl->sign == mul_fl->sign) ? 0 : 1;

        /* handle overflow and underflow */
        return( over_under_flow_lf(fl) );
    } else {
        /* set true 0 */

        fl->long_fract = 0;
        fl->expo = 0;
        fl->sign = 0;
        return(0);
    }

} 


/*-------------------------------------------------------------------*/
/* Divide short float                                                */
/*                                                                   */
/* Input:                                                            */
/*      fl      Dividend short float                                 */
/*      div_fl  Divisor short float                                  */
/* Value:                                                            */
/*              exeption                                             */
/*-------------------------------------------------------------------*/
int div_sf(SHORT_FLOAT *fl, SHORT_FLOAT *div_fl)
{
t_int64     wk;

    if (div_fl->short_fract) {
        if (fl->short_fract) {
            /* normalize operands */
            normal_sf( fl );
            normal_sf( div_fl );

            /* position fracts and compute expo */
            if (fl->short_fract < div_fl->short_fract) {
                wk = (t_int64) fl->short_fract << 24;
                fl->expo = fl->expo - div_fl->expo + 64;
            } else {
                wk = (t_int64) fl->short_fract << 20;
                fl->expo = fl->expo - div_fl->expo + 65;
            }
            /* divide fractions */
            fl->short_fract = (int32)wk / div_fl->short_fract;

            /* determine sign */
            fl->sign = (fl->sign == div_fl->sign) ? 0 : 1;

            /* handle overflow and underflow */
            return( over_under_flow_sf(fl) );
        } else {
            /* fraction of dividend 0, set true 0 */

            fl->short_fract = 0;
            fl->expo = 0;
            fl->sign = 0;
        }
    } else {
                                                        /* divisor 0 */

        return(3);
    }
    return(0);

}


/*-------------------------------------------------------------------*/
/* Divide long float                                                 */
/*                                                                   */
/* Input:                                                            */
/*      fl      Dividend long float                                  */
/*      div_fl  Divisor long float                                   */
/* Value:                                                            */
/*              exeption                                             */
/*-------------------------------------------------------------------*/
int div_lf(LONG_FLOAT *fl, LONG_FLOAT *div_fl)
{
t_int64 wk;
t_int64 wk2;
int     i;

    if (div_fl->long_fract) {
        if (fl->long_fract) {
            /* normalize operands */
            normal_lf( fl );
            normal_lf( div_fl );

            /* position fracts and compute expo */
            if (fl->long_fract < div_fl->long_fract) {
                fl->expo = fl->expo - div_fl->expo + 64;
            } else {
                fl->expo = fl->expo - div_fl->expo + 65;
                div_fl->long_fract <<= 4;
            }

            /* partial divide first hex digit */
            wk2 = fl->long_fract / div_fl->long_fract;
            wk = (fl->long_fract % div_fl->long_fract) << 4;

            /* partial divide middle hex digits */
            i = 13;
            while (i--) {
                wk2 = (wk2 << 4)
                    | (wk / div_fl->long_fract);
                wk = (wk % div_fl->long_fract) << 4;
            }

            /* partial divide last hex digit */
            fl->long_fract = (wk2 << 4)
                           | (wk / div_fl->long_fract);

            /* determine sign */
            fl->sign = (fl->sign == div_fl->sign) ? 0 : 1;

            /* handle overflow and underflow */
            return( over_under_flow_lf(fl) );
        } else {
            /* fraction of dividend 0, set true 0 */

            fl->long_fract = 0;
            fl->expo = 0;
            fl->sign = 0;
        }
    } else {
                                                        /* divisor 0 */

        return(3);
    }
    return(0);

}


/*-------------------------------------------------------------------*/
/* Scale long float                                                  */
/*                                                                   */
/* Input:                                                            */
/*      fpr     Pointer to FPAC                                      */
/*      AC      AC with exponent                                     */
/* Value:                                                            */
/*              exeption                                             */
/*-------------------------------------------------------------------*/
int scale_lf(t_int64 *fpr, int AC)
{
    int32 j, k, t;
    t_int64 FPAC, tempfp, holdfp;
    int pgm_check;

    pgm_check = 0;
    FPAC = *fpr;                                        /* load FPAC */

    j = (AC >> 8) & 0x7F;                               /* expo of AC */
    k = (int32)(FPAC >> 56) & 0x7F;                     /* expo of FPAC */
    tempfp = FPAC & FPP_SIGN;                           /* save sign */
    t = j - k;
    if (t > 0) {                                        /* Positive shift */
        FPAC &= FPP_MANT;
        FPAC = FPAC >> (t * 4);
        FPAC &= FPP_MANT;                               /* AC expo becomes expo */
        holdfp = j;
        FPAC |= (holdfp << 56);
        }
    if (t < 0) {                                        /* Negative shift */
        FPAC &= FPP_MANT;
        FPAC = FPAC << ((0-t) * 4);
        pgm_check = 4;                                  /* MOF bit on */
        FPAC &= FPP_MANT;                               /* AC expo becomes expo */
        holdfp = j;
        FPAC |= (holdfp << 56);
        }
    if ((FPAC & FPP_MANT) != 0)
        FPAC |= tempfp;                                 /* restore sign */

    if (0 == (FPAC & FPP_MANT))                         /* zero? */
        FPAC = 0;                                       /* true zero */

    *fpr = FPAC;                                        /* store FPAC */
    return (pgm_check);
}

