
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
MX_OBJS  := 
MOD_LIST_NAME := SCSI_MODULES
SCSI_SRCS = $(wildcard $(L_OBJS:%.o=%.c))

AHA152X	= -DDEBUG_AHA152X -DAUTOCONF

.SUFFIXES:
.SUFFIXES: .c .o .h .a

ifeq (${CFLAGS},)
CFLAGS = -D__KERNEL__=1 \
	-DMODULE -Wall -Wstrict-prototypes -I. -I../../include \
	-O2 -fomit-frame-pointer -m486

include ../../.config

TOPDIR = ../..

endif

ifeq ($(CONFIG_SCSI),y)
L_OBJS += hosts.o scsi.o scsi_ioctl.o constants.o scsicam.o
  ifeq ($(CONFIG_PROC_FS),y)
    L_OBJS += scsi_proc.o
  endif
LX_OBJS += scsi_syms.o
else
  ifeq ($(CONFIG_SCSI),m)
    MX_OBJS += scsi_syms.o
    M_OBJS += scsi_mod.o
  endif
endif

ifeq ($(CONFIG_CHR_DEV_ST),y)
L_OBJS += st.o
else
  ifeq ($(CONFIG_CHR_DEV_ST),m)
  M_OBJS += st.o
  endif
endif

ifeq ($(CONFIG_BLK_DEV_SD),y)
L_OBJS += sd.o sd_ioctl.o
else
  ifeq ($(CONFIG_BLK_DEV_SD),m)
  M_OBJS += sd_mod.o
  endif
endif

ifeq ($(CONFIG_BLK_DEV_SR),y)
L_OBJS += sr.o sr_ioctl.o
else
  ifeq ($(CONFIG_BLK_DEV_SR),m)
  M_OBJS += sr_mod.o
  endif
endif

ifeq ($(CONFIG_CHR_DEV_SG),y)
L_OBJS += sg.o
else
  ifeq ($(CONFIG_CHR_DEV_SG),m)
  M_OBJS += sg.o
  endif
endif

ifeq ($(CONFIG_SCSI_ADVANSYS),y)
L_OBJS += advansys.o
else
  ifeq ($(CONFIG_SCSI_ADVANSYS),m)
  M_OBJS += advansys.o
  endif
endif

ifeq ($(CONFIG_A3000_SCSI),y)
L_OBJS += a3000.o wd33c93.o
else
  ifeq ($(CONFIG_A3000_SCSI),m)
  M_OBJS += a3000.o wd33c93.o
  endif
endif

ifeq ($(CONFIG_A2091_SCSI),y)
L_OBJS += a2091.o wd33c93.o
else
  ifeq ($(CONFIG_A2091_SCSI),m)
  M_OBJS += a2091.o wd33c93.o
  endif
endif

ifeq ($(CONFIG_GVP11_SCSI),y)
L_OBJS += gvp11.o wd33c93.o
else
  ifeq ($(CONFIG_GVP11_SCSI),m)
  M_OBJS += gvp11.o wd33c93.o
  endif
endif

ifeq ($(CONFIG_ATARI_SCSI),y)
L_OBJS += atari_scsi.o
else
  ifeq ($(CONFIG_ATARI_SCSI),m)
  M_OBJS += atari_scsi.o
  endif
endif

ifeq ($(CONFIG_SCSI_PPA),y)
L_OBJS += ppa.o
else
  ifeq ($(CONFIG_SCSI_PPA),m)
  M_OBJS += ppa.o
  endif
endif

ifeq ($(CONFIG_SCSI_QLOGIC_FAS),y)
L_OBJS += qlogicfas.o
else
  ifeq ($(CONFIG_SCSI_QLOGIC_FAS),m)
  M_OBJS += qlogicfas.o
  endif
endif


ifeq ($(CONFIG_SCSI_QLOGIC_ISP),y)
L_OBJS += qlogicisp.o 
else
  ifeq ($(CONFIG_SCSI_QLOGIC_ISP),m)
  M_OBJS += qlogicisp.o 
  endif
