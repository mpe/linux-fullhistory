# Makefile for linux/drivers/scsi
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

L_TARGET := scsi.a
L_OBJS   := 
M_OBJS   := 
MX_OBJS  := 
MIX_OBJS :=
MOD_LIST_NAME := SCSI_MODULES
SCSI_SRCS = $(wildcard $(L_OBJS:%.o=%.c))

CFLAGS_aha152x.o =   -DDEBUG_AHA152X -DAUTOCONF
CFLAGS_gdth.o    = # -DDEBUG_GDTH=2 -D__SERIAL__ -D__COM2__ -DGDTH_STATISTICS
CFLAGS_seagate.o =   -DARBITRATE -DPARITY -DSEAGATE_USE_ASM

.SUFFIXES:
.SUFFIXES: .c .o .h .a

ifeq (${CFLAGS},)
CFLAGS = -D__KERNEL__=1 \
	-DMODULE -Wall -Wstrict-prototypes -I. -I../../include \
	-O2 -fomit-frame-pointer

include ../../.config

TOPDIR = ../..

endif

ifeq ($(CONFIG_SCSI),y)
  # We must attach scsi_syms.o to scsi.o, as otherwise there is nothing to
  # pull the object file from the archive.
  O_TARGET := scsi_n_syms.o
  O_OBJS   := scsi.o
  ifeq ($(CONFIG_MODULES),y)
    OX_OBJS  := scsi_syms.o
  endif
  L_OBJS += scsi_n_syms.o hosts.o scsi_ioctl.o constants.o scsicam.o
  L_OBJS += scsi_error.o scsi_obsolete.o scsi_queue.o
  L_OBJS += scsi_proc.o
else
  ifeq ($(CONFIG_SCSI),m)
    MIX_OBJS += scsi_syms.o
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
L_OBJS += sr.o sr_ioctl.o sr_vendor.o
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

ifeq ($(CONFIG_SCSI_PCI2000),y)
L_OBJS += pci2000.o
else
    ifeq ($(CONFIG_SCSI_PCI2000),m)
    M_OBJS += pci2000.o
    endif
endif

ifeq ($(CONFIG_SCSI_PCI2220I),y)
L_OBJS += pci2220i.o
else
    ifeq ($(CONFIG_SCSI_PCI2220I),m)
    M_OBJS += pci2220i.o
    endif
endif

ifeq ($(CONFIG_SCSI_PSI240I),y)
L_OBJS += psi240i.o
else
    ifeq ($(CONFIG_SCSI_PSI240I),m)
    M_OBJS += psi240i.o
    endif
endif

ifeq ($(CONFIG_MVME16x_SCSI),y)
L_OBJS += mvme16x.o 53c7xx.o
else
  ifeq ($(CONFIG_MVME16x_SCSI),m)
  M_OBJS += mvme16x.o 53c7xx.o
  endif
endif

ifeq ($(CONFIG_BVME6000_SCSI),y)
L_OBJS += bvme6000.o 53c7xx.o
else
  ifeq ($(CONFIG_BVME6000_SCSI),m)
  M_OBJS += bvme6000.o 53c7xx.o
  endif
endif

ifeq ($(CONFIG_A4000T_SCSI),y)
L_OBJS += amiga7xx.o 53c7xx.o
else
  ifeq ($(CONFIG_A4000T_SCSI),m)
  M_OBJS += amiga7xx.o 53c7xx.o
  endif
endif

ifeq ($(CONFIG_A4091_SCSI),y)
L_OBJS += amiga7xx.o 53c7xx.o
else
  ifeq ($(CONFIG_A4091_SCSI),m)
  M_OBJS += amiga7xx.o 53c7xx.o
  endif
endif

ifeq ($(CONFIG_BLZ603EPLUS_SCSI),y)
L_OBJS += amiga7xx.o 53c7xx.o
else
  ifeq ($(CONFIG_BLZ603EPLUS_SCSI),m)
  M_OBJS += amiga7xx.o 53c7xx.o
  endif
endif

ifeq ($(CONFIG_WARPENGINE_SCSI),y)
L_OBJS += amiga7xx.o 53c7xx.o
else
  ifeq ($(CONFIG_WARPENGINE_SCSI),m)
  M_OBJS += amiga7xx.o 53c7xx.o
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

