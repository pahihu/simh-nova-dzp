/* nova_dzp.c: NOVA moving head disk simulator

   Copyright (c) 1993-2008, Robert M. Supnik

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Robert M Supnik shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   dzp          moving head disk

   27-Apr-12    RMS     Changed ??? string digraphs to ?, per C rules
   04-Jul-04    BKR     device name changed to DG's DKP from DEC's DP,
                        DEV_SET/CLR/INTR macro use started,
                        fixed 'P' pulse code and secret quirks,
                        added 6097 diag and size support,
                        fixed losing unit drive type during unit change,
                        tightened sector size determination calculations,
                        controller DONE flag handling fixed,
                        fixed cylinder overflow test error,
                        seek error code fixed,
                        restructured dzp_go() and dzp_svc() routines
                        (for known future fixes needed),
                        fixed DIA status calculation,
                        fixed DKP read/write loop to properly emulate DG cylinder and sector overflows,
                        added trace facility,
                        changed 'stime' calculation to force delay time if no cylinders are crossed
                        (this fixes some DG code that assumes disk seek takes some time),
                        fixed boot code to match DG hardware standard
   04-Jan-04    RMS     Changed attach routine to use sim_fsize
   28-Nov-03    CEO     Boot from DP now puts device address in SR
   24-Nov-03    CEO     Added support for disk sizing on 6099/6103
   19-Nov-03    CEO     Corrected major DMA Mapping bug
   25-Apr-03    RMS     Revised autosizing
   08-Oct-02    RMS     Added DIB
   06-Jan-02    RMS     Revised enable/disable support
   30-Nov-01    RMS     Added read only unit, extended SET/SHOW support
   24-Nov-01    RMS     Changed FLG, CAPAC to arrays
   26-Apr-01    RMS     Added device enable/disable support
   12-Dec-00    RMS     Added Eclipse support from Charles Owen
   15-Oct-00    RMS     Editorial changes
   14-Apr-99    RMS     Changed t_addr to unsigned
   15-Sep-97    RMS     Fixed bug in DIB/DOB for new disks
   15-Sep-97    RMS     Fixed bug in cylinder extraction (found by Charles Owen)
   10-Sep-97    RMS     Fixed bug in error reporting (found by Charles Owen)
   25-Nov-96    RMS     Defaulted to autosize
   29-Jun-96    RMS     Added unit disable support
*/

#include "nova_defs.h"

#define XXX 0

#define DZP_NUMDR       4                               /* #drives */
#define DZP_NUMWD       256                             /* words/sector */
#define UNIT_V_WLK      (UNIT_V_UF + 0)                 /* write locked */
#define UNIT_V_DTYPE    (UNIT_V_UF + 1)                 /* disk type */
#define UNIT_M_DTYPE    017
#define UNIT_V_AUTO     (UNIT_V_UF + 5)                 /* autosize */
#define UNIT_WLK        (1 << UNIT_V_WLK)
#define UNIT_DTYPE      (UNIT_M_DTYPE << UNIT_V_DTYPE)
#define UNIT_AUTO       (1 << UNIT_V_AUTO)
#define GET_DTYPE(x)    (((x) >> UNIT_V_DTYPE) & UNIT_M_DTYPE)
#define FUNC            u3                              /* function */
#define CYL             u4                              /* on cylinder */
#define UNIT_WPRT       (UNIT_WLK | UNIT_RO)            /* write protect */
#define UNIT_V_BSY      (UNIT_V_UF + 6)
#define UNIT_V_RDY      (UNIT_V_UF + 7)
#define UNIT_BSY        (1 << UNIT_V_BSY)
#define UNIT_RDY        (1 << UNIT_V_RDY)

static char *uflag_bits[] = {
  "WLK", "", "", "", "", "AUTO", "BSY", "RDY"
};

/* Unit, surface, sector, count register

   Original format: 2b, 6b, 4b, 4b
   Revised format:  2b, 5b, 5b, 4b
*/

#define USSC_V_COUNT    0                               /* count */
#define USSC_M_COUNT    017

#define USSC_V_OSECTOR  4                               /* old: sector */
#define USSC_M_OSECTOR  017
#define USSC_V_OSURFACE 8                               /* old: surface */
#define USSC_M_OSURFACE 077

#define USSC_V_NSECTOR  4                               /* new: sector */
#define USSC_M_NSECTOR  037
#define USSC_V_NSURFACE 9                               /* new: surface */
#define USSC_M_NSURFACE 037

#define USSC_V_UNIT     14                              /* unit */
#define USSC_M_UNIT     03
#define USSC_UNIT       (USSC_M_UNIT << USSC_V_UNIT)


#define USSC_V_COUNT    0
#define USSC_M_ZCOUNT   037
#define USSC_V_ZSECTOR  5
#define USSC_M_ZSECTOR  037
#define USSC_V_ZSURFACE 10
#define USSC_M_ZSURFACE 037

#define USSC_V_ZUNIT    5           /* in dzp_fccy */
#define USSC_M_ZUNIT    03

/* Flags, command, cylinder register

   Original format: 5b, 2b, 1b + 8b (surrounding command)
   Revised format:  5b, 2b, 9b
*/

#define FCCY_V_OCYL     0                               /* old: cylinder */
#define FCCY_M_OCYL     0377
#define FCCY_V_OCMD     8                               /* old: command */
#define FCCY_M_OCMD     3
#define FCCY_V_OCEX     10                              /* old: cyl extend */
#define FCCY_OCEX       (1 << FCCY_V_OCEX)

#define FCCY_V_NCYL     0                               /* new: cylinder */
#define FCCY_M_NCYL     0777
#define FCCY_V_NCMD     9                               /* new: command */
#define FCCY_M_NCMD     3

#define FCCY_V_ZCYL     0
#define FCCY_M_ZCYL     01777
#define FCCY_V_ZCMD     7
#define FCCY_M_ZCMD     017

#define  FCCY_READ      0
#define  FCCY_WRITE     1
#define  FCCY_SEEK      2
#define  FCCY_RECAL     3
#define  FCCY_OTHER     4
#define FCCY_FLAGS      0174000                         /* flags */

static char* fccy_cmds[] = {"read", "write", "seek", "recal", "other"};


/* Status */

#define STA_ERR         0000001                         /* error */
#define STA_DLT         0000002                         /* data late */
#define STA_CRC         0000004                         /* crc error */
#define STA_UNS         0000010                         /* unsafe */
#define STA_XCY         0000020                         /* cross cylinder */
#define STA_CYL         0000040                         /* nx cylinder */
#define STA_DRDY        0000100                         /* drive ready */
#define STA_SEEK3       0000200                         /* seeking unit 3 */
#define STA_SEEK2       0000400                         /* seeking unit 2 */
#define STA_SEEK1       0001000                         /* seeking unit 1 */
#define STA_SEEK0       0002000                         /* seeking unit 0 */
#define STA_SKDN3       0004000                         /* seek done unit 3 */
#define STA_SKDN2       0010000                         /* seek done unit 2 */
#define STA_SKDN1       0020000                         /* seek done unit 1 */
#define STA_SKDN0       0040000                         /* seek done unit 0 */
#define STA_DONE        0100000                         /* operation done */

