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
ALL_SUB_DIRS := $(SUB_DIRS) ftape joystick pcmcia

#
# This file contains the font map for the default (hardware) font
#
FONTMAPFILE = cp437.uni

O_TARGET := char.o
M_OBJS   :=
O_OBJS   := tty_io.o n_tty.o tty_ioctl.o mem.o random.o raw.o
OX_OBJS  := pty.o misc.o

KEYMAP   =defkeymap.o
KEYBD    =pc_keyb.o
CONSOLE  =console.o
SERIAL   =serial.o

ifeq ($(ARCH),m68k)
  KEYMAP   =
  KEYBD    =
endif

ifeq ($(ARCH),arm)
  KEYMAP   =
  KEYBD    =
  CONSOLE  =
  SERIAL   =
endif

ifneq ($(CONFIG_SUN_SERIAL),)
  SERIAL   =
endif

ifdef CONFIG_VT
O_OBJS += vt.o vc_screen.o consolemap.o consolemap_deftbl.o
OX_OBJS += $(CONSOLE) selection.o
endif

ifeq ($(CONFIG_SERIAL),y)
OX_OBJS += $(SERIAL)
else
  ifeq ($(CONFIG_SERIAL),m)
  MX_OBJS += $(SERIAL)
  endif
endif

ifeq ($(CONFIG_SERIAL_21285),y)
O_OBJS += serial_21285.o
endif

ifndef CONFIG_SUN_KEYBOARD
  ifdef CONFIG_VT
    OX_OBJS += keyboard.o
    O_OBJS  += $(KEYMAP) $(KEYBD)
  endif
else
  ifdef CONFIG_PCI
    OX_OBJS += keyboard.o
    O_OBJS  += $(KEYMAP)
  endif
endif

ifdef CONFIG_MAGIC_SYSRQ
OX_OBJS += sysrq.o
endif

ifeq ($(CONFIG_ATARI_DSP56K),y)
O_OBJS += dsp56k.o
S = y
else
  ifeq ($(CONFIG_ATARI_DSP56K),m)
  M_OBJS += dsp56k.o
  SM = y
  endif
endif

ifeq ($(CONFIG_ROCKETPORT),y)
O_OBJS += rocket.o
else
  ifeq ($(CONFIG_ROCKETPORT),m)
  M_OBJS += rocket.o
  endif
endif

ifeq ($(CONFIG_DIGI),y)
O_OBJS += pcxx.o
else
  ifeq ($(CONFIG_DIGI),m)
  M_OBJS += pcxx.o
  endif
endif

ifeq ($(CONFIG_DIGIEPCA),y)
O_OBJS += epca.o
else
  ifeq ($(CONFIG_DIGIEPCA),m)
  M_OBJS += epca.o
  endif
endif

ifeq ($(CONFIG_CYCLADES),y)
O_OBJS += cyclades.o
else
  ifeq ($(CONFIG_CYCLADES),m)
  M_OBJS += cyclades.o
  endif
endif

ifeq ($(CONFIG_STALLION),y)
O_OBJS += stallion.o
else
  ifeq ($(CONFIG_STALLION),m)
  M_OBJS += stallion.o
  endif
endif

ifeq ($(CONFIG_ISTALLION),y)
O_OBJS += istallion.o
else
  ifeq ($(CONFIG_ISTALLION),m)
  M_OBJS += istallion.o
  endif
endif

ifeq ($(CONFIG_COMPUTONE),y)
O_OBJS += ip2.o ip2main.o
else
  ifeq ($(CONFIG_COMPUTONE),m)
  M_OBJS += ip2.o ip2main.o
  endif
endif

ifeq ($(CONFIG_RISCOM8),y)
O_OBJS += riscom8.o
else
  ifeq ($(CONFIG_RISCOM8),m)
  M_OBJS += riscom8.o
  endif
endif

ifeq ($(CONFIG_ISI),y)
O_OBJS += isicom.o
else
  ifeq ($(CONFIG_ISI),m)
  M_OBJS += isicom.o
  endif
endif

ifeq ($(CONFIG_ESPSERIAL),y)
O_OBJS += esp.o
else
  ifeq ($(CONFIG_ESPSERIAL),m)
  M_OBJS += esp.o
  endif
endif

ifeq ($(CONFIG_SYNCLINK),m)
  M_OBJS += synclink.o
endif

ifeq ($(CONFIG_N_HDLC),m)
  M_OBJS += n_hdlc.o
endif

