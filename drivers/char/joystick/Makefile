#
# Makefile for the joystick drivers.
#

O_TARGET := js.o
OX_OBJS  :=
O_OBJS   :=
MX_OBJS  :=
M_OBJS   :=

ifeq ($(CONFIG_JOYSTICK),y)
OX_OBJS += joystick.o
else
  ifeq ($(CONFIG_JOYSTICK),m)
  MX_OBJS += joystick.o
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

ifeq ($(CONFIG_JOY_ASSASSIN),y)
O_OBJS += joy-assassin.o
else
  ifeq ($(CONFIG_JOY_ASSASSIN),m)
  M_OBJS += joy-assassin.o
  endif
endif

ifeq ($(CONFIG_JOY_CONSOLE),y)
O_OBJS += joy-console.o
else
  ifeq ($(CONFIG_JOY_CONSOLE),m)
  M_OBJS += joy-console.o
  endif
endif

ifeq ($(CONFIG_JOY_CREATIVE),y)
O_OBJS += joy-creative.o
else
  ifeq ($(CONFIG_JOY_CREATIVE),m)
  M_OBJS += joy-creative.o
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

ifeq ($(CONFIG_JOY_MAGELLAN),y)
O_OBJS += joy-magellan.o
else
  ifeq ($(CONFIG_JOY_MAGELLAN),m)
  M_OBJS += joy-magellan.o
  endif
endif

ifeq ($(CONFIG_JOY_PCI),y)
O_OBJS += joy-pci.o
else
  ifeq ($(CONFIG_JOY_PCI),m)
  M_OBJS += joy-pci.o
  endif
endif

ifeq ($(CONFIG_JOY_SIDEWINDER),y)
O_OBJS += joy-sidewinder.o
else
  ifeq ($(CONFIG_JOY_SIDEWINDER),m)
  M_OBJS += joy-sidewinder.o
  endif
endif

ifeq ($(CONFIG_JOY_SPACEORB),y)
O_OBJS += joy-spaceorb.o
else
  ifeq ($(CONFIG_JOY_SPACEORB),m)
  M_OBJS += joy-spaceorb.o
  endif
endif

ifeq ($(CONFIG_JOY_SPACEBALL),y)
O_OBJS += joy-spaceball.o
else
  ifeq ($(CONFIG_JOY_SPACEBALL),m)
  M_OBJS += joy-spaceball.o
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

ifeq ($(CONFIG_JOY_WARRIOR),y)
O_OBJS += joy-warrior.o
else
  ifeq ($(CONFIG_JOY_WARRIOR),m)
  M_OBJS += joy-warrior.o
  endif
endif

include $(TOPDIR)/Rules.make
