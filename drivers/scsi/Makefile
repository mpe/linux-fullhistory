
# Makefile for linux/drivers/scsi
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

ifdef CONFIG_KERNEL_ELF
# This is used for ELF - it needs to migrate or be moved.
LD_RFLAG = -m elf_i386
endif

L_TARGET := scsi.a
L_OBJS   := 
M_OBJS   := 
MOD_LIST_NAME := SCSI_MODULES
SCSI_SRCS = $(wildcard $(L_OBJS:%.o=%.c))

AHA152X	= -DDEBUG_AHA152X -DAUTOCONF -DSKIP_BIOSTEST -DIRQ=11

ifeq (${CFLAGS},)
CFLAGS = -D__KERNEL__=1 \
	-DMODULE -Wall -Wstrict-prototypes -I. -I../../include \
	-O2 -fomit-frame-pointer -m486

include ../../.config

SYMTAB_OBJS = scsi_syms.o

TOPDIR = ../..
include ../../versions.mk

endif

ifdef CONFIG_SCSI
L_OBJS += hosts.o scsi.o scsi_ioctl.o constants.o scsicam.o scsi_proc.o
else
ifdef CONFIG_MODVERSIONS
# Create this before we build anything else.
SCSI_MODULE_VER := scsi_syms.ver
endif
M_OBJS += scsi_mod.o
endif

ifdef CONFIG_CHR_DEV_ST
L_OBJS += st.o
else
M_OBJS += st.o
endif

ifdef CONFIG_BLK_DEV_SD
L_OBJS += sd.o sd_ioctl.o
else
M_OBJS += sd_mod.o
endif

ifdef CONFIG_BLK_DEV_SR
L_OBJS += sr.o sr_ioctl.o
else
M_OBJS += sr_mod.o
endif

ifdef CONFIG_CHR_DEV_SG
L_OBJS += sg.o
else
M_OBJS += sg.o
endif

ifdef CONFIG_SCSI_QLOGIC
L_OBJS += qlogic.o
else
M_OBJS += qlogic.o
endif

ifdef CONFIG_SCSI_AHA152X
L_OBJS += aha152x.o
else
M_OBJS += aha152x.o
endif

ifdef CONFIG_SCSI_AHA1542
L_OBJS += aha1542.o
else
M_OBJS += aha1542.o
endif

ifdef CONFIG_SCSI_AHA1740
L_OBJS += aha1740.o
else
M_OBJS += aha1740.o
endif

ifdef CONFIG_SCSI_AIC7XXX
L_OBJS += aic7xxx.o
else
M_OBJS += aic7xxx.o
endif

ifdef CONFIG_SCSI_BUSLOGIC
L_OBJS += buslogic.o
else
M_OBJS += buslogic.o
endif

ifdef CONFIG_SCSI_EATA_DMA
L_OBJS += eata_dma.o
else
M_OBJS += eata_dma.o
endif

ifdef CONFIG_SCSI_EATA_PIO
L_OBJS += eata_pio.o
else
M_OBJS += eata_pio.o
endif

ifdef CONFIG_SCSI_U14_34F
L_OBJS += u14-34f.o
else
M_OBJS += u14-34f.o
endif

ifdef CONFIG_SCSI_DEBUG
L_OBJS += scsi_debug.o
else
M_OBJS += scsi_debug.o
endif

ifdef CONFIG_SCSI_FUTURE_DOMAIN
L_OBJS += fdomain.o
else
M_OBJS += fdomain.o
endif

ifdef CONFIG_SCSI_IN2000
L_OBJS += in2000.o
else
M_OBJS += in2000.o
endif

ifdef CONFIG_SCSI_GENERIC_NCR5380
L_OBJS += g_NCR5380.o
endif

ifdef CONFIG_SCSI_NCR53C7xx
L_OBJS += 53c7,8xx.o 
else
M_OBJS += 53c7,8xx.o
endif

ifdef CONFIG_SCSI_PAS16
L_OBJS += pas16.o
endif

ifdef CONFIG_SCSI_SEAGATE
L_OBJS += seagate.o
else
ifdef CONFIG_SCSI_FD_8xx
L_OBJS += seagate.o
else
M_OBJS += seagate.o
endif
endif

ifdef CONFIG_SCSI_7000FASST
L_OBJS += wd7000.o
else
M_OBJS += wd7000.o
endif

ifdef CONFIG_SCSI_T128
L_OBJS += t128.o
endif

ifdef CONFIG_SCSI_ULTRASTOR
L_OBJS += ultrastor.o
else
M_OBJS += ultrastor.o
endif

ifdef CONFIG_SCSI_EATA
L_OBJS += eata.o
else
M_OBJS += eata.o
endif

include $(TOPDIR)/Rules.make

aha152x.o: aha152x.c
	$(CC) $(CFLAGS) $(AHA152X) -c aha152x.c 

aic7xxx_asm:	aic7xxx_asm.c
	$(CC) $(CFLAGS) -o $@ aic7xxx_asm.c

aic7xxx.c: aic7xxx_seq.h
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

modules: $(SCSI_MODULE_VER)
