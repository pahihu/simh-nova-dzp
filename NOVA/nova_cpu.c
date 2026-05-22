/* nova_cpu.c: NOVA CPU simulator

   Copyright (c) 1993-2020, Robert M. Supnik

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

   cpu          Nova central processor

   03-Oct-20    RMS     Fixed bug in history handling of C bit (Samuel Deutsch)
   07-Sep-17    RMS     Fixed sim_eval declaration in history routine (COVERITY)
   17-Mar-13    RMS     Added clarifying brances to IND_STEP macro (Dave Bryan)
   04-Jul-07    BKR     DEV_SET/CLR macros now used,
                        support for non-existant devices added
                        CPU bootstrap code warning: high-speed devices may not boot properly,
                        execution history facility added,
                        documented Nova 3 secret LDB/STB/SAVN behavior,
                        added support for secret Nova 3 LDB/STB/SAVN substitute actions,
                        'ind_max' changed from 16 to 65536 for better unmapped system compatibility,
                        INT_TRAP added for Nova 3, 4 trap instruction handling,
   28-Apr-07    RMS     Removed clock initialization
   06-Feb-06    RMS     Fixed bug in DIVS (Mark Hittinger)
   22-Sep-05    RMS     Fixed declarations (Sterling Garwood)
   25-Aug-05    RMS     Fixed DIVS case 2^31 / - 1
   14-Jan-04    RMS     Fixed device enable/disable support (Bruce Ray)
   19-Jan-03    RMS     Changed CMASK to CDMASK for Apple Dev Kit conflict
   03-Oct-02    RMS     Added DIB infrastructure
   30-Dec-01    RMS     Added old PC queue
   07-Dec-01    RMS     Revised to use breakpoint package
   30-Nov-01    RMS     Added extended SET/SHOW support
   10-Aug-01    RMS     Removed register in declarations
   17-Jul-01    RMS     Moved function prototype
   26-Apr-01    RMS     Added device enable/disable support
   05-Mar-01    RMS     Added clock calibration
   22-Dec-00    RMS     Added Bruce Ray's second terminal
   15-Dec-00    RMS     Added Charles Owen's CPU bootstrap
   08-Dec-00    RMS     Changes from Bruce Ray
                        -- fixed trap test to include Nova 3
                        -- fixed DIV and DIVS divide by 0
                        -- fixed RETN to set SP from FP
                        -- fixed IORST to preserve carry
                        -- added "secret" Nova 4 PSHN/SAVEN instructions
                        -- added plotter support
   15-Oct-00    RMS     Fixed bug in MDV test, added stack, byte, trap instructions
   14-Apr-98    RMS     Changed t_addr to unsigned
   15-Sep-97    RMS     Added read and write breakpoints

   The register state for the NOVA CPU is:

   AC[0:3]<0:15>        general registers
   C                    carry flag
   PC<0:14>             program counter
   
   The NOVA has three instruction formats: memory reference, I/O transfer,
   and operate.  The memory reference format is:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 0| op  | AC  |in| mode|     displacement      |    memory reference
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   <0:4>        mnemonic        action

   00000        JMP             PC = MA
   00001        JMS             AC3 = PC, PC = MA
   00010        ISZ             M[MA] = M[MA] + 1, skip if M[MA] == 0
   00011        DSZ             M[MA] = M[MA] - 1, skip if M[MA] == 0
   001'n        LDA             ACn   = M[MA]
   010'n        STA             M[MA] = ACn

   <5:7>        mode            action

   000  page zero direct        MA = zext (IR<8:15>)
   001  PC relative direct      MA = PC + sext (IR<8:15>)
   010  AC2 relative direct     MA = AC2 + sext (IR<8:15>)
   011  AC3 relative direct     MA = AC3 + sext (IR<8:15>)
   100  page zero indirect      MA = M[zext (IR<8:15>)]
   101  PC relative indirect    MA = M[PC + sext (IR<8:15>)]
   110  AC2 relative indirect   MA = M[AC2 + sext (IR<8:15>)]
   111  AC3 relative indirect   MA = M[AC3 + sext (IR<8:15>)]

   Memory reference instructions can access an address space of 32K words.
   An instruction can directly reference the first 256 words of memory
   (called page zero), as well as 256 words relative to the PC, AC2, or
   AC3; it can indirectly access all 32K words.  If an indirect address
   is in locations 00020-00027, the indirect address is incremented and
   rewritten to memory before use; if in 00030-00037, decremented and
   rewritten.

   The I/O transfer format is:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 0  1  1| AC  | opcode |pulse|      device     |    I/O transfer
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   The IOT instruction sends the opcode, pulse, and specified AC to the
   specified I/O device.  The device may accept data, provide data,
   initiate or cancel operations, or skip on status.

   The operate format is:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 1|srcAC|dstAC| opcode |shift|carry|nl|  skip  |    operate
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
                   \______/ \___/ \___/  |  |  |  |
                       |      |     |    |  |  |  +--- reverse skip sense
                       |      |     |    |  |  +--- skip if C == 0
                       |      |     |    |  +--- skip if result == 0
                       |      |     |    +--- don't load result
                       |      |     +--- carry in (load as is,
                       |      |                    set to Zero,
                       |      |                    set to One,
                       |      |                    load Complement)
                       |      +--- shift (none,
                       |                  left one,
                       |                  right one,
                       |                  byte swap)
                       +--- operation (complement,
                                       negate,
                                       move,
                                       increment,
                                       add complement,
                                       subtract,
                                       add,
                                       and)

   The operate instruction can be microprogrammed to perform operations
   on the source and destination AC's and the Carry flag.

   Some notes from Bruce Ray:

   1.   DG uses the value of the autoindex location -before- the
        modification to determine if additional indirect address
        levels are to be performed.  Most DG emulators conform to
        this standard, but some vendor machines (i.e. Point 4 Mark 8)
        do not.

   2.   Infinite indirect references may occur on unmapped systems
        and can "hang" the hardware.  Some DG diagnostics perform
        10,000s of references during a single instruction.

   3.   Nova 3 adds the following instructions to the standard Nova
        instruction set:

        trap instructions
        stack push/pop instructions
        save/return instructions
        stack register manipulation instructions
        unsigned MUL/DIV

    4.  Nova 4 adds the following instructions to the Nova 3 instruction
        set:

        signed MUL/DIV
        load/store byte
        secret (undocumented) stack instructions [PSHN, SAVN]

    5.  Nova, Nova 3 and Nova 4 unsigned mul/div instructions are the
        same instruction code values on all machines.

    6.  Undocumented Nova 3 behaviour for LDB, STB and SAVN has been
        added to appropriate code.

    7.  Most 3rd party vendors had a user-controlled method to increase the
        logical address space from 32 KW to 64 KW.  This capability came at
        the expense of disabling multi-level indirect addressing when the 64KW
        mode is in effect, and keeping DG multi-level indirect compatibility
        when 64KW mode is inactive.  The most common implementation was to use
        an "NIOP <ac>,CPU" instruction to control whether 32 KW or 64 KW
        addressing mode was wanted, and <ac> bit 15 (the least-significant bit
        of an accumulator) determined which mode was set:
        0 = 32 KW (DG compatible), 1 = 64 KW.

        This feature has been implemented in our Nova emulation for all to enjoy.


   This routine is the instruction decode routine for the NOVA.
   It is called from the simulator control program to execute
   instructions in simulated memory, starting at the simulated PC.
   It runs until 'reason' is set non-zero.

   General notes:

   1. Reasons to stop.  The simulator can be stopped by:

        HALT instruction
        breakpoint encountered
        infinite indirection loop
        unknown I/O device and STOP_DEV flag set
        I/O error in I/O simulator

   2. Interrupts.  Interrupts are maintained by four parallel variables:

        dev_done        device done flags
        dev_disable     device interrupt disable flags
        dev_busy        device busy flags
        int_req         interrupt requests

      In addition, int_req contains the interrupt enable and ION pending
      flags.  If ION and ION pending are set, and at least one interrupt
      request is pending, then an interrupt occurs.  Note that the 16b PIO
      mask must be mapped to the simulator's device bit mapping.
 
   3. Non-existent memory.  On the NOVA, reads to non-existent memory
      return zero, and writes are ignored.  In the simulator, the
      largest possible memory is instantiated and initialized to zero.
      Thus, only writes need be checked against actual memory size.

   4. Adding I/O devices.  These modules must be modified:

        nova_defs.h     add interrupt request definition
        nova_sys.c      add sim_devices entry
*/

#include "nova_defs.h"


#define PCQ_SIZE        64                              /* must be 2**n */
#define PCQ_MASK        (PCQ_SIZE - 1)
#define PCQ_ENTRY       pcq[pcq_p = (pcq_p - 1) & PCQ_MASK] = PC


#define INCA(x)         (((x) + 1) & AMASK)
#define DECA(x)         (((x) - 1) & AMASK)
#define SEXT(x)         (((x) & SIGN)? ((x) | ~DMASK): (x))
#define STK_CHECK(x,y)  if (((x) & 0377) < (y)) \
                            int_req = int_req | INT_STK
