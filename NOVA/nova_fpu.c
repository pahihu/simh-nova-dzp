/* nova_fpu.c: NOVA FPU simulator
 */
 
#include "nova_defs.h"
#include "dg_fpmath.h"

#define IOP_S           iopS
#define IOP_C           iopC
#define IOP_P           iopP

#define OP_NIO          (ioNIO << 2)
#define OP_DIA          (ioDIA << 2)
#define OP_DOA          (ioDOA << 2)
#define OP_DOB          (ioDOB << 2)
#define OP_DOC          (ioDOC << 2)

#define D_FPU           0
#define D_FPU1          0
#define D_FPU2          0

/* memory reference instructions */
#define FPP_FLDS        (OP_DOB + IOP_P + D_FPU1)     /* load single */
#define FPP_FLDD        (OP_DOB + IOP_P + D_FPU2)     /* load double */
#define FPP_FSRS        (OP_DOB + IOP_S + D_FPU1)     /* store single */
#define FPP_FSRD        (OP_DOB + IOP_S + D_FPU2)     /* store double */

/* arithmetic instructions */
#define FPP_FAS         (OP_DOA +         D_FPU1)     /* add single */
#define FPP_FAD         (OP_DOA +         D_FPU2)     /* add double */
#define FPP_FSS         (OP_DOA + IOP_S + D_FPU1)     /* subtract single */
#define FPP_FSD         (OP_DOA + IOP_S + D_FPU2)     /* subtract double */
#define FPP_FMS         (OP_DOA + IOP_P + D_FPU1)     /* multiply single */
#define FPP_FMD         (OP_DOA + IOP_P + D_FPU2)     /* multiply double */
#define FPP_FDS         (OP_DOA + IOP_C + D_FPU1)     /* divide single */
#define FPP_FDD         (OP_DOA + IOP_C + D_FPU2)     /* divide double */

/* TEMP instructions */
#define FPP_FMFT        (OP_NIO + IOP_P + D_FPU2)     /* move FPAC to TEMP */
#define FPP_FMTF        (OP_NIO + IOP_C + D_FPU2)     /* move TEMP to FPAC */
#define FPP_FATS        (OP_DOC +         D_FPU1)     /* add TEMP single */
#define FPP_FATD        (OP_DOC +         D_FPU2)     /* add TEMP double */
#define FPP_FSTS        (OP_DOC + IOP_S + D_FPU1)     /* subtract TEMP single */
#define FPP_FSTD        (OP_DOC + IOP_S + D_FPU2)     /* subtract TEMP double */
#define FPP_FMTS        (OP_DOC + IOP_P + D_FPU1)     /* multiply TEMP single */
#define FPP_FMTD        (OP_DOC + IOP_P + D_FPU2)     /* multiply TEMP double */
#define FPP_FDTS        (OP_DOC + IOP_C + D_FPU1)     /* divide TEMP single */
#define FPP_FDTD        (OP_DOC + IOP_C + D_FPU2)     /* divide TEMP double */

/* shift and logical instructions */
#define FPP_FABS        (OP_NIO + IOP_P + D_FPU1)     /* absolute value */
#define FPP_FCLR        (OP_NIO + IOP_S + D_FPU1)     /* clear FPAC */
#define FPP_FLDX        (OP_DOB + IOP_C + D_FPU2)     /* load exponent */
#define FPP_FNEG        (OP_NIO + IOP_C + D_FPU1)     /* negate */
#define FPP_FNRM        (OP_NIO + IOP_S + D_FPU2)     /* normalize */
#define FPP_FSCL        (OP_DOB +         D_FPU2)     /* scale */
#define FPP_FHWD        (OP_DIA +         D_FPU1)     /* read high word */

/* status instructions */
#define FPP_FRST        (OP_DIA + IOP_C + D_FPU)      /* read STATUS */
#define FPP_FWST        (OP_DOA +         D_FPU)      /* write STATUS */

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


int32 fpp1(int32 pulse, int32 code, int32 AC)
{
    int32 rval, ir;

    rval = 0;
    ir = (code << 2) + pulse;
    switch (ir) {
        case FPP_FCLR: /* clear FPAC */
            break;
        case FPP_FNEG: /* negate */
            break;
        case FPP_FABS: /* absolute value */
            break;
        case FPP_FHWD: /* read high word */
            break;
        case FPP_FAS: /* add single */
            break;
        case FPP_FSS: /* subtract single */
            break;
        case FPP_FDS: /* divide single */
            break;
        case FPP_FMS: /* multiply single */
            break;
        case FPP_FSRS: /* store single */
            break;
        case FPP_FLDS: /* load single */
            break;
        case FPP_FATS: /* add TEMP single */
            break;
        case FPP_FSTS: /* subtract TEMP single */
            break;
        case FPP_FDTS: /* divide TEMP single */
            break;
        case FPP_FMTS: /* multiply TEMP single */
            break;
        }
    return rval;
}


int32 fpp2(int32 pulse, int32 code, int32 AC)
{
    int32 rval, ir;

    rval = 0;
    ir = (code << 2) + pulse;
    switch (ir) {
        case FPP_FNRM: /* normalize */
            break;
        case FPP_FMTF: /* move TEMP to FPAC */
            break;
        case FPP_FMFT: /* move FPAC to TEMP */
            break;
        case FPP_FAD: /* add double */
            break;
        case FPP_FSD: /* subtract double */
            break;
        case FPP_FDD: /* divide double */
            break;
        case FPP_FMD: /* multiply double */
            break;
        case FPP_FSCL: /* scale */
            break;
        case FPP_FSRD: /* store double */
            break;
        case FPP_FLDX: /* load exponent */
            break;
        case FPP_FLDD: /* load double */
            break;
        case FPP_FATD: /* add TEMP double */
            break;
        case FPP_FSTD: /* subtract TEMP double */
            break;
        case FPP_FDTD: /* divide TEMP double */
            break;
        case FPP_FMTD: /* multiply TEMP double */
            break;
        }
    return rval;
}


int32 fpp(int32 pulse, int32 code, int32 AC)
{
    int32 rval, ir;

    rval = 0;
    ir = (code << 2) + pulse;
    switch (ir) {
        case  FPP_FRST: /* read STATUS */
            break;
        case  FPP_FWST: /* write STATUS */
            break;
        }

    return rval;
}

