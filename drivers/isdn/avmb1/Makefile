#
# $Id: Makefile,v 1.16 2000/03/17 12:15:44 calle Exp $
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
# Revision 1.16  2000/03/17 12:15:44  calle
# ALL_SUB_DIRS were wrong.
#
# Revision 1.15  2000/03/16 15:21:03  calle
# Bugfix in c4_remove: loop 5 times instead of 4 :-(
#
# Revision 1.14  2000/03/13 17:50:55  calle
# Added avm_cs.c for 2.3.x PCMCIA support.
#
# Revision 1.13  2000/03/08 17:06:33  calle
# - changes for devfs and 2.3.49
# - capifs now configurable (no need with devfs)
# - New Middleware ioctl CAPI_NCCI_GETUNIT
# - Middleware again tested with 2.2.14 and 2.3.49 (with and without devfs)
#
# Revision 1.12  2000/03/06 18:00:23  calle
# - Middleware extention now working with 2.3.49 (capifs).
# - Fixed typos in debug section of capi.c
# - Bugfix: Makefile corrected for b1pcmcia.c
#
# Revision 1.11  2000/03/06 09:17:07  calle
# - capifs: fileoperations now in inode (change for 2.3.49)
# - Config.in: Middleware extention not a tristate, uups.
#
# Revision 1.10  2000/03/03 16:48:38  calle
# - Added CAPI2.0 Middleware support (CONFIG_ISDN_CAPI)
#   It is now possible to create a connection with a CAPI2.0 applikation
#   and than to handle the data connection from /dev/capi/ (capifs) and also
#   using async or sync PPP on this connection.
#   The two major device number 190 and 191 are not confirmed yet,
#   but I want to save the code in cvs, before I go on.
#
# Revision 1.9  2000/03/03 15:50:42  calle
# - kernel CAPI:
#   - Changed parameter "param" in capi_signal from __u32 to void *.
#   - rewrote notifier handling in kcapi.c
#   - new notifier NCCI_UP and NCCI_DOWN
# - User CAPI:
#   - /dev/capi20 is now a cloning device.
#   - middleware extentions prepared.
# - capidrv.c
#   - locking of list operations and module count updates.
#
# Revision 1.8  2000/01/25 14:33:38  calle
# - Added Support AVM B1 PCI V4.0 (tested with prototype)
#   - splitted up t1pci.c into b1dma.c for common function with b1pciv4
#   - support for revision register
#
# Revision 1.7  1999/09/15 08:16:03  calle
# Implementation of 64Bit extention complete.
#
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

SUB_DIRS     :=
MOD_SUB_DIRS :=
ALL_SUB_DIRS := # fcpci fcclassic
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

ifeq ($(CONFIG_ISDN_CAPI),y)
  O_TARGET += avmb1.o
  OX_OBJS  += kcapi.o
  O_OBJS   += capi.o
  ifdef CONFIG_ISDN_CAPIFS
  	OX_OBJS   += capifs.o
  endif
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
  ifdef CONFIG_ISDN_DRV_AVMB1_T1PCI
  	O_OBJS   += t1pci.o
  endif
  ifdef CONFIG_ISDN_DRV_AVMB1_C4
  	O_OBJS   += c4.o
  endif
  ifdef CONFIG_ISDN_DRV_AVMB1_FCPCI
	SUB_DIRS += fcpci
	MOD_SUB_DIRS += fcpci
  endif
  ifdef CONFIG_ISDN_DRV_AVMB1_FCCLASSIC
	SUB_DIRS += fcclassic
	MOD_SUB_DIRS += fcclassic
  endif
  OX_OBJS  += capiutil.o capidrv.o b1.o b1dma.o
else
  ifeq ($(CONFIG_ISDN_CAPI),m)
  	O_TARGET += kernelcapi.o
  	OX_OBJS  += kcapi.o
  	M_OBJS   += capi.o kernelcapi.o
  	ifdef CONFIG_ISDN_CAPIFS
  		MX_OBJS   += capifs.o
  	endif
  	ifdef CONFIG_ISDN_DRV_AVMB1_B1ISA
  		M_OBJS   += b1isa.o
  	endif
  	ifdef CONFIG_ISDN_DRV_AVMB1_B1PCI
  		M_OBJS  += b1pci.o
  	endif
  	ifdef CONFIG_ISDN_DRV_AVMB1_T1ISA
  		M_OBJS   += t1isa.o
  	endif
  	ifdef CONFIG_ISDN_DRV_AVMB1_B1PCMCIA
  		MX_OBJS  += b1pcmcia.o
		ifeq ($(CONFIG_HOTPLUG),y)
			ifneq ($(CONFIG_PCMCIA),n)
  				M_OBJS   += avm_cs.o
			endif
		endif
  	endif
  	ifdef CONFIG_ISDN_DRV_AVMB1_T1PCI
  		M_OBJS   += t1pci.o
  	endif
  	ifdef CONFIG_ISDN_DRV_AVMB1_C4
  		M_OBJS   += c4.o
  	endif
  	ifdef CONFIG_ISDN_DRV_AVMB1_FCPCI
    		MOD_SUB_DIRS += fcpci
	endif
  	ifdef CONFIG_ISDN_DRV_AVMB1_FCCLASSIC
    		MOD_SUB_DIRS += fcclassic
	endif
  	MX_OBJS  += capiutil.o capidrv.o b1.o b1dma.o
  endif
endif

include $(TOPDIR)/Rules.make
