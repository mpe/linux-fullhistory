#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

#
# sbus audio drivers
#

O_TARGET := sparcaudio.o
O_OBJS   :=
M_OBJS   :=
M :=
MM :=

ifeq ($(CONFIG_SPARCAUDIO),y)
M=y
else
  ifeq ($(CONFIG_SPARCAUDIO),m)
  MM=y
  endif
endif

ifeq ($(CONFIG_SPARCAUDIO_AMD7930),y)
M=y
OX_OBJS += amd7930.o
else
  ifeq ($(CONFIG_SPARCAUDIO_AMD7930),m)
  MM=y
  MX_OBJS += amd7930.o
  endif
endif

ifeq ($(CONFIG_SPARCAUDIO_CS4231),y)
M=y
O_OBJS += cs4231.o
else
  ifeq ($(CONFIG_SPARCAUDIO_CS4231),m)
  MM=y
  M_OBJS += cs4231.o
  endif
endif

ifeq ($(CONFIG_SPARCAUDIO_DBRI),y)
M=y
OX_OBJS += dbri.o
else
  ifeq ($(CONFIG_SPARCAUDIO_DBRI),m)
  MM=y
  MX_OBJS += dbri.o
  endif
endif

ifeq ($(CONFIG_SPARCAUDIO_DUMMY),y)
M=y
O_OBJS += dmy.o
else
  ifeq ($(CONFIG_SPARCAUDIO_DUMMY),m)
  MM=y
  M_OBJS += dmy.o
  endif
endif

ifdef M
OX_OBJS += audio.o
else
  ifdef MM
  MX_OBJS += audio.o
  endif
endif

include $(TOPDIR)/Rules.make
