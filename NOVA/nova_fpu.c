/* nova_fpu.c: NOVA FPU simulator
 */
 
#include "nova_defs.h"
#include "dg_fpmath.h"

/* cccppdd */
#define FUN_M_DEV       03
#define FUN_V_PULSE     2
#define FUN_M_PULSE     03
#define FUN_V_CODE      4
#define FUN_M_CODE      07

#define MKFN(pulse,code,dev)    (((pulse) << FUN_V_PULSE) + ((code) << FUN_V_CODE) + (dev))


#define IOP_S           (iopS << FUN_V_PULSE)
#define IOP_C           (iopC << FUN_V_PULSE)
#define IOP_P           (iopP << FUN_V_PULSE)

#define IO_NIO          (ioNIO << FUN_V_CODE)
#define IO_DIA          (ioDIA << FUN_V_CODE)
#define IO_DIB          (ioDIB << FUN_V_CODE)
#define IO_DOA          (ioDOA << FUN_V_CODE)
#define IO_DOB          (ioDOB << FUN_V_CODE)
#define IO_DOC          (ioDOC << FUN_V_CODE)

#define D_FPU           0
#define D_FPU1          1
#define D_FPU2          2


/* memory reference instructions */
#define FPP_FLDS        (IO_DOB + IOP_P + D_FPU1)       /* load single */
#define FPP_FLDD        (IO_DOB + IOP_P + D_FPU2)       /* load double */
#define FPP_FSRS        (IO_DOB + IOP_S + D_FPU1)       /* store single */
#define FPP_FSRD        (IO_DOB + IOP_S + D_FPU2)       /* store double */

/* arithmetic instructions */
#define FPP_FAS         (IO_DOA +         D_FPU1)       /* add single */
#define FPP_FAD         (IO_DOA +         D_FPU2)       /* add double */
#define FPP_FSS         (IO_DOA + IOP_S + D_FPU1)       /* subtract single */
#define FPP_FSD         (IO_DOA + IOP_S + D_FPU2)       /* subtract double */
#define FPP_FMS         (IO_DOA + IOP_P + D_FPU1)       /* multiply single */
#define FPP_FMD         (IO_DOA + IOP_P + D_FPU2)       /* multiply double */
#define FPP_FDS         (IO_DOA + IOP_C + D_FPU1)       /* divide single */
#define FPP_FDD         (IO_DOA + IOP_C + D_FPU2)       /* divide double */

/* TEMP instructions */
#define FPP_FMFT        (IO_NIO + IOP_P + D_FPU2)       /* move FPAC to TEMP */
#define FPP_FMTF        (IO_NIO + IOP_C + D_FPU2)       /* move TEMP to FPAC */
#define FPP_FATS        (IO_DOC +         D_FPU1)       /* add TEMP single */
#define FPP_FATD        (IO_DOC +         D_FPU2)       /* add TEMP double */
#define FPP_FSTS        (IO_DOC + IOP_S + D_FPU1)       /* subtract TEMP single */
#define FPP_FSTD        (IO_DOC + IOP_S + D_FPU2)       /* subtract TEMP double */
#define FPP_FMTS        (IO_DOC + IOP_P + D_FPU1)       /* multiply TEMP single */
#define FPP_FMTD        (IO_DOC + IOP_P + D_FPU2)       /* multiply TEMP double */
#define FPP_FDTS        (IO_DOC + IOP_C + D_FPU1)       /* divide TEMP single */
#define FPP_FDTD        (IO_DOC + IOP_C + D_FPU2)       /* divide TEMP double */

/* shift and logical instructions */
#define FPP_FABS        (IO_NIO + IOP_P + D_FPU1)       /* absolute value */
#define FPP_FCLR        (IO_NIO + IOP_S + D_FPU1)       /* clear FPAC */
#define FPP_FLDX        (IO_DOB + IOP_C + D_FPU2)       /* load exponent */
#define FPP_FNEG        (IO_NIO + IOP_C + D_FPU1)       /* negate */
#define FPP_FNRM        (IO_NIO + IOP_S + D_FPU2)       /* normalize */
#define FPP_FSCL        (IO_DOB +         D_FPU2)       /* scale */
#define FPP_FHWD        (IO_DIA +         D_FPU1)       /* read high word */