#define IND_STEP(x)     GetMap(x) & A_IND;  /* return next level indicator */ \
                        if ( ((x) <= AUTO_TOP) && ((x) >= AUTO_INC) ) {  \
                            if ( (x) < AUTO_DEC )                        \
                                PutMap(x, (GetMap(x) + 1) & DMASK);      \
                            else                                         \
                                PutMap(x, (GetMap(x) - 1) & DMASK);      \
                            }                                            \
                        x = GetMap(x) & AMASK

#define INCREMENT_PC    PC = (PC + 1) & AMASK           /* increment PC */

#define UNIT_V_MDV      (UNIT_V_UF + 0)                 /* MDV present */
#define UNIT_V_STK      (UNIT_V_UF + 1)                 /* stack instr */
#define UNIT_V_BYT      (UNIT_V_UF + 2)                 /* byte instr */
#define UNIT_V_64KW     (UNIT_V_UF + 3)                 /* 64KW mem support */
#define UNIT_V_MMPU     (UNIT_V_UF + 4)                 /* Nova 840 MMPU */
#define UNIT_V_MSIZE    (UNIT_V_UF + 5)                 /* dummy mask */
#define UNIT_MDV        (1 << UNIT_V_MDV)
#define UNIT_STK        (1 << UNIT_V_STK)
#define UNIT_BYT        (1 << UNIT_V_BYT)
#define UNIT_64KW       (1 << UNIT_V_64KW)
#define UNIT_MMPU       (1 << UNIT_V_MMPU)
#define UNIT_MSIZE      (1 << UNIT_V_MSIZE)
#define UNIT_IOPT       (UNIT_MDV | UNIT_STK | UNIT_BYT | UNIT_64KW | UNIT_MMPU)
#define UNIT_NOVA840    (UNIT_MDV | UNIT_MMPU)
#define UNIT_NOVA3      (UNIT_MDV | UNIT_STK)
#define UNIT_NOVA4      (UNIT_MDV | UNIT_STK | UNIT_BYT)
#define UNIT_KERONIX    (UNIT_MDV | UNIT_64KW)

#define UNIT_V_17B      (UNIT_V_UF + 0)                 /* 17B MAP */
#define UNIT_17B        (1 << UNIT_V_17B)

#define MODE_64K        (cpu_unit.flags & UNIT_64KW)
#define MODE_64K_ACTIVE ((cpu_unit.flags & UNIT_64KW) && (0xFFFF == AMASK))


typedef struct
    {
    int32    pc;
    int16    ir;
    int16    ac0 ;
    int16    ac1 ;
    int16    ac2 ;
    int16    ac3 ;
    int16    carry ;
    int16    sp ;
    int16    fp ;
    int32    devDone ;
    int32    devBusy ;
    int32    devDisable ;
    int32    devIntr ;
    }   Hist_entry ;


uint16 M[MAXMEMSIZE] = { 0 };                           /* memory */
int32 AC[4] = { 0 };                                    /* accumulators */
int32 C = 0;                                            /* carry flag */
int32 PC = 0;
int32 saved_PC = 0;                                     /* program counter */
int32 SP = 0;                                           /* stack pointer */
int32 FP = 0;                                           /* frame pointer */
int32 SR = 0;                                           /* switch register */
int32 dev_done = 0;                                     /* device done flags */
int32 dev_busy = 0;                                     /* device busy flags */
int32 dev_disable = 0;                                  /* int disable flags */
int32 int_req = 0;                                      /* interrupt requests */
int32 int_no_ion_pending = INT_NO_ION_PENDING;
int32 pimask = 0;                                       /* priority int mask */
int32 pwr_low = 0;                                      /* power fail flag */
int32 ind_max = 65536;                                  /* iadr nest limit */
int32 stop_dev = 0;                                     /* stop on ill dev */
uint16 pcq[PCQ_SIZE] = { 0 };                           /* PC queue */
int32 pcq_p = 0;                                        /* PC queue ptr */
REG *pcq_r = NULL;                                      /* PC queue reg ptr */
struct ndev dev_table[64];                              /* dispatch table */
int32 AMASK = 077777 ;                                  /* current memory address mask  */
                                                        /* (default to 32KW)  */
static  int32    hist_p   = 0 ;                         /* history pointer */
static  int32    hist_cnt = 0 ;                         /* history count   */
static  Hist_entry * hist = NULL ;                      /* instruction history */


