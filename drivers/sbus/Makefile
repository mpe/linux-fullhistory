#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

SUB_DIRS     :=
MOD_SUB_DIRS := $(SUB_DIRS) char audio
ALL_SUB_DIRS := $(SUB_DIRS) char audio

L_OBJS   := sbus.o dvma.o
L_TARGET := sbus.a

# Character devices for SBUS-based machines.
#
ifeq ($(CONFIG_SBUSCHAR),y)
SUB_DIRS += char
L_OBJS += char/sunchar.o
endif

# Audio devices for SBUS-based machines.
#
ifeq ($(CONFIG_SPARCAUDIO),y)
SUB_DIRS += audio
L_OBJS += audio/sparcaudio.o
endif

include $(TOPDIR)/Rules.make
