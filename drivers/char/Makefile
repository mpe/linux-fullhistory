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

SUB_DIRS     := 
MOD_SUB_DIRS := $(SUB_DIRS)
ALL_SUB_DIRS := $(SUB_DIRS) ftape

#
# This file contains the font map for the default (hardware) font
#
FONTMAPFILE = cp437.uni

L_TARGET := char.a
M_OBJS   :=
L_OBJS   := tty_io.o n_tty.o tty_ioctl.o mem.o random.o
LX_OBJS  := pty.o

ifdef CONFIG_VT
L_OBJS += console.o vt.o vc_screen.o consolemap.o consolemap_deftbl.o
LX_OBJS += selection.o
endif

ifeq ($(CONFIG_SERIAL),y)
  ifndef CONFIG_SUN_SERIAL
  LX_OBJS += serial.o
  endif
else
  ifeq ($(CONFIG_SERIAL),m)
  MX_OBJS += serial.o
  endif
endif

ifndef CONFIG_SUN_KEYBOARD
ifdef CONFIG_VT
L_OBJS += keyboard.o
endif
ifneq ($(ARCH),m68k)
L_OBJS += pc_keyb.o defkeymap.o
endif
ifdef CONFIG_MAGIC_SYSRQ
L_OBJS += sysrq.o
endif
endif

ifeq ($(CONFIG_ATARI_DSP56K),y)
L_OBJS += dsp56k.o
S = y
else
  ifeq ($(CONFIG_ATARI_DSP56K),m)
  M_OBJS += dsp56k.o
  SM = y
  endif
endif

ifeq ($(CONFIG_ROCKETPORT),y)
L_OBJS += rocket.o
else
  ifeq ($(CONFIG_ROCKETPORT),m)
  M_OBJS += rocket.o
  endif
endif

ifeq ($(CONFIG_DIGI),y)
L_OBJS += pcxx.o
else
  ifeq ($(CONFIG_DIGI),m)
  M_OBJS += pcxx.o
  endif
endif

ifeq ($(CONFIG_DIGIEPCA),y)
L_OBJS += epca.o
else
  ifeq ($(CONFIG_DIGIEPCA),m)
  M_OBJS += epca.o
  endif
endif

ifeq ($(CONFIG_CYCLADES),y)
L_OBJS += cyclades.o
else
  ifeq ($(CONFIG_CYCLADES),m)
  M_OBJS += cyclades.o
  endif
endif

ifeq ($(CONFIG_STALLION),y)
L_OBJS += stallion.o
else
  ifeq ($(CONFIG_STALLION),m)
  M_OBJS += stallion.o
  endif
endif

ifeq ($(CONFIG_ISTALLION),y)
L_OBJS += istallion.o
else
  ifeq ($(CONFIG_ISTALLION),m)
  M_OBJS += istallion.o
  endif
endif

ifeq ($(CONFIG_RISCOM8),y)
L_OBJS += riscom8.o
else
  ifeq ($(CONFIG_RISCOM8),m)
  M_OBJS += riscom8.o
  endif
endif

ifeq ($(CONFIG_ESPSERIAL),y)
L_OBJS += esp.o
else
  ifeq ($(CONFIG_ESPSERIAL),m)
  M_OBJS += esp.o
  endif
endif

ifeq ($(CONFIG_SPECIALIX),y)
L_OBJS += specialix.o
else
  ifeq ($(CONFIG_SPECIALIX),m)
  M_OBJS += specialix.o
  endif
endif

ifeq ($(CONFIG_ATIXL_BUSMOUSE),y)
M = y
L_OBJS += atixlmouse.o
else
  ifeq ($(CONFIG_ATIXL_BUSMOUSE),m)
  M_OBJS += atixlmouse.o
  MM = m
  endif
endif

ifeq ($(CONFIG_BUSMOUSE),y)
M = y
L_OBJS += busmouse.o
else
  ifeq ($(CONFIG_BUSMOUSE),m)
  M_OBJS += busmouse.o
  MM = m
  endif
endif

ifeq ($(CONFIG_PRINTER),y)
L_OBJS += lp.o
else
  ifeq ($(CONFIG_PRINTER),m)
  M_OBJS += lp.o
  endif
endif