t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_reset (DEVICE *dptr);
t_stat cpu_set_size (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat cpu_boot (int32 unitno, DEVICE *dptr);
t_stat build_devtab (void);

t_stat hist_set( UNIT * uptr, int32 val, char * cptr, void * desc ) ;
t_stat hist_show( FILE * st, UNIT * uptr, int32 val, void * desc ) ;
static int hist_save( int32 pc, int32 our_ir ) ;
char * devBitNames( int32 flags, char * ptr, char * sepStr ) ;

void mask_out (int32 mask);

int32 GetMap(int32 addr);
int32 PutMap(int32 addr, int32 data);

#define SVR_MAP     0
#define USR_MAP     1
#define DCH_MAP     2

#define MODE_SVR    0
#define MODE_USR    1

#define MAP_M_WP    0200
#define MAP_M_PPAGE 0177

int32 MapMode;          /* MODE_SVR MODE_USR */
int32 MapUse;           /* which map to use */
int32 Map[3][32];       /* WP + PPAGE */
int32 MapDevProt[8];    /* device protection  */
int32 MapProtCtrl;      /* protection control, see LDPC_M_xxx */
int32 MapStatus;        /* Map Status         */
int32 MapPageCheck;     /* Initiate Page Check */
int32 MapInstrAddr;     /* Instr. Address     */
int32 MapInvAddr;       /* Invalid Address    */
int32 MapModeINT;       /* last INT MapMode   */

int32 MapEnable = 0;    /* Enable User Map in progress */
int32 MapSingleCycle = 0;/* Single Cycle in progress */
int32 MapFetchDelay = 0;/* delay MMPU operation (Enable User Map/Enable Single Cycle) */
int32 FetchCycle = 0;   /* FETCH cycle */
/* INT off and clear INT_NO_ION_PENDING mask, this will prevent ION_DELAY */
#define INHIBIT_IO      { int_req &= ~INT_ION; int_no_ion_pending = 0; }

#define LO(x)        ((x) & 07)
#define HI(x)        LO((x) >> 3)

#define DOA_M_TYP    03
#define DOA_V_TYP    14

#define DOA_LDM      0000000    /* LOAD MAP */
#define LDM_M_DCH    0020000
#define LDM_V_LPAGE  8
#define LDM_M_LPAGE  037
#define LDM_M_WP     0000200
#define LDM_M_PPAGE  0000177

#define DOA_LDDP     0040000    /* LOAD DEVICE PROTECTION */
#define LDDP_V_DCLS  8
#define LDDP_M_DCLS  037
#define LDDP_M_DP    0000377

#define DOA_IPC      0100000    /* INITIATE PAGE CHECK */
#define IPC_M_DCH    0020000
#define IPC_V_LPAGE  8
#define IPC_M_LPAGE  037

#define DOA_LDPC     0140000    /* LOAD PROTECTION CONTROL */
#define LDPC_M_DEFER 0020000
#define LDPC_M_WP    0010000
#define LDPC_M_IOP   0004000
#define LDPC_M_DCH   0002000
static char *ldpc_bits[] = {
    "?","?","?",
    "?","?","?",
    "?","?","?",
    "?","DCH","IOP",
    "WP","DEFER","1",
    "1"
};

#define DEFER_PROT   ((USR_MAP == MapUse) && (MapProtCtrl & LDPC_M_DEFER))
#define WRITE_PROT   ((USR_MAP == MapUse) && (MapProtCtrl & LDPC_M_WP))
#define IO_PROT      ((USR_MAP == MapUse) && (MapProtCtrl & LDPC_M_IOP))
#define DCH_ENABLED  (MapProtCtrl & LDPC_M_DCH)
#define VALID_PROT   ((USR_MAP == MapUse) && (MapStatus & MAP_STA_V))
#define IO_V_JMP     040
#define DEFER_WR_JMP 041

#define MAP_STA_USR   0100000    /* READ STATUS */
#define MAP_STA_WR    0040000
#define MAP_STA_IO    0020000
#define MAP_STA_V     0010000
#define MAP_STA_SIM   0004000
#define MAP_STA_RSVD  0002000
#define MAP_STA_DEFER 0001000
#define MAP_STA_FP    0000400
#define MAP_STA_WP    0000200
#define MAP_STA_PPAGE 0000177

static char *map_sta_bits[] = {
    "","","",
    "","","",
    "","WP","FP",
    "DEFER","?","SIM",
    "V","IO","WR",
    "USR"
};


int32 mmpu_trace = 0;
#define MMPU_TRACE(x)   (mmpu_trace & (1 << (x)))

extern void decode_bits(char *msg, int32 x, char** bits);

void decode_ldpc(char *msg, int32 x)
{
    if (MMPU_TRACE(2)) {
        decode_bits(msg, x, ldpc_bits);
        }
}

void decode_map_sta(char *msg, int32 x)
{
    if (MMPU_TRACE(2)) {
        printf(" PAGE=%03o", x & 0177);
        decode_bits(msg, x, map_sta_bits);
        }
}

t_stat mmpu_reset(DEVICE *dptr)
{
    int i;

    MapMode = MODE_SVR;         /* supervisor mode */
    MapProtCtrl &= ~LDPC_M_DCH; /* data channel map disabled */
    for (i = 0; i < 32; i++) {
        Map[SVR_MAP][i] = i;    /* logical page 31 mapped to physical page 31 */
        Map[DCH_MAP][i] = i;
        }
    Map[USR_MAP][31] = 31;
    MapUse = SVR_MAP;           /* use SVR map */
    return SCPE_OK;
}

t_stat mmpu_svc (UNIT *uptr)
{
    return SCPE_OK;
}

int32 mmpu(int32 pulse, int32 code, int32 AC)
{
    int32 rval, page;
    int32 devcls;
    char trace_buf[128];

    static char * f[8] =
        { "NIO", "DIA", "DOA", "DIB", "DOB", "DIC", "DOC", "SKP" } ;
    static char * s[4] =
        { " ", "S", "C", "P" } ;

    if (MMPU_TRACE(0)) {
        sprintf( trace_buf, "  [MMPU  %s%s %06o ", f[code & 0x07], s[pulse & 0x03], (AC & 0xFFFF) ) ;
        }

    rval = 0;
    switch (code) {
    case ioDIA: /* READ INSTR. ADDRESS */
        rval = MapInstrAddr & 077777;
        if (MMPU_TRACE(1)) {
            printf("RDINSTA %06o", rval);
            }
        break;
    case ioDOA:
        switch ((AC >> DOA_V_TYP) & DOA_M_TYP) {
        case 0: /* LOAD MAP */
            if ((AC & LDM_M_DCH) && (AC & LDM_M_WP))
                AC &= ~LDM_M_WP;
            page = (AC >> LDM_V_LPAGE) & LDM_M_LPAGE;
            if (MMPU_TRACE(1)) {
                printf("LDM %06o (%s %02o:%03o)", AC, AC & LDM_M_DCH ? "DCH" : "USR", page, 0377 & AC);
                }
            if (AC & LDM_M_DCH) {
                Map[DCH_MAP][page] = 0177 & AC;
            } else {
                Map[USR_MAP][page] = 0377 & AC;
                if (31 == page)
                    Map[SVR_MAP][31] = Map[USR_MAP][31];
            }
            break;
        case 1: /* LOAD DEVICE PROTECTION */
            devcls = (AC >> LDDP_V_DCLS) & LDDP_M_DCLS;
            if (MMPU_TRACE(1)) {
                printf("LDDP %06o (page %02o %03o)", AC, devcls, AC & LDDP_M_DP);
                }
            MapDevProt[devcls] = AC & LDDP_M_DP;
            break;
        case 2: /* INITIATE PAGE CHECK */
            if (MMPU_TRACE(1)) {
                printf("IPC %06o", AC);
                }
            MapPageCheck = AC;
            break;
        case 3: /* LOAD PROTECTION CONTROL */
            MapProtCtrl = AC;
            if (MMPU_TRACE(1)) {
                printf("LDPC %06o", AC);
                decode_ldpc("LDPC", AC);
                }
            ind_max = (LDPC_M_DEFER & MapProtCtrl) ? 16 : 65536;
            break;
        }
        break;
    case ioDIB: /* READ INVALID ADDRESS */
        rval = MapInvAddr;
        if (MMPU_TRACE(1)) {
            printf("RDINVA %06o", rval);
            }
        break;
    case ioDOB:
        break;
    case ioDIC: /* READ STATUS */
#define MAP_STA_WP    0000200
#define MAP_STA_PPAGE 0000177
        rval = MapStatus & ~0377;
        if (MODE_USR == MapModeINT) rval |= MAP_STA_USR;
        page = (MapPageCheck >> IPC_V_LPAGE) & IPC_M_LPAGE;
        rval |= Map[(MapPageCheck & IPC_M_DCH) ? DCH_MAP : USR_MAP][page];
        if (MMPU_TRACE(1)) {
            printf("RDSTA %06o", rval);
            decode_map_sta("STATUS",rval);
            }
        break;
    case ioDOC:
        break;
    }
    
    if (ioNIO == code) {
        switch (pulse) {
        case iopS: /* ENABLE USER MAP */
            if (MMPU_TRACE(2)) {
                printf("ENUM");
                }
            MapEnable = 1; /* 3 cycles */
            MapFetchDelay = 3;
            break;
        case iopP: /* ENABLE SINGLE CYCLE */
            if (MMPU_TRACE(2)) {
                printf("ENSC");
                }
            MapSingleCycle = 1; /* 2 cycles */
            MapFetchDelay = 2;
            /* no protection features are enabled, uses USR_MAP, then after execute SVR_MAP */
            /* - write prot. cannot be set for DCH map  */
            /* - validity prot. cannot be disabled      */
            /* - no protection => will not trap?        */
            /* - what is the purpose of the STA_SIM?    */
            /* - what happens if an error occurs during SingleCycle? */
            /* - what happens if an error occurs during Page31 mapping in SVR_MODE ? */
            MapStatus = 0;
            break;
        case iopC: /* SUPERVISOR CALL */
            if (MMPU_TRACE(2)) {
                printf("SVC");
                }
            INHIBIT_IO;
            MapMode = MODE_SVR; MapUse = SVR_MAP;
            PC = 042;
        }
        }

    if (MMPU_TRACE(0)) {
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

/* 0 - SVR, 1 - USR, 2 - DCH map, 3 - DevProt */
t_stat mmpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
if ((addr & 077) <= 07 && 3 == (addr >> 6)) {
    if (vptr != NULL) *vptr = MapDevProt[addr & 077] & 0377;
    return SCPE_OK;
    }
else if ((addr & 077) <= 037 && addr <= 0237) {
    if (vptr != NULL) *vptr = Map[(addr >> 6) & 3][addr & 077] & 0377;
    return SCPE_OK;
    }
/* uptr->u4 = -2;  signal to print_sys in eclipse_sys.c: do not map */
return SCPE_NXM;
}

/* Memory deposit */

t_stat mmpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
if ((addr & 077) <= 07 && 3 == (addr >> 6)) {
    MapDevProt[addr & 037] = (int32)val & 0377;
    return SCPE_OK;
    }
/* uptr->u4 = -2; signal to print_sys in eclipse_sys.c: do not map */
else if ((addr & 077) <= 037 && addr <= 0237) {
    Map[(addr >> 6) & 3][addr & 037] = (int32)val & 0377;
    return SCPE_OK;
    }
return SCPE_NXM;
}


/* CPU data structures

   cpu_dev      CPU device descriptor
   cpu_unit     CPU unit descriptor
   cpu_reg      CPU register list
   cpu_mod      CPU modifiers list
*/

UNIT cpu_unit = {
    UDATA (NULL, UNIT_FIX+UNIT_BINK+UNIT_MDV,  DFTMEMSIZE /* MAXMEMSIZE */ )
    };

REG cpu_reg[] = {
    { ORDATA (PC, saved_PC, 15) },
    { ORDATA (AC0, AC[0], 16) },
    { ORDATA (AC1, AC[1], 16) },
    { ORDATA (AC2, AC[2], 16) },
    { ORDATA (AC3, AC[3], 16) },
    { FLDATA (C, C, 16) },
    { ORDATA (SP, SP, 16) },
    { ORDATA (FP, FP, 16) },
    { ORDATA (SR, SR, 16) },
    { ORDATA (PI, pimask, 16) },
    { FLDATA (ION, int_req, INT_V_ION) },
    { FLDATA (ION_DELAY, int_req, INT_V_NO_ION_PENDING) },
    { FLDATA (STKOVF, int_req, INT_V_STK) },
    { FLDATA (PWR, pwr_low, 0) },
    { ORDATA (INT, int_req, INT_V_ION+1), REG_RO },
    { ORDATA (BUSY, dev_busy, INT_V_ION+1), REG_RO },
    { ORDATA (DONE, dev_done, INT_V_ION+1), REG_RO },
    { ORDATA (DISABLE, dev_disable, INT_V_ION+1), REG_RO },
    { FLDATA (STOP_DEV, stop_dev, 0) },
    { DRDATA (INDMAX, ind_max, 32), REG_NZ + PV_LEFT },
    { ORDATA (AMASK, AMASK, 16) },
    { DRDATA (MEMSIZE, cpu_unit.capac, 32), REG_NZ + PV_LEFT },
    { BRDATA (PCQ, pcq, 8, 16, PCQ_SIZE), REG_RO+REG_CIRC },
    { ORDATA (PCQP, pcq_p, 6), REG_HRO },
    { ORDATA (WRU, sim_int_char, 8) },
    { NULL }
    };

MTAB cpu_mod[] = {
    { UNIT_IOPT, UNIT_NOVA840, "NOVA840", "NOVA840", NULL },
    { UNIT_IOPT, UNIT_NOVA3, "NOVA3", "NOVA3", NULL },
    { UNIT_IOPT, UNIT_NOVA4, "NOVA4", "NOVA4", NULL },
    { UNIT_IOPT, UNIT_KERONIX, "KERONIX", "KERONIX", NULL },
    { UNIT_IOPT, UNIT_MDV,   "MDV",   "MDV",   NULL },
    { UNIT_IOPT, UNIT_MMPU,  "MMPU",  "MMPU",   NULL },
    { UNIT_IOPT, UNIT_64KW,  "EXT64KW", "EXT64KW",  NULL },
    { UNIT_IOPT,        0,   "none",  "NONE",  NULL },
    { UNIT_MSIZE, ( 4 * 1024), NULL,  "4K", &cpu_set_size },
    { UNIT_MSIZE, ( 8 * 1024), NULL,  "8K", &cpu_set_size },
    { UNIT_MSIZE, (12 * 1024), NULL, "12K", &cpu_set_size },
    { UNIT_MSIZE, (16 * 1024), NULL, "16K", &cpu_set_size },
    { UNIT_MSIZE, (20 * 1024), NULL, "20K", &cpu_set_size },
    { UNIT_MSIZE, (24 * 1024), NULL, "24K", &cpu_set_size },
    { UNIT_MSIZE, (28 * 1024), NULL, "28K", &cpu_set_size },
    { UNIT_MSIZE, (32 * 1024), NULL, "32K", &cpu_set_size },
    { UNIT_MSIZE, (36 * 1024), NULL, "36K", &cpu_set_size },
    { UNIT_MSIZE, (40 * 1024), NULL, "40K", &cpu_set_size },
    { UNIT_MSIZE, (44 * 1024), NULL, "44K", &cpu_set_size },
    { UNIT_MSIZE, (48 * 1024), NULL, "48K", &cpu_set_size },
    { UNIT_MSIZE, (52 * 1024), NULL, "52K", &cpu_set_size },
    { UNIT_MSIZE, (56 * 1024), NULL, "56K", &cpu_set_size },
    { UNIT_MSIZE, (60 * 1024), NULL, "60K", &cpu_set_size },
    { UNIT_MSIZE, (64 * 1024), NULL, "64K", &cpu_set_size },
    { UNIT_MSIZE, (128 * 1024), NULL, "128K", &cpu_set_size },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO|MTAB_SHP, 0, "HISTORY", "HISTORY",
      &hist_set, &hist_show },

    { 0 }
    };

