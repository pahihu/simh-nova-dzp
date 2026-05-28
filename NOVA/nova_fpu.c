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
#define STA_EFLGS   (STA_OVF + STA_UNF + STA_DVZ + STA_MOF)

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
static t_int64 tempfp;

t_stat fpp_reset(DEVICE *dptr);
int32 fpp(int32 pulse, int32 code, int32 AC);

extern int32 GetMap(int32 addr);
extern int32 PutMap(int32 addr, int32 data);


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

static void GetMapS(int32 addr, t_int64 *data)
{
    *data  = ((t_uint64)GetMap(addr + 0) << 48)
           | ((t_uint64)GetMap(addr + 1) << 32);
}

static void GetMapD(int32 addr, t_int64 *data)
{
    *data  = ((t_uint64)GetMap(addr + 0) << 48)
           | ((t_uint64)GetMap(addr + 1) << 32)
           | ((t_uint64)GetMap(addr + 2) << 16)
           | ((t_uint64)GetMap(addr + 3));
}

static void PutMapS(int32 addr, t_int64 *data)
{
    PutMap(addr+0, (int32)(*data >> 48) & 0177777);
    PutMap(addr+1, (int32)(*data >> 32) & 0177777);
}

static void PutMapD(int32 addr, t_int64 *data)
{
    PutMap(addr+0, (int32)(*data >> 48) & 0177777);
    PutMap(addr+1, (int32)(*data >> 32) & 0177777);
    PutMap(addr+2, (int32)(*data >> 16) & 0177777);
    PutMap(addr+3, (int32)(*data >>  0) & 0177777);
}

static void SetFPSR(int k)
{
    switch (k) {
        case DGF_OVF:
            FPSR |= STA_OVF;
            break;
        case DGF_UNF:
            FPSR |= STA_UNF;
            break;
        case DGF_DVZ:
            FPSR |= STA_DVZ;
            break;
        case DGF_MOF:
            FPSR |= STA_MOF;
        }
}

int32 fpp1(int32 pulse, int32 code, int32 AC)
{
    int32 rval, ir;
    t_int64 tempfp;
    SHORT_FLOAT sf1, sf2;
    int k;

    rval = 0;
    ir = (code << 2) + pulse;
    switch (ir) {
        case FPP_FCLR: /* clear FPAC */
            FPAC = 0;
            break;
        case FPP_FNEG: /* negate */
            if (FPAC)
                FPAC ^= FPP_SIGN;
            break;
        case FPP_FABS: /* absolute value */
            FPAC &= ~FPP_SIGN;
            break;
        case FPP_FHWD: /* read high word */
            rval = (int32)(FPAC >> 48) & 0177777;
            break;
        case FPP_FAS: /* add single */
        case FPP_FSS: /* subtract single */
            GetMapS(AC, &tempfp);
            if (FPP_FSS == ir)
                tempfp ^= FPP_SIGN;
            get_sf(&sf1, &FPAC);
            get_sf(&sf2, &tempfp);
            k = add_sf(&sf1, &sf2, 1);
            SetFPSR(k);
            store_sf(&sf1, &FPAC);
            break;
        case FPP_FDS: /* divide single */
            GetMapS(AC, &tempfp);
            if (0 == (tempfp & FPP_MANT)) {
                FPSR |= STA_DVZ;
                }
            else {
                get_sf(&sf1, &FPAC);
                get_sf(&sf2, &tempfp);
                k = div_sf(&sf1, &sf2);
                SetFPSR(k);
                store_sf(&sf1, &FPAC);
                }
            break;
        case FPP_FMS: /* multiply single */
            GetMapS(AC, &tempfp);
            get_sf(&sf1, &FPAC);
            get_sf(&sf2, &tempfp);
            k = mul_sf(&sf1, &sf2);
            SetFPSR(k);
            store_sf(&sf1, &FPAC);
            break;
        case FPP_FSRS: /* store single */
            PutMapS(AC, &FPAC);
            break;
        case FPP_FLDS: /* load single */
            GetMapS(AC, &FPAC);
            break;
        case FPP_FATS: /* add TEMP single */
        case FPP_FSTS: /* subtract TEMP single */
            tempfp = TEMP;
            if (FPP_FSTS == ir)
                tempfp ^= FPP_SIGN;
            get_sf(&sf1, &FPAC);
            get_sf(&sf2, &tempfp);
            k = add_sf(&sf1, &sf2, 1);
            SetFPSR(k);
            store_sf(&sf1, &FPAC);
            break;
        case FPP_FDTS: /* divide TEMP single */
            tempfp = TEMP;
            if (0 == (tempfp & FPP_MANT)) {
                FPSR |= STA_DVZ;
                }
            else {
                get_sf(&sf1, &FPAC);
                get_sf(&sf2, &tempfp);
                k = div_sf(&sf1, &sf2);
                SetFPSR(k);
                store_sf(&sf1, &FPAC);
                }
            break;
        case FPP_FMTS: /* multiply TEMP single */
            tempfp = TEMP;
            get_sf(&sf1, &FPAC);
            get_sf(&sf2, &tempfp);
            k = mul_sf(&sf1, &sf2);
            SetFPSR(k);
            store_sf(&sf1, &FPAC);
            break;
        }
    return rval;
}