endif


ifeq ($(CONFIG_SCSI_AHA152X),y)
L_OBJS += aha152x.o
else
  ifeq ($(CONFIG_SCSI_AHA152X),m)
  M_OBJS += aha152x.o
  endif
endif

ifeq ($(CONFIG_SCSI_AHA1542),y)
L_OBJS += aha1542.o
else
  ifeq ($(CONFIG_SCSI_AHA1542),m)
  M_OBJS += aha1542.o
  endif
endif

ifeq ($(CONFIG_SCSI_AHA1740),y)
L_OBJS += aha1740.o
else
  ifeq ($(CONFIG_SCSI_AHA1740),m)
  M_OBJS += aha1740.o
  endif
endif

ifeq ($(CONFIG_SCSI_AIC7XXX),y)
L_OBJS += aic7xxx.o
else
  ifeq ($(CONFIG_SCSI_AIC7XXX),m)
  M_OBJS += aic7xxx.o
  endif
endif

ifeq ($(CONFIG_SCSI_AM53C974),y)
L_OBJS += AM53C974.o
endif

ifeq ($(CONFIG_SCSI_BUSLOGIC),y)
L_OBJS += BusLogic.o
else
  ifeq ($(CONFIG_SCSI_BUSLOGIC),m)
  M_OBJS += BusLogic.o
  endif
endif

ifeq ($(CONFIG_SCSI_EATA_DMA),y)
L_OBJS += eata_dma.o
else
  ifeq ($(CONFIG_SCSI_EATA_DMA),m)
  M_OBJS += eata_dma.o
  endif
endif

ifeq ($(CONFIG_SCSI_EATA_PIO),y)
L_OBJS += eata_pio.o
else
  ifeq ($(CONFIG_SCSI_EATA_PIO),m)
  M_OBJS += eata_pio.o
  endif
endif

ifeq ($(CONFIG_SCSI_U14_34F),y)
L_OBJS += u14-34f.o
else
  ifeq ($(CONFIG_SCSI_U14_34F),m)
  M_OBJS += u14-34f.o
  endif
endif

ifeq ($(CONFIG_SCSI_SUNESP),y)
L_OBJS += esp.o
else
  ifeq ($(CONFIG_SCSI_SUNESP),m)
  M_OBJS += esp.o
  endif
endif

ifeq ($(CONFIG_SCSI_DEBUG),y)
L_OBJS += scsi_debug.o
else
  ifeq ($(CONFIG_SCSI_DEBUG),m)
  M_OBJS += scsi_debug.o
  endif
endif

ifeq ($(CONFIG_SCSI_FUTURE_DOMAIN),y)
L_OBJS += fdomain.o
else
  ifeq ($(CONFIG_SCSI_FUTURE_DOMAIN),m)
  M_OBJS += fdomain.o
  endif
endif

ifeq ($(CONFIG_SCSI_IN2000),y)
L_OBJS += in2000.o
else
  ifeq ($(CONFIG_SCSI_IN2000),m)
  M_OBJS += in2000.o
  endif
endif

ifeq ($(CONFIG_SCSI_GENERIC_NCR5380),y)
L_OBJS += g_NCR5380.o
EXTRA_CFLAGS = -DGENERIC_NCR5380_OVERRIDE="{{(NCR5380_map_type)0x350,5,0, BOARD_NCR53C400}};"
else
  ifeq ($(CONFIG_SCSI_GENERIC_NCR5380),m)
  EXTRA_CFLAGS = -DGENERIC_NCR5380_OVERRIDE="{{(NCR5380_map_type)0x350,5,0, BOARD_NCR53C400}};"
  M_OBJS += g_NCR5380.o
  endif
endif

ifeq ($(CONFIG_SCSI_NCR53C7xx),y)
L_OBJS += 53c7,8xx.o 
else
  ifeq ($(CONFIG_SCSI_NCR53C7xx),m)
  M_OBJS += 53c7,8xx.o
  endif
endif