#define STA_CNTFUL      0200000                         /* controll full */
#define STA_RWDONE      0400000

static char* sta_bits[] = {
  "ERR","DLT","CRC","UNS",
  "XCY","CYL","DRDY","SK3",
  "SEEK2","SEEK1","SEEK0","SKDN3",
  "SKDN2","SKDN1","SKDN0","DONE"
};

#define STA_DYN         (STA_DRDY | STA_CYL)            /* set from unit */
#define STA_EFLGS       (STA_ERR | STA_DLT | STA_CRC | STA_UNS | \
                         STA_XCY | STA_CYL)             /* error flags */
#define STA_DFLGS       (STA_DONE | STA_SKDN0 | STA_SKDN1 | \
                         STA_SKDN2 | STA_SKDN3)         /* done flags */

#define ZSTA_CNTFUL     0100000
#define ZSTA_RWDN       0040000     /* STA_DRDY  */
#define ZSTA_SKDN0      0020000     /* STA_SKDN0 */
#define ZSTA_SKDN1      0010000     /* STA_SKDN1 */
#define ZSTA_SKDN2      0004000     /* STA_SKDN2 */
#define ZSTA_SKDN3      0002000     /* STA_SKDN3 */
#define ZSTA_PAR        0001000     /* STA_CRC   */
#define ZSTA_SECADD     0000400     /* STA_XCY | STA_UNS */
#define ZSTA_ECC        0000200
#define ZSTA_BADSEC     0000100
#define ZSTA_CYLADD     0000040     /* STA_CYL */
#define ZSTA_SECSRF     0000020
#define ZSTA_VFY        0000010
#define ZSTA_RWTIM      0000004
#define ZSTA_DATLAT     0000002     /* STA_DLT */
#define ZSTA_RWFLT      0000001     /* STA_ERR */

static char* zsta_bits[] = {
  "RWFLT","DATLAT","RWTIM","VFY",
  "SECSRF","CYLADD","BADSEC","ECC",
  "SECADD","PAR","SKDN3","SKDN2",
  "SKDN1","SKDN0","RWDN","CNTFUL"
};


#define ZUSTA_INST      0100000
#define ZUSTA_RES       0040000
#define ZUSTA_TSP       0020000
#define ZUSTA_RDY       0010000
#define ZUSTA_BSY       0004000
#define ZUSTA_OFF       0002000
#define ZUSTA_WRDIS     0001000
#define ZUSTA_RSVD      0000400
#define ZUSTA_ILLADR    0000200
#define ZUSTA_ILLCMD    0000100
#define ZUSTA_DCFLT     0000040
#define ZUSTA_UNS       0000020
#define ZUSTA_POSFLT    0000010
#define ZUSTA_CLKFLT    0000004
#define ZUSTA_WRFLT     0000002
#define ZUSTA_DRVFLT    0000001
#define ZUSTA_EFLGS     0001776

static char* zusta_bits[] = {
  "DRVFLT","WRFLT","CLKFLT","POSFLT",
  "UNS","DCFLT","ILLCMD","ILLADR",
  "RSVD","WRDIS","OFF","BSY",
  "RDY","TSP","RES","INST"
};

#define GET_SA(cy,sf,sc,t) (((((cy)*dzp_tab[t].surf)+(sf))* \
    dzp_tab[t].sect)+(sc))

/* This controller supports many different disk drive types:

   type         #sectors/       #surfaces/      #cylinders/     new format?
                 surface         cylinder        drive

   6067         24              5               815     Zebra 1/2   ~50MB   
   6060         24             19               411     Zebra       ~95MB
   6061         24             19               815     Zebra 2     ~190MB
   6122         35             19               815     Vulcan      ~277MB
   
   seek time avg            35ms
   sector access time avg    8.3ms
   recal                    max.350ms

   In theory, each drive can be a different type.  The size field in
   each unit selects the drive capacity for each drive and thus the
   drive type.  DISKS MUST BE DECLARED IN ASCENDING SIZE.
*/

#define TYPE_6067       0
#define SECT_6067       24
#define SURF_6067       5
#define CYL_6067        815
#define SIZE_6067       (SECT_6067 * SURF_6067 * CYL_6067 * DZP_NUMWD)

#define TYPE_6060       1
#define SECT_6060       24
#define SURF_6060       19
#define CYL_6060        411
#define SIZE_6060       (SECT_6060 * SURF_6060 * CYL_6060 * DZP_NUMWD)

#define TYPE_6061       2
#define SECT_6061       24
#define SURF_6061       19
#define CYL_6061        815
#define SIZE_6061       (SECT_6061 * SURF_6061 * CYL_6061 * DZP_NUMWD)

#define TYPE_6122       3
#define SECT_6122       35
#define SURF_6122       19
#define CYL_6122        815
#define SIZE_6122       (SECT_6122 * SURF_6122 * CYL_6122 * DZP_NUMWD)


struct drvtyp {
    int32       sect;                                   /* sectors */
    int32       surf;                                   /* surfaces */
    int32       cyl;                                    /* cylinders */
    int32       size;                                   /* #blocks */
    };

struct drvtyp dzp_tab[] = {
    { SECT_6067, SURF_6067, CYL_6067, SIZE_6067 },
    { SECT_6060, SURF_6060, CYL_6060, SIZE_6060 },
    { SECT_6061, SURF_6061, CYL_6061, SIZE_6061 },
    { SECT_6122, SURF_6122, CYL_6122, SIZE_6122 },
    { 0 }
    };

#define TRACEP(...)  if (dzp_trace > 127) {printf("%s: ", __func__); printf(__VA_ARGS__);}
#define DZP_TRACE(x)    (dzp_trace & (1<<(x)))
#define DZP_TRACE_FP    stderr
/*  current trace bit use (bit 0 = LSB)
     1 0   I/O instructions
     2 1   pre-seek/read/write event setup
     4 2   post seek events
     8 3   read/write events
    16 4   post read/write events
 */

extern uint16 M[];
extern UNIT cpu_unit;
extern int32 int_req, dev_busy, dev_done, dev_disable;
extern int32 saved_PC, SR, AMASK;

int32 dzp_ma = 0;                                       /* memory address */
int32 dzp_map = 0;                                      /* DCH map 0=A 3=B */
int32 dzp_ussc = 0;                                     /* sf/sc/cnt */
int32 dzp_ussc_ext = 0;                                 /* ext sf/sc/cnt */
int32 dzp_fccy = 0;                                     /* flags/unit */
int32 dzp_zccy = 0;                                     /* cylinder */
int32 dzp_zunit = 0;                                    /* unit */
int32 dzp_sta = 0;                                      /* status register */
int32 dzp_swait = 100;                                  /* seek latency */
int32 dzp_rwait = 100;                                  /* rotate latency */
int32 dzp_diagmode = 0;                                 /* diagnostic mode */

int32 dzp_trace = 0 ;

DEVICE dzp_dev;
int32 dzp (int32 pulse, int32 code, int32 AC);
t_stat dzp_svc (UNIT *uptr);
t_stat dzp_reset (DEVICE *dptr);
t_stat dzp_boot (int32 unitno, DEVICE *dptr);
t_stat dzp_attach (UNIT *uptr, char *cptr);
t_stat dzp_go ( int32 pulse );
t_stat dzp_set_size (UNIT *uptr, int32 val, char *cptr, void *desc);