ifeq ($(CONFIG_SCSI_SGIWD93),y)
L_OBJS += sgiwd93.o wd33c93.o
else
  ifeq ($(CONFIG_SCSI_SGIWD93),m)
  M_OBJS += sgiwd93.o wd33c93.o
  endif
endif

ifeq ($(CONFIG_SCSI_MCA_53C9X),y)
L_OBJS += NCR53C9x.o mca_53c9x.o
else
  ifeq ($(CONFIG_SCSI_MCA_53C9X),m)
  M_OBJS += NCR53C9x.o mca_53c9x.o
  endif
endif

ifeq ($(CONFIG_CYBERSTORM_SCSI),y)
L_OBJS += NCR53C9x.o cyberstorm.o
else
  ifeq ($(CONFIG_CYBERSTORM_SCSI),m)
  M_OBJS += NCR53C9x.o cyberstorm.o
  endif
endif

ifeq ($(CONFIG_CYBERSTORMII_SCSI),y)
L_OBJS += NCR53C9x.o cyberstormII.o
else
  ifeq ($(CONFIG_CYBERSTORMII_SCSI),m)
  M_OBJS += NCR53C9x.o cyberstormII.o
  endif
endif

ifeq ($(CONFIG_BLZ2060_SCSI),y)
L_OBJS += NCR53C9x.o blz2060.o
else
  ifeq ($(CONFIG_BLZ2060_SCSI),m)
  M_OBJS += NCR53C9x.o blz2060.o
  endif
endif

ifeq ($(CONFIG_BLZ1230_SCSI),y)
L_OBJS += NCR53C9x.o blz1230.o
else
  ifeq ($(CONFIG_BLZ1230_SCSI),m)
  M_OBJS += NCR53C9x.o blz1230.o
  endif
endif

ifeq ($(CONFIG_FASTLANE_SCSI),y)
L_OBJS += NCR53C9x.o fastlane.o
else
  ifeq ($(CONFIG_FASTLANE_SCSI),m)
  M_OBJS += NCR53C9x.o fastlane.o
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

ifeq ($(CONFIG_SCSI_IMM),y)
L_OBJS += imm.o
else
  ifeq ($(CONFIG_SCSI_IMM),m)
  M_OBJS += imm.o
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

ifeq ($(CONFIG_SCSI_ACARD),y)
L_OBJS += atp870u.o
else
  ifeq ($(CONFIG_SCSI_ACARD),m)
  M_OBJS += atp870u.o
  endif
endif

ifeq ($(CONFIG_SCSI_INITIO),y)
L_OBJS += initio.o
else
  ifeq ($(CONFIG_SCSI_INITIO),m)
  M_OBJS += initio.o
  endif
endif

ifeq ($(CONFIG_SCSI_QLOGIC_FC),y)
L_OBJS += qlogicfc.o 
else
  ifeq ($(CONFIG_SCSI_QLOGIC_FC),m)   
  M_OBJS += qlogicfc.o 
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

ifeq ($(CONFIG_SCSI_DC390T),y)
L_OBJS += tmscsim.o
else
  ifeq ($(CONFIG_SCSI_DC390T),m)
  M_OBJS += tmscsim.o
  endif
endif

ifeq ($(CONFIG_SCSI_AM53C974),y)
L_OBJS += AM53C974.o
else
  ifeq ($(CONFIG_SCSI_AM53C974),m)
  M_OBJS += AM53C974.o
  endif
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

ifeq ($(CONFIG_SCSI_QLOGICPTI),y)
L_OBJS += qlogicpti.o
else
  ifeq ($(CONFIG_SCSI_QLOGICPTI),m)
  M_OBJS += qlogicpti.o
  endif
endif

ifeq ($(CONFIG_SCSI_MESH),y)
L_OBJS += mesh.o
else
  ifeq ($(CONFIG_SCSI_MESH),m)
  M_OBJS += mesh.o
  endif
endif

ifeq ($(CONFIG_SCSI_MAC53C94),y)
L_OBJS += mac53c94.o
else
  ifeq ($(CONFIG_SCSI_MAC53C94),m)
  M_OBJS += mac53c94.o
  endif
