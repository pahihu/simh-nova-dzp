I would like to say thanks for the generous help of Bruce Ray of Wild Hare Computer Systems Inc.


Nova FPU in Fixnum days
=======================

I've added the Nova FPU to SimH using Dutch Owen's floating point routines found in the
eclipse_cpu.c sources. The status settings and interrupt logic worked out using the DTOS 
NFPU TEST, DTOS NMORT short and standalone NMORT long programs. It seems to pass the NMORTs.

Milestones

* FPU priority interrupt mask
* difference between normal and parallel mode
* when to set FPU SR flags
* DTOS NFPU TEST uses not documented diagnostic instructions
* FLDX bugfix (exponent placement)
* mul_fl() bugfix (signed int32 to signed int64 conversion)

Documentation

* 015-000023-03 Programmer's Reference Manual NOVA LINE COMPUTERS, 1976\
    *Floating point arithmetic pp.58*


Mapped Nova 840 in 3 days
=========================

I have added the Nova 840 MMPU to SimH with 128KW memory. Since the 6067-type RDOS disk contains MRDOS,
I was able to experiment with it. Passing NMORTs was another 2.5 hours.

Milestones

* DCH map is the same as the supervisor map on reset
* fixed Enable User Map, Enable Single Cycle
* indirection is a single cycle (READ + WRITE)
* NIOC x,MMPU can be executed even when device is protected (supervisor call)
* fixed I/O protection check

Passing short (DTOS) and long NMORTs

* accessing logical page 31 from supervisor mode does not set the validity protection bit
* NIOC x, MMPU sets the invalid instruction address register

Documentation

* 015-000023-03 Programmer's Reference Manual NOVA LINE COMPUTERS, 1976\
    *Memory management pp.45*


DZP in 3 days
=============

What bugged me, that [Wild Hare Computer Systems Inc.](https://novasareforever.org/emulators/emulator-downloads)
 produced an excellent Nova/Eclipse simulator, which is based on OpenSimH. 
It contains a 6067-type RDOS disk image loaded with goodies, but I cannot access it,
because there is no DZP disk controller in SimH.
Let's do it. It is based on the DKP controller code.

Milestones

* device code 027
* double DOC sector addressing
* P does not set controller's BUSY/DONE
* RDOS boot stopped after a seek => needs INTs (INT generation after RECAL/SEEK)
* hang up, DISK FORMAT ERROR, DISK STATUS ERROR etc.
* tracing Nova boot I/O in the Wild Hare simulator (where mine goes wild?)
* only SEEK/RECAL should set the cylinder count in the unit (leftover from DKP)
* read ends on a surface boundary
* start R/W while SEEK/RECAL in progress

Documentation

* 015-000021-07 Peripheral Programming Manual, 1977\
    *6060 Series DG/Disc Storage Subsystem pp.249*

* 014-000617-01 Nova 4 Programmer's Reference, 1978-80\
    *Chapter IV. Input/Output pp.33*
    
* 600-294-00-A Model ZDF-1 Disk and Tape Drive Controller Manual, 1984\
    *6.0 Disk Program Control pp.81*
    
* 014-000644-00 Model 6122 DG/Disc Storage Subsystem Programmers Reference, 1979\
    *Controller registers pp.9-10 (how drive attention flags generate INTs)*
    
**NOTE**: Do not build in the cloned repo. The makefile behaves differently when detects Git. Good luck!
    

