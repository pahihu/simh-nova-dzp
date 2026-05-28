/* nova_fpu.c: NOVA FPU simulator
 */
 
#include "nova_defs.h"
#include "dg_fpmath.h"

#define IOP_S           iopS
#define IOP_C           iopC
#define IOP_P           iopP

#define OP_NIO          (ioNIO << 2)
#define OP_DIA          (ioDIA << 2)
#define OP_DIB          (ioDIB << 2)
#define OP_DOA          (ioDOA << 2)
#define OP_DOB          (ioDOB << 2)
#define OP_DOC          (ioDOC << 2)

#define D_FPU           0
#define D_FPU1          0
#define D_FPU2          0

/* memory reference instructions */
#define FPP_FLDS        (OP_DOB + IOP_P + D_FPU1)       /* load single */
#define FPP_FLDD        (OP_DOB + IOP_P + D_FPU2)       /* load double */
#define FPP_FSRS        (OP_DOB + IOP_S + D_FPU1)       /* store single */
#define FPP_FSRD        (OP_DOB + IOP_S + D_FPU2)       /* store double */

/* arithmetic instructions */
#define FPP_FAS         (OP_DOA +         D_FPU1)       /* add single */
#define FPP_FAD         (OP_DOA +         D_FPU2)       /* add double */
#define FPP_FSS         (OP_DOA + IOP_S + D_FPU1)       /* subtract single */
#define FPP_FSD         (OP_DOA + IOP_S + D_FPU2)       /* subtract double */
#define FPP_FMS         (OP_DOA + IOP_P + D_FPU1)       /* multiply single */
#define FPP_FMD         (OP_DOA + IOP_P + D_FPU2)       /* multiply double */
#define FPP_FDS         (OP_DOA + IOP_C + D_FPU1)       /* divide single */
#define FPP_FDD         (OP_DOA + IOP_C + D_FPU2)       /* divide double */

/* TEMP instructions */
#define FPP_FMFT        (OP_NIO + IOP_P + D_FPU2)       /* move FPAC to TEMP */
#define FPP_FMTF        (OP_NIO + IOP_C + D_FPU2)       /* move TEMP to FPAC */
#define FPP_FATS        (OP_DOC +         D_FPU1)       /* add TEMP single */
#define FPP_FATD        (OP_DOC +         D_FPU2)       /* add TEMP double */
#define FPP_FSTS        (OP_DOC + IOP_S + D_FPU1)       /* subtract TEMP single */
#define FPP_FSTD        (OP_DOC + IOP_S + D_FPU2)       /* subtract TEMP double */
#define FPP_FMTS        (OP_DOC + IOP_P + D_FPU1)       /* multiply TEMP single */
#define FPP_FMTD        (OP_DOC + IOP_P + D_FPU2)       /* multiply TEMP double */
#define FPP_FDTS        (OP_DOC + IOP_C + D_FPU1)       /* divide TEMP single */
#define FPP_FDTD        (OP_DOC + IOP_C + D_FPU2)       /* divide TEMP double */

/* shift and logical instructions */
#define FPP_FABS        (OP_NIO + IOP_P + D_FPU1)       /* absolute value */
#define FPP_FCLR        (OP_NIO + IOP_S + D_FPU1)       /* clear FPAC */
#define FPP_FLDX        (OP_DOB + IOP_C + D_FPU2)       /* load exponent */
#define FPP_FNEG        (OP_NIO + IOP_C + D_FPU1)       /* negate */
#define FPP_FNRM        (OP_NIO + IOP_S + D_FPU2)       /* normalize */
#define FPP_FSCL        (OP_DOB +         D_FPU2)       /* scale */
#define FPP_FHWD        (OP_DIA +         D_FPU1)       /* read high word */

/* status instructions */
#define FPP_FRST        (OP_DIA + IOP_C + D_FPU)        /* read STATUS */
#define FPP_FWST        (OP_DOA +         D_FPU)        /* write STATUS */

/* diagnostic instructions */
#define FPP_RDW1        (OP_DIA +         D_FPU1)       /* read word 1 */
#define FPP_RDW2        (OP_DIB +         D_FPU1)       /* read word 2 */
#define FPP_RDW3        (OP_DIA +         D_FPU2)       /* read word 3 */
#define FPP_RDW4        (OP_DIB +         D_FPU2)       /* read word 4 */
#define FPP_FCLK        (OP_NIO + IOP_P + D_FPU)        /* FPU clock */


#define STA_ANY     0100000
#define STA_OVF     0040000
#define STA_UNF     0020000
#define STA_DVZ     0010000
#define STA_MOF     0004000
#define STA_EFLGS   (STA_OVF + STA_UNF + STA_DVZ + STA_MOF) /* error flags */
#define STA_IFLGS   (STA_OVF + STA_UNF + STA_DVZ)       /* INT gen. error flags */

#define STA_GTZ     0002000
#define STA_EQZ     0001000
#define STA_LTZ     0000400
#define STA_RSVD    0000370
#define STA_IND     0000004
#define STA_PPM     0000002
#define STA_DMD     0000001