void decode_bits(char *msg, int32 x, char** bits)
{
  int i;
  
  if (dzp_trace < 128)
    return;
  
  printf(" %s=",msg);
  for (i = 0; i < 16; i++) {
    if (x & 1) printf("%s ",bits[i]);
    x >>= 1;
  }
  printf("\r\n");
}

void decode_sta(char *msg, int32 x)
{
  decode_bits(msg,x,sta_bits);
}

void decode_zsta(char *msg, int32 x)
{
  decode_bits(msg,x,zsta_bits);
}

void decode_zusta(char *msg, int32 x)
{
  decode_bits(msg,x,zusta_bits);
}

void decode_uflags(char *msg, UNIT *uptr)
{
  if (dzp_trace < 128)
    return;
  printf("CBSY=%d CDN=%d",DEV_IS_BUSY(INT_DZP),DEV_IS_DONE(INT_DZP));
  decode_bits(msg,(uptr->flags) >> UNIT_V_UF,uflag_bits);
}


#define USSC_V_CNTMSB   5
#define USSC_V_SECMSB   10
#define USSC_V_HDMSB    11

int32 GET_COUNT(int32 x, int32 dt)
{
    int32 ret;

    ret = x & USSC_M_ZCOUNT;
    ret |= (dzp_ussc_ext >> (USSC_V_CNTMSB - 5)) & 040;
    ret &= 077;
    return ret;
}

int32 GET_SECT(int32 x, int32 dt)
{
    int32 ret;

    ret = (x >> USSC_V_ZSECTOR) & USSC_M_ZSECTOR;
    ret |= (dzp_ussc_ext >> (USSC_V_SECMSB - 5)) & 040;
    ret &= 077;
    return ret;
}

int32 GET_SURF(int32 x, int32 dt)
{
    int32 ret;

    ret = (x >> USSC_V_ZSURFACE) & USSC_M_ZSURFACE;
    ret |= (dzp_ussc_ext >> (USSC_V_HDMSB - 5)) & 040;
    ret &= 077;
    return ret;
}

void DZP_UPDATE_USSC(int32 dt, int32 count, int32 surf, int32 sect)
{
  int32 ncount;
  
  ncount = GET_COUNT(dzp_ussc, dt) + count;
  dzp_ussc_ext = ((ncount & 040) << (USSC_V_CNTMSB-5)) 
                 | ((sect & 040) << (USSC_V_SECMSB-5))
                 | ((surf & 040) << (USSC_V_HDMSB-5));
  dzp_ussc = ((ncount) & USSC_M_ZCOUNT)
        | ((surf & USSC_M_ZSURFACE) << USSC_V_ZSURFACE)
        | ((sect & USSC_M_ZSECTOR) << USSC_V_ZSECTOR);
}

int32 GET_UNIT(int32 x)
{
    int32 ret;

    ret = dzp_zunit;
    return ret;
}

#define GET_ZCMD(x)         (((x) >> FCCY_V_ZCMD) & FCCY_M_ZCMD)
#define FCCY_ZALT1          9
#define FCCY_ZALT2          10


static char* dzp_cmds[] = {
    "read", "recal", "seek", "stop",
    "fwoffs", "revoffs", "wrlk", "release",
    "trespass", "alt1", "alt2", "nop",
    "verify", "rdbufs", "write", "format"
};


int32 GET_CMD(int32 x, int32 dt)
{
    int32 ret;


    ret = (x >> FCCY_V_ZCMD) & FCCY_M_ZCMD;
    switch (ret) {
    case 0: /* read */       ret = FCCY_READ; break;
    case 1: /* recalibrate */ret = FCCY_RECAL; break;
    case 2: /* seek */       ret = FCCY_SEEK; break;
    case 3: /* stop drive */ ret = FCCY_OTHER; break;
    case 4: /* offs fwd */   ret = FCCY_OTHER; break;
    case 5: /* offs rev */   ret = FCCY_OTHER; break;
    case 6: /* write disable */  ret = FCCY_OTHER; break;
    case 7: /* release */    ret = FCCY_OTHER; break;
    case 8: /* trespass */   ret = FCCY_OTHER; break;
    case 9: /* set alt1 */   ret = FCCY_OTHER; break;
    case 10: /* set alt2 */  ret = FCCY_OTHER; break;
    case 11: /* nop */ break;
    case 12: /* verify */    ret = FCCY_OTHER; break;
    case 13: /* read buffers */  ret = FCCY_READ; break;
    case 14: /* write */ ret = FCCY_WRITE; break;
    case 15: /* format */    ret = FCCY_OTHER; break;
    }
    return ret;
}


int32 GET_CYL(int32 x, int32 dt)
{
    int32 ret;

    ret = (dzp_zccy >> FCCY_V_ZCYL) & FCCY_M_ZCYL;
    return ret;
}

/* DZP data structures

   dzp_dev      DZP device descriptor
   dzp_unit     DZP unit list
   dzp_reg      DZP register list
   dzp_mod      DZP modifier list
*/

DIB dzp_dib = { DEV_DZP, INT_DZP, PI_DZP, &dzp };

UNIT dzp_unit[] = {
    { UDATA (&dzp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO+
             UNIT_ROABLE+(TYPE_6067 << UNIT_V_DTYPE), SIZE_6067) },
    { UDATA (&dzp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO+
             UNIT_ROABLE+(TYPE_6067 << UNIT_V_DTYPE), SIZE_6067) },
    { UDATA (&dzp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO+
             UNIT_ROABLE+(TYPE_6067 << UNIT_V_DTYPE), SIZE_6067) },
    { UDATA (&dzp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO+
             UNIT_ROABLE+(TYPE_6067 << UNIT_V_DTYPE), SIZE_6067) }
    };

REG dzp_reg[] = {
    { ORDATA (FCCY, dzp_fccy, 16) },
    { ORDATA (ZCCY, dzp_zccy, 10) },
    { ORDATA (USSC, dzp_ussc, 16) },
    { ORDATA (ZUNIT, dzp_zunit, 2) },
    { ORDATA (STA, dzp_sta, 16) },
    { ORDATA (MA, dzp_ma, 16) },
    { FLDATA (INT, int_req, INT_V_DZP) },
    { FLDATA (BUSY, dev_busy, INT_V_DZP) },
    { FLDATA (DONE, dev_done, INT_V_DZP) },
    { FLDATA (DISABLE, dev_disable, INT_V_DZP) },
    { FLDATA (DIAG,  dzp_diagmode, 0) },
    { DRDATA (TRACE, dzp_trace,   32) },
    { ORDATA (MAP, dzp_map, 2) },
    { DRDATA (STIME, dzp_swait, 24), PV_LEFT },
    { DRDATA (RTIME, dzp_rwait, 24), PV_LEFT },
    { URDATA (CAPAC, dzp_unit[0].capac, 10, T_ADDR_W, 0,
              DZP_NUMDR, PV_LEFT | REG_HRO) },
    { NULL }
    };

