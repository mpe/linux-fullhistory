#
# Makefile for the kernel character device drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now inherited from the
# parent makes..
#

O_OBJS		:=
OX_OBJS		:=
M_OBJS		:=
MX_OBJS		:=

# Object file lists.

obj-y		:=
obj-m		:=
obj-n		:=
obj-		:=

SUB_DIRS     := 
MOD_SUB_DIRS := $(SUB_DIRS)
ALL_SUB_DIRS := $(SUB_DIRS) ftape joystick pcmcia rio

#
# This file contains the font map for the default (hardware) font
#
FONTMAPFILE = cp437.uni

O_TARGET := char.o
obj-y	 += tty_io.o n_tty.o tty_ioctl.o mem.o raw.o pty.o misc.o random.o

# All of the (potential) objects that export symbols.
# This list comes from 'grep -l EXPORT_SYMBOL *.[hc]'.

export-objs     :=	busmouse.o console.o i2c-old.o keyboard.o sysrq.o \
			misc.o pty.o random.o selection.o serial.o videodev.o \
			tty_io.o bttv.o cpia.o

KEYMAP   =defkeymap.o
KEYBD    =pc_keyb.o
CONSOLE  =console.o
SERIAL   =serial.o

ifeq ($(ARCH),s390)
  KEYMAP   =
  KEYBD    =
  CONSOLE  =
  SERIAL   =
endif

ifeq ($(ARCH),m68k)
   ifdef CONFIG_AMIGA
      KEYBD = amikeyb.o
   else
      KEYBD    =
   endif
   SERIAL   =
endif

ifeq ($(ARCH),arm)
  KEYMAP   =
  KEYBD    =
  CONSOLE  =
  SERIAL   =
endif

ifeq ($(ARCH),sh)
  KEYMAP   =
  KEYBD    =
  CONSOLE  =
  SERIAL =
  ifeq ($(CONFIG_SERIAL),y)
  SERIAL = generic_serial.o sh-sci.o
  else
    ifeq ($(CONFIG_SERIAL),m)
    SERIAL = sh-sci.o
    endif
  endif
endif

ifeq ($(CONFIG_DECSTATION),y)
  KEYBD    =
  SERIAL   =
endif

ifeq ($(CONFIG_BAGET_MIPS),y)
  KEYBD    =
  SERIAL   =
endif

ifneq ($(CONFIG_SUN_SERIAL),)
  SERIAL   =
endif


obj-$(CONFIG_VT) += vt.o vc_screen.o consolemap.o consolemap_deftbl.o $(CONSOLE) selection.o
obj-$(CONFIG_SERIAL) += $(SERIAL)
obj-$(CONFIG_SERIAL_21285) += serial_21285.o

ifndef CONFIG_SUN_KEYBOARD
  obj-$(CONFIG_VT) += keyboard.o $(KEYMAP) $(KEYBD)
else
  obj-$(CONFIG_PCI) += keyboard.o $(KEYMAP)
endif

obj-$(CONFIG_MAGIC_SYSRQ) += sysrq.o

obj-$(CONFIG_ATARI_DSP56K) += dsp56k.o
ifeq ($(CONFIG_ATARI_DSP56K),y)
S = y
else
  ifeq ($(CONFIG_ATARI_DSP56K),m)
  SM = y
  endif
endif

obj-$(CONFIG_ROCKETPORT) += rocket.o
obj-$(CONFIG_MOXA_SMARTIO) += mxser.o
obj-$(CONFIG_MOXA_INTELLIO) += moxa.o
obj-$(CONFIG_DIGI) += pcxx.o
obj-$(CONFIG_DIGIEPCA) += epca.o
obj-$(CONFIG_CYCLADES) += cyclades.o
obj-$(CONFIG_STALLION) += stallion.o
obj-$(CONFIG_ISTALLION) += istallion.o
obj-$(CONFIG_COMPUTONE) += ip2.o ip2main.o
obj-$(CONFIG_RISCOM8) += riscom8.o
obj-$(CONFIG_ISI) += isicom.o
obj-$(CONFIG_ESPSERIAL) += esp.o
obj-$(CONFIG_SYNCLINK) += synclink.o
obj-$(CONFIG_N_HDLC) += n_hdlc.o
obj-$(CONFIG_SPECIALIX) += specialix.o
obj-$(CONFIG_AMIGA_BUILTIN_SERIAL) += amiserial.o
obj-$(CONFIG_SX) += sx.o


# If either is in the kernel, generic_serial goes in the kernel, and
# the module is no longer required. The "in kernel" case is last to be
# able to override the module case....  This is an example of the new
# "makefile automatically figures out the dependencies"....  -- REW

GS = n
ifeq ($(CONFIG_SX),m)
  GS = m
endif
ifeq ($(CONFIG_RIO),m)
  GS = m 
endif
ifeq ($(CONFIG_SX),y)
  GS = y 
endif
ifeq ($(CONFIG_RIO),y)
  GS = y 
endif
obj-$(GS) += generic_serial.o