DEVICE cpu_dev = {
    "CPU", &cpu_unit, cpu_reg, cpu_mod,
    1, 8, 16 /* = 64 KW, 15 = 32KW */, 1, 8, 16,
    &cpu_ex, &cpu_dep, &cpu_reset,
    NULL, NULL, NULL
    };
    
DIB mmpu_dib = { DEV_MMPU, 0, 0, &mmpu };
    
UNIT mmpu_unit = { UDATA (NULL, UNIT_17B, MAXMEMSIZE) };

REG mmpu_reg[] = {
    { ORDATA (STATUS, MapStatus, 16) },
    { ORDATA (ENABLE, MapUse, 16) },
    { ORDATA (ACTIVE, MapUse, 16) },
    { ORDATA (CYCLE, MapSingleCycle, 16) },
    { ORDATA (CHECK, MapPageCheck, 16) },
    { DRDATA (TRACE, mmpu_trace,   32) },
    { NULL }
};

MTAB mmpu_mod[] = {
    { UNIT_17B, UNIT_17B, "17bit", "17B", NULL },
    { 0 }
};

DEVICE mmpu_dev = {
    "MMPU", &mmpu_unit, mmpu_reg, mmpu_mod,
    1, 8, 17, 1, 8, 16,
    &mmpu_ex, &mmpu_dep, &mmpu_reset,
    NULL, NULL, NULL,
    &mmpu_dib, DEV_DISABLE
};


#include <setjmp.h>
static jmp_buf MapTrap;
#define TRAP(pc)    { if (MapSingleCycle) MapStatus |= MAP_STA_SIM; if (USR_MAP == MapUse) longjmp(MapTrap, pc); }