ifeq ($(CONFIG_SCSI_PAS16),y)
L_OBJS += pas16.o
else
  ifeq ($(CONFIG_SCSI_PAS16),m)
  M_OBJS += pas16.o
  endif
endif

ifeq ($(CONFIG_SCSI_SEAGATE),y)
L_OBJS += seagate.o
else
  ifeq ($(CONFIG_SCSI_SEAGATE),m)
  M_OBJS += seagate.o
  endif
endif
ifndef CONFIG_SCSI_SEAGATE
  ifeq ($(CONFIG_SCSI_FD_8xx),y)
  L_OBJS += seagate.o
  else
    ifeq ($(CONFIG_SCSI_FD_8xx),m)
    M_OBJS += seagate.o
    endif
  endif
endif

ifeq ($(CONFIG_SCSI_7000FASST),y)
L_OBJS += wd7000.o
else
  ifeq ($(CONFIG_SCSI_7000FASST),m)
  M_OBJS += wd7000.o
  endif
endif

ifeq ($(CONFIG_SCSI_T128),y)
L_OBJS += t128.o
else
  ifeq ($(CONFIG_SCSI_T128),m)
  M_OBJS += t128.o
  endif
endif

ifeq ($(CONFIG_SCSI_DTC3280),y)
L_OBJS += dtc.o
else
  ifeq ($(CONFIG_SCSI_DTC3280),m)
  M_OBJS += dtc.o
  endif
endif

ifeq ($(CONFIG_SCSI_ULTRASTOR),y)
L_OBJS += ultrastor.o
else
  ifeq ($(CONFIG_SCSI_ULTRASTOR),m)
  M_OBJS += ultrastor.o
  endif
endif

ifeq ($(CONFIG_SCSI_EATA),y)
L_OBJS += eata.o
else
  ifeq ($(CONFIG_SCSI_EATA),m)
  M_OBJS += eata.o
  endif
endif

ifeq ($(CONFIG_SCSI_NCR53C406A),y)
L_OBJS += NCR53c406a.o
else
  ifeq ($(CONFIG_SCSI_NCR53C406A),m)
  M_OBJS += NCR53c406a.o
  endif
endif

include $(TOPDIR)/Rules.make

aha152x.o: aha152x.c
	$(CC) $(CFLAGS) $(AHA152X) -c aha152x.c 

aic7xxx_asm:	aic7xxx_asm.c
	$(HOSTCC) -o $@ aic7xxx_asm.c

aic7xxx.c: aic7xxx_seq.h
aic7xxx_seq.h:	aic7xxx_asm aic7xxx.seq
	./aic7xxx_asm -o $@ aic7xxx.seq

seagate.o: seagate.c
	$(CC) $(CFLAGS) -DARBITRATE -DSLOW_HANDSHAKE -DFAST32 -c seagate.c 

# For debugging, use the -g flag
53c7,8xx.o : 53c7,8xx.c
	$(CC) $(CFLAGS) -g -c 53c7,8xx.c

53c8xx_d.h 53c8xx_u.h : 53c7,8xx.scr script_asm.pl
	ln -sf 53c7,8xx.scr fake.c
	$(CPP) -traditional -DCHIP=810 fake.c | grep -v '^#' | perl script_asm.pl 
	mv script.h 53c8xx_d.h
	mv scriptu.h 53c8xx_u.h
	rm fake.c

scsi_mod.o: $(MX_OBJS) hosts.o scsi.o scsi_ioctl.o constants.o \
		scsicam.o scsi_proc.o
	$(LD) $(LD_RFLAG) -r -o $@ $(MX_OBJS) hosts.o scsi.o scsi_ioctl.o constants.o scsicam.o scsi_proc.o

sr_mod.o: sr.o sr_ioctl.o
	$(LD) $(LD_RFLAG) -r -o $@ sr.o sr_ioctl.o

sd_mod.o: sd.o sd_ioctl.o
	$(LD) $(LD_RFLAG) -r -o $@ sd.o sd_ioctl.o