MTAB dzp_mod[] = {
    { UNIT_WLK, 0, "write enabled", "WRITEENABLED", NULL },
    { UNIT_WLK, UNIT_WLK, "write locked", "LOCKED", NULL },
    { (UNIT_DTYPE+UNIT_ATT), (TYPE_6067 << UNIT_V_DTYPE) + UNIT_ATT,
      "6067", NULL, NULL },
    { (UNIT_AUTO+UNIT_DTYPE+UNIT_ATT), (TYPE_6067 << UNIT_V_DTYPE),
      "6067", NULL, NULL },
    { (UNIT_DTYPE+UNIT_ATT), (TYPE_6060 << UNIT_V_DTYPE) + UNIT_ATT,
      "6060", NULL, NULL },
    { (UNIT_AUTO+UNIT_DTYPE+UNIT_ATT), (TYPE_6060 << UNIT_V_DTYPE),
      "6060", NULL, NULL },
    { (UNIT_DTYPE+UNIT_ATT), (TYPE_6061 << UNIT_V_DTYPE) + UNIT_ATT,
      "6061", NULL, NULL },
    { (UNIT_AUTO+UNIT_DTYPE+UNIT_ATT), (TYPE_6061 << UNIT_V_DTYPE),
      "6061", NULL, NULL },
    { (UNIT_DTYPE+UNIT_ATT), (TYPE_6122 << UNIT_V_DTYPE) + UNIT_ATT,
      "6122", NULL, NULL },
    { (UNIT_AUTO+UNIT_DTYPE+UNIT_ATT), (TYPE_6122 << UNIT_V_DTYPE),
      "6122", NULL, NULL },
    { (UNIT_AUTO+UNIT_ATT), UNIT_AUTO, "autosize", NULL, NULL },
    { UNIT_AUTO, UNIT_AUTO, NULL, "AUTOSIZE", NULL },
    { (UNIT_AUTO+UNIT_DTYPE), (TYPE_6067 << UNIT_V_DTYPE),
      NULL, "6067", &dzp_set_size },
    { (UNIT_AUTO+UNIT_DTYPE), (TYPE_6060 << UNIT_V_DTYPE),
      NULL, "6060", &dzp_set_size },
    { (UNIT_AUTO+UNIT_DTYPE), (TYPE_6061 << UNIT_V_DTYPE),
      NULL, "6061", &dzp_set_size },
    { (UNIT_AUTO+UNIT_DTYPE), (TYPE_6122 << UNIT_V_DTYPE),
      NULL, "6122", &dzp_set_size },
    { 0 }
    };

DEVICE dzp_dev = {
    "DZP", dzp_unit, dzp_reg, dzp_mod,
    DZP_NUMDR /*numunits*/, 8 /*aradix*/, 30 /*awidth*/, 1 /*aincr*/, 8 /*dradix*/, 16 /*dwidth*/,
    NULL, NULL, &dzp_reset,
    &dzp_boot, &dzp_attach, NULL,
    &dzp_dib, DEV_DISABLE
    };


/* IOT routine */

