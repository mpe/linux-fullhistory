#
# Makefile for the Linux ACPI interpreter
#

SUB_DIRS        := 
MOD_SUB_DIRS    := $(SUB_DIRS)
MOD_IN_SUB_DIRS :=
ALL_SUB_DIRS    := $(SUB_DIRS)

O_TARGET := acpi.o
O_OBJS   :=
M_OBJS   :=

export ACPI_CFLAGS
ACPI_CFLAGS := -D_LINUX

EXTRA_CFLAGS += -I./include

EXTRA_CFLAGS += $(ACPI_CFLAGS)

# if the interpreter is used, it overrides arch/i386/kernel/acpi.c
ifeq ($(CONFIG_ACPI_INTERPRETER),y)

  SUB_DIRS += common dispatcher events hardware\
	      interpreter namespace parser resources tables

  ACPI_OBJS := $(patsubst %,%.o,$(SUB_DIRS))
  ACPI_OBJS += $(patsubst %.c,%.o,$(wildcard *.c))

  O_OBJS += $(ACPI_OBJS)
endif

include $(TOPDIR)/Rules.make

clean:
	$(RM) *.o */*.o
