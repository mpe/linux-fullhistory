
# Makefile for kernel/blk_drv/scsi
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

.c.s:
	$(CC) $(CFLAGS) -S $<
.s.o:
	$(AS) -c -o $*.o $<
.c.o:
	$(CC) $(CFLAGS) -c $<

AHA152X	= -DDEBUG_AHA152X -DAUTOCONF

ifeq (${CFLAGS},)
CFLAGS = -D__KERNEL__=1 \
	-Wall -Wstrict-prototypes -I. -I../../include \
	-O2 -fomit-frame-pointer -m486

include ../../.config
endif

SCSI_OBJS =
SCSI_SRCS =
SCSI_MODULE_OBJS =

ifdef CONFIG_SCSI

SCSI_OBJS := hosts.o scsi.o scsi_ioctl.o constants.o scsicam.o
SCSI_SRCS := hosts.c scsi.c scsi_ioctl.c constants.c scsicam.c

ifdef CONFIG_CHR_DEV_ST
SCSI_OBJS := $(SCSI_OBJS) st.o
SCSI_SRCS := $(SCSI_SRCS) st.c
endif

ifdef CONFIG_BLK_DEV_SD
SCSI_OBJS := $(SCSI_OBJS) sd.o sd_ioctl.o
SCSI_SRCS := $(SCSI_SRCS) sd.c sd_ioctl.c
endif

ifdef CONFIG_BLK_DEV_SR
SCSI_OBJS := $(SCSI_OBJS) sr.o sr_ioctl.o
SCSI_SRCS := $(SCSI_SRCS) sr.c sr_ioctl.c
endif

ifdef CONFIG_CHR_DEV_SG
SCSI_OBJS := $(SCSI_OBJS) sg.o
SCSI_SRCS := $(SCSI_SRCS) sg.c
endif

SCSI_SRCS := $(SCSI_SRCS) qlogic.c
ifdef CONFIG_SCSI_QLOGIC
SCSI_OBJS := $(SCSI_OBJS) qlogic.o
else
SCSI_MODULE_OBJS := $(SCSI_MODULE_OBJS) qlogic.o
endif

ifdef CONFIG_SCSI_AHA152X
SCSI_OBJS := $(SCSI_OBJS) aha152x.o
SCSI_SRCS := $(SCSI_SRCS) aha152x.c
endif

ifdef CONFIG_SCSI_AHA1542
SCSI_SRCS := $(SCSI_SRCS) aha1542.c
SCSI_OBJS := $(SCSI_OBJS) aha1542.o
else
SCSI_MODULE_OBJS := $(SCSI_MODULE_OBJS) aha1542.o
endif

ifdef CONFIG_SCSI_AHA1740
SCSI_OBJS := $(SCSI_OBJS) aha1740.o
SCSI_SRCS := $(SCSI_SRCS) aha1740.c
endif

ifdef CONFIG_SCSI_AHA274X
SCSI_OBJS := $(SCSI_OBJS) aha274x.o
SCSI_SRCS := $(SCSI_SRCS) aha274x.c
endif

ifdef CONFIG_SCSI_BUSLOGIC
SCSI_OBJS := $(SCSI_OBJS) buslogic.o
SCSI_SRCS := $(SCSI_SRCS) buslogic.c
endif

SCSI_SRCS := $(SCSI_SRCS) eata_dma.c
ifdef CONFIG_SCSI_EATA_DMA
SCSI_OBJS := $(SCSI_OBJS) eata_dma.o
else
SCSI_MODULE_OBJS := $(SCSI_MODULE_OBJS) eata_dma.o
endif

ifdef CONFIG_SCSI_U14_34F
SCSI_OBJS := $(SCSI_OBJS) u14-34f.o
SCSI_SRCS := $(SCSI_SRCS) u14-34f.c
else
SCSI_MODULE_OBJS := $(SCSI_MODULE_OBJS) u14-34f.o
endif

ifdef CONFIG_SCSI_DEBUG
SCSI_SRCS := $(SCSI_SRCS) scsi_debug.c
SCSI_OBJS := $(SCSI_OBJS) scsi_debug.o
else
SCSI_MODULE_OBJS := $(SCSI_MODULE_OBJS) scsi_debug.o
endif

ifdef CONFIG_SCSI_FUTURE_DOMAIN
SCSI_OBJS := $(SCSI_OBJS) fdomain.o
SCSI_SRCS := $(SCSI_SRCS) fdomain.c
endif