int32 dzp (int32 pulse, int32 code, int32 AC)
{
UNIT *uptr;
int32 u, rval, dtype;
char trace_buf[256];
int32 cmd;

rval = 0;
uptr = dzp_dev.units + GET_UNIT (dzp_ussc);             /* select unit */
dtype = GET_DTYPE (uptr->flags);                        /* get drive type */
decode_uflags("DZP",uptr);

if ( DZP_TRACE(0) )
    {
    static char * f[8] =
        { "NIO", "DIA", "DOA", "DIB", "DOB", "DIC", "DOC", "SKP" } ;
    static char * s[4] =
        { " ", "S", "C", "P" } ;

        sprintf( trace_buf, "  [DZP  %s%s %06o ", f[code & 0x07], s[pulse & 0x03], (AC & 0xFFFF) ) ;
        }

switch (code) {                                         /* decode IR<5:7> */

    case ioDIA:                                         /* DIA */
        dzp_sta = dzp_sta & (~STA_DRDY) ;               /* keep error flags  */
        if (uptr->flags & UNIT_ATT)                     /* update ready */
            dzp_sta = dzp_sta | STA_DRDY;
        if (uptr->CYL >= dzp_tab[dtype].cyl)
            dzp_sta = dzp_sta | STA_CYL;                /* bad cylinder? */
        if (dzp_sta & STA_EFLGS)
            dzp_sta = dzp_sta | STA_ERR;
        rval = dzp_sta;
        if (FCCY_ZALT1 == GET_ZCMD(dzp_fccy)) {
            rval = dzp_ma & 077777 ;                        /* return buf addr */
        } else if (FCCY_ZALT2 == GET_ZCMD(dzp_fccy)) {      /* ECC HI */
            rval = 0;
        } else {
            rval = 0;
            if (dzp_sta & STA_CNTFUL) rval |= ZSTA_CNTFUL;
            if (dzp_sta & STA_RWDONE)  rval |= ZSTA_RWDN;
            /* R/W Done is device Done, but it triggers the INT here also */
            if (dzp_sta & STA_SKDN0) rval |= ZSTA_SKDN0;
            if (dzp_sta & STA_SKDN1) rval |= ZSTA_SKDN1;
            if (dzp_sta & STA_SKDN2) rval |= ZSTA_SKDN2;
            if (dzp_sta & STA_SKDN3) rval |= ZSTA_SKDN3;
            if (dzp_sta & STA_CRC)   rval |= ZSTA_PAR;
            if (dzp_sta & (STA_XCY | STA_UNS)) rval |= ZSTA_SECADD;
            if (dzp_sta & STA_CYL)   rval |= ZSTA_CYLADD;
            if (dzp_sta & STA_DLT)   rval |= ZSTA_DATLAT;
            if (dzp_sta & STA_ERR)   rval |= ZSTA_RWFLT;
            if (rval) {
              decode_sta("STAT",dzp_sta);
              decode_zsta("TXSTAT",rval);
            }
        }
        break;

    case ioDOA:                                         /* DOA */
        if (AC & 0100000) {                             /* clear rw done? */
            dzp_sta = dzp_sta & ~(STA_CYL|STA_XCY|STA_UNS|STA_CRC|STA_RWDONE);
        }
        if ((dev_busy & INT_DZP) == 0) {
            dzp_fccy = AC;                              /* save cmd, cyl */
            dzp_zunit = ((dzp_fccy >> USSC_V_ZUNIT) & USSC_M_ZUNIT);
            dzp_sta = dzp_sta & ~(AC & FCCY_FLAGS);     /* clear STA_DONE and STA_SKDNx */
            TRACEP(" UNO=%01o ZCMD=%s CMD=%s ",dzp_zunit,dzp_cmds[GET_ZCMD(dzp_fccy)],fccy_cmds[GET_CMD(dzp_fccy,dtype)]);
            }
        if (AC & 0100000) {
            DEV_CLR_DONE( INT_DZP );                    /* assume done flags 0 */
#if 0
            if (!DEV_IS_BUSY( INT_DZP) && (dzp_sta & STA_DFLGS))/* done flags = 0? */
                DEV_SET_DONE( INT_DZP )    ;            /* nope - set done  */
#endif
            DEV_UPDATE_INTR;                            /* update intr  */
        }        
        break;

    case ioDIB:                                         /* DIB */
        rval = 0;
        if (FCCY_ZALT2 == GET_ZCMD(dzp_fccy)) {     /* ECC LO */
            rval = 0;
        } else if (FCCY_ZALT1 == GET_ZCMD(dzp_fccy)) {  /* read ext disk addr */
            rval = dzp_ussc_ext;
        }
        else {
            decode_uflags("ioDIB",uptr);
            if (uptr->flags & UNIT_RDY)             /* update ready */
                rval |= ZUSTA_RDY;
            if (uptr->flags & UNIT_BSY)
                rval |= ZUSTA_BSY;
            if (uptr->flags & UNIT_WPRT)
                rval |= ZUSTA_WRDIS;
            if ((uptr->CYL >= dzp_tab[dtype].cyl) /* bad cylinder? */
                || (GET_SECT(dzp_ussc, dtype) >= dzp_tab[dtype].sect )) /* or bad sector? */
                rval |= ZUSTA_ILLADR;
            if ((uptr->flags & UNIT_WPRT) && (FCCY_WRITE == GET_CMD(dzp_fccy, dtype)))
                rval |= ZUSTA_ILLCMD;
            if (dzp_sta & STA_EFLGS)
                rval |= ZUSTA_WRFLT;
            if (rval & ZUSTA_EFLGS)
                rval |= ZUSTA_DRVFLT;
            decode_zusta("DRSTAT",rval);
        } 
        break;

    case ioDOB:                                         /* DOB */
        if ((dev_busy & INT_DZP) == 0) {
            dzp_ma = AC & DMASK;                        /* old was AMASK? */
            if (AC & 0100000)
                dzp_map = 3;                            /* high bit is map */
            else
                dzp_map = 0;
        }
        break;

    case ioDIC:                                         /* DIC */
        rval = dzp_ussc;                                /* return unit, sect */
        break;

    case ioDOC:                                         /* DOC */
        if ((dev_busy & INT_DZP) == 0)                  /* if device is not busy */
        {
            if (FCCY_SEEK == GET_CMD(dzp_fccy, dtype)) {
              dzp_zccy = AC;
              TRACEP(" CYL=%04d ",GET_CYL(dzp_zccy,dtype));
              break;
            }
            else {
              dzp_ussc_ext = dzp_ussc; dzp_ussc = AC;
            }
            TRACEP(" SURF=%02d SECT=%02d CNT=%02d ",GET_SURF(dzp_ussc,dtype),GET_SECT(dzp_ussc,dtype),GET_COUNT(dzp_ussc,dtype));
        }
#ifdef OBSOLETE
        if (((dtype == TYPE_6099) ||                    /* (BKR: don't forget 6097) */
             (dtype == TYPE_6097) ||                    /* for 6099 and 6103 */
             (dtype == TYPE_6103)) &&                   /* if data<0> set, */
            (AC & 010000) )
        dzp_diagmode = 1;                               /* set diagnostic mode */
#endif
        
        break;
    }                                                   /* end switch code */

u = GET_UNIT(dzp_ussc);                                 /* update current unit */
uptr = dzp_dev.units + u ;                              /* select unit */
dtype = GET_DTYPE (uptr->flags);                        /* get drive type */

if ( DZP_TRACE(0) )
    {
    if ( code & 1 ) {
        if (rval & 0xFFFF) {
            printf(trace_buf);
            printf( "  [%06o]  ", (rval & 0xFFFF) ) ;
            printf( "]  \r\n" ) ;
            }
        } else {
            printf(trace_buf);
            printf( "]  \r\n" ) ;
            }
    }

cmd = GET_CMD(dzp_fccy, dtype);
switch (pulse) {                                        /* decode IR<8:9> */

    case iopS:                                          /* start */
        DEV_SET_BUSY( INT_DZP ) ;                       /*  set busy    */
        DEV_CLR_DONE( INT_DZP ) ;                       /*  clear done  */
        DEV_UPDATE_INTR ;                               /*  update ints */
        dzp_sta = dzp_sta & ~(STA_EFLGS);               /*  clear controller flags  */
        if (dzp_diagmode) {                             /* in diagnostic mode? */
            dzp_diagmode = 0;                           /* reset it     */
#ifdef OBSOLETE
            if (dtype == TYPE_6097)                     /* (BKR - quad floppy) */
                dzp_ussc = 010001;
            if (dtype == TYPE_6099)                     /* return size bits */
                dzp_ussc = 010002;
            if (dtype == TYPE_6103)                     /* for certain types */
                dzp_ussc = 010003;
#endif
            } 
        else {                                          /* normal mode ... */
            if (dzp_go (pulse)) {                       /* do command    */
                    break ;                             /* break if no error  */
            }
            DEV_CLR_BUSY( INT_DZP ) ;                   /*  clear busy  */
            DEV_SET_DONE( INT_DZP ) ;                   /*  set done    */
            DEV_UPDATE_INTR ;                           /*  update ints */
            dzp_sta = dzp_sta | STA_DONE;               /*  set controller done  */
        }
        break;

    case iopC:                                          /* clear */
        DEV_CLR_BUSY( INT_DZP ) ;                       /*  clear busy  */
        DEV_CLR_DONE( INT_DZP ) ;                       /*  set done    */
        DEV_UPDATE_INTR ;                               /*  update ints */
        dzp_sta = dzp_sta & ~(STA_DFLGS + STA_EFLGS);/*  clear controller flags  */
        if (dzp_unit[u].FUNC == FCCY_READ || dzp_unit[u].FUNC == FCCY_WRITE)
            sim_cancel (&dzp_unit[u]);                  /*  cancel any r/w op  */
        break;

    case iopP:                                          /* pulse */
        if ( dzp_diagmode )
            {
            dzp_diagmode = 0 ;                          /*  clear DG diagnostic mode  */
            }
        else
            {
#if 0
            if (FCCY_SEEK == cmd) {
                // DEV_SET_BUSY( INT_DZP )
                ;
            } else
            {
                DEV_CLR_DONE( INT_DZP ) ;                   /*  clear done  */
            }            
            DEV_UPDATE_INTR ;
#endif
            dzp_sta |= STA_CNTFUL;

            /*  DG "undocumented feature": 'P' pulse can not start a read/write operation!
             *  Diagnostic routines will use this crock to do 'crazy things' to size a disk
             *  and many assume that a recal is done, other assume that they can stop the
             *  read operation before any damage is done.  Must also [re]calculate unit, function
             *  and type because DOx instruction may have updated the controller info after
             *  start of this procedure and before our 'P' handler.   BKR
             */
            if (dzp_go(pulse)) {
                break;                              /* no error - do not set done and status  */
            }
#if 0
            if (FCCY_SEEK == cmd) {
                // DEV_CLR_BUSY( INT_DZP )
                ;
            } else
            {
                DEV_SET_DONE( INT_DZP ) ;               /* set done */
            }
            DEV_UPDATE_INTR ;                           /* update ints */
#endif
            dzp_sta &= ~STA_CNTFUL;
            uptr->flags &= ~UNIT_BSY;
            dzp_sta = dzp_sta | (STA_SKDN0 >> u);   /* set controller seek done */
            break;
            }
        }                                           /* end case pulse */

return rval;
}