t_stat sim_instr (void)
{
int32 IR, i;
t_stat reason;

/* Restore register state */

if (build_devtab () != SCPE_OK)                         /* build dispatch */
    return SCPE_IERR;
PC = saved_PC & AMASK;                                  /* load local PC */
C = C & CBIT;
mask_out (pimask);                                      /* reset int system */
reason = 0;

/* Main instruction fetch/decode loop */

if ((i = setjmp(MapTrap))) {                            /* MMPU trap */
    INHIBIT_IO;
    MapMode = MODE_SVR; MapUse = SVR_MAP;
    PC = i;
    reason = 0;
}

while (reason == 0) {                                   /* loop until halted */

    if (sim_interval <= 0) {                            /* check clock queue */
        if ( (reason = sim_process_event ()) )
            break;
        }

    if (VALID_PROT) {                                   /* validity protection */
        INHIBIT_IO;                                 /* during defer/execute cycle */
        MapMode = MODE_SVR; MapUse = SVR_MAP;
        PC = IO_V_JMP;
        }

    if (!MapFetchDelay && MapSingleCycle) {             /* end of Single Cycle */
        MapUse = SVR_MAP;
        MapSingleCycle = 0;
        }
        
    if (int_req > INT_PENDING) {                        /* interrupt or exception? */
        int32 MA, indf;

        if (int_req & INT_TRAP) {                       /* trap instruction? */
            int_req = int_req & ~INT_TRAP ;             /* clear */
            PCQ_ENTRY;                                  /* save old PC */
            PutMap(TRP_SAV, (PC - 1) & AMASK);
            MA = TRP_JMP;                               /* jmp @47 */
            }
        else {
            int_req = int_req & ~INT_ION;               /* intr off */
            PCQ_ENTRY;                                  /* save old PC */
            PutMap(INT_SAV, PC);                        /* save in logical 0 */
            MapModeINT = MapMode;
            MapMode = MODE_SVR; MapUse = SVR_MAP;
            if (int_req & INT_STK) {                    /* stack overflow? */
                int_req = int_req & ~INT_STK;           /* clear */
                MA = STK_JMP;                           /* jmp @3 */
                }
            else
                MA = INT_JMP;                           /* intr: jmp @1 */
        }
        if ( MODE_64K_ACTIVE ) {
            indf = IND_STEP (MA);
            }
        else
            {
            for (i = 0, indf = 1; indf && (i < ind_max); i++) {
                indf = IND_STEP (MA);                       /* indirect loop */
                }
            if (i >= ind_max) {
                reason = STOP_IND_INT;
                break;
                }
            }
        PC = MA;
    }                                                   /* end interrupt */

    if (sim_brk_summ && sim_brk_test (PC, SWMASK ('E'))) { /* breakpoint? */
        reason = STOP_IBKPT;                            /* stop simulation */
        break;
        }

    MapInstrAddr = PC;                                  /* save instr. address */
    FetchCycle = 1; IR = GetMap(PC); FetchCycle = 0;    /* fetch instr */
    
    if ( hist_cnt )
        {
        hist_save( PC, IR ) ;                           /*  PC, int_req unchanged */
        }

    INCREMENT_PC ;
    int_req = int_req | INT_NO_ION_PENDING;             /* clear ION delay */
    sim_interval = sim_interval - 1;

/* Operate instruction */

    if (IR & I_OPR) {                                   /* operate? */
        int32 src, srcAC, dstAC;

        srcAC = I_GETSRC (IR);                          /* get reg decodes */
        dstAC = I_GETDST (IR);
        switch (I_GETCRY (IR)) {                        /* decode carry */
        case 0:                                         /* load */
            src = AC[srcAC] | C;
            break;
        case 1:                                         /* clear */
            src = AC[srcAC];
            break;
        case 2:                                         /* set */
            src = AC[srcAC] | CBIT;
            break;
        case 3:                                         /* complement */
            src = AC[srcAC] | (C ^ CBIT);
            break;
            }                                           /* end switch carry */

        switch (I_GETALU (IR)) {                        /* decode ALU */
        case 0:                                         /* COM */
            src = src ^ DMASK;
            break;
        case 1:                                         /* NEG */
            src = ((src ^ DMASK) + 1) & CDMASK;
            break;
        case 2:                                         /* MOV */
            break;
        case 3:                                         /* INC */
            src = (src + 1) & CDMASK;
            break;
        case 4:                                         /* ADC */
            src = ((src ^ DMASK) + AC[dstAC]) & CDMASK;
            break;
        case 5:                                         /* SUB */
            src = ((src ^ DMASK) + AC[dstAC] + 1) & CDMASK;
            break;
        case 6:                                         /* ADD */
            src = (src + AC[dstAC]) & CDMASK;
            break;
        case 7:                                         /* AND */
            src = src & (AC[dstAC] | CBIT);
            break;
            }                                           /* end switch oper */

        switch (I_GETSHF (IR)) {                        /* decode shift */
        case 0:                                         /* nop */
            break;
        case 1:                                         /* L */
            src = ((src << 1) | (src >> 16)) & CDMASK;
            break;
        case 2:                                         /* R */
            src = ((src >> 1) | (src << 16)) & CDMASK;
            break;
        case 3:                                         /* S */
            src = ((src & 0377) << 8) | ((src >> 8) & 0377) |
                (src & CBIT);
            break;
            }                                           /* end switch shift */

        switch (I_GETSKP (IR)) {                        /* decode skip */
        case 0:                                         /* nop */
            if ((IR & I_NLD) && (cpu_unit.flags & UNIT_STK)) {
                int_req = int_req | INT_TRAP ;           /* Nova 3 or 4 trap */
                continue ;
                }
            break;
        case 1:                                         /* SKP */
            INCREMENT_PC ;
            break;
        case 2:                                         /* SZC */
            if (src < CBIT)
                INCREMENT_PC ;
            break;
        case 3:                                         /* SNC */
            if (src >= CBIT)
                INCREMENT_PC ;
            break;
        case 4:                                         /* SZR */
            if ((src & DMASK) == 0)
                INCREMENT_PC ;
            break;
        case 5:                                         /* SNR */
            if ((src & DMASK) != 0)
                INCREMENT_PC ;
            break;
        case 6:                                         /* SEZ */
            if (src <= CBIT)
                INCREMENT_PC ;
            break;
        case 7:                                         /* SBN */
            if (src > CBIT)
                INCREMENT_PC ;
            break;
            }                                           /* end switch skip */
        if ((IR & I_NLD) == 0) {                        /* load? */
            AC[dstAC] = src & DMASK;
            C = src & CBIT;
            }                                           /* end if load */
        }                                               /* end if operate */

/* Memory reference instructions */

    else if (IR < 060000) {                             /* mem ref? */
        int32 src, MA, indf;

        MA = I_GETDISP (IR);                            /* get disp */
        switch (I_GETMODE (IR)) {                       /* decode mode */
        case 0:                                         /* page zero */
            break;
        case 1:                                         /* PC relative */
            if (MA & DISPSIGN)
                MA = 0177400 | MA;
            MA = (MA + PC - 1) & AMASK;
            break;
        case 2:                                         /* AC2 relative */
            if (MA & DISPSIGN)
                MA = 0177400 | MA;
            MA = (MA + AC[2]) & AMASK;
            break;
        case 3:                                         /* AC3 relative */
            if (MA & DISPSIGN)
                MA = 0177400 | MA;
            MA = (MA + AC[3]) & AMASK;
            break;
            }                                           /* end switch mode */

        if ( (indf = IR & I_IND) ) {                    /* indirect? */
            if ( MODE_64K_ACTIVE ) {                    /* 64k mode? */
                indf = IND_STEP (MA);
                }
            else                                        /* compat mode */
                {
                 for (i = 0; indf && (i < ind_max); i++) {   /* count */
                    indf = IND_STEP (MA);               /* resolve indirect */
                }
                if (i >= ind_max) {                     /* too many? */
                    if (DEFER_PROT) {
                        MapStatus |= MAP_STA_DEFER;
                        MapInvAddr = indf;
                        TRAP(DEFER_WR_JMP);
                        }
                    else
                        reason = STOP_IND;
                    break;
                }
            }
        }

        switch (I_GETOPAC (IR)) {                       /* decode op + AC */
        case 001:                                       /* JSR */
            AC[3] = PC;
        case 000:                                       /* JMP */
            PCQ_ENTRY;
            PC = MA;
            break;
        case 002:                                       /* ISZ */
            src = (GetMap(MA) + 1) & DMASK;
            if (MEM_ADDR_OK(MA))
                PutMap(MA, src);
            if (src == 0)
                INCREMENT_PC ;
            break;
        case 003:                                       /* DSZ */
            src = (GetMap(MA) - 1) & DMASK;
            if (MEM_ADDR_OK(MA))
                PutMap(MA, src);
            if (src == 0)
                INCREMENT_PC ;
            break;
        case 004:                                       /* LDA 0 */
            AC[0] = GetMap(MA);
            break;
        case 005:                                       /* LDA 1 */
            AC[1] = GetMap(MA);
            break;
        case 006:                                       /* LDA 2 */
            AC[2] = GetMap(MA);
            break;
        case 007:                                       /* LDA 3 */
            AC[3] = GetMap(MA);
            break;
        case 010:                                       /* STA 0 */
            if (MEM_ADDR_OK(MA))
                PutMap(MA, AC[0]);
            break;
        case 011:                                       /* STA 1 */
            if (MEM_ADDR_OK(MA))
                PutMap(MA, AC[1]);
            break;
        case 012:                                       /* STA 2 */
            if (MEM_ADDR_OK(MA))
                PutMap(MA, AC[2]);
            break;
        case 013:                                       /* STA 3 */
            if (MEM_ADDR_OK(MA))
                PutMap(MA, AC[3]);
            break;
            }                                           /* end switch */
        }                                               /* end mem ref */

/* IOT instruction */

    else {                                              /* IOT */
        int32 dstAC, pulse, code, device, iodata;

        dstAC = I_GETDST (IR);                          /* decode fields */
        code = I_GETIOT (IR);
        pulse = I_GETPULSE (IR);
        device = I_GETDEV (IR);
        if (IO_PROT) {
            if (MapDevProt[HI(code)] & (1 << (7 - LO(code)))) {
                MapStatus |= MAP_STA_IO;
                MapInvAddr = MapInstrAddr;
                TRAP(IO_V_JMP);
            }
        }
        if (code == ioSKP) {                            /* IO skip? */
            switch (pulse) {                            /* decode IR<8:9> */

            case 0:                                     /* skip if busy */
                if ((device == DEV_CPU)? (int_req & INT_ION) != 0:
                    (dev_busy & dev_table[device].mask) != 0)
                    INCREMENT_PC ;
                break;

            case 1:                                     /* skip if not busy */
                if ((device == DEV_CPU)? (int_req & INT_ION) == 0:
                    (dev_busy & dev_table[device].mask) == 0)
                    INCREMENT_PC ;
                break;

            case 2:                                     /* skip if done */
                if ((device == DEV_CPU)? pwr_low != 0:
                    (dev_done & dev_table[device].mask) != 0)
                    INCREMENT_PC ;
                break;

            case 3:                                     /* skip if not done */
                if ((device == DEV_CPU)? pwr_low == 0:
                    (dev_done & dev_table[device].mask) == 0)
                    INCREMENT_PC ;
                break;
                }                                       /* end switch */
            }                                           /* end IO skip */

 /* Hmm, this means a Nova 3 _must_ have DEV_MDV enabled - not true in DG land  */

        else if (device == DEV_MDV) {
            switch (code) {                             /* case on opcode */

            case ioNIO:                                 /* frame ptr */
                if (cpu_unit.flags & UNIT_STK) {
                    if (pulse == iopN)
                        FP = AC[dstAC] & AMASK ;
                    if (pulse == iopC)
                        AC[dstAC] = FP & AMASK ;
                    }
                break;

            case ioDIA:                                 /* load byte */
                if (cpu_unit.flags & UNIT_BYT)
                    {
                    AC[dstAC] = (GetMap(AC[pulse] >> 1) >> ((AC[pulse] & 1)? 0: 8)) & 0377 ;
                    }
                else if (cpu_unit.flags & UNIT_STK)  /*  if Nova 3 this is really a SAV... 2007-Jun-01, BKR  */
                    {
                    SP = INCA (SP);
                    if (MEM_ADDR_OK (SP))
                        PutMap(SP, AC[0]);
                    SP = INCA (SP);
                    if (MEM_ADDR_OK (SP))
                        PutMap(SP, AC[1]);
                    SP = INCA (SP);
                    if (MEM_ADDR_OK (SP))
                        PutMap(SP, AC[2]);
                    SP = INCA (SP);
                    if (MEM_ADDR_OK (SP))
                        PutMap(SP, FP);
                    SP = INCA (SP);
                    if (MEM_ADDR_OK (SP))
                        PutMap(SP, (C >> 1) | (AC[3] & AMASK));
                    AC[3] = FP = SP & AMASK;  
                    STK_CHECK (SP, 5);
                    }
                else
                    {
                    AC[dstAC] = 0;
                    }
                break;

            case ioDOA:                                 /* stack ptr */
                if (cpu_unit.flags & UNIT_STK) {
                    if (pulse == iopN)
                        SP = AC[dstAC] & AMASK;
                    if (pulse == iopC)
                        AC[dstAC] = SP & AMASK;
                    }
                break;

            case ioDIB:                                 /* push, pop */
                if (cpu_unit.flags & UNIT_STK) {
                    if (pulse == iopN) {                /* push (PSHA) */
                        SP = INCA (SP);
                        if (MEM_ADDR_OK (SP))
                            PutMap(SP, AC[dstAC]);
                        STK_CHECK (SP, 1);
                        }
                    if ((pulse == iopS) &&              /* Nova 4 pshn (PSHN) */
                        (cpu_unit.flags & UNIT_BYT)) {
                        SP = INCA (SP);
                        if (MEM_ADDR_OK (SP))
                            PutMap(SP, AC[dstAC]);
                        if ( (SP & 0xFFFF) > (GetMap(042) & 0xFFFF) )
                            {
                            int_req = int_req | INT_STK ;
                            }
                        }
                    if (pulse == iopC) {                /* pop (POPA) */
                        AC[dstAC] = GetMap(SP);
                        SP = DECA (SP);
                        }
                    }
                break;

            case ioDOB:                                 /* store byte */
                if (cpu_unit.flags & UNIT_BYT)
                  {
                    int32 MA, val;
                   MA = AC[pulse] >> 1;
                    val = AC[dstAC] & 0377;
                    if (MEM_ADDR_OK (MA)) PutMap(MA, (AC[pulse] & 1)?
                      ((GetMap(MA) & ~0377) | val)
                    : ((GetMap(MA) &  0377) | (val << 8)));
                    }
                else if (cpu_unit.flags & UNIT_STK)  /*  if Nova 3 this is really a SAV... 2007-Jun-01, BKR  */
                    {
                    SP = INCA (SP);
                    if (MEM_ADDR_OK (SP))
                        PutMap(SP, AC[0]);
                    SP = INCA (SP);
                    if (MEM_ADDR_OK (SP))
                        PutMap(SP, AC[1]);
                    SP = INCA (SP);
                    if (MEM_ADDR_OK (SP))
                        PutMap(SP, AC[2]);
                    SP = INCA (SP);
                    if (MEM_ADDR_OK (SP))
                        PutMap(SP, FP);
                    SP = INCA (SP);
                    if (MEM_ADDR_OK (SP))
                        PutMap(SP, (C >> 1) | (AC[3] & AMASK));
                    AC[3] = FP = SP & AMASK;  
                    STK_CHECK (SP, 5);
                    }
                break;

            case ioDIC:                                 /* save, return */
                if (cpu_unit.flags & UNIT_STK) {
                    if (pulse == iopN) {                /* save */
                        SP = INCA (SP);
                        if (MEM_ADDR_OK (SP))
                            PutMap(SP, AC[0]);
                        SP = INCA (SP);
                        if (MEM_ADDR_OK (SP))
                            PutMap(SP, AC[1]);
                        SP = INCA (SP);
                        if (MEM_ADDR_OK (SP))
                            PutMap(SP, AC[2]);
                        SP = INCA (SP);
                        if (MEM_ADDR_OK (SP))
                            PutMap(SP, FP);
                        SP = INCA (SP);
                        if (MEM_ADDR_OK (SP))
                            PutMap(SP, (C >> 1) | (AC[3] & AMASK));
                        AC[3] = FP = SP & AMASK;  
                        STK_CHECK (SP, 5);
                        }
                    else if (pulse == iopC) {                /* retn */
                        PCQ_ENTRY;
                        SP = FP & AMASK;
                        C = (GetMap(SP) << 1) & CBIT;
                        PC = GetMap(SP) & AMASK;
                        SP = DECA (SP);
                        AC[3] = GetMap(SP);
                        SP = DECA (SP);
                        AC[2] = GetMap(SP);
                        SP = DECA (SP);
                        AC[1] = GetMap(SP);
                        SP = DECA (SP);
                        AC[0] = GetMap(SP);
                        SP = DECA (SP);
                        FP = AC[3] & AMASK;
                        }
                    else if ((pulse == iopS) &&              /* Nova 4 SAVN */
                        (cpu_unit.flags & UNIT_BYT)) {
                        int32 frameSz = GetMap(PC) ;
                        PC = INCA (PC) ;
                        SP = INCA (SP);
                        if (MEM_ADDR_OK (SP))
                            PutMap(SP, AC[0]);
                        SP = INCA (SP);
                        if (MEM_ADDR_OK (SP))
                            PutMap(SP, AC[1]);
                        SP = INCA (SP);
                        if (MEM_ADDR_OK (SP))
                            PutMap(SP, AC[2]);
                        SP = INCA (SP);
                        if (MEM_ADDR_OK (SP))
                            PutMap(SP, FP);
                        SP = INCA (SP);
                        if (MEM_ADDR_OK (SP))
                            PutMap(SP, (C >> 1) | (AC[3] & AMASK));
                        AC[3] = FP = SP & AMASK ;
                        SP = (SP + frameSz) & AMASK ;
                        if (SP > GetMap(042))
                            {
                            int_req = int_req | INT_STK;
                            }
                        }
                    }
                break;

            case ioDOC:
                if ((dstAC == 2) && (cpu_unit.flags & UNIT_MDV))
                    {  /*  Nova, Nova3 or Nova 4  */
                    uint32 mddata, uAC0, uAC1, uAC2;

                    uAC0 = (uint32) AC[0];
                    uAC1 = (uint32) AC[1];
                    uAC2 = (uint32) AC[2];
                    if (pulse == iopP)
                        {                /* mul */
                        mddata = (uAC1 * uAC2) + uAC0;
                        AC[0]  = (mddata >> 16) & DMASK;
                        AC[1]  = mddata & DMASK;
                        }
                    if (pulse == iopS)
                        {                /* div */
                        if ((uAC0 >= uAC2) || (uAC2 == 0))
                            {
                            C = CBIT;
                            }
                        else
                            {
                            C = 0;
                            mddata = (uAC0 << 16) | uAC1;
                            AC[1]  = mddata / uAC2;
                            AC[0]  = mddata % uAC2;
                            }
                        }
                    }
                else if ((dstAC == 3) && (cpu_unit.flags & UNIT_BYT) /* assuming UNIT_BYT = Nova 4 */)
                    {
                    int32 mddata;
                    if (pulse == iopC)
                        {                /* muls */
                        mddata = (SEXT (AC[1]) * SEXT (AC[2])) + SEXT (AC[0]);
                        AC[0]  = (mddata >> 16) & DMASK;
                        AC[1]  = mddata & DMASK;
                        }
                    else if (pulse == iopN)
                        {                /* divs */
                        if ((AC[2] == 0) ||             /* overflow? */
                            ((AC[0] == 0100000) && (AC[1] == 0) && (AC[2] == 0177777)))
                            {
                            C = CBIT;
                            }
                        else
                            {
                            mddata = (SEXT (AC[0]) << 16) | AC[1];
                            AC[1]  = mddata / SEXT (AC[2]);
                            AC[0]  = mddata % SEXT (AC[2]);
                            if ((AC[1] > 077777) || (AC[1] < -0100000))
                                {
                                C = CBIT;
                                }
                            else
                                {
                                C = 0;
                                }
                            AC[0] = AC[0] & DMASK;
                            }
                        }
                    }
                else if ((dstAC == 3) && (cpu_unit.flags & UNIT_STK))  /*  if Nova 3 this is really a PSHA... 2007-Jun-01, BKR  */
                    {
                    SP = INCA (SP);
                    if (MEM_ADDR_OK (SP))
                        PutMap(SP, AC[dstAC]);
                    STK_CHECK (SP, 1);
                    }
                break;
                }                                       /* end case code */
            }                                           /* end if mul/div */

        else if (device == DEV_CPU) {                   /* CPU control */
            switch (code) {                             /* decode IR<5:7> */

            case ioNIO:                                 /* NIOP <x> CPU ? */
                if ( pulse == iopP )
                    if ( MODE_64K )
                    {
                        /*  Keronix/Point4/SCI/INI/IDP (and others)    */
                        /*  64 KW memory extension:                    */
                        /*  NIOP - set memory mode (32/64 KW) per AC:  */
                        /*  B15: 0 = 32 KW, 1 = 64 KW mode             */
                        AMASK = (AC[dstAC] & 0x0001) ? 0177777 : 077777 ;
                    }
                break ;

            case ioDIA:                                 /* read switches */
                AC[dstAC] = SR;
                break;

            case ioDIB:                                 /* int ack */
                AC[dstAC] = 0;
                DEV_UPDATE_INTR ;
                iodata = int_req & (-int_req);
                for (i = DEV_LOW; i <= DEV_HIGH; i++)  {
                    if (iodata & dev_table[i].mask) {
                        AC[dstAC] = i;
                        break;
                        }
                    }
                break;

            case ioDOB:                                 /* mask out */
                mask_out (pimask = AC[dstAC]);
                break;

            case ioDIC:                                 /* io reset */
                reset_all (0);                          /* reset devices */
                mask_out( 0 ) ;                         /* clear all device masks  */
                AMASK = 077777 ;                        /* reset memory mode */
                break;

            case ioDOC:                                 /* halt */
                reason = STOP_HALT;
                break;
                }                                       /* end switch code */

            switch (pulse) {                            /* decode IR<8:9> */

            case iopS:                                  /* ion */
                int_req = (int_req | INT_ION) & ~int_no_ion_pending;
                break;

            case iopC:                                  /* iof */
                int_req = int_req & ~INT_ION;
                int_no_ion_pending = INT_NO_ION_PENDING;
                break;
                }                                       /* end switch pulse */
            }                                           /* end CPU control */

        else if (dev_table[device].routine) {           /* normal device */
            iodata = dev_table[device].routine (pulse, code, AC[dstAC]);
            reason = iodata >> IOT_V_REASON;
            if (code & 1)
                AC[dstAC] = iodata & 0177777;
            }

/* bkr, 2007-May-30
 *    if device does not exist certain I/O instructions will still
 *    return data: DIA/B/C will return idle data bus value and
 *    SKPBZ/SKPDZ will sense zero value (and will therefore skip).
 *
 *    Perform these non-supported device functions only if 'stop_dev'
 *    is zero (i.e. I/O access trap is not in effect).
 */
    else if ( stop_dev == 0 )
        {
        switch (code)                                   /* decode IR<5:7> */
            {
        case ioDIA:
        case ioDIB:
        case ioDIC:
            AC[dstAC] = 0 ;  /*  idle I/O bus data  */
            break;

        case ioSKP:
            /*  (This should have been caught in previous CPU skip code)  */
            if ( (pulse == 1 /* SKPBZ */) || (pulse == 3 /* SKPDZ */) )
                {
                INCREMENT_PC ;
                }
            }    /*  end of 'switch'  */
        }    /*  end of handling non-existant device  */
      else reason = stop_dev;
      }                                                 /* end if IOT */    
    }                                                   /* end while */

/* Simulation halted */

saved_PC = PC;
pcq_r->qptr = pcq_p;                                    /* update pc q ptr */
return ( reason ) ;
}