ifeq ($(CONFIG_SPECIALIX),y)
O_OBJS += specialix.o
else
  ifeq ($(CONFIG_SPECIALIX),m)
  M_OBJS += specialix.o
  endif
endif

ifeq ($(CONFIG_SX),y)
O_OBJS += sx.o generic_serial.o
else
  ifeq ($(CONFIG_SX),m)
  M_OBJS += sx.o
  endif
endif

ifeq ($(CONFIG_ATIXL_BUSMOUSE),y)
O_OBJS += atixlmouse.o
else
  ifeq ($(CONFIG_ATIXL_BUSMOUSE),m)
  M_OBJS += atixlmouse.o
  endif
endif

ifeq ($(CONFIG_LOGIBUSMOUSE),y)
O_OBJS += logibusmouse.o
else
  ifeq ($(CONFIG_LOGIBUSMOUSE),m)
  M_OBJS += logibusmouse.o
  endif
endif

ifeq ($(CONFIG_PRINTER),y)
O_OBJS += lp.o
else
  ifeq ($(CONFIG_PRINTER),m)
  M_OBJS += lp.o
  endif
endif

ifeq ($(CONFIG_JOYSTICK),y)
O_OBJS += joystick/js.o
SUB_DIRS += joystick
MOD_SUB_DIRS += joystick
else
  ifeq ($(CONFIG_JOYSTICK),m)
  MOD_SUB_DIRS += joystick
  endif
endif

ifeq ($(CONFIG_BUSMOUSE),y)
M = y
OX_OBJS += busmouse.o
else
  ifeq ($(CONFIG_BUSMOUSE),m)
  MM = m
  MX_OBJS += busmouse.o
  endif
endif

ifeq ($(CONFIG_DTLK),y)
O_OBJS += dtlk.o
else
  ifeq ($(CONFIG_DTLK),m)
  M_OBJS += dtlk.o
  endif
endif

ifeq ($(CONFIG_R3964),y)
O_OBJS += n_r3964.o
else
  ifeq ($(CONFIG_R3964),m)
  M_OBJS += n_r3964.o
  endif
endif

ifeq ($(CONFIG_APPLICOM),y)
O_OBJS += applicom.o
else
  ifeq ($(CONFIG_APPLICOM),m)
  M_OBJS += applicom.o
  endif
endif

ifeq ($(CONFIG_MS_BUSMOUSE),y)
O_OBJS += msbusmouse.o
else
  ifeq ($(CONFIG_MS_BUSMOUSE),m)
  M_OBJS += msbusmouse.o
  endif
endif

ifeq ($(CONFIG_82C710_MOUSE),y)
O_OBJS += qpmouse.o
else
  ifeq ($(CONFIG_82C710_MOUSE),m)
  M_OBJS += qpmouse.o
  endif
endif

ifeq ($(CONFIG_SOFT_WATCHDOG),y)
O_OBJS += softdog.o
else
  ifeq ($(CONFIG_SOFT_WATCHDOG),m)
  M_OBJS += softdog.o
  endif
endif

ifeq ($(CONFIG_PCWATCHDOG),y)
O_OBJS += pcwd.o
else
  ifeq ($(CONFIG_PCWATCHDOG),m)
  M_OBJS += pcwd.o
  endif
endif

ifeq ($(CONFIG_ACQUIRE_WDT),y)
O_OBJS += acquirewdt.o
else
  ifeq ($(CONFIG_ACQUIRE_WDT),m)
  M_OBJS += acquirewdt.o
  endif
endif

ifeq ($(CONFIG_AMIGAMOUSE),y)
O_OBJS += amigamouse.o
else
  ifeq ($(CONFIG_AMIGAMOUSE),m)
    M_OBJS += amigamouse.o
  endif
endif

ifeq ($(CONFIG_ATARIMOUSE),y)
O_OBJS += atarimouse.o
else
  ifeq ($(CONFIG_ATARIMOUSE),m)
    M_OBJS += atarimouse.o
  endif
endif

ifeq ($(CONFIG_ADBMOUSE),y)
O_OBJS += adbmouse.o
else
  ifeq ($(CONFIG_ADBMOUSE),m)
    M_OBJS += adbmouse.o
  endif
endif

ifeq ($(CONFIG_PC110_PAD),y)
O_OBJS += pc110pad.o
else
  ifeq ($(CONFIG_PC110_PAD),m)
    M_OBJS += pc110pad.o
  endif
endif