/* New command, start vs pulse handled externally
   Returns true if command ok, false if error
*/

t_stat dzp_go ( int32 pulse )
{
UNIT *uptr;
int32 oldCyl, u, dtype;
float fact;

TRACEP("pulse=%d\r\n",pulse);

dzp_sta = dzp_sta & ~STA_EFLGS;                         /* clear errors */
u = GET_UNIT (dzp_ussc);                                /* get unit number */
uptr = dzp_dev.units + u;                               /* get unit */
decode_uflags("DZP_GO",uptr);
if (((uptr->flags & UNIT_ATT) == 0) || sim_is_active (uptr)) {
    TRACEP("attached or busy\r\n");
    dzp_sta = dzp_sta | STA_ERR;                        /* attached or busy? */
    return FALSE;
    }

if (dzp_diagmode) {                                     /* diagnostic mode? */
    TRACEP("diagmode\r\n");
    dzp_sta = (dzp_sta | STA_DONE);                     /* Set error bit only */
    DEV_CLR_BUSY( INT_DZP ) ;                           /* clear busy  */
    DEV_SET_DONE( INT_DZP ) ;                           /* set   done  */
    DEV_UPDATE_INTR ;                                   /* update interrupts  */
    return ( TRUE ) ;                                   /* do not do function */
    }

oldCyl = uptr->CYL ;                                    /* get old cylinder  */
dtype  = GET_DTYPE (uptr->flags);                       /* get drive type */
uptr->FUNC = GET_CMD (dzp_fccy, dtype) ;                /* save command */
if (uptr->FUNC == FCCY_SEEK || uptr->FUNC == FCCY_RECAL) {
    uptr->CYL  = GET_CYL (dzp_fccy, dtype) ;
    uptr->flags |= UNIT_BSY;
    uptr->flags &= ~UNIT_RDY;
    }
if (iopP == pulse) {
    // ZDF-1 pp. 101 clear the CNTFUL
    dzp_sta &= ~STA_CNTFUL;
    // the controller can accept another command, while SEEK/RECAL in progress!
    // then it waits until the prev. operation ends, and issues the command
    }

if ( DZP_TRACE(1) )
    {
    int32        xSect ;
    int32        xSurf ;
    int32        xCyl ;
    int32        xCnt ;

    xSect = GET_SECT(dzp_ussc, dtype) ;
    xSurf = GET_SURF(dzp_ussc, dtype) ;
    xCyl  = GET_CYL (dzp_fccy, dtype) ;
    xCnt  = 64 - (GET_COUNT(dzp_ussc, dtype)) ;

    fprintf( DZP_TRACE_FP,
        "  [%s:%c  %-5s:  %3d / %2d / %2d   %2d   %06o ] \r\n",
        "DZP",
        (char) (u + '0'),
        ((uptr->FUNC == FCCY_READ) ?
              "read"
            : ((uptr->FUNC == FCCY_WRITE) ?
                  "write"
                : ((uptr->FUNC == FCCY_SEEK) ?
                      "seek"
                    : ((uptr->FUNC == FCCY_RECAL) ?
                      "recal"
                      : "other"
                      )
                  )
              )
        ),
        (unsigned) xCyl,
        (unsigned) xSurf,
        (unsigned) xSect,
        (unsigned) xCnt,
        (unsigned) (dzp_ma & 0xFFFF) /* show all 16-bits in case DCH B */
        ) ;
    }


switch (uptr->FUNC) {                                   /* decode command */
    case FCCY_READ:
    case FCCY_WRITE:
    if (((uptr->flags & UNIT_ATT) == 0) ||              /* not attached? */
        ((uptr->flags & UNIT_WPRT) && (uptr->FUNC == FCCY_WRITE)))
            {
            TRACEP("not attached\r\n");
            dzp_sta = dzp_sta | STA_DONE | STA_ERR;        /* error */
            }
    else if ( uptr->CYL  >= dzp_tab[dtype].cyl )        /* bad cylinder */
        {
        TRACEP("bad cylinder\r\n");
        dzp_sta = dzp_sta | STA_DONE | STA_ERR | STA_CYL ;
        }
    else if ( GET_SURF(dzp_ussc, dtype) >= dzp_tab[dtype].surf ) /* bad surface */
        {
        TRACEP("bad surface\r\n");
        dzp_sta = dzp_sta | STA_DONE | STA_ERR | STA_UNS;   /* older drives may not even do this... */
        /*    dzp_sta = dzp_sta | STA_DONE | STA_ERR | STA_XCY ;  /-  newer disks give this error  */
        }
    else if ( GET_SECT(dzp_ussc, dtype) >= dzp_tab[dtype].sect ) /* or bad sector? */
        {
        TRACEP("bad sector\r\n");
    /*  dzp_sta = dzp_sta | STA_DONE | STA_ERR | STA_UNS;   /- older drives may not even do this... */
        dzp_sta = dzp_sta | STA_DONE | STA_ERR | STA_XCY ;  /*  newer disks give this error  */
        }
    if ( (pulse != iopS) || (dzp_sta & STA_ERR) )
        {
        TRACEP("not S or has err\r\n");
        return ( FALSE ) ;
        }
        if (uptr->flags & UNIT_BSY) {
            dzp_svc (uptr);                             /* service it */
            sim_activate_abs (uptr, dzp_rwait);         /* replace on queue */
            }
        else sim_activate (uptr, dzp_rwait);            /* schedule read or write request */
        break;

    case FCCY_RECAL:                                    /* recalibrate */
        // uptr->FUNC = FCCY_SEEK ;                     /* save command */
        uptr->CYL  = 0 ;

    case FCCY_SEEK:                                     /* seek */
        if ( ! (uptr->flags & UNIT_ATT) )               /* not attached? */
            {
            TRACEP("seek: not attached\r\n");
            dzp_sta = dzp_sta | STA_DONE | STA_ERR;     /* error */
            }
        else if ( uptr->CYL >= dzp_tab[dtype].cyl )     /* bad cylinder? */
            {
            TRACEP("seek: bad cylinder\r\n");
            dzp_sta = dzp_sta | STA_ERR | STA_CYL;
            }
        if ( (pulse != iopP) || (dzp_sta & STA_ERR) )
            {
            TRACEP("seek: not P or has err\r\n");
            return ( FALSE ) ;                          /* only 'P' pulse start seeks!  */
            }

        /*  do the seek  */
        /* must check for "do we support seeking bits" flag before setting SEEK0'ish bits!  */
        dzp_sta = dzp_sta | (STA_SEEK0 >> u);           /* set seeking */
        oldCyl = abs(oldCyl - uptr->CYL) ;
        if ( (dzp_swait) && (! (oldCyl)) )              /* enforce minimum wait if req  */
            oldCyl = 1 ;
        fact = (FCCY_RECAL == uptr->FUNC) ? 3.5 : 1.0;
        // printf("seek: before activate swait=%d oldCyl=%d fact=%f\r\n",dzp_swait,oldCyl,fact);
        sim_activate ( uptr, (fact * dzp_swait * oldCyl) ) ;
        break;
        }                                               /* end case command */

return ( TRUE ) ;                                       /* no error */
}