ifeq ($(CONFIG_RIO),y)
obj-y += rio/rio.o  generic_serial.o
SUB_DIRS += rio
MOD_SUB_DIRS += rio
else
  ifeq ($(CONFIG_RIO),m)
  obj-m += generic_serial.o
  MOD_SUB_DIRS += rio
  endif
endif

obj-$(CONFIG_ATIXL_BUSMOUSE) += atixlmouse.o
obj-$(CONFIG_LOGIBUSMOUSE) += logibusmouse.o
obj-$(CONFIG_PRINTER) += lp.o

ifeq ($(CONFIG_JOYSTICK),y)
obj-y += joystick/js.o
SUB_DIRS += joystick
MOD_SUB_DIRS += joystick
else
  ifeq ($(CONFIG_JOYSTICK),m)
  MOD_SUB_DIRS += joystick
  endif
endif

obj-$(CONFIG_BUSMOUSE) += busmouse.o
ifeq ($(CONFIG_BUSMOUSE),y)
M = y
else
  ifeq ($(CONFIG_BUSMOUSE),m)
  MM = m
  endif
endif

obj-$(CONFIG_DTLK) += dtlk.o
obj-$(CONFIG_R3964) += n_r3964.o
obj-$(CONFIG_APPLICOM) += applicom.o
obj-$(CONFIG_MS_BUSMOUSE) += msbusmouse.o
obj-$(CONFIG_82C710_MOUSE) += qpmouse.o
obj-$(CONFIG_SOFT_WATCHDOG) += softdog.o
obj-$(CONFIG_PCWATCHDOG) += pcwd.o
obj-$(CONFIG_ACQUIRE_WDT) += acquirewdt.o
obj-$(CONFIG_MIXCOMWD) += mixcomwd.o
obj-$(CONFIG_AMIGAMOUSE) += amigamouse.o
obj-$(CONFIG_ATARIMOUSE) += atarimouse.o
obj-$(CONFIG_ADBMOUSE) += adbmouse.o busmouse.o
obj-$(CONFIG_PC110_PAD) += pc110pad.o
obj-$(CONFIG_WDT) += wdt.o
obj-$(CONFIG_WDTPCI) += wdt_pci.o
obj-$(CONFIG_RTC) += rtc.o
obj-$(CONFIG_EFI_RTC) += efirtc.o
ifeq ($(CONFIG_PPC),)
  obj-$(CONFIG_NVRAM) += nvram.o
endif

obj-$(CONFIG_VIDEO_DEV) += videodev.o

obj-$(CONFIG_21285_WATCHDOG) += wdt285.o
obj-$(CONFIG_977_WATCHDOG) += wdt977.o
obj-$(CONFIG_DS1620) += ds1620.o

#
# for external dependencies in arm/config.in and video/config.in
#
ifeq ($(CONFIG_BUS_I2C),y)
	L_I2C=y
else
  ifeq ($(CONFIG_BUS_I2C),m)
	L_I2C=m
  endif
endif

obj-$(CONFIG_VIDEO_BT848) += bttv.o msp3400.o tda8425.o tda985x.o tea6300.o
ifeq ($(CONFIG_VIDEO_BT848),y)
L_TUNERS=y
else
  ifeq ($(CONFIG_VIDEO_BT848),m)
    L_TUNERS=m
  endif
endif

obj-$(CONFIG_VIDEO_ZR36120) += zoran.o
ifeq ($(CONFIG_VIDEO_ZR36120),y)
L_I2C=y
L_TUNERS=y
L_DECODERS=y
else
  ifeq ($(CONFIG_VIDEO_ZR36120),m)
    L_I2C=m
    L_TUNERS=m
    L_DECODERS=m
  endif
endif

obj-$(CONFIG_I2C_PARPORT) += i2c-parport.o
ifeq ($(CONFIG_I2C_PARPORT),y)
L_I2C = y
else
  ifeq ($(CONFIG_I2C_PARPORT),m)
    L_I2C = m
  endif
endif

obj-$(CONFIG_VIDEO_SAA5249) += saa5249.o
ifeq ($(CONFIG_VIDEO_SAA5249),y)
L_I2C=y
else
  ifeq ($(CONFIG_VIDEO_SAA5249),m)
    L_I2C=m
  endif
endif

obj-$(CONFIG_VIDEO_CQCAM) += c-qcam.o
obj-$(CONFIG_VIDEO_BWQCAM) += bw-qcam.o
obj-$(CONFIG_VIDEO_ZORAN) += buz.o
ifeq ($(CONFIG_VIDEO_ZORAN),y)
L_I2C=y
L_DECODERS=y
else
  ifeq ($(CONFIG_VIDEO_ZORAN),m)
    L_I2C=m
    L_DECODERS=m
  endif
endif

