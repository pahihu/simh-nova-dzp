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
* NRDOS boot stopped after a seek => needs INTs (INT generation after RECAL/SEEK)
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
    