/* Unit service

   If seek done, put on cylinder;
   else, do read or write
   If controller was busy, clear busy, set done, interrupt

   Memory access: sectors are read into/written from an intermediate
   buffer to allow word-by-word mapping of memory addresses on the
   Eclipse.  This allows each word written to memory to be tested
   for out of range.
*/

t_stat dzp_svc (UNIT *uptr)
{
int32 sa, bda;
int32 dx, pa, u;
int32 dtype, err, newsect, newsurf;
uint32 awc;
t_stat rval;
static uint16 tbuf[DZP_NUMWD];                          /* transfer buffer */

decode_uflags("DZP_SVC",uptr);

rval  = SCPE_OK;
dtype = GET_DTYPE (uptr->flags);                        /* get drive type */
u     = uptr - dzp_dev.units;                           /* get unit number */

if (uptr->FUNC == FCCY_SEEK || uptr->FUNC == FCCY_RECAL) {                          /* seek? */
    if ( ! (uptr->flags & UNIT_ATT) )                   /* not attached? */
        {
        dzp_sta = dzp_sta | STA_DONE | STA_ERR;         /* error (changed during queue time?) */
        }
    else if ( uptr->CYL >= dzp_tab[dtype].cyl )         /* bad cylinder? */
        {
        dzp_sta = dzp_sta | STA_ERR | STA_CYL;
        }
    if (!DEV_IS_BUSY(INT_DZP)) {
        DEV_SET_DONE( INT_DZP ) ;                       /* initiates INT when not busy */
        DEV_UPDATE_INTR ;
        }
    dzp_sta = (dzp_sta | (STA_SKDN0 >> u))              /* set seek done */
                & ~(STA_SEEK0 >> u);                    /* clear seeking */
    dzp_sta &= ~STA_CNTFUL;
    uptr->flags &= ~UNIT_BSY;                           /* clear busy */
    uptr->flags |= UNIT_RDY;
    if ( DZP_TRACE(2) )
        {
        fprintf( DZP_TRACE_FP,
            "  [%s:%c  post %s : %4d ] \r\n",
            "DZP",
            (char) (u + '0'),
            (uptr->FUNC == FCCY_SEEK) ? "seek" : "recal",
            (unsigned) (uptr->CYL)
            ) ;
        }
    return SCPE_OK;
    }

/*  read or write  */

if (((uptr->flags & UNIT_ATT) == 0) ||                  /* not attached? */
    ((uptr->flags & UNIT_WPRT) && (uptr->FUNC == FCCY_WRITE)))
    {
    dzp_sta = dzp_sta | STA_DONE | STA_ERR;             /* error */
    }
else if ( uptr->CYL >= dzp_tab[dtype].cyl )             /* bad cylinder */
    {
    dzp_sta = dzp_sta | STA_DONE | STA_ERR | STA_CYL ;
    dzp_sta  = dzp_sta | STA_ERR | STA_CYL;
    DEV_SET_DONE( INT_DZP ) ;
    DEV_UPDATE_INTR ;
    return SCPE_OK ;
    }
else if ( GET_SURF(dzp_ussc, dtype) >= dzp_tab[dtype].surf ) /* bad surface */
    {
    dzp_sta = dzp_sta | STA_DONE | STA_ERR | STA_UNS;   /* older drives may not even do this... */
/*  dzp_sta = dzp_sta | STA_DONE | STA_ERR | STA_XCY ;  /- newer disks give this error  */
/* set sector to some bad value and wait then exit?  */
    }
else if ( GET_SECT(dzp_ussc, dtype) >= dzp_tab[dtype].sect )   /* or bad sector? */
    {
/*    dzp_sta = dzp_sta | STA_DONE | STA_ERR | STA_UNS;   /- older DG drives do not even give error(!), but we do */
    dzp_sta = dzp_sta | STA_DONE | STA_ERR | STA_XCY ;  /* newer disks give this error  */
    }
else {
err = 0 ;
do  {
    if ( DZP_TRACE(3) )
        {
        fprintf( DZP_TRACE_FP,
            "  [%s:%c  %-5s:  %3d / %2d / %2d   %06o @ %2d] \r\n",
            "DZP",
            (char) (u + '0'),
            ((uptr->FUNC == FCCY_READ) ?
                  "read"
                : ((uptr->FUNC == FCCY_WRITE) ?
                      "write"
                    : "<?>")
            ),
            (unsigned) (uptr->CYL),
            (unsigned) (GET_SURF(dzp_ussc, dtype)),
            (unsigned) (GET_SECT(dzp_ussc, dtype)),
            (unsigned) (dzp_ma & 0xFFFF),
            (unsigned) (GET_COUNT(dzp_ussc, dtype)) /* show all 16-bits in case DCH B */
            ) ;
        }


    if ( GET_SECT(dzp_ussc, dtype) >= dzp_tab[dtype].sect )   /* or bad sector? */
        {
        /* sector overflows to 0 ;
         * surface gets incremented
         */
        newsurf = GET_SURF(dzp_ussc, dtype) + 1 ;
        DZP_UPDATE_USSC( dtype, 0, newsurf, 0 );

        if ( (GET_SURF(dzp_ussc, dtype)) >= dzp_tab[dtype].surf )
            {
        /*  dzp_sta = dzp_sta | STA_DONE | STA_ERR | STA_UNS;   /- older drives may not even do this... */
            dzp_sta = dzp_sta | STA_DONE | STA_ERR | STA_XCY ;  /*  newer disks give this error  */
            /* DG retains overflowed surface number,
             * other vendors have different/expanded options
             */
            break ;
            }
        }
    sa = GET_SA (uptr->CYL, GET_SURF (dzp_ussc, dtype),
         GET_SECT (dzp_ussc, dtype), dtype);            /* get disk block */
    bda = sa * DZP_NUMWD * sizeof(uint16) ;             /* to words, bytes */
    err = fseek (uptr->fileref, bda, SEEK_SET);         /* position drive */

    if (uptr->FUNC == FCCY_READ) {                      /* read? */
            awc = fxread (tbuf, sizeof(uint16), DZP_NUMWD, uptr->fileref);
            for ( ; awc < DZP_NUMWD; awc++) tbuf[awc] = 0;
            if ((err = ferror (uptr->fileref)))
                break;
            for (dx = 0; dx < DZP_NUMWD; dx++) {            /* loop thru buffer */
                pa = MapAddr (dzp_map, (dzp_ma & AMASK));
                if (MEM_ADDR_OK (pa))
                    M[pa] = tbuf[dx];
                dzp_ma = (dzp_ma + 1) & AMASK;
                }
        }
    else if (uptr->FUNC == FCCY_WRITE) {                /* write? */
            for (dx = 0; dx < DZP_NUMWD; dx++) {        /* loop into buffer */
                pa = MapAddr (dzp_map, (dzp_ma & AMASK));
                tbuf[dx] = M[pa];
                dzp_ma = (dzp_ma + 1) & AMASK;
                }
            fxwrite (tbuf, sizeof(int16), DZP_NUMWD, uptr->fileref);
            if ((err = ferror (uptr->fileref)))
                break;
            }

    if (err != 0) {
        perror ("DZP I/O error");
        clearerr (uptr->fileref);
        rval = SCPE_IOERR;
        break ;
        }

newsect = GET_SECT (dzp_ussc, dtype) + 1 ;              /*  update next sector  */
newsurf = GET_SURF (dzp_ussc, dtype) ;                  /*  and next head  */
                                                        /*  (count set below)    */
DZP_UPDATE_USSC( dtype, 1, newsurf, newsect );
}  /*  end read/write loop  */

    while ( (GET_COUNT(dzp_ussc, dtype)) ) ;
    dzp_sta = dzp_sta | STA_DONE | STA_RWDONE;          /* set status */
    
    /* correct sector */
    if ( GET_SECT(dzp_ussc, dtype) >= dzp_tab[dtype].sect ) {
        /* sector overflows to 0 ;
         * surface gets incremented
         */
        newsurf = GET_SURF(dzp_ussc, dtype) + 1 ;
        DZP_UPDATE_USSC( dtype, 0, newsurf, 0 );
        }

    if ( DZP_TRACE(4) )
           {
           fprintf( DZP_TRACE_FP,
                   "  [%s:%c  post %-5s:  %3d / %2d / %2d   %06o ] \r\n",
                   "DZP",
                    (char) (u + '0'),
                    (FCCY_READ == uptr->FUNC) ? "read" : "write",
                    (unsigned) (uptr->CYL),
                    (unsigned) (GET_SURF(dzp_ussc, dtype)),
                    (unsigned) (GET_SECT(dzp_ussc, dtype)),
                    (unsigned) (dzp_ma & 0xFFFF) /* show all 16-bits in case DCH B */
                    ) ;
            }
    }

DEV_CLR_BUSY( INT_DZP ) ;
DEV_SET_DONE( INT_DZP ) ;
DEV_UPDATE_INTR ;
return rval;
}