ifdef CONFIG_SCSI_IN2000
SCSI_SRCS := $(SCSI_SRCS) in2000.c
SCSI_OBJS := $(SCSI_OBJS) in2000.o
else
SCSI_MODULE_OBJS := $(SCSI_MODULE_OBJS) in2000.o
endif

ifdef CONFIG_SCSI_GENERIC_NCR5380
SCSI_OBJS := $(SCSI_OBJS) g_NCR5380.o
SCSI_SRCS := $(SCSI_SRCS) g_NCR5380.c
endif

ifdef CONFIG_SCSI_NCR53C7xx
SCSI_OBJS := $(SCSI_OBJS) 53c7,8xx.o 
SCSI_SRCS := $(SCSI_SRCS) 53c7,8xx.c
else
SCSI_MODULE_OBJS := $(SCSI_MODULE_OBJS) 53c7,8xx.o
endif

ifdef CONFIG_SCSI_PAS16
SCSI_OBJS := $(SCSI_OBJS) pas16.o
SCSI_SRCS := $(SCSI_SRCS) pas16.c
endif

ifdef CONFIG_SCSI_SEAGATE
SCSI_OBJS := $(SCSI_OBJS) seagate.o
SCSI_SRCS := $(SCSI_SRCS) seagate.c
else
ifdef CONFIG_SCSI_FD_8xx
SCSI_OBJS := $(SCSI_OBJS) seagate.o
SCSI_SRCS := $(SCSI_SRCS) seagate.c
endif
endif

ifdef CONFIG_SCSI_7000FASST
SCSI_OBJS := $(SCSI_OBJS) wd7000.o
SCSI_SRCS := $(SCSI_SRCS) wd7000.c
endif

ifdef CONFIG_SCSI_T128
SCSI_OBJS := $(SCSI_OBJS) t128.o
SCSI_SRCS := $(SCSI_SRCS) t128.c
endif

ifdef CONFIG_SCSI_ULTRASTOR
SCSI_OBJS := $(SCSI_OBJS) ultrastor.o
SCSI_SRCS := $(SCSI_SRCS) ultrastor.c
endif

ifdef CONFIG_SCSI_EATA
SCSI_OBJS := $(SCSI_OBJS) eata.o
SCSI_SRCS := $(SCSI_SRCS) eata.c
else
SCSI_MODULE_OBJS := $(SCSI_MODULE_OBJS) eata.o
endif


scsi.a: $(SCSI_OBJS)
	rm -f scsi.a
	$(AR) rcs scsi.a $(SCSI_OBJS)
	sync

aha152x.o: aha152x.c
	$(CC) $(CFLAGS) $(AHA152X) -c aha152x.c 

aic7770:	aic7770.c
		$(CC) $(CFLAGS) -o $@ aic7770.c

aha274x_seq.h:	aic7770 aha274x.seq
		./aic7770 -o $@ aha274x.seq

seagate.o: seagate.c
	$(CC) $(CFLAGS) -DARBITRATE -DSLOW_HANDSHAKE -DFAST32 -c seagate.c 

# For debugging, use the -g flag
53c7,8xx.o : 53c7,8xx.c
	$(CC) $(CFLAGS) -g -c 53c7,8xx.c

53c8xx_d.h 53c8xx_u.h : 53c7,8xx.scr script_asm.pl
	ln 53c7,8xx.scr fake.c
	$(CPP) -traditional -DCHIP=810 fake.c | grep -v '^#' | perl script_asm.pl 
	mv script.h 53c8xx_d.h
	mv scriptu.h 53c8xx_u.h
	rm fake.c

modules: $(SCSI_MODULE_OBJS)
	echo $(SCSI_MODULE_OBJS) > ../../modules/SCSI_MODULES
	(cd ../../modules;for i in $(SCSI_MODULE_OBJS); do ln -sf ../drivers/scsi/$$i .; done)

dep:
	$(CPP) -M $(AHA152X) $(SCSI_SRCS) > .depend
	$(CPP) -M -DMODULE $(SCSI_MODULE_OBJS:.o=.c) >> .depend

else

scsi.a:
	rm -f scsi.a
	@echo No SCSI drivers configured
	$(AR) rcs scsi.a

dep:

endif

#
# include a dependency file if one exists
#
ifeq (.depend,$(wildcard .depend))
include .depend
endif