/* status instructions */
#define FPP_FRST        (IO_DIA + IOP_C + D_FPU)        /* read STATUS */
#define FPP_FWST        (IO_DOA +         D_FPU)        /* write STATUS */

/* diagnostic instructions */
#define FPP_F1WD        (IO_DIA +         D_FPU1)       /* read word 1 */
#define FPP_F2WD        (IO_DIB +         D_FPU1)       /* read word 2 */
#define FPP_F3WD        (IO_DIA +         D_FPU2)       /* read word 3 */
#define FPP_F4WD        (IO_DIB +         D_FPU2)       /* read word 4 */
#define FPP_FCLK        (IO_NIO + IOP_P + D_FPU)        /* FPU clock */


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
#define INTDS       (FPSR & STA_IND)
#define INTEN       (!MODE_DIAG && !INTDS)

static char * ff[8] =
    { "NIO", "DIA", "DOA", "DIB", "DOB", "DIC", "DOC", "SKP" } ;
static char * ss[4] =
    { " ", "S", "C", "P" } ;
static char * dd[] =
    { "FPU", "FPU1", "FPU2", "D3" };

static char* sta_bits[] = {
    "DMD", "PPM", "IND",
      "U",   "U",   "U",
      "U",   "U", "LTZ",
    "EQZ", "GTZ", "MOF",
    "DVZ", "UNF", "OVF",
    "ANY"
};

#define UNIT_V_UP       (UNIT_V_UF + 0)                 /* FPU Enabled */
#define UNIT_UP         (1 << UNIT_V_UP)
#define FUNC            u3
#define UNIT_V_BSY      (UNIT_V_UF + 1)
#define UNIT_BSY        (1 << UNIT_V_BSY)

extern int32 int_req, dev_busy, dev_done, dev_disable;

static t_int64 FPAC, TEMP;
static int16 FPSR;
static int32 fpp_trace;
int32 fpp_busy;
static t_int64 tempfp;

#define FPP_TRACE(x)    (fpp_trace && (fpp_trace & (1 << (x))))

t_stat fpp_reset(DEVICE *dptr);
int32 fpp(int32 pulse, int32 code, int32 AC);
t_stat fpp_svc(UNIT *uptr);

extern int32 GetMap(int32 addr);
extern int32 PutMap(int32 addr, int32 data);


DIB fpp_dib = { DEV_FPU, INT_FPU, PI_FPU, &fpp };

UNIT fpp_unit = { UDATA (&fpp_svc, UNIT_UP, MAXMEMSIZE) };

REG fpp_reg[] = {
    { ORDATA (STATUS, FPSR, 16) },
    { ORDATA (FPAC, FPAC, 64) },
    { ORDATA (TEMP, TEMP, 64) },
    { FLDATA (INT, int_req, INT_V_DZP) },
    { FLDATA (BUSY, dev_busy, INT_V_DZP) },
    { FLDATA (DONE, dev_done, INT_V_DZP) },
    { FLDATA (DISABLE, dev_disable, INT_V_DZP) },
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

extern void decode_bits(char *msg, int32 x, char** bits);

static void decode_sta(char *msg, int32 x)
{
    if (FPP_TRACE(2)) {
        decode_bits(msg, x, sta_bits);
        }
}

t_stat fpp_reset(DEVICE *dptr)
{
    int i;

    FPSR = 0;                   /* clear STATUS */
    fpp_busy = 0;               /* clear unit BUSY */
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
            if (INTDS) {
                FPSR |= STA_ANY;
                }
            break;
        case DGF_UNF:
            FPSR |= STA_UNF;
            DEV_CLR_BUSY(INT_FPU);
            if (INTDS) {
                FPSR |= STA_ANY;
                }
            break;
        case DGF_DVZ:
            FPSR |= STA_DVZ;
            DEV_CLR_BUSY(INT_FPU);
            if (INTDS) {
                FPSR |= STA_ANY;
                }
            break;
        case DGF_MOF:
            FPSR |= STA_MOF;
        }
}