ifeq ($(CONFIG_JOYSTICK),y)
L_OBJS += joystick.o
else
  ifeq ($(CONFIG_JOYSTICK),m)
  M_OBJS += joystick.o
  endif
endif

ifeq ($(CONFIG_MS_BUSMOUSE),y)
M = y
L_OBJS += msbusmouse.o
else
  ifeq ($(CONFIG_MS_BUSMOUSE),m)
  M_OBJS += msbusmouse.o
  MM = m
  endif
endif

ifeq ($(CONFIG_PSMOUSE),y)
M = y
L_OBJS += psaux.o
else
  ifeq ($(CONFIG_PSMOUSE),m)
  M_OBJS += psaux.o
  MM = m
  endif
endif

ifeq ($(CONFIG_UMISC),y)
# To support third-party modules, misc.c must reside in the kernel
M = y
endif

ifeq ($(CONFIG_SOFT_WATCHDOG),y)
M = y
L_OBJS += softdog.o
else
  ifeq ($(CONFIG_SOFT_WATCHDOG),m)
  M_OBJS += softdog.o
  MM = m
  endif   
endif

ifeq ($(CONFIG_PCWATCHDOG),y)
M = y
L_OBJS += pcwd.o
else
  ifeq ($(CONFIG_PCWATCHDOG),m)
  M_OBJS += pcwd.o
  MM = m
  endif
endif

ifeq ($(CONFIG_ACQUIRE_WDT),y)
M = y
L_OBJS += acquirewdt.o
else
  ifeq ($(CONFIG_ACQUIRE_WDT),m)
  M_OBJS += acquirewdt.o
  MM = m
  endif
endif

ifeq ($(CONFIG_AMIGAMOUSE),y)
M = y
L_OBJS += amigamouse.o
else
  ifeq ($(CONFIG_AMIGAMOUSE),m)
    M_OBJS += amigamouse.o
    MM = m
  endif
endif

ifeq ($(CONFIG_ATARIMOUSE),y)
M = y
L_OBJS += atarimouse.o
else
  ifeq ($(CONFIG_ATARIMOUSE),m)
    M_OBJS += atarimouse.o
    MM = m
  endif
endif

ifdef CONFIG_SUN_MOUSE
M = y
endif

ifeq ($(CONFIG_PC110_PAD),y)
M = y
L_OBJS += pc110pad.o
else
  ifeq ($(CONFIG_PC110_PAD),m)
    M_OBJS += pc110pad.o
    MM = m
  endif
endif

ifeq ($(CONFIG_SUN_OPENPROMIO),y)
M = y
else
  ifeq ($(CONFIG_SUN_OPENPROMIO),m)
  MM = m
  endif
endif

ifeq ($(CONFIG_WDT),y)
M = y
L_OBJS += wdt.o
else
  ifeq ($(CONFIG_WDT),m)
    M_OBJS += wdt.o
    MM = m
  endif
endif

ifeq ($(CONFIG_RTC),y)
M = y
L_OBJS += rtc.o
endif

ifeq ($(CONFIG_NVRAM),y)
M = y
  ifeq ($(CONFIG_PMAC)$(CONFIG_CHRP),)
  L_OBJS += nvram.o
  endif
else
  ifeq ($(CONFIG_NVRAM),m)
    MM = m
    ifeq ($(CONFIG_PMAC)$(CONFIG_CHRP),)
    M_OBJS += nvram.o
    endif
  endif
endif

ifeq ($(CONFIG_VIDEO_DEV),y)
LX_OBJS += videodev.o
else
  ifeq ($(CONFIG_VIDEO_DEV),m)
    MX_OBJS += videodev.o
  endif
endif

ifeq ($(CONFIG_BUS_I2C),y)
	L_I2C=y
else
  ifeq ($(CONFIG_BUS_I2C),m)
	M_I2C=y
  endif
endif

ifeq ($(CONFIG_VIDEO_BT848),y)
L_OBJS += bttv.o msp3400.o tuner.o
L_I2C=y
else
  ifeq ($(CONFIG_VIDEO_BT848),m)
    M_OBJS += bttv.o msp3400.o tuner.o
    M_I2C=y
  endif
endif

