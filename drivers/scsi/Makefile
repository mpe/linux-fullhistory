
# Makefile for linux/drivers/scsi
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

.s.o:
	$(AS) -c -o $*.o $<
.c.o:
	$(CC) $(CFLAGS) -c $<

ifdef CONFIG_KERNEL_ELF
# This is used for ELF - it needs to migrate or be moved.
LD_RFLAG = -m elf_i386
endif

AHA152X	= -DDEBUG_AHA152X -DAUTOCONF -DSKIP_BIOSTEST -DIRQ=11

ifeq (${CFLAGS},)
CFLAGS = -D__KERNEL__=1 \
	-DMODULE -Wall -Wstrict-prototypes -I. -I../../include \
	-O2 -fomit-frame-pointer -m486

include ../../.config

.EXPORT_ALL_VARIABLES:

SYMTAB_OBJS = scsi_syms.o

TOPDIR = ../..
include ../../versions.mk

endif

SCSI_OBJS =
SCSI_SRCS =
SCSI_DEP =
SCSI_MODULE_OBJS =

SCSI_SRCS := hosts.c scsi.c scsi_ioctl.c constants.c scsicam.c scsi_proc.c

ifdef CONFIG_SCSI
SCSI_OBJS := hosts.o scsi.o scsi_ioctl.o constants.o scsicam.o scsi_proc.o
else
ifdef CONFIG_MODVERSIONS
# Create this before we build anything else.
SCSI_MODULE_VER := scsi_syms.ver
endif
SCSI_MODULE_OBJS := $(SCSI_MODULE_OBJS) scsi_mod.o
endif

SCSI_SRCS := $(SCSI_SRCS) st.c
ifdef CONFIG_CHR_DEV_ST
SCSI_OBJS := $(SCSI_OBJS) st.o
else
SCSI_MODULE_OBJS := $(SCSI_MODULE_OBJS) st.o
endif

SCSI_SRCS := $(SCSI_SRCS) sd.c sd_ioctl.c
ifdef CONFIG_BLK_DEV_SD
SCSI_OBJS := $(SCSI_OBJS) sd.o sd_ioctl.o
else
SCSI_MODULE_OBJS := $(SCSI_MODULE_OBJS) sd_mod.o
endif

SCSI_SRCS := $(SCSI_SRCS) sr.c sr_ioctl.c
ifdef CONFIG_BLK_DEV_SR
SCSI_OBJS := $(SCSI_OBJS) sr.o sr_ioctl.o
else
SCSI_MODULE_OBJS := $(SCSI_MODULE_OBJS) sr_mod.o
endif

SCSI_SRCS := $(SCSI_SRCS) sg.c
ifdef CONFIG_CHR_DEV_SG
SCSI_OBJS := $(SCSI_OBJS) sg.o
else
SCSI_MODULE_OBJS := $(SCSI_MODULE_OBJS) sg.o
endif

SCSI_SRCS := $(SCSI_SRCS) qlogic.c
ifdef CONFIG_SCSI_QLOGIC
SCSI_OBJS := $(SCSI_OBJS) qlogic.o
else
SCSI_MODULE_OBJS := $(SCSI_MODULE_OBJS) qlogic.o
endif

SCSI_SRCS := $(SCSI_SRCS) aha152x.c
ifdef CONFIG_SCSI_AHA152X
SCSI_OBJS := $(SCSI_OBJS) aha152x.o
else
SCSI_MODULE_OBJS := $(SCSI_MODULE_OBJS) aha152x.o
endif

SCSI_SRCS := $(SCSI_SRCS) aha1542.c
ifdef CONFIG_SCSI_AHA1542
SCSI_OBJS := $(SCSI_OBJS) aha1542.o
else
SCSI_MODULE_OBJS := $(SCSI_MODULE_OBJS) aha1542.o
endif

SCSI_SRCS := $(SCSI_SRCS) aha1740.c
ifdef CONFIG_SCSI_AHA1740
SCSI_OBJS := $(SCSI_OBJS) aha1740.o
else
SCSI_MODULE_OBJS := $(SCSI_MODULE_OBJS) aha1740.o
endif

SCSI_SRCS := $(SCSI_SRCS) aic7xxx.c
SCSI_DEP := $(SCSI_DEP) aic7xxx_seq.h
ifdef CONFIG_SCSI_AIC7XXX
SCSI_OBJS := $(SCSI_OBJS) aic7xxx.o
else
SCSI_MODULE_OBJS := $(SCSI_MODULE_OBJS) aic7xxx.o
endif

SCSI_SRCS := $(SCSI_SRCS) buslogic.c
ifdef CONFIG_SCSI_BUSLOGIC
SCSI_OBJS := $(SCSI_OBJS) buslogic.o
else
SCSI_MODULE_OBJS := $(SCSI_MODULE_OBJS) buslogic.o
endif

SCSI_SRCS := $(SCSI_SRCS) eata_dma.c
ifdef CONFIG_SCSI_EATA_DMA
SCSI_OBJS := $(SCSI_OBJS) eata_dma.o
else
SCSI_MODULE_OBJS := $(SCSI_MODULE_OBJS) eata_dma.o
endif

SCSI_SRCS := $(SCSI_SRCS) eata_pio.c
ifdef CONFIG_SCSI_EATA_PIO
SCSI_OBJS := $(SCSI_OBJS) eata_pio.o
else
SCSI_MODULE_OBJS := $(SCSI_MODULE_OBJS) eata_pio.o
endif

SCSI_SRCS := $(SCSI_SRCS) u14-34f.c
ifdef CONFIG_SCSI_U14_34F
SCSI_OBJS := $(SCSI_OBJS) u14-34f.o
else
SCSI_MODULE_OBJS := $(SCSI_MODULE_OBJS) u14-34f.o
endif

