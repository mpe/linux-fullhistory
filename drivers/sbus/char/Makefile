#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

# Dave Redman Frame Buffer tuning support.
# OK this is kind of ugly but it does allow drivers to be added fairly
# easily. and you can even choose what sort of support you want.
ifdef SUN_FB_CGSIX
 FB_OBJS +=	cgsix.o
endif
ifdef SUN_FB_CGTHREE
 FB_OBJS +=	cgthree.o
endif
ifdef SUN_FB_TCX
  FB_OBJS +=	tcx.o
endif
ifdef SUN_FB_BWTWO
 FB_OBJS +=	bwtwo.o
endif
ifdef SUN_FB_LEO
 FB_OBJS +=	leo.o
endif
ifdef SUN_FB_CGFOURTEEN
 FB_OBJS +=	cgfourteen.o
endif
ifdef TADPOLE_FB_WEITEK
 FB_OBJS +=	weitek.o
endif
ifdef SUN_FB_CREATOR
 ifeq ($(ARCH),sparc64)
 FB_OBJS += creator.o
 endif
endif
#ifdef SUN_FB_FAST_ONE
# FB_OBJS +=	sun_8bit_fast1.o
#endif
#ifdef SUN_FB_FAST_TWO
# FB_OBJS +=	sun_8bit_fast2.o
#endif
#ifdef SUN_FB_FAST_MONO
# FB_OBJS +=	sun_mono_fast1.o
#endif
#ifdef SUN_FB_GENERIC
# FB_OBJS +=	sun_8bit_generic.o
#endif

O_TARGET := sunchar.o
O_OBJ    := ${FB_OBJS} suncons.o  sunfb.o
O_OBJS   := ${O_OBJ} sunkbd.o sunkeymap.o sunmouse.o sunserial.o
M_OBJS   :=

ifeq ($(CONFIG_SUN_OPENPROMIO),y)
O_OBJS += openprom.o
else
  ifeq ($(CONFIG_SUN_OPENPROMIO),m)
  M_OBJS += openprom.o
  endif
endif

ifeq ($(CONFIG_SUN_MOSTEK_RTC),y)
O_OBJS += rtc.o
else
  ifeq ($(CONFIG_SUN_MOSTEK_RTC),m)
  M_OBJS += rtc.o
  endif
endif

ifeq ($(CONFIG_SUN_BPP),y)
O_OBJS += bpp.o
else
  ifeq ($(CONFIG_SUN_BPP),m)
  M_OBJS += bpp.o
  endif
endif

ifeq ($(CONFIG_SUN_VIDEOPIX),y)
O_OBJS += vfc.o
else
  ifeq ($(CONFIG_SUN_VIDEOPIX),m)
  M_OBJS += vfc.o
  endif
endif

include $(TOPDIR)/Rules.make

vfc.o: vfc_dev.o vfc_i2c.o
	$(LD) -r -o vfc.o vfc_dev.o vfc_i2c.o
