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

ACPI_OBJS := driver.o ec.o cpu.o os.o sys.o tables.o
ACPI_OBJS += $(patsubst %.c,%.o,$(wildcard */*.c))

EXTRA_CFLAGS += -I./include -D_LINUX

# if the interpreter is used, it overrides arch/i386/kernel/acpi.c
ifeq ($(CONFIG_ACPI_INTERPRETER),y)
  O_OBJS += $(ACPI_OBJS)
endif

include $(TOPDIR)/Rules.make

clean:
	$(RM) *.o */*.o