ifeq ($(CONFIG_WDT),y)
O_OBJS += wdt.o
else
  ifeq ($(CONFIG_WDT),m)
    M_OBJS += wdt.o
  endif
endif

ifeq ($(CONFIG_RTC),y)
O_OBJS += rtc.o
else
  ifeq ($(CONFIG_RTC),m)
    M_OBJS += rtc.o
  endif
endif

ifeq ($(CONFIG_NVRAM),y)
  ifeq ($(CONFIG_PPC),)
  O_OBJS += nvram.o
  endif
else
  ifeq ($(CONFIG_NVRAM),m)
    ifeq ($(CONFIG_PPC),)
    M_OBJS += nvram.o
    endif
  endif
endif

ifeq ($(CONFIG_VIDEO_DEV),y)
OX_OBJS += videodev.o
else
  ifeq ($(CONFIG_VIDEO_DEV),m)
    MX_OBJS += videodev.o
  endif
endif

#
# for external dependencies in arm/config.in and video/config.in
#
ifeq ($(CONFIG_BUS_I2C),y)
	L_I2C=y
else
  ifeq ($(CONFIG_BUS_I2C),m)
	M_I2C=y
  endif
endif

ifeq ($(CONFIG_VIDEO_BT848),y)
O_OBJS += bttv.o msp3400.o tda8425.o tda9855.o tea6300.o
L_I2C=y
L_TUNERS=y
else
  ifeq ($(CONFIG_VIDEO_BT848),m)
    M_OBJS += bttv.o msp3400.o tda8425.o tda9855.o tea6300.o
    M_I2C=y
    M_TUNERS=y
  endif
endif

ifeq ($(CONFIG_VIDEO_ZR36120),y)
O_OBJS += zoran.o
L_I2C=y
L_TUNERS=y
L_DECODERS=y
else
  ifeq ($(CONFIG_VIDEO_ZR36120),m)
    M_OBJS += zoran.o
    M_I2C=y
    M_TUNERS=y
    M_DECODERS=y
  endif
endif

ifeq ($(CONFIG_I2C_PARPORT),y)
O_OBJS += i2c-parport.o
L_I2C = y
else
  ifeq ($(CONFIG_I2C_PARPORT),m)
    M_OBJS += i2c-parport.o
    M_I2C = y
  endif
endif

ifeq ($(CONFIG_VIDEO_SAA5249),y)
O_OBJS += saa5249.o
L_I2C=y
else
  ifeq ($(CONFIG_VIDEO_SAA5249),m)
    M_OBJS += saa5249.o
    M_I2C=y
  endif
endif

ifeq ($(CONFIG_VIDEO_CQCAM),y)
O_OBJS += c-qcam.o
else
  ifeq ($(CONFIG_VIDEO_CQCAM),m)
    M_OBJS += c-qcam.o
  endif
endif

ifeq ($(CONFIG_VIDEO_BWQCAM),y)
O_OBJS += bw-qcam.o
else
  ifeq ($(CONFIG_VIDEO_BWQCAM),m)
    M_OBJS += bw-qcam.o
  endif
endif

ifeq ($(CONFIG_VIDEO_ZORAN),y)
O_OBJS += buz.o
L_I2C=y
L_DECODERS=y
else
  ifeq ($(CONFIG_VIDEO_ZORAN),m)
    M_OBJS += buz.o
    M_I2C=y
    M_DECODERS=y
  endif
endif

ifeq ($(CONFIG_VIDEO_LML33),y)
O_OBJS += bt856.o bt819.o
else
  ifeq ($(CONFIG_VIDEO_LML33),m)
    M_OBJS += bt856.o bt819.o
  endif
endif

ifeq ($(CONFIG_VIDEO_PMS),y)
O_OBJS += pms.o
else
  ifeq ($(CONFIG_VIDEO_PMS),m)
  M_OBJS += pms.o
  endif
endif

ifeq ($(CONFIG_VIDEO_PLANB),y)
O_OBJS += planb.o
else
  ifeq ($(CONFIG_VIDEO_PLANB),m)
  M_OBJS += planb.o
  endif
endif

ifeq ($(CONFIG_VIDEO_VINO),y)
O_OBJS += vino.o
else
  ifeq ($(CONFIG_VIDEO_VINO),m)
  M_OBJS += vino.o
  endif
endif

ifeq ($(CONFIG_VIDEO_STRADIS),y)
O_OBJS += vino.o
else
  ifeq ($(CONFIG_VIDEO_STRADIS),m)
  M_OBJS += stradis.o
  endif