/* Reset routine */

t_stat dzp_reset (DEVICE *dptr)
{
int32 u;
UNIT *uptr;

DEV_CLR_BUSY( INT_DZP ) ;                               /*  clear busy    */
DEV_CLR_DONE( INT_DZP ) ;                               /*  clear done    */
DEV_UPDATE_INTR ;                                       /*  update ints    */
dzp_fccy = dzp_ussc = dzp_ma = dzp_sta = 0;             /* clear registers */
dzp_ussc_ext = dzp_zunit = dzp_zccy = 0;
dzp_diagmode = 0;                                       /* clear diagnostic mode */
dzp_map = 0;
for (u = 0; u < DZP_NUMDR; u++) {                       /* loop thru units */
    uptr = dzp_dev.units + u;
    sim_cancel (uptr);                                  /* cancel activity */
    uptr->CYL = uptr->FUNC = 0;                         /* FCCY_READ, CYL 0 */
    uptr->flags &= ~UNIT_BSY;
    uptr->flags |= UNIT_RDY;
    }
return SCPE_OK;
}

/* Attach routine (with optional autosizing) */

t_stat dzp_attach (UNIT *uptr, char *cptr)
{
int32 i, p;
t_stat   r;

uptr->capac = dzp_tab[GET_DTYPE (uptr->flags)].size;    /* restore capac */
r = attach_unit (uptr, cptr);                           /* attach */
if ((r != SCPE_OK) || !(uptr->flags & UNIT_AUTO))
    return r;
if ((p = sim_fsize (uptr->fileref)) == 0)               /* get file size */
    return SCPE_OK;
for (i = 0; dzp_tab[i].sect != 0; i++) {
    if (p <= (dzp_tab[i].size * (int32) sizeof (uint16))) {
        uptr->flags = (uptr->flags & ~UNIT_DTYPE) | (i << UNIT_V_DTYPE);
        uptr->capac = dzp_tab[i].size;
        return SCPE_OK;
        }
    }
return SCPE_OK;
}

/* Set size command validation routine */

t_stat dzp_set_size (UNIT *uptr, int32 val, char *cptr, void *desc)
{
if (uptr->flags & UNIT_ATT)
    return SCPE_ALATT;
uptr->capac = dzp_tab[GET_DTYPE (val)].size;
return SCPE_OK;
}

/* Bootstrap routine */

#if defined(_OLD_CODE_)

#define BOOT_START  02000
#define BOOT_UNIT   02021
#define BOOT_SEEK   02022
#define BOOT_LEN    (sizeof(boot_rom) / sizeof(int32))

static const int32 boot_rom[] = {
    0060233,                    /* NIOC 0,DKP           ; clear disk */
    0020420,                    /* LDA 0,USSC           ; unit, sfc, sec, cnt */
    0063033,                    /* DOC 0,DKP            ; select disk */
    0020417,                    /* LDA 0,SEKCMD         ; command, cylinder */
    0061333,                    /* DOAP 0,DKP           ; start seek */
    0024415,                    /* LDA 1,SEKDN */
    0060433,                    /* DIA 0,DKP            ; get status */
    0123415,                    /* AND# 1,0,SZR         ; skip if done */
    0000776,                    /* JMP .-2 */
    0102400,                    /* SUB 0,0              ; mem addr = 0 */
    0062033,                    /* DOB 0,DKP */
    0020411,                    /* LDA 0,REDCMD         ; command, cylinder */
    0061133,                    /* DOAS 0,DKP           ; start read */
    0060433,                    /* DIA 0, DKP           ; get status */
    0101113,                    /* MOVL# 0,0,SNC        ; skip if done */
    0000776,                    /* JMP .-2 */
    0000377,                    /* JMP 377 */
    0000016,                    /* USSC:   0.B1+0.B7+0.B11+16 */
    0175000,                    /* SEKCMD: 175000 */
    0074000,                    /* SEKDN:  074000 */
    0174000                     /* REDCMD: 174000 */
    };


t_stat dzp_boot (int32 unitno, DEVICE *dptr)
{
int32 i, dtype;
extern int32 saved_PC, SR;

for (i = 0; i < BOOT_LEN; i++) M[BOOT_START + i] = boot_rom[i];
unitno = unitno & USSC_M_UNIT;
dtype = GET_DTYPE (dzp_unit[unitno].flags);
M[BOOT_UNIT] = M[BOOT_UNIT] | (unitno << USSC_V_UNIT);
M[BOOT_SEEK] = 0176000;
saved_PC = BOOT_START;
SR = 0100000 + DEV_DZP;
return SCPE_OK;
}

#endif      /*  _OLD_CODE_  */



#define BOOT_START  0375
#define BOOT_LEN    (sizeof (boot_rom) / sizeof (int32))

static const int32 boot_rom[] = {
      0062677                     /* IORST                ; reset the I/O system  */
    , 0060127                     /* NIOS DZP             ; start the disk        */
    , 0000377                     /* JMP 377              ; wait for the world    */
    } ;


t_stat dzp_boot (int32 unitno, DEVICE *dptr)
{
size_t i;

for (i = 0; i < BOOT_LEN; i++)
    M[BOOT_START + i] = (uint16) boot_rom[i];
saved_PC = BOOT_START;
SR = 0100000 + DEV_DZP;
return SCPE_OK;
}
