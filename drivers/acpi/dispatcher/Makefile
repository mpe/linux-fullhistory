#
# Makefile for all Linux ACPI interpreter subdirectories
#

SUB_DIRS        := 
MOD_SUB_DIRS    := $(SUB_DIRS)
MOD_IN_SUB_DIRS :=
ALL_SUB_DIRS    := $(SUB_DIRS)

O_TARGET := ../$(shell basename `pwd`).o
O_OBJS   :=
M_OBJS   :=

ACPI_OBJS := $(patsubst %.c,%.o,$(wildcard *.c))

EXTRA_CFLAGS += -I../include

EXTRA_CFLAGS += $(ACPI_CFLAGS)

# if the interpreter is used, it overrides arch/i386/kernel/acpi.c
ifeq ($(CONFIG_ACPI_INTERPRETER),y)
  O_OBJS := $(ACPI_OBJS)
endif

include $(TOPDIR)/Rules.make

clean:
	$(RM) *.o 