/* New priority mask out */

void mask_out (int32 newmask)
{
int32 i;

dev_disable = 0;
for (i = DEV_LOW; i <= DEV_HIGH; i++)  {
    if (newmask & dev_table[i].pi)
        dev_disable = dev_disable | dev_table[i].mask;
    }
DEV_UPDATE_INTR ;
return;
}

/* Reset routine */

t_stat cpu_reset (DEVICE *dptr)
{
int_req = int_req & ~(INT_ION | INT_STK | INT_TRAP);
pimask = 0;
dev_disable = 0;
pwr_low = 0;
AMASK = 077777 ;                                        /* 32KW mode */
pcq_r = find_reg ("PCQ", NULL, dptr);
if (pcq_r)
    pcq_r->qptr = 0;
else return SCPE_IERR;
sim_brk_types = sim_brk_dflt = SWMASK ('E');
return SCPE_OK;
}

/* Memory examine */

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
if (addr >= MEMSIZE)
    return SCPE_NXM;
if (vptr != NULL)
    *vptr = GetMap(addr) & DMASK;
return SCPE_OK;
}

/* Memory deposit */

t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
if (addr >= MEMSIZE)
    return SCPE_NXM;
PutMap(addr, val & DMASK);
return SCPE_OK;
}

/* Alter memory size */