int32 fpp1(int32 pulse, int32 code, int32 AC)
{
    int32 rval, fn;
    t_int64 tempfp;
    SHORT_FLOAT sf1, sf2;
    int k;
    int32 fwait;
    UNIT *uptr;
    char trace_buf[128];

    if (FPP_TRACE(0)) {
        sprintf( trace_buf, "  [FPU1  %s%s %06o ", ff[code & 0x07], ss[pulse & 0x03], (AC & 0xFFFF) ) ;
        }

    fwait = rval = 0;
    uptr = fpp_dev.units + 0;
    fn = MKFN(pulse,code,D_FPU1);
    switch (fn) {
        case FPP_FCLR: /* clear FPAC */
            if (FPP_TRACE(1)) printf("FCLR");
            fwait = 37;
            FPAC = 0;
            break;
        case FPP_FNEG: /* negate */
            if (FPP_TRACE(1)) printf("FNEG");
            fwait = 37;
            if (FPAC)
                FPAC ^= FPP_SIGN;
            break;
        case FPP_FABS: /* absolute value */
            if (FPP_TRACE(1)) printf("FABS");
            fwait = 37;
            FPAC &= ~FPP_SIGN;
            break;
        case FPP_FHWD: /* read high word, read word 1 */
            if (FPP_TRACE(1)) printf("FHWD");
            fwait = 22;
            rval = (int32)(FPAC >> 48) & 0177777;
            break;
        case FPP_F2WD: /* read word 2 */
            if (FPP_TRACE(1)) printf("F2WD");
            fwait = 22;
            rval = (int32)(FPAC >> 32) & 0177777;
            break;
        case FPP_FAS: /* add single */
        case FPP_FSS: /* subtract single */
            if (FPP_TRACE(1)) printf("%s", (FPP_FAS == fn) ? "FAS" : "FSS");
            GetMapS(AC, &tempfp);
            if (FPP_FSS == fn) {
                fwait = 88;
                tempfp ^= FPP_SIGN;
                }
            else {
                fwait = 82;
                }
            get_sf(&sf1, &FPAC);
            get_sf(&sf2, &tempfp);
            k = add_sf(&sf1, &sf2, 1);
            SetFPSR(k);
            store_sf(&sf1, &FPAC);
            break;
        case FPP_FDS: /* divide single */
            if (FPP_TRACE(1)) printf("FDS");
            GetMapS(AC, &tempfp);
            if (0 == (tempfp & FPP_MANT)) {
                fwait = 71;
                SetFPSR(DGF_DVZ);
                }
            else {
                fwait = 144;
                get_sf(&sf1, &FPAC);
                get_sf(&sf2, &tempfp);
                k = div_sf(&sf1, &sf2);
                SetFPSR(k);
                store_sf(&sf1, &FPAC);
                }
            break;
        case FPP_FMS: /* multiply single */
            if (FPP_TRACE(1)) printf("FMS");
            fwait = 120;
            GetMapS(AC, &tempfp);
            get_sf(&sf1, &FPAC);
            get_sf(&sf2, &tempfp);
            k = mul_sf(&sf1, &sf2);
            SetFPSR(k);
            store_sf(&sf1, &FPAC);
            break;
        case FPP_FSRS: /* store single */
            if (FPP_TRACE(1)) printf("FSRS");
            fwait = 54;
            PutMapS(AC, &FPAC);
            break;
        case FPP_FLDS: /* load single */
            if (FPP_TRACE(1)) printf("FLDS");
            fwait = 63;
            GetMapS(AC, &FPAC);
            break;
        case FPP_FATS: /* add TEMP single */
        case FPP_FSTS: /* subtract TEMP single */
            if (FPP_TRACE(1)) printf("%s", (FPP_FATS == fn) ? "FATS" : "FSTS");
            fwait = 56;
            tempfp = TEMP;
            if (FPP_FSTS == fn) {
                fwait = 62;
                tempfp ^= FPP_SIGN;
                }
            get_sf(&sf1, &FPAC);
            get_sf(&sf2, &tempfp);
            k = add_sf(&sf1, &sf2, 1);
            SetFPSR(k);
            store_sf(&sf1, &FPAC);
            break;
        case FPP_FDTS: /* divide TEMP single */
            if (FPP_TRACE(1)) printf("FDTS");
            tempfp = TEMP;
            if (0 == (tempfp & FPP_MANT)) {
                fwait = 45;
                SetFPSR(DGF_DVZ);
                }
            else {
                fwait = 118;
                get_sf(&sf1, &FPAC);
                get_sf(&sf2, &tempfp);
                k = div_sf(&sf1, &sf2);
                SetFPSR(k);
                store_sf(&sf1, &FPAC);
                }
            break;
        case FPP_FMTS: /* multiply TEMP single */
            if (FPP_TRACE(1)) printf("FMTS");
            fwait = 94;
            tempfp = TEMP;
            get_sf(&sf1, &FPAC);
            get_sf(&sf2, &tempfp);
            k = mul_sf(&sf1, &sf2);
            SetFPSR(k);
            store_sf(&sf1, &FPAC);
            break;
        }

    if (fwait) {
        DEV_SET_BUSY(INT_FPU);                          /* set busy */
        if (INTEN) {                                    /* INT enabled? */
            DEV_CLR_DONE(INT_FPU);                      /* clear done */
            DEV_UPDATE_INTR;
            }

        uptr->FUNC = fn;
        if (uptr->flags & UNIT_BSY) {
            printf("FPU1 busy!\r\n"); fpp_busy = 1;
            }
        else {
            uptr->flags |= UNIT_BSY;
            sim_activate (uptr, fwait >> 3);
            }
        }
    else {
        printf("FPU1 invalid op\r\n");
        }

    if (FPP_TRACE(0)) {
        if ( code & 1 ) {
            if (rval & 0xFFFF) {
                printf("%s", trace_buf);
                printf( "  [%06o]  ", (rval & 0xFFFF) ) ;
                printf( "]  \r\n" ) ;
                }
            }
            else {
                printf("%s", trace_buf);
                printf( "]  \r\n" ) ;
                }
        }

    return rval;
}