ifeq ($(CONFIG_VIDEO_SAA5249),y)
L_OBJS += saa5249.o
L_I2C=y
else
  ifeq ($(CONFIG_VIDEO_SAA5249),m)
    M_OBJS += saa5249.o
    M_I2C=y
  endif
endif

ifeq ($(CONFIG_VIDEO_BWQCAM),y)
L_OBJS += bw-qcam.o
else
  ifeq ($(CONFIG_VIDEO_BWQCAM),m)
    M_OBJS += bw-qcam.o
  endif
endif

ifeq ($(CONFIG_VIDEO_CQCAM),y)
L_OBJS += c-qcam.o
else
  ifeq ($(CONFIG_VIDEO_CQCAM),m)
    M_OBJS += c-qcam.o
  endif
endif

ifeq ($(CONFIG_VIDEO_PMS),y)
L_OBJS += pms.o
else
  ifeq ($(CONFIG_VIDEO_PMS),m)
  M_OBJS += pms.o
  endif
endif

ifeq ($(CONFIG_RADIO_AZTECH),y)
L_OBJS += radio-aztech.o
else
  ifeq ($(CONFIG_RADIO_AZTECH),m)
  M_OBJS += radio-aztech.o
  endif
endif                                             

ifeq ($(CONFIG_RADIO_SF16FMI),y)
L_OBJS += radio-sf16fmi.o
else
  ifeq ($(CONFIG_RADIO_SF16FMI),m)
  M_OBJS += radio-sf16fmi.o
  endif
endif                                             

ifeq ($(CONFIG_RADIO_RTRACK),y)
L_OBJS += radio-aimslab.o
else
  ifeq ($(CONFIG_RADIO_RTRACK),m)
  M_OBJS += radio-aimslab.o
  endif
endif                                             

ifeq ($(CONFIG_MISC_RADIO),y)
L_OBJS += radio.o
  ifeq ($(CONFIG_RADIO_RTRACK),y)
  L_OBJS += rtrack.o
  endif
endif

ifeq ($(CONFIG_QIC02_TAPE),y)
L_OBJS += tpqic02.o
else
  ifeq ($(CONFIG_QIC02_TAPE),m)
  M_OBJS += tpqic02.o
  endif
endif

ifeq ($(CONFIG_FTAPE),y)
L_OBJS       += ftape/ftape.o
SUB_DIRS     += ftape
ifneq ($(CONFIG_ZFTAPE),n)
MOD_SUB_DIRS += ftape
endif
else
  ifeq ($(CONFIG_FTAPE),m)
  MOD_SUB_DIRS += ftape
  endif
endif

ifdef CONFIG_APM
LX_OBJS += apm_bios.o
M = y
endif

ifdef CONFIG_H8
LX_OBJS += h8.o
M = y
endif

ifdef M
LX_OBJS += misc.o
else
  ifdef MM
  MX_OBJS += misc.o
  endif
endif

ifeq ($(L_I2C),y)
LX_OBJS += i2c.o
else
  ifeq ($(M_I2C),y)
  MX_OBJS += i2c.o
  endif
endif

ifdef CONFIG_VT
  ifdef CONFIG_TGA_CONSOLE
  L_OBJS += tga.o
  endif
  ifdef CONFIG_VGA_CONSOLE
  L_OBJS += vga.o vesa_blank.o
  endif
endif

ifeq ($(CONFIG_HFMODEM),y)
ALL_SUB_DIRS += hfmodem
SUB_DIRS += hfmodem
L_OBJS += hfmodem/hfmodem.o
else
  ifeq ($(CONFIG_HFMODEM),m)
  ALL_SUB_DIRS += hfmodem
  MOD_SUB_DIRS += hfmodem
  endif
endif

include $(TOPDIR)/Rules.make

fastdep:

conmakehash: conmakehash.c
	$(HOSTCC) -o conmakehash conmakehash.c

consolemap_deftbl.c: $(FONTMAPFILE) conmakehash
	./conmakehash $(FONTMAPFILE) > consolemap_deftbl.c

consolemap_deftbl.o: consolemap_deftbl.c $(TOPDIR)/include/linux/types.h

defkeymap.c: defkeymap.map
	loadkeys --mktable defkeymap.map > defkeymap.c