t_stat cpu_set_size (UNIT *uptr, int32 val, char *cptr, void *desc)
{
int32 mc = 0;
t_addr i;

if ((val <= 0) || (val > MAXMEMSIZE) || ((val & 07777) != 0))
    return SCPE_ARG;
for (i = val; i < MEMSIZE; i++)
    mc = mc | GetMap(i);
if ((mc != 0) && (!get_yn ("Really truncate memory [N]?", FALSE)))
    return SCPE_OK;
MEMSIZE = val;
for (i = MEMSIZE; i < MAXMEMSIZE; i++)
    PutMap(i, 0);
return SCPE_OK;
}

/* Build dispatch table */

t_stat build_devtab (void)
{
DEVICE *dptr;
DIB *dibp;
int32 i, dn;

for (i = 0; i < 64; i++) {                              /* clr dev_table */
    dev_table[i].mask = 0;
    dev_table[i].pi = 0;
    dev_table[i].routine = NULL;
    }
for (i = 0; (dptr = sim_devices[i]) != NULL; i++) {     /* loop thru dev */
    if (!(dptr->flags & DEV_DIS) &&                     /* enabled and */
        ( (dibp = (DIB *) dptr->ctxt)) ) {              /* defined DIB? */
        dn = dibp->dnum;                                /* get dev num */
        dev_table[dn].mask = dibp->mask;                /* copy entries */
        dev_table[dn].pi = dibp->pi;
        dev_table[dn].routine = dibp->routine;
        }
    }
return SCPE_OK;
}

/* BKR notes:
 *
 *    Data General APL (Automatic Program Load) boot code
 *
 *    - This bootstrap code is called the "APL option" in DG documentation (Automatic
 *      Program Load), and cost ~$400 USD (in 1970 - wow!) to load 32(10) words from
 *      a PROM to main (core) memory location 0 - 32.
 *    - This code is documented in various DG Nova programming manuals and was
 *      quite static (i.e. no revisions or updates to code were made). 
 *    - switch register is used to determine device code and device type.
 *    - lower 6-bits of switch register determines device code (0-63.).
 *    - most significant bit determines if device is "low speed" or "high speed".
 *    - "high speed" devices have effective boot program logic of:
 *
 *        IORST
 *        NIOS <device>
 *        JMP .
 *
 *    - "high speed" devices use data channel (DCH) to read first sector/record
 *      of device into memory (usually starting at location 0), which then over-writes
 *      the 'JMP .' instruction of boot code.  This usually has a jump to some other
 *      device and operating system specific boot code that was loaded from the device.
 *    - "low speed" devices are assumed to be sequential character-oriented devices
 *      (i.e. Teletype (r) reader, paper tape reader).
 *    - "low speed" devices are assumed to start read operations with a 'S' pulse,
 *      read data buffer with a DIA instruction and have standard DG I/O Busy/Done logic.
 *    - "low speed" devices usually read in a more full-featured 'binary loader' with
 *      the APL boot code:
 *
 *      DG paper tape: 091-000004-xx, Binary Loader (BLDR.AB)
 *
 *    - The Binary Loader was in turn used to load tapes in the usual DG 'absolute binary' format.
 */

#define BOOT_START  00000
#define BOOT_LEN    (sizeof(boot_rom) / sizeof(int32))

static const int32 boot_rom[] = {
    0062677,                    /*      IORST           ;reset all I/O  */
    0060477,                    /*      READS 0         ;read SR into AC0 */
    0024026,                    /*      LDA 1,C77       ;get dev mask */
    0107400,                    /*      AND 0,1         ;isolate dev code */
    0124000,                    /*      COM 1,1         ;- device code - 1 */
    0010014,                    /* LOOP: ISZ OP1        ;device code to all */
    0010030,                    /*      ISZ OP2         ;I/O instructions */
    0010032,                    /*      ISZ OP3         */
    0125404,                    /*      INC 1,1,SZR     ;done? */
    0000005,                    /*      JMP LOOP        ;no, increment again */
    0030016,                    /*      LDA 2,C377      ;place JMP 377 into */
    0050377,                    /*      STA 2,377       ;location 377 */
    0060077,                    /* OP1: 060077          ;start device (NIOS 0) */
    0101102,                    /*      MOVL 0,0,SZC    ;test switch 0, low speed? */
    0000377,                    /* C377: JMP 377        ;no - jmp 377 & wait */
    0004030,                    /* LOOP2: JSR GET+1     ;get a frame */
    0101065,                    /*      MOVC 0,0,SNR    ;is it non-zero? */
    0000017,                    /*      JMP LOOP2       ;no, ignore */
    0004027,                    /* LOOP4: JSR GET       ;yes, get full word */
    0046026,                    /*      STA 1,@C77      ;store starting at 100 */
                                /*                      ;2's complement of word ct */
    0010100,                    /*      ISZ 100         ;done? */
    0000022,                    /*      JMP LOOP4       ;no, get another */
    0000077,                    /* C77: JMP 77          ;yes location ctr and */
                                /*                      ;jmp to last word */
    0126420,                    /* GET: SUBZ 1,1        ; clr AC1, set carry */
                                /* OP2:                 */
    0063577,                    /* LOOP3: 063577        ;done? (SKPDN 0) - 1 */
    0000030,                    /*      JMP LOOP3       ;no -- wait */
    0060477,                    /* OP3: 060477          ;y -- read in ac0 (DIAS 0,0) */
    0107363,                    /*      ADDCS 0,1,SNC   ;add 2 frames swapped - got 2nd? */
    0000030,                    /*      JMP LOOP3       ;no go back after it */
    0125300,                    /*      MOVS 1,1        ;yes swap them */
    0001400,                    /*      JMP 0,3         ;rtn with full word */
    0000000                     /*      0               ;padding */
    };

t_stat cpu_boot (int32 unitno, DEVICE *dptr)
{
size_t i;

for (i = 0; i < BOOT_LEN; i++) PutMap(BOOT_START + i, boot_rom[i]);
saved_PC = BOOT_START;
return SCPE_OK;
}

/* History subsystem

global routines

t_stat hist_set( UNIT * uptr, int32 val, char * cptr, void * desc, void ** HistCookie, sizeof(usrHistInfo) ) ;
t_stat hist_show( FILE * st, UNIT * uptr, int32 val, void * desc, void * HistCookie ) ;
int hist_save( int32 next_pc, int32 our_ir, void * usrHistInfo )

local user struct:

usrHistInfo

local user routines:

int uHist_save( int32 next_pc, int32 our_ir, void * usrHistInfo ) ;
int uHist_fprintf( FILE * fp, int itemNum, void * usrHistInfo ) ;

typedef struct
    {
    int    hMax ;        // total # entries in queue (0 = inactive)
    int    hCount ;      // current entry
    void * hPtr ;        // pointer to save area
    int    hSize ;       // size of each user save area (not used by global routines?)
    }  Hist_info ;
 */

/*  generalized CPU execution trace  */

#define HIST_IR_INVALID      -1
#define HIST_MIN              0     /*  0 == deactivate history feature, else size of queue  */
#define HIST_MAX        1000000     /*  completely arbitrary max size value  */

/*  save history entry  (proposed local routine) */

