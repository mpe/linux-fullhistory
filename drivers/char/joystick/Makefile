#
# Makefile for the joystick drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now inherited from the
# parent makes..
#

O_TARGET := js.o
O_OBJS   :=
M_OBJS   :=

ifeq ($(CONFIG_JOYSTICK),y)
O_OBJS += joystick.o
else
  ifeq ($(CONFIG_JOYSTICK),m)
  M_OBJS += joystick.o
  endif
endif

ifeq ($(CONFIG_JOY_AMIGA),y)
O_OBJS += joy-amiga.o
else
  ifeq ($(CONFIG_JOY_AMIGA),m)
  M_OBJS += joy-amiga.o
  endif
endif

ifeq ($(CONFIG_JOY_ANALOG),y)
O_OBJS += joy-analog.o
else
  ifeq ($(CONFIG_JOY_ANALOG),m)
  M_OBJS += joy-analog.o
  endif
endif

ifeq ($(CONFIG_JOY_ASSASIN),y)
O_OBJS += joy-assasin.o
else
  ifeq ($(CONFIG_JOY_ASSASIN),m)
  M_OBJS += joy-assasin.o
  endif
endif

ifeq ($(CONFIG_JOY_CONSOLE),y)
O_OBJS += joy-console.o
else
  ifeq ($(CONFIG_JOY_CONSOLE),m)
  M_OBJS += joy-console.o
  endif
endif

ifeq ($(CONFIG_JOY_DB9),y)
O_OBJS += joy-db9.o
else
  ifeq ($(CONFIG_JOY_DB9),m)
  M_OBJS += joy-db9.o
  endif
endif

ifeq ($(CONFIG_JOY_GRAVIS),y)
O_OBJS += joy-gravis.o
else
  ifeq ($(CONFIG_JOY_GRAVIS),m)
  M_OBJS += joy-gravis.o
  endif
endif

ifeq ($(CONFIG_JOY_LIGHTNING),y)
O_OBJS += joy-lightning.o
else
  ifeq ($(CONFIG_JOY_LIGHTNING),m)
  M_OBJS += joy-lightning.o
  endif
endif

ifeq ($(CONFIG_JOY_LOGITECH),y)
O_OBJS += joy-logitech.o
else
  ifeq ($(CONFIG_JOY_LOGITECH),m)
  M_OBJS += joy-logitech.o
  endif
endif

ifeq ($(CONFIG_JOY_SIDEWINDER),y)
O_OBJS += joy-sidewinder.o
else
  ifeq ($(CONFIG_JOY_SIDEWINDER),m)
  M_OBJS += joy-sidewinder.o
  endif
endif

ifeq ($(CONFIG_JOY_THRUSTMASTER),y)
O_OBJS += joy-thrustmaster.o
else 
  ifeq ($(CONFIG_JOY_THRUSTMASTER),m)
  M_OBJS += joy-thrustmaster.o
  endif
endif

ifeq ($(CONFIG_JOY_TURBOGRAFX),y)
O_OBJS += joy-turbografx.o
else 
  ifeq ($(CONFIG_JOY_TURBOGRAFX),m)
  M_OBJS += joy-turbografx.o
  endif
endif

include $(TOPDIR)/Rules.make