int32 fpp2(int32 pulse, int32 code, int32 AC)
{
    int32 rval, fn;
    t_int64 tempfp;
    LONG_FLOAT df1, df2;
    int k;
    int32 fwait;
    UNIT *uptr;
    char trace_buf[128];

    if (FPP_TRACE(0)) {
        sprintf( trace_buf, "  [FPU2  %s%s %06o ", ff[code & 0x07], ss[pulse & 0x03], (AC & 0xFFFF) ) ;
        }

    fwait = rval = 0;
    uptr = fpp_dev.units + 0;
    fn = MKFN(pulse,code,D_FPU2);
    switch (fn) {
        case FPP_F3WD: /* read word 3 */
            if (FPP_TRACE(1)) printf("FRDW3");
            fwait = 22;
            rval = (int32)(FPAC >> 16) & 0177777;
            break;
        case FPP_F4WD: /* read word 4 */
            if (FPP_TRACE(1)) printf("FRDW4");
            fwait = 22;
            rval = (int32)(FPAC & 0177777);
            break;
        case FPP_FNRM: /* normalize */
            if (FPP_TRACE(1)) printf("FNRM");
            fwait = 38;
            get_lf(&df1, &FPAC);
            k = normal_lf(&df1);
            SetFPSR(k);
            store_lf(&df1, &FPAC);
            break;
        case FPP_FMTF: /* move TEMP to FPAC */
            if (FPP_TRACE(1)) printf("FMTF");
            fwait = 37;
            FPAC = TEMP;
            break;
        case FPP_FMFT: /* move FPAC to TEMP */
            if (FPP_TRACE(1)) printf("FMFT");
            fwait = 37;
            TEMP = FPAC;
            break;
        case FPP_FAD: /* add double */
        case FPP_FSD: /* subtract double */
            if (FPP_TRACE(1)) printf("%s", (FPP_FAD == fn) ? "FAD" : "FSD");
            fwait = 98;
            GetMapD(AC, &tempfp);
            if (FPP_FSD == fn) {
                fwait = 104;
                tempfp ^= FPP_SIGN;
                }
            get_lf(&df1, &FPAC);
            get_lf(&df2, &tempfp);
            k = add_lf(&df1, &df2, 1);
            SetFPSR(k);
            store_lf(&df1, &FPAC);
            break;
        case FPP_FDD: /* divide double */
            if (FPP_TRACE(1)) printf("FDD");
            GetMapD(AC, &tempfp);
            if (0 == (tempfp & FPP_MANT)) {
                fwait = 87;
                SetFPSR(DGF_DVZ);
                }
            else {
                fwait = 224;
                get_lf(&df1, &FPAC);
                get_lf(&df2, &tempfp);
                k = div_lf(&df1, &df2);
                SetFPSR(k);
                store_lf(&df1, &FPAC);
                }
            break;
        case FPP_FMD: /* multiply double */
            if (FPP_TRACE(1)) printf("FMD");
            fwait = 200;
            GetMapD(AC, &tempfp);
            get_lf(&df1, &FPAC);
            get_lf(&df2, &tempfp);
            k = mul_lf(&df1, &df2);
            SetFPSR(k);
            store_lf(&df1, &FPAC);
            break;
        case FPP_FSCL: /* scale */
            if (FPP_TRACE(1)) printf("FSCL");
            fwait = 32;
            k = scale_lf(&FPAC, AC);
            SetFPSR(k);
            break;
        case FPP_FSRD: /* store double */
            if (FPP_TRACE(1)) printf("FSRD");
            fwait = 71;
            PutMapD(AC, &FPAC);
            break;
        case FPP_FLDX: /* load exponent */
            if (FPP_TRACE(1)) printf("FLDX");
            fwait = 37;
            FPAC &= ~FPP_EXPO;
            FPAC |= (t_int64)(AC & 077400) << 56;
            break;
        case FPP_FLDD: /* load double */
            if (FPP_TRACE(1)) printf("FLDD");
            fwait = 79;
            GetMapD(AC, &FPAC);
            break;
        case FPP_FATD: /* add TEMP double */
        case FPP_FSTD: /* subtract TEMP double */
            if (FPP_TRACE(1)) printf("%s", (FPP_FATD == fn) ? "FATD" : "FSTD");
            fwait = 56;
            tempfp = TEMP;
            if (FPP_FSTD == fn) {
                fwait = 62;
                tempfp ^= FPP_SIGN;
                }
            get_lf(&df1, &FPAC);
            get_lf(&df2, &tempfp);
            k = add_lf(&df1, &df2, 1);
            SetFPSR(k);
            store_lf(&df1, &FPAC);
            break;
        case FPP_FDTD: /* divide TEMP double */
            if (FPP_TRACE(1)) printf("FDTD");
            tempfp = TEMP;
            if (0 == (tempfp & FPP_MANT)) {
                fwait = 45;
                SetFPSR(DGF_DVZ);
                }
            else {
                fwait = 182;
                get_lf(&df1, &FPAC);
                get_lf(&df2, &tempfp);
                k = div_lf(&df1, &df2);
                SetFPSR(k);
                store_lf(&df1, &FPAC);
                }
            break;
        case FPP_FMTD: /* multiply TEMP double */
            if (FPP_TRACE(1)) printf("FMTD");
            fwait = 158;
            tempfp = TEMP;
            get_lf(&df1, &FPAC);
            get_lf(&df2, &tempfp);
            k = mul_lf(&df1, &df2);
            SetFPSR(k);
            store_lf(&df1, &FPAC);
            break;
        }

    if (fwait) {
        DEV_SET_BUSY(INT_FPU);                          /* set busy */
        if (INTEN) {                                    /* INT enabled? */
            DEV_CLR_DONE(INT_FPU);                      /* clear done */
            DEV_UPDATE_INTR;
            }

        uptr->FUNC = fn;
        if (uptr->flags & UNIT_BSY) {
            printf("FPU2 busy!\r\n"); fpp_busy = 1;
            }
        else {
            uptr->flags |= UNIT_BSY;
            sim_activate (uptr, fwait >> 3);
            }
        }
    else {
        printf("FPU2 invalid op\r\n");
        }

    if (FPP_TRACE(0)) {
        if ( code & 1 ) {
            if (rval & 0xFFFF) {
                printf("%s", trace_buf);
                printf( "  [%06o]  ", (rval & 0xFFFF) ) ;
                printf( "]  \r\n" ) ;
                }
            }
            else {
                printf("%s", trace_buf);
                printf( "]  \r\n" ) ;
                }
        }
    return rval;
}