#define STA_MODE    0000003
#define MODE_NORM   (0 == (FPSR & STA_MODE))
#define MODE_PAR    (2 == (FPSR & STA_MODE))
#define MODE_DIAG   (FPSR & STA_DMD)
#define INTEN       (0 == (FPSR & STA_IND))


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

extern int32 int_req, dev_busy, dev_done, dev_disable;

static t_int64 FPAC, TEMP;
static int16 FPSR;
static int32 fpp_trace;
static t_int64 tempfp;

t_stat fpp_reset(DEVICE *dptr);
int32 fpp(int32 pulse, int32 code, int32 AC);

extern int32 GetMap(int32 addr);
extern int32 PutMap(int32 addr, int32 data);


DIB fpp_dib = { DEV_FPU, INT_FPU, PI_FPU, &fpp };

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
            DEV_CLR_BUSY(INT_FPU);
            if (INTEN) {
                DEV_SET_DONE(INT_FPU);
                DEV_UPDATE_INTR;
                }
            else {
                FPSR |= STA_ANY;
                }
            break;
        case DGF_UNF:
            FPSR |= STA_UNF;
            DEV_CLR_BUSY(INT_FPU);
            if (INTEN) {
                DEV_SET_DONE(INT_FPU);
                DEV_UPDATE_INTR;
                }
            else {
                FPSR |= STA_ANY;
                }
            break;
        case DGF_DVZ:
            FPSR |= STA_DVZ;
            DEV_CLR_BUSY(INT_FPU);
            if (INTEN) {
                DEV_SET_DONE(INT_FPU);
                DEV_UPDATE_INTR;
                }
            else {
                FPSR |= STA_ANY;
                }
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
    int32 fwait;

    DEV_SET_BUSY(INT_FPU);                              /* set busy */
    if (INTEN) {                                        /* INT enabled? */
        DEV_CLR_DONE(INT_FPU);                          /* clear done */
        DEV_UPDATE_INTR;
        }

    rval = 0;
    ir = (code << 2) + pulse;
    switch (ir) {
        case FPP_FCLR: /* clear FPAC */
            fwait = 4;
            FPAC = 0;
            break;
        case FPP_FNEG: /* negate */
            fwait = 4;
            if (FPAC)
                FPAC ^= FPP_SIGN;
            break;
        case FPP_FABS: /* absolute value */
            fwait = 4;
            FPAC &= ~FPP_SIGN;
            break;
        case FPP_FHWD: /* read high word, read word 1 */
            fwait = 2;
            rval = (int32)(FPAC >> 48) & 0177777;
            break;
        case FPP_RDW2: /* read word 2 */
            fwait = 2;
            rval = (int32)(FPAC >> 32) & 0177777;
            break;
        case FPP_FAS: /* add single */
        case FPP_FSS: /* subtract single */
            GetMapS(AC, &tempfp);
            if (FPP_FSS == ir) {
                fwait = 9;
                tempfp ^= FPP_SIGN;
                }
            else {
                fwait = 8;
                }
            get_sf(&sf1, &FPAC);
            get_sf(&sf2, &tempfp);
            k = add_sf(&sf1, &sf2, 1);
            SetFPSR(k);
            store_sf(&sf1, &FPAC);
            break;
        case FPP_FDS: /* divide single */
            GetMapS(AC, &tempfp);
            if (0 == (tempfp & FPP_MANT)) {
                fwait = 7;
                FPSR |= STA_DVZ;
                }
            else {
                fwait = 14;
                get_sf(&sf1, &FPAC);
                get_sf(&sf2, &tempfp);
                k = div_sf(&sf1, &sf2);
                SetFPSR(k);
                store_sf(&sf1, &FPAC);
                }
            break;
        case FPP_FMS: /* multiply single */
            fwait = 12;
            GetMapS(AC, &tempfp);
            get_sf(&sf1, &FPAC);
            get_sf(&sf2, &tempfp);
            k = mul_sf(&sf1, &sf2);
            SetFPSR(k);
            store_sf(&sf1, &FPAC);
            break;
        case FPP_FSRS: /* store single */
            fwait = 5;
            PutMapS(AC, &FPAC);
            break;
        case FPP_FLDS: /* load single */
            fwait = 6;
            GetMapS(AC, &FPAC);
            break;
        case FPP_FATS: /* add TEMP single */
        case FPP_FSTS: /* subtract TEMP single */
            fwait = 6;
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
                fwait = 5;
                FPSR |= STA_DVZ;
                }
            else {
                fwait = 12;
                get_sf(&sf1, &FPAC);
                get_sf(&sf2, &tempfp);
                k = div_sf(&sf1, &sf2);
                SetFPSR(k);
                store_sf(&sf1, &FPAC);
                }
            break;
        case FPP_FMTS: /* multiply TEMP single */
            fwait = 9;
            tempfp = TEMP;
            get_sf(&sf1, &FPAC);
            get_sf(&sf2, &tempfp);
            k = mul_sf(&sf1, &sf2);
            SetFPSR(k);
            store_sf(&sf1, &FPAC);
            break;
        }

    if (0 == (FPSR & STA_IFLGS)) {                      /* no errors? */
        DEV_CLR_BUSY(INT_FPU);                          /* clear busy */
        if (INTEN) {                                    /* INT enabled? */
            DEV_SET_DONE(INT_FPU);                      /* signal INT */
            DEV_UPDATE_INTR;
            }
        }
    return rval;
}