obj-$(CONFIG_VIDEO_LML33) += bt856.o bt819.o
obj-$(CONFIG_VIDEO_PMS) += pms.o
obj-$(CONFIG_VIDEO_PLANB) += planb.o
obj-$(CONFIG_VIDEO_VINO) += vino.o
obj-$(CONFIG_VIDEO_STRADIS) += stradis.o
obj-$(CONFIG_VIDEO_CPIA) += cpia.o
obj-$(CONFIG_VIDEO_CPIA_PP) += cpia_pp.o
obj-$(CONFIG_VIDEO_CPIA_USB) += cpia_usb.o
obj-$(CONFIG_RADIO_AZTECH) += radio-aztech.o
obj-$(CONFIG_RADIO_RTRACK2) += radio-rtrack2.o
obj-$(CONFIG_RADIO_SF16FMI) += radio-sf16fmi.o
obj-$(CONFIG_RADIO_CADET) += radio-cadet.o
obj-$(CONFIG_RADIO_TYPHOON) += radio-typhoon.o
obj-$(CONFIG_RADIO_TERRATEC) += radio-terratec.o
obj-$(CONFIG_RADIO_RTRACK) += radio-aimslab.o
obj-$(CONFIG_RADIO_ZOLTRIX) += radio-zoltrix.o
obj-$(CONFIG_RADIO_MIROPCM20) += radio-miropcm20.o
obj-$(CONFIG_RADIO_GEMTEK) += radio-gemtek.o
obj-$(CONFIG_RADIO_TRUST) += radio-trust.o
obj-$(CONFIG_TUNER_3036) += tuner-3036.o
obj-$(CONFIG_QIC02_TAPE) += tpqic02.o

ifeq ($(CONFIG_FTAPE),y)
obj-y       += ftape/ftape.o
SUB_DIRS     += ftape
ifneq ($(CONFIG_ZFTAPE),n)
MOD_SUB_DIRS += ftape
endif
else
  ifeq ($(CONFIG_FTAPE),m)
  MOD_SUB_DIRS += ftape
  endif
endif

obj-$(CONFIG_H8) += h8.o
obj-$(CONFIG_PPDEV) += ppdev.o

# set when a framegrabber supports external tuners
obj-$(L_TUNERS) += tuner.o

# set when a framegrabber supports external decoders
obj-$(L_DECODERS) += saa7110.o saa7111.o saa7185.o

# set when a framegrabber implements i2c support
obj-$(L_I2C) += i2c-old.o

ifeq ($(CONFIG_DZ),y)
  L_OBJS += dz.o
endif

obj-$(CONFIG_NWBUTTON) += nwbutton.o
obj-$(CONFIG_NWFLASH) += nwflash.o

ifeq ($(CONFIG_DRM),y)
  SUB_DIRS += drm
  ALL_SUB_DIRS += drm
  MOD_SUB_DIRS += drm
endif

ifeq ($(CONFIG_PCMCIA),y)
  SUB_DIRS += pcmcia
  MOD_IN_SUB_DIRS += pcmcia
else
  ifeq ($(CONFIG_PCMCIA),m)
    MOD_IN_SUB_DIRS += pcmcia
  endif
endif

ifeq ($(CONFIG_AGP), y)
  SUB_DIRS += agp
  ALL_SUB_DIRS += agp
  MOD_SUB_DIRS += agp
else
  ifeq ($(CONFIG_AGP), m)
    ALL_SUB_DIRS += agp
    MOD_SUB_DIRS += agp
  endif
endif

# Extract lists of the multi-part drivers.
# The 'int-*' lists are the intermediate files used to build the multi's.

multi-y         := $(filter $(list-multi), $(obj-y))
multi-m         := $(filter $(list-multi), $(obj-m))
int-y           := $(sort $(foreach m, $(multi-y), $($(basename $(m))-objs)))
int-m           := $(sort $(foreach m, $(multi-m), $($(basename $(m))-objs)))

# Files that are both resident and modular: remove from modular.

obj-m           := $(filter-out $(obj-y), $(obj-m))
int-m           := $(filter-out $(int-y), $(int-m))

# Take multi-part drivers out of obj-y and put components in.

obj-y           := $(filter-out $(list-multi), $(obj-y)) $(int-y)

# Translate to Rules.make lists.

O_OBJS          := $(filter-out $(export-objs), $(obj-y))
OX_OBJS         := $(filter     $(export-objs), $(obj-y))
M_OBJS          := $(sort $(filter-out $(export-objs), $(obj-m)))
MX_OBJS         := $(sort $(filter     $(export-objs), $(obj-m)))

include $(TOPDIR)/Rules.make

fastdep:

conmakehash: conmakehash.c
	$(HOSTCC) $(HOSTCFLAGS) -o conmakehash conmakehash.c

consolemap_deftbl.c: $(FONTMAPFILE) conmakehash
	./conmakehash $(FONTMAPFILE) > consolemap_deftbl.c

consolemap_deftbl.o: consolemap_deftbl.c $(TOPDIR)/include/linux/types.h

defkeymap.c: defkeymap.map
	loadkeys --mktable defkeymap.map > defkeymap.c

zoran.o: zr36120.o zr36120_i2c.o zr36120_mem.o
	$(LD) $(LD_RFLAG) -r -o $@ zr36120.o zr36120_i2c.o zr36120_mem.o