SCSI_SRCS := $(SCSI_SRCS) scsi_debug.c
ifdef CONFIG_SCSI_DEBUG
SCSI_OBJS := $(SCSI_OBJS) scsi_debug.o
else
SCSI_MODULE_OBJS := $(SCSI_MODULE_OBJS) scsi_debug.o
endif

SCSI_SRCS := $(SCSI_SRCS) fdomain.c
ifdef CONFIG_SCSI_FUTURE_DOMAIN
SCSI_OBJS := $(SCSI_OBJS) fdomain.o
else
SCSI_MODULE_OBJS := $(SCSI_MODULE_OBJS) fdomain.o
endif

SCSI_SRCS := $(SCSI_SRCS) in2000.c
ifdef CONFIG_SCSI_IN2000
SCSI_OBJS := $(SCSI_OBJS) in2000.o
else
SCSI_MODULE_OBJS := $(SCSI_MODULE_OBJS) in2000.o
endif

SCSI_SRCS := $(SCSI_SRCS) g_NCR5380.c
ifdef CONFIG_SCSI_GENERIC_NCR5380
SCSI_OBJS := $(SCSI_OBJS) g_NCR5380.o
endif

SCSI_SRCS := $(SCSI_SRCS) 53c7,8xx.c
ifdef CONFIG_SCSI_NCR53C7xx
SCSI_OBJS := $(SCSI_OBJS) 53c7,8xx.o 
else
SCSI_MODULE_OBJS := $(SCSI_MODULE_OBJS) 53c7,8xx.o
endif

SCSI_SRCS := $(SCSI_SRCS) pas16.c
ifdef CONFIG_SCSI_PAS16
SCSI_OBJS := $(SCSI_OBJS) pas16.o
endif

SCSI_SRCS := $(SCSI_SRCS) seagate.c
ifdef CONFIG_SCSI_SEAGATE
SCSI_OBJS := $(SCSI_OBJS) seagate.o
else
ifdef CONFIG_SCSI_FD_8xx
SCSI_OBJS := $(SCSI_OBJS) seagate.o
else
SCSI_MODULE_OBJS := $(SCSI_MODULE_OBJS) seagate.o
endif
endif

SCSI_SRCS := $(SCSI_SRCS) wd7000.c
ifdef CONFIG_SCSI_7000FASST
SCSI_OBJS := $(SCSI_OBJS) wd7000.o
else
SCSI_MODULE_OBJS := $(SCSI_MODULE_OBJS) wd7000.o
endif

SCSI_SRCS := $(SCSI_SRCS) t128.c
ifdef CONFIG_SCSI_T128
SCSI_OBJS := $(SCSI_OBJS) t128.o
endif

SCSI_SRCS := $(SCSI_SRCS) ultrastor.c
ifdef CONFIG_SCSI_ULTRASTOR
SCSI_OBJS := $(SCSI_OBJS) ultrastor.o
else
SCSI_MODULE_OBJS := $(SCSI_MODULE_OBJS) ultrastor.o
endif

SCSI_SRCS := $(SCSI_SRCS) eata.c
ifdef CONFIG_SCSI_EATA
SCSI_OBJS := $(SCSI_OBJS) eata.o
else
SCSI_MODULE_OBJS := $(SCSI_MODULE_OBJS) eata.o
endif


scsi.a: $(SCSI_OBJS)
	rm -f scsi.a
	$(AR) rcs scsi.a $(SCSI_OBJS)
	sync

aha152x.o: aha152x.c
	$(CC) $(CFLAGS) $(AHA152X) -c aha152x.c 

aic7xxx_asm:	aic7xxx_asm.c
	$(CC) $(CFLAGS) -o $@ aic7xxx_asm.c

aic7xxx_seq.h:	aic7xxx_asm aic7xxx.seq
	./aic7xxx_asm -o $@ aic7xxx.seq

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

scsi_mod.o: $(SCSI_MODULE_VER) hosts.o scsi.o scsi_ioctl.o constants.o \
		scsicam.o scsi_proc.o $(SYMTAB_OBJS)
	$(LD) $(LD_RFLAG) -r -o $@ hosts.o scsi.o scsi_ioctl.o constants.o scsicam.o scsi_proc.o $(SYMTAB_OBJS)

sr_mod.o: $(SCSI_MODULE_VER) sr.o sr_ioctl.o
	$(LD) $(LD_RFLAG) -r -o $@ sr.o sr_ioctl.o

sd_mod.o: $(SCSI_MODULE_VER) sd.o sd_ioctl.o
	$(LD) $(LD_RFLAG) -r -o $@ sd.o sd_ioctl.o

$(SYMTAB_OBJS): $(SYMTAB_OBJS:.o=.c)

modules: $(SCSI_MODULE_VER) $(SCSI_MODULE_OBJS)
	echo $(SCSI_MODULE_OBJS) > ../../modules/SCSI_MODULES
	(cd ../../modules;for i in $(SCSI_MODULE_OBJS); do ln -sf ../drivers/scsi/$$i .; done)

dep:	$(SCSI_DEP)
	$(CPP) -M $(AHA152X) constants.c $(SCSI_OBJS:.o=.c) > .depend
	$(CPP) -M -DMODULE $(patsubst scsi_mod.c,scsi.c hosts.c scsi_ioctl.c \
					constants.c scsicam.c scsi_proc.c, \
			   $(patsubst sd_mod.c,sd.c sd_ioctl.c, \
			   $(patsubst sr_mod.c,sr.c sr_ioctl.c, $(SCSI_MODULE_OBJS:.o=.c)))) >> .depend

include $(TOPDIR)/Rules.make