int32 fpp2(int32 pulse, int32 code, int32 AC)
{
    int32 rval, ir;
    t_int64 tempfp;
    LONG_FLOAT df1, df2;
    int k;
    int32 fwait;

    DEV_SET_BUSY(INT_FPU);                              /* set busy */
    if (INTEN) {                                        /* INT enabled? */
        DEV_CLR_DONE(INT_FPU);                          /* clear done */
        DEV_UPDATE_INTR;
        }

    rval = 0;
    ir = (code << 2) + pulse;
    switch (ir) {
        case FPP_RDW3: /* read word 3 */
            fwait = 2;
            rval = (int32)(FPAC >> 16) & 0177777;
            break;
        case FPP_RDW4: /* read word 4 */
            fwait = 2;
            rval = (int32)(FPAC & 0177777);
            break;
        case FPP_FNRM: /* normalize */
            fwait = 4;
            get_lf(&df1, &FPAC);
            k = normal_lf(&df1);
            SetFPSR(k);
            store_lf(&df1, &FPAC);
            break;
        case FPP_FMTF: /* move TEMP to FPAC */
            fwait = 4;
            FPAC = TEMP;
            break;
        case FPP_FMFT: /* move FPAC to TEMP */
            fwait = 4;
            TEMP = FPAC;
            break;
        case FPP_FAD: /* add double */
        case FPP_FSD: /* subtract double */
            fwait = 10;
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
                fwait = 9;
                FPSR |= STA_DVZ;
                }
            else {
                fwait = 22;
                get_lf(&df1, &FPAC);
                get_lf(&df2, &tempfp);
                k = div_lf(&df1, &df2);
                SetFPSR(k);
                store_lf(&df1, &FPAC);
                }
            break;
        case FPP_FMD: /* multiply double */
            fwait = 20;
            GetMapD(AC, &tempfp);
            get_lf(&df1, &FPAC);
            get_lf(&df2, &tempfp);
            k = mul_lf(&df1, &df2);
            SetFPSR(k);
            store_lf(&df1, &FPAC);
            break;
        case FPP_FSCL: /* scale */
            fwait = 3;
            k = scale_lf(&FPAC, AC);
            SetFPSR(k);
            break;
        case FPP_FSRD: /* store double */
            fwait = 7;
            PutMapD(AC, &FPAC);
            break;
        case FPP_FLDX: /* load exponent */
            fwait = 4;
            FPAC &= ~FPP_EXPO;
            FPAC |= (t_int64)(AC & 077400) << 56;
            break;
        case FPP_FLDD: /* load double */
            fwait = 8;
            GetMapD(AC, &FPAC);
            break;
        case FPP_FATD: /* add TEMP double */
        case FPP_FSTD: /* subtract TEMP double */
            fwait = 6;
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
                fwait = 5;
                FPSR |= STA_DVZ;
                }
            else {
                fwait = 18;
                get_lf(&df1, &FPAC);
                get_lf(&df2, &tempfp);
                k = div_lf(&df1, &df2);
                SetFPSR(k);
                store_lf(&df1, &FPAC);
                }
            break;
        case FPP_FMTD: /* multiply TEMP double */
            fwait = 16;
            tempfp = TEMP;
            get_lf(&df1, &FPAC);
            get_lf(&df2, &tempfp);
            k = mul_lf(&df1, &df2);
            SetFPSR(k);
            store_lf(&df1, &FPAC);
            break;
        }

    if (0 == (FPSR & STA_IFLGS)) {                      /* no errors? */
        DEV_CLR_BUSY(INT_FPU);                          /* clear busy */
        if (INTEN) {                                    /* INT enabled? */
            DEV_SET_DONE(INT_FPU);                      /* signal INT */
            DEV_UPDATE_INTR;
            }
        }
    return rval;
}


int32 fpp(int32 pulse, int32 code, int32 AC)
{
    int32 rval, ir;
    int32 fwait = 0;

    DEV_SET_BUSY(INT_FPU);                              /* set busy */
    if (INTEN) {                                        /* INT enabled? */
        DEV_CLR_DONE(INT_FPU);                          /* clear done */
        DEV_UPDATE_INTR;
        }

    rval = 0;
    ir = (code << 2) + pulse;
    switch (ir) {
        case FPP_FCLK: /* FPU clock */
            break;
        case FPP_FRST: /* read STATUS */
            fwait = 3;
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
            fwait = 3;
            FPSR = AC;
            break;
        }

    if (0 == (FPSR & STA_IFLGS)) {                      /* no errors? */
        DEV_CLR_BUSY(INT_FPU);                          /* clear busy */
        if (INTEN) {                                    /* INT enabled? */
            DEV_SET_DONE(INT_FPU);                      /* signal INT */
            DEV_UPDATE_INTR;
            }
        }
    return rval;
}

