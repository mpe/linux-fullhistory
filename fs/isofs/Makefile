#
# Makefile for the Linux isofs filesystem routines.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile.

O_TARGET := isofs.o
O_OBJS   := namei.o inode.o file.o dir.o util.o rock.o symlink.o

ifdef CONFIG_JOLIET
O_OBJS += joliet.o
endif

M_OBJS   := $(O_TARGET)

include $(TOPDIR)/Rules.make
