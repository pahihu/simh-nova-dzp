/* nova_fpu.c: NOVA FPU simulator
 */
 
#include "nova_defs.h"
#include "dg_fpmath.h"

#define IOP_S           0100
#define IOP_C           0200
#define IOP_P           0300

#define OP_NIO          060000
#define OP_DIA          060400
#define OP_DOA          061000
#define OP_DOB          062000
#define OP_DOC          063000

/* memory reference instructions */
#define FPP_FLDS        (OP_DOB + IOP_P + DEV_FPU1)     /* load single */
#define FPP_FLDD        (OP_DOB + IOP_P + DEV_FPU2)     /* load double */
#define FPP_FSRS        (OP_DOB + IOP_S + DEV_FPU1)     /* store single */
#define FPP_FSRD        (OP_DOB + IOP_S + DEV_FPU2)     /* store double */

/* arithmetic instructions */
#define FPP_FAS         (OP_DOA +         DEV_FPU1)     /* add single */
#define FPP_FAD         (OP_DOA +         DEV_FPU2)     /* add double */
#define FPP_FSS         (OP_DOA + IOP_S + DEV_FPU1)     /* subtract single */
#define FPP_FSD         (OP_DOA + IOP_S + DEV_FPU2)     /* subtract double */
#define FPP_FMS         (OP_DOA + IOP_P + DEV_FPU1)     /* multiply single */
#define FPP_FMD         (OP_DOA + IOP_P + DEV_FPU2)     /* multiply double */
#define FPP_FDS         (OP_DOA + IOP_C + DEV_FPU1)     /* divide single */
#define FPP_FDD         (OP_DOA + IOP_C + DEV_FPU2)     /* divide double */

/* TEMP instructions */
#define FPP_FMFT        (OP_NIO + IOP_P + DEV_FPU2)     /* move FPAC to TEMP */
#define FPP_FMTF        (OP_NIO + IOP_C + DEV_FPU2)     /* move TEMP to FPAC */
#define FPP_FATS        (OP_DOC +         DEV_FPU1)     /* add TEMP single */
#define FPP_FATD        (OP_DOC +         DEV_FPU2)     /* add TEMP double */
#define FPP_FSTS        (OP_DOC + IOP_S + DEV_FPU1)     /* subtract TEMP single */
#define FPP_FSTD        (OP_DOC + IOP_S + DEV_FPU2)     /* subtract TEMP double */
#define FPP_FMTS        (OP_DOC + IOP_P + DEV_FPU1)     /* multiply TEMP single */
#define FPP_FMTD        (OP_DOC + IOP_P + DEV_FPU2)     /* multiply TEMP double */
#define FPP_FDTS        (OP_DOC + IOP_C + DEV_FPU1)     /* divide TEMP single */
#define FPP_FDTD        (OP_DOC + IOP_C + DEV_FPU2)     /* divide TEMP double */

/* shift and logical instructions */
#define FPP_FABS         (OP_NIO + IOP_P + DEV_FPU1)     /* absolute value */
#define FPP_FCLR         (OP_NIO + IOP_S + DEV_FPU1)     /* clear FPAC */
#define FPP_FLDX         (OP_DOB + IOP_C + DEV_FPU2)     /* load exponent */
#define FPP_FNEG         (OP_NIO + IOP_C + DEV_FPU1)     /* negate */
#define FPP_FNRM         (OP_NIO + IOP_S + DEV_FPU2)     /* normalize */
#define FPP_FSCL         (OP_DOB +         DEV_FPU2)     /* scale */
#define FPP_FHWD         (OP_DIA +         DEV_FPU1)     /* read high word */

/* status instructions */
#define FPP_FRST         (OP_DIA + IOP_C + DEV_FPU)      /* read STATUS */
#define FPP_FWST         (OP_DOA +         DEV_FPU)      /* write STATUS */

#define STA_ANY     0100000
#define STA_OVF     0040000
#define STA_UNF     0020000
#define STA_DVZ     0010000
#define STA_MOF     0004000
#define STA_GTZ     0002000
#define STA_EQZ     0001000
#define STA_LTZ     0000400
#define STA_RSVD    0000370
#define STA_IND     0000004
#define STA_PPM     0000002
#define STA_DMD     0000001

static char* sta_bits[] = {
    "DMD", "PPM", "IND",
      "U",   "U",   "U",
      "U",   "U", "LTZ",
    "EQZ", "GTZ", "MOF",
    "DVZ", "UNF", "OVF",
    "ANY"
};

#define UNIT_V_UP       (UNIT_V_UF)                     /* FPU Enabled */
#define UNIT_UP         (1 << UNIT_V_UP)

static t_int64 FPAC, TEMP;
static int16 FPSR;
static int32 fpp_trace;

t_stat fpp_reset(DEVICE *dptr);
int32 fpp(int32 pulse, int32 code, int32 AC);


DIB fpp_dib = { DEV_FPU, 0, 0, &fpp };

UNIT fpp_unit = { UDATA (NULL, UNIT_UP, MAXMEMSIZE) };

REG fpp_reg[] = {
    { ORDATA (STATUS, FPSR, 16) },
    { ORDATA (FPAC, FPAC, 64) },
    { ORDATA (TEMP, TEMP, 64) },
    { DRDATA (TRACE, fpp_trace,   32) },
    { NULL }
};

MTAB fpp_mod[] = {
    { UNIT_UP, UNIT_UP, "Enabled (UP)", "UP", NULL },
    { UNIT_UP,       0, "Disabled (DOWN)", "DOWN", NULL },
    { 0 }
};


DEVICE fpp_dev = {
    "FPU", &fpp_unit, fpp_reg, fpp_mod,
    1, 16, 17, 1, 16, 16,
    NULL, NULL, &fpp_reset,
    NULL, NULL, NULL,
    &fpp_dib, DEV_DIS | DEV_DISABLE
};


t_stat fpp_reset(DEVICE *dptr)
{
    int i;

    FPSR = 0;                   /* clear STATUS */
    return SCPE_OK;
}


int32 fpp(int32 pulse, int32 code, int32 AC)
{
    int32 rval;

    rval = 0;
    return rval;
}

