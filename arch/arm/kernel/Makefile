#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).

HEAD_OBJ  = head-$(PROCESSOR).o
ENTRY_OBJ = entry-$(PROCESSOR).o

O_TARGET := kernel.o
O_OBJS   := $(ENTRY_OBJ) ioport.o irq.o process.o ptrace.o setup.o \
	    signal.o sys_arm.o time.o traps.o fiq.o

all: lib kernel.o $(HEAD_OBJ) init_task.o

ifeq ($(CONFIG_MODULES),y)
OX_OBJS = armksyms.o
else
O_OBJS += armksyms.o
endif

ifdef CONFIG_ARCH_ACORN
  O_OBJS += ecard.o iic.o
  ifdef CONFIG_ARCH_ARC
    O_OBJS += oldlatches.o
  endif
  O_OBJS += dma-$(MACHINE).o
  OX_OBJS += dma.o
endif

ifeq ($(MACHINE),ebsa110)
  O_OBJS += dma-dummy.o leds-ebsa110.o
endif

ifeq ($(MACHINE),ebsa285)
  OX_OBJS += dma.o
  O_OBJS += dma-ebsa285.o leds-ebsa285.o
  ifdef CONFIG_PCI
    O_OBJS += dec21285.o
  endif
endif

ifeq ($(MACHINE),nexuspci)
  O_OBJS += dma-dummy.o
  ifdef CONFIG_PCI
    O_OBJS += plx9080.o
  endif
endif

$(HEAD_OBJ): $(HEAD_OBJ:.o=.S)
	$(CC) -D__ASSEMBLY__ -DTEXTADDR=$(TEXTADDR) -traditional -c $(HEAD_OBJ:.o=.S) -o $@

$(ENTRY_OBJ): $(ENTRY_OBJ:.o=.S)
	$(CC) $(CFLAGS) -D__ASSEMBLY__ -c $(ENTRY_OBJ:.o=.S) -o $@

include $(TOPDIR)/Rules.make

$(ENTRY_OBJ): ../lib/constants.h

.PHONY: lib

lib:
	$(MAKE) -C ../lib constants.h
