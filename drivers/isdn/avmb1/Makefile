#
# $Id: Makefile,v 1.6 1999/07/20 06:41:44 calle Exp $
#
# Makefile for the CAPI and AVM-B1 device drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now inherited from the
# parent makes..
#
# $Log: Makefile,v $
# Revision 1.6  1999/07/20 06:41:44  calle
# Bugfix: After the redesign of the AVM B1 driver, the driver didn't even
#         compile, if not selected as modules.
#
# Revision 1.5  1999/07/01 15:26:20  calle
# complete new version (I love it):
# + new hardware independed "capi_driver" interface that will make it easy to:
#   - support other controllers with CAPI-2.0 (i.e. USB Controller)
#   - write a CAPI-2.0 for the passive cards
#   - support serial link CAPI-2.0 boxes.
# + wrote "capi_driver" for all supported cards.
# + "capi_driver" (supported cards) now have to be configured with
#   make menuconfig, in the past all supported cards where included
#   at once.
# + new and better informations in /proc/capi/
# + new ioctl to switch trace of capi messages per controller
#   using "avmcapictrl trace [contr] on|off|...."
# + complete testcircle with all supported cards and also the
#   PCMCIA cards (now patch for pcmcia-cs-3.0.13 needed) done.
#
# Revision 1.4  1997/03/30 17:10:40  calle
# added support for AVM-B1-PCI card.
#
# Revision 1.3  1997/03/22 02:00:57  fritz
# -Reworked toplevel Makefile. From now on, no different Makefiles
#  for standalone- and in-kernel-compilation are needed any more.
# -Added local Rules.make for above reason.
# -Experimental changes in teles3.c for enhanced IRQ-checking with
#  2.1.X and SMP kernels.
# -Removed diffstd-script, same functionality is in stddiff -r.
# -Enhanced scripts std2kern and stddiff.
#
# Revision 1.1  1997/03/05 21:26:14  fritz
# Renamed, according naming conventions in CVS tree.
#
# Revision 1.1  1997/03/04 21:50:26  calle
# Frirst version in isdn4linux
#
# Revision 2.2  1997/02/12 09:31:39  calle
#
# Revision 1.1  1997/01/31 10:32:20  calle
# Initial revision
#
#

#
# Objects that don't export a symtab
#
L_OBJS := # used as component of an L_TARGET
O_OBJS := # used as component of an O_TARGET
M_OBJS := # used as module
#
# Objects that do export a symtab
#
LX_OBJS := # used as component of an L_TARGET
OX_OBJS := # used as component of an O_TARGET
MX_OBJS := # used as module
#
# Targets, created by linking others
#
O_TARGET := # used for .o targets (from O and OX objects)
L_TARGET := # used for .a targets (from L and LX objects)

ifeq ($(CONFIG_ISDN_DRV_AVMB1),y)
  O_TARGET += avmb1.o
  OX_OBJS  += kcapi.o
  O_OBJS   += capi.o
  ifdef CONFIG_ISDN_DRV_AVMB1_B1ISA
  O_OBJS   += b1isa.o
  endif
  ifdef CONFIG_ISDN_DRV_AVMB1_B1PCI
  O_OBJS  += b1pci.o
  endif
  ifdef CONFIG_ISDN_DRV_AVMB1_T1ISA
  O_OBJS   += t1isa.o
  endif
  ifdef CONFIG_ISDN_DRV_AVMB1_B1PCMCIA
  OX_OBJS   += b1pcmcia.o
  endif
  OX_OBJS  += capiutil.o capidrv.o b1.o
else
  ifeq ($(CONFIG_ISDN_DRV_AVMB1),m)
  O_TARGET += kernelcapi.o
  OX_OBJS  += kcapi.o
  M_OBJS   += capi.o kernelcapi.o
  ifdef CONFIG_ISDN_DRV_AVMB1_B1ISA
  M_OBJS   += b1isa.o
  endif
  ifdef CONFIG_ISDN_DRV_AVMB1_B1PCI
  M_OBJS  += b1pci.o
  endif
  ifdef CONFIG_ISDN_DRV_AVMB1_T1ISA
  M_OBJS   += t1isa.o
  endif
  MX_OBJS  += capiutil.o capidrv.o b1.o
  ifdef CONFIG_ISDN_DRV_AVMB1_B1PCMCIA
  MX_OBJS  += b1pcmcia.o
  endif
  endif
endif

include $(TOPDIR)/Rules.make