int32 fpp2(int32 pulse, int32 code, int32 AC)
{
    int32 rval, ir;
    t_int64 tempfp;
    LONG_FLOAT df1, df2;
    int k;

    rval = 0;
    ir = (code << 2) + pulse;
    switch (ir) {
        case FPP_FNRM: /* normalize */
            get_lf(&df1, &FPAC);
            k = normal_lf(&df1);
            SetFPSR(k);
            store_lf(&df1, &FPAC);
            break;
        case FPP_FMTF: /* move TEMP to FPAC */
            FPAC = TEMP;
            break;
        case FPP_FMFT: /* move FPAC to TEMP */
            TEMP = FPAC;
            break;
        case FPP_FAD: /* add double */
        case FPP_FSD: /* subtract double */
            GetMapD(AC, &tempfp);
            if (FPP_FSD == ir)
                tempfp ^= FPP_SIGN;
            get_lf(&df1, &FPAC);
            get_lf(&df2, &tempfp);
            k = add_lf(&df1, &df2, 1);
            SetFPSR(k);
            store_lf(&df1, &FPAC);
            break;
        case FPP_FDD: /* divide double */
            GetMapD(AC, &tempfp);
            if (0 == (tempfp & FPP_MANT)) {
                FPSR |= STA_DVZ;
                }
            else {
                get_lf(&df1, &FPAC);
                get_lf(&df2, &tempfp);
                k = div_lf(&df1, &df2);
                SetFPSR(k);
                store_lf(&df1, &FPAC);
                }
            break;
        case FPP_FMD: /* multiply double */
            GetMapD(AC, &tempfp);
            get_lf(&df1, &FPAC);
            get_lf(&df2, &tempfp);
            k = mul_lf(&df1, &df2);
            SetFPSR(k);
            store_lf(&df1, &FPAC);
            break;
        case FPP_FSCL: /* scale */
            k = scale_lf(&FPAC, AC);
            SetFPSR(k);
            break;
        case FPP_FSRD: /* store double */
            PutMapD(AC, &FPAC);
            break;
        case FPP_FLDX: /* load exponent */
            FPAC &= ~FPP_EXPO;
            FPAC |= (t_int64)(AC & 077400) << 56;
            break;
        case FPP_FLDD: /* load double */
            GetMapD(AC, &FPAC);
            break;
        case FPP_FATD: /* add TEMP double */
        case FPP_FSTD: /* subtract TEMP double */
            tempfp = TEMP;
            if (FPP_FSTD == ir)
                tempfp ^= FPP_SIGN;
            get_lf(&df1, &FPAC);
            get_lf(&df2, &tempfp);
            k = add_lf(&df1, &df2, 1);
            SetFPSR(k);
            store_lf(&df1, &FPAC);
            break;
        case FPP_FDTD: /* divide TEMP double */
            tempfp = TEMP;
            if (0 == (tempfp & FPP_MANT)) {
                FPSR |= STA_DVZ;
                }
            else {
                get_lf(&df1, &FPAC);
                get_lf(&df2, &tempfp);
                k = div_lf(&df1, &df2);
                SetFPSR(k);
                store_lf(&df1, &FPAC);
                }
            break;
        case FPP_FMTD: /* multiply TEMP double */
            tempfp = TEMP;
            get_lf(&df1, &FPAC);
            get_lf(&df2, &tempfp);
            k = mul_lf(&df1, &df2);
            SetFPSR(k);
            store_lf(&df1, &FPAC);
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
        case FPP_FRST: /* read STATUS */
            rval = FPSR;
            if (rval & STA_EFLGS)
                rval |= STA_ANY;
            if ((0 == (FPAC & FPP_SIGN)) && (FPAC & FPP_MANT))
                rval |= STA_GTZ;
            if ((0 == (FPAC & FPP_SIGN)) && (0 == (FPAC & FPP_MANT)))
                rval |= STA_EQZ;
            if (FPAC & FPP_SIGN)
                rval |= STA_LTZ;
            FPSR &= ~(STA_ANY + STA_EFLGS);
            break;
        case FPP_FWST: /* write STATUS */
            FPSR = AC;
            break;
        }

    return rval;
}

