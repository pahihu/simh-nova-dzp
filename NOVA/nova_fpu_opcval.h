#define IOP_S   (1 << 6)
#define IOP_C   (2 << 6)
#define IOP_P   (3 << 6)

#define IO_NIO  0060000
#define IO_DIA  0060400
#define IO_DIB  0061400
#define IO_DOA  0061000
#define IO_DOB  0062000
#define IO_DOC  0063000

#define D_FPU1  074
#define D_FPU2  075
#define D_FPU   076

(IO_NIO+IOP_S+D_FPU1+I_NPN), (IO_NIO+IOP_C+D_FPU1+I_NPN), (IO_NIO+IOP_P+D_FPU1+I_NPN),
(IO_DIA+      D_FPU1+I_R  ), (IO_DIA+      D_FPU1+I_R  ),
(IO_DIB+      D_FPU1+I_R  ),
(IO_DOA+      D_FPU1+I_R  ), (IO_DOA+IOP_S+D_FPU1+I_R  ), (IO_DOA+IOP_C+D_FPU1+I_R  ), (IO_DOA+IOP_P+D_FPU1+I_R  ),
(IO_DOB+IOP_S+D_FPU1+I_R  ), (IO_DOB+IOP_P+D_FPU1+I_R  ),
(IO_DOC+      D_FPU1+I_NPN), (IO_DOC+IOP_S+D_FPU1+I_NPN), (IO_DOC+IOP_C+D_FPU1+I_NPN), (IO_DOC+IOP_P+D_FPU1+I_NPN),
(IO_NIO+IOP_S+D_FPU2+I_NPN), (IO_NIO+IOP_C+D_FPU2+I_NPN), (IO_NIO+IOP_P+D_FPU2+I_NPN),
(IO_DIA+      D_FPU2+I_R  ),
(IO_DIB+      D_FPU2+I_R  ),
(IO_DOA+      D_FPU2+I_R  ), (IO_DOA+IOP_S+D_FPU2+I_R  ), (IO_DOA+IOP_C+D_FPU2+I_R  ), (IO_DOA+IOP_P+D_FPU2+I_R  ),
(IO_DOB+      D_FPU2+I_R  ), (IO_DOB+IOP_S+D_FPU2+I_R  ), (IO_DOB+IOP_C+D_FPU2+I_R  ), (IO_DOB+IOP_P+D_FPU2+I_R  ),
(IO_DOC+      D_FPU2+I_NPN), (IO_DOC+IOP_S+D_FPU2+I_NPN), (IO_DOC+IOP_C+D_FPU2+I_NPN), (IO_DOC+IOP_P+D_FPU2+I_NPN),
(IO_NIO+IOP_P+D_FPU +I_NPN), (IO_DIA+IOP_C+D_FPU +I_R  ), (IO_DOA+      D_FPU +I_R  ),

#undef IOP_S
#undef IOP_C
#undef IOP_P

#undef IO_NIO
#undef IO_DIA
#undef IO_DIB
#undef IO_DOA
#undef IO_DOB
#undef IO_DOC

#undef D_FPU1
#undef D_FPU2
#undef D_FPU