endif

ifeq ($(CONFIG_RADIO_AZTECH),y)
O_OBJS += radio-aztech.o
else
  ifeq ($(CONFIG_RADIO_AZTECH),m)
  M_OBJS += radio-aztech.o
  endif
endif

ifeq ($(CONFIG_RADIO_RTRACK2),y)
O_OBJS += radio-rtrack2.o
else
  ifeq ($(CONFIG_RADIO_RTRACK2),m)
  M_OBJS += radio-rtrack2.o
  endif
endif

ifeq ($(CONFIG_RADIO_SF16FMI),y)
O_OBJS += radio-sf16fmi.o
else
  ifeq ($(CONFIG_RADIO_SF16FMI),m)
  M_OBJS += radio-sf16fmi.o
  endif
endif

ifeq ($(CONFIG_RADIO_CADET),y)
O_OBJS += radio-cadet.o
else
  ifeq ($(CONFIG_RADIO_CADET),m)
  M_OBJS += radio-cadet.o
  endif
endif

ifeq ($(CONFIG_RADIO_TYPHOON),y)
O_OBJS += radio-typhoon.o
else
  ifeq ($(CONFIG_RADIO_TYPHOON),m)
  M_OBJS += radio-typhoon.o
  endif
endif

ifeq ($(CONFIG_RADIO_TERRATEC),y)
O_OBJS += radio-terratec.o
else
  ifeq ($(CONFIG_RADIO_TERRATEC),m)
  M_OBJS += radio-terratec.o
  endif
endif

ifeq ($(CONFIG_RADIO_RTRACK),y)
O_OBJS += radio-aimslab.o
else
  ifeq ($(CONFIG_RADIO_RTRACK),m)
  M_OBJS += radio-aimslab.o
  endif
endif

ifeq ($(CONFIG_RADIO_ZOLTRIX),y)
O_OBJS += radio-zoltrix.o
else
  ifeq ($(CONFIG_RADIO_ZOLTRIX),m)
  M_OBJS += radio-zoltrix.o
  endif
endif

ifeq ($(CONFIG_RADIO_MIROPCM20),y)
O_OBJS += radio-miropcm20.o
else
  ifeq ($(CONFIG_RADIO_MIROPCM20),m)
  M_OBJS += radio-miropcm20.o
  endif
endif

ifeq ($(CONFIG_RADIO_GEMTEK),y)
O_OBJS += radio-gemtek.o
else
  ifeq ($(CONFIG_RADIO_GEMTEK),m)
  M_OBJS += radio-gemtek.o
  endif
endif

ifeq ($(CONFIG_RADIO_TRUST),y)
O_OBJS += radio-trust.o
else
  ifeq ($(CONFIG_RADIO_TRUST),m)
  M_OBJS += radio-trust.o
  endif
endif

ifeq ($(CONFIG_QIC02_TAPE),y)
O_OBJS += tpqic02.o
else
  ifeq ($(CONFIG_QIC02_TAPE),m)
  M_OBJS += tpqic02.o
  endif
endif

ifeq ($(CONFIG_FTAPE),y)
O_OBJS       += ftape/ftape.o
SUB_DIRS     += ftape
ifneq ($(CONFIG_ZFTAPE),n)
MOD_SUB_DIRS += ftape
endif
else
  ifeq ($(CONFIG_FTAPE),m)
  MOD_SUB_DIRS += ftape
  endif
endif

ifdef CONFIG_H8
OX_OBJS += h8.o
endif

ifeq ($(CONFIG_PPDEV),y)
O_OBJS += ppdev.o
else
  ifeq ($(CONFIG_PPDEV),m)
  M_OBJS += ppdev.o
  endif
endif


# set when a framegrabber supports external tuners
ifeq ($(L_TUNERS),y)
O_OBJS += tuner.o
else
  ifeq ($(M_TUNERS),y)
    M_OBJS += tuner.o
  endif
endif

# set when a framegrabber supports external decoders
ifeq ($(L_DECODERS),y)
O_OBJS += saa7110.o saa7111.o saa7185.o
else
  ifeq ($(M_DECODERS),y)
    M_OBJS += saa7110.o saa7111.o saa7185.o
  endif
endif

# set when a framegrabber implements i2c support
ifeq ($(L_I2C),y)
OX_OBJS += i2c-old.o
else
 ifeq ($(M_I2C),y)
 MX_OBJS += i2c-old.o
  endif
endif

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