endif

ifeq ($(CONFIG_SCSI_GDTH),y)
L_OBJS += gdth.o
else
  ifeq ($(CONFIG_SCSI_GDTH),m)
  M_OBJS += gdth.o
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
else
  ifeq ($(CONFIG_SCSI_GENERIC_NCR5380),m)
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

ifeq ($(CONFIG_SCSI_NCR53C8XX),y)
L_OBJS += ncr53c8xx.o 
else
  ifeq ($(CONFIG_SCSI_NCR53C8XX),m)
  M_OBJS += ncr53c8xx.o
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

ifeq ($(CONFIG_SCSI_IBMMCA),y)
L_OBJS += ibmmca.o
else
  ifeq ($(CONFIG_SCSI_IBMMCA),m)
  M_OBJS += ibmmca.o
  endif
endif

ifeq ($(CONFIG_SCSI_FD_MCS),y)
L_OBJS += fd_mcs.o
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

ifeq ($(CONFIG_SCSI_PLUTO),y)
L_OBJS += pluto.o
else
  ifeq ($(CONFIG_SCSI_PLUTO),m)
  M_OBJS += pluto.o
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

ifeq ($(CONFIG_SCSI_MEGARAID),y)
L_OBJS += megaraid.o
else
  ifeq ($(CONFIG_SCSI_MEGARAID),m)
  M_OBJS += megaraid.o
  endif
endif

ifeq ($(CONFIG_BLK_DEV_IDESCSI),y)
L_OBJS += ide-scsi.o
else
  ifeq ($(CONFIG_BLK_DEV_IDESCSI),m)
  M_OBJS += ide-scsi.o
  endif
endif

ifeq ($(CONFIG_JAZZ_ESP),y)
L_OBJS += NCR53C9x.o jazz_esp.o
endif

include $(TOPDIR)/Rules.make

53c8xx_d.h: 53c7,8xx.scr script_asm.pl
	ln -sf 53c7,8xx.scr fake8.c
	$(CPP) -traditional -DCHIP=810 fake8.c | grep -v '^#' | perl script_asm.pl 
	mv script.h 53c8xx_d.h
	mv scriptu.h 53c8xx_u.h
	rm fake8.c

53c8xx_u.h: 53c8xx_d.h

53c7xx_d.h: 53c7xx.scr script_asm.pl
	ln -sf 53c7xx.scr fake7.c
	$(CPP) -traditional -DCHIP=710 fake7.c | grep -v '^#' | perl -s script_asm.pl -ncr7x0_family
	mv script.h 53c7xx_d.h
	mv scriptu.h 53c7xx_u.h
	rm fake7.c

53c7xx_u.h: 53c7xx_d.h

53c7xx.o : 53c7xx_d.h 53c7xx.c
	$(CC) $(CFLAGS) -c 53c7xx.c

initio.o: ini9100u.c i91uscsi.c
	$(CC) $(CFLAGS) -c ini9100u.c -o ini9100u.o
	$(CC) $(CFLAGS) -c i91uscsi.c -o i91uscsi.o
	$(LD) -r -o initio.o ini9100u.o i91uscsi.o
	rm -f ini9100u.o i91uscsi.o

megaraid.o: megaraid.c
	$(CC) $(CFLAGS) -c megaraid.c

scsi_mod.o: $(MIX_OBJS) hosts.o scsi.o scsi_ioctl.o constants.o \
		scsicam.o scsi_proc.o scsi_error.o scsi_obsolete.o scsi_queue.o
	$(LD) $(LD_RFLAG) -r -o $@ $(MIX_OBJS) hosts.o scsi.o scsi_ioctl.o \
		constants.o scsicam.o scsi_proc.o               \
		scsi_error.o scsi_obsolete.o scsi_queue.o \

sr_mod.o: sr.o sr_ioctl.o sr_vendor.o
	$(LD) $(LD_RFLAG) -r -o $@ sr.o sr_ioctl.o sr_vendor.o

sd_mod.o: sd.o sd_ioctl.o
	$(LD) $(LD_RFLAG) -r -o $@ sd.o sd_ioctl.o