int32 fpp(int32 pulse, int32 code, int32 AC)
{
    int32 rval, fn;
    int32 fwait = 0;
    UNIT *uptr;
    char trace_buf[128];

    if (FPP_TRACE(0)) {
        sprintf( trace_buf, "  [FPU  %s%s %06o ", ff[code & 0x07], ss[pulse & 0x03], (AC & 0xFFFF) ) ;
        }

    fwait = rval = 0;
    uptr = fpp_dev.units + 0;
    fn = MKFN(pulse,code,D_FPU);
    switch (fn) {
        case FPP_FCLK: /* FPU clock */
            if (FPP_TRACE(1)) printf("FCLK");
            fwait = 8;
            break;
        case FPP_FRST: /* read STATUS */
            fwait = 28;
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
            if (FPP_TRACE(1)) printf("FRST %06o", rval);
            decode_sta("FRST", rval);
            break;
        case FPP_FWST: /* write STATUS */
            if (FPP_TRACE(1)) printf("FWST %06o", AC);
            fwait = 28;
            FPSR = AC;
            decode_sta("FWST", AC);
            break;
        }
#if 0
    if (fwait) {
        DEV_SET_BUSY(INT_FPU);                          /* set busy */
        if (INTEN) {                                    /* INT enabled? */
            DEV_CLR_DONE(INT_FPU);                      /* clear done */
            DEV_UPDATE_INTR;
            }

        uptr->FUNC = fn;
        if (uptr->flags & UNIT_BSY) {
            printf("FPU busy!\r\n"); fpp_busy = 1;
            }
        else {
            uptr->flags |= UNIT_BSY;
            sim_activate (uptr, fwait >> 3);
            }
        }
    else {
        printf("FPU invalid op\r\n");
        }
#endif
    if (FPP_TRACE(0)) {
        if ( code & 1 ) {
            if (rval & 0xFFFF) {
                printf("%s", trace_buf);
                printf( "  [%06o]  ", (rval & 0xFFFF) ) ;
                printf( "]  \r\n" ) ;
                }
            }
            else {
                printf("%s", trace_buf);
                printf( "]  \r\n" ) ;
                }
        }
    return rval;
}

t_stat fpp_svc(UNIT *uptr)
{
    int32 u;

    u = uptr - fpp_dev.units;                           /* get unit number */
    if ( FPP_TRACE(3) ) {
        printf("[%s:%c  post %s%s %s]\r\n",
            "FPU",
            (char) (u + '0'),
            ff[(uptr->FUNC >> FUN_V_CODE) & FUN_M_CODE],
            ss[(uptr->FUNC >> FUN_V_PULSE) & FUN_M_PULSE],
            dd[uptr->FUNC & FUN_M_DEV]
            );
        }

    uptr->flags &= ~UNIT_BSY;

    DEV_CLR_BUSY(INT_FPU);
    if (INTEN) {
        DEV_SET_DONE(INT_FPU);
        DEV_UPDATE_INTR;
        }
    return SCPE_OK;
}