static int hist_save( int32 pc, int32 our_ir )
{
Hist_entry *    hist_ptr ;

if ( hist )
    if ( hist_cnt )
    {
        hist_p = (hist_p + 1) ;        /* next entry  */
        if ( hist_p >= hist_cnt )
        {
        hist_p = 0 ;
        }
    hist_ptr = &hist[ hist_p ] ;

    /*  (machine-specific stuff)  */

    hist_ptr->pc    = pc ;
    hist_ptr->ir    = our_ir ;
    hist_ptr->ac0   = AC[ 0 ] ;
    hist_ptr->ac1   = AC[ 1 ] ;
    hist_ptr->ac2   = AC[ 2 ] ;
    hist_ptr->ac3   = AC[ 3 ] ;
    hist_ptr->carry = C >> 16 ;
    hist_ptr->fp    = FP ;
    hist_ptr->sp    = SP ;
    hist_ptr->devBusy    = dev_busy ;
    hist_ptr->devDone    = dev_done ;
    hist_ptr->devDisable = dev_disable ;
    hist_ptr->devIntr    = int_req ;
    /*  how 'bout state and AMASK?  */
    return ( hist_p ) ;
    }
return ( -1 ) ;
}    /*  end of 'hist_save'  */

/*  setup history save area (proposed global routine)  */

t_stat hist_set( UNIT * uptr, int32 val, char * cptr, void * desc )
{
int32   i, lnt ;
t_stat  r ;

if ( cptr == NULL )
    {
    for (i = 0 ; i < hist_cnt ; ++i )
        {
        hist[i].pc = 0 ;
        hist[i].ir = HIST_IR_INVALID ;
        }
    hist_p = 0 ;
    return ( SCPE_OK ) ;
    }
lnt = (int32) get_uint(cptr, 10, HIST_MAX, &r) ;
if ( (r != SCPE_OK) || (lnt && (lnt < HIST_MIN)) )
    {
    return ( SCPE_ARG ) ;
    }
hist_p = 0;
if ( hist_cnt )
    {
    free( hist ) ;
    hist_cnt = 0 ;
    hist = NULL ;
    }
if ( lnt )
    {
    hist = (Hist_entry *) calloc( lnt, sizeof(Hist_entry) ) ;
    if ( hist == NULL )
        {
        return ( SCPE_MEM ) ;
        }
    hist_cnt = lnt ;
    }
return ( SCPE_OK ) ;
}   /*  end of 'hist_set'  */


int hist_fprintf( FILE * fp, int itemNum, Hist_entry * hptr )
{
extern t_value *sim_eval ;

if ( hptr )
    {
    if ( itemNum == 0 )
        {
        fprintf( fp, "\n\n" ) ;
        }
    fprintf( fp, "%05o / %06o   %06o  %06o  %06o  %06o  %o   ",
        (hptr->pc  & 0x7FFF),
        (hptr->ir  & 0xFFFF),
        (hptr->ac0 & 0xFFFF),
        (hptr->ac1 & 0xFFFF),
        (hptr->ac2 & 0xFFFF),
        (hptr->ac3 & 0xFFFF),
        (hptr->carry & 1)
        ) ;
    if ( cpu_unit.flags & UNIT_STK  /* Nova 3 or Nova 4 */ ) 
        {
        fprintf( fp, "%06o  %06o   ", SP, FP ) ;
        }

    sim_eval[0] = (hptr->ir & 0xFFFF) ;
    if ( (fprint_sym(fp, (hptr->pc & AMASK), sim_eval, &cpu_unit, SWMASK ('M'))) > 0 )
        {
        fprintf( fp, "(undefined) %04o", (hptr->ir & 0xFFFF) ) ;
    }
    /*
    display ION flag value, pend value?
    display devBusy, devDone, devIntr info?
    */

    if ( 0 )                                            /*  display INTRP codes?  */
        {
        char    tmp[ 500 ] ;

        devBitNames( hptr->devIntr, tmp, NULL ) ;
        fprintf( fp, "    %s", tmp ) ;
        }

    fprintf( fp, "\n" ) ;
    }
return ( 0 ) ;
}   /*  end of 'hist_fprintf'  */


/* show execution history (proposed global routine) */

t_stat hist_show( FILE * st, UNIT * uptr, int32 val, void * desc )
{
int32           k, di, lnt ;
char *          cptr = (char *) desc ;
t_stat          r ;
Hist_entry *    hptr ;


if (hist_cnt == 0)
    {
    return ( SCPE_NOFNC ) ;                             /* enabled? */
    }
if ( cptr )
    {                                                   /*  number of entries specified  */
    lnt = (int32) get_uint( cptr, 10, hist_cnt, &r ) ;
    if ( (r != SCPE_OK) || (lnt == 0) )
        {
        return ( SCPE_ARG ) ;
        }
        }
    else
        {
        lnt = hist_cnt ;                                /*  display all entries  */
        }
    di = hist_p - lnt;                                  /* work forward */
    if ( di < 0 )
        {
        di = di + hist_cnt ;
        }

for ( k = 0 ; k < lnt ; ++k )
    {                                                   /* print specified */
    hptr = &hist[ (++di) % hist_cnt] ;                  /* entry pointer   */
    if ( hptr->ir != HIST_IR_INVALID )                  /* valid entry?    */
        {
        hist_fprintf( st, k, hptr ) ;
        }                                               /* end else instruction */
    }                                                   /* end for */
return SCPE_OK;
}    /*  end of 'hist_show'  */



struct Dbits
    {
    int32      dBit ;
    int32      dInvertMask ;
    char *     dName ;
    }  devBits [] =

    {
    { INT_TRAP,   0,    "TRAP"   },    /* (in order of approximate DG interrupt mask priority) */
    { INT_ION,    0,    "ION"    },
    { INT_NO_ION_PENDING, 1, "IONPND"  },    /*  (invert this logic to provide cleaner display)  */
    { INT_STK,    0,    "STK"    },
    { INT_PIT,    0,    "PIT"    },
    { INT_DKP,    0,    "DKP"    },
    { INT_DSK,    0,    "DSK"    },
    { INT_MTA,    0,    "MTA"    },
    { INT_LPT,    0,    "LPT"    },
    { INT_PTR,    0,    "PTR"    },
    { INT_PTP,    0,    "PTP"    },
    { INT_PLT,    0,    "PLT"    },
    { INT_CLK,    0,    "CLK"    },
    { INT_ALM,    0,    "ALM"    },
    { INT_QTY,    0,    "QTY"    },
    { INT_TTO1,   0,    "TTO1"   },
    { INT_TTI1,   0,    "TTI1"   },
    { INT_TTO,    0,    "TTO"    },
    { INT_TTI,    0,    "TTI"    },
    {        0,   0,    NULL     }
    } ;


char * devBitNames( int32 flags, char * ptr, char * sepStr )
{
int    a ;

if ( ptr )
    {
    *ptr = 0 ;
    for ( a = 0 ; (devBits[a].dBit) ; ++a )
      if ( devBits[a].dBit & ((devBits[a].dInvertMask)? ~flags : flags) )
        {
        if ( *ptr )
            {
            strcat( ptr, (sepStr) ? sepStr : " " ) ;
            strcat( ptr, devBits[a].dName ) ;
            }
        else
            {
            strcpy( ptr, devBits[a].dName ) ;
            }
        }
    }
return ( ptr ) ;
}   /*  end of 'devBitNames'  */


#define MAP_V_PAGE      10
#define MAP_M_PAGE      037
#define MAP_M_OFFSET    01777

int32 MapLastPage;

int32 MapAddrEx(int32 map, int32 addr)
{
    int32 phypage;
    int32 paddr;

    if (MapFetchDelay && !--MapFetchDelay) {
        if (MapEnable) {
            MapMode = MODE_USR; MapUse = USR_MAP;
            MapEnable = 0;
        } else if (MapSingleCycle) {
            MapUse = USR_MAP;
        }
    }
    MapLastPage = (addr >> MAP_V_PAGE) & MAP_M_PAGE;
    if ((SVR_MAP == map) && (31 == MapLastPage)) {
        map = USR_MAP;
        }
    phypage = Map[map][MapLastPage];
    if ((USR_MAP == map) && 0377 == phypage) {
        MapStatus |= MAP_STA_V;
        MapInvAddr = addr;
    }
    paddr = (phypage << MAP_V_PAGE) | (addr & MAP_M_OFFSET);
    return paddr;
}

/* 1-to-1 map for I/O devices, almost */

int32 MapAddr (int32 map, int32 addr)
{
    int32 paddr;
    static int cnt=0;
    
    /*if (MMPU_TRACE(2)) {
        if (DCH_ENABLED)
        printf("MapAddr: map=%d DCH_ENABLED=%d\r\n",map,DCH_ENABLED?1:0);
        }*/

    map = /*map &&*/ DCH_ENABLED ? DCH_MAP : SVR_MAP;
    paddr = MapAddrEx(map, addr);
    return paddr;
}

int32 GetMap(int32 addr)
{
    int32 paddr;

    paddr = MapAddrEx(MapUse, addr);
    if (VALID_PROT) {
        /* SingleCycle??? */
        if (FetchCycle)
            TRAP(IO_V_JMP);
        return 0;
        }
    return M[paddr];
}

int32 PutMap(int32 addr, int32 data)
{
    int32 paddr;

    paddr = MapAddrEx(MapUse, addr);
    if (VALID_PROT) {
        /* SingleCycle??? */
        TRAP(IO_V_JMP);
        }
    if (WRITE_PROT && (Map[MapUse][MapLastPage] & MAP_M_WP)) {
        MapStatus |= MAP_STA_WR;
        MapInvAddr = addr;
        TRAP(DEFER_WR_JMP);
    }
    M[paddr] = data;
    return data;
}

