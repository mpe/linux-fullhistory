#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).

HEAD_OBJ  = head-$(PROCESSOR).o
ENTRY_OBJ = entry-$(PROCESSOR).o

O_TARGET := kernel.o
O_OBJS   := $(ENTRY_OBJ) ioport.o irq.o process.o ptrace.o signal.o sys_arm.o time.o traps.o

all: kernel.o $(HEAD_OBJ) init_task.o

ifeq ($(CONFIG_MODULES),y)
OX_OBJS = armksyms.o
else
O_OBJS += armksyms.o
endif

ifdef CONFIG_ARCH_ACORN
  O_OBJS += setup.o ecard.o iic.o dma.o
  ifdef CONFIG_ARCH_ARC
    O_OBJS += oldlatches.o
  endif
endif

ifeq ($(MACHINE),ebsa110)
  O_OBJS += setup-ebsa110.o dma.o
endif

ifeq ($(MACHINE),nexuspci)
  O_OBJS += setup-ebsa110.o
endif

$(HEAD_OBJ): $(HEAD_OBJ:.o=.S)
	$(CC) -D__ASSEMBLY__ -traditional -c $(HEAD_OBJ:.o=.S) -o $@

include $(TOPDIR)/Rules.make

$(ENTRY_OBJ:.o=.S): ../lib/constants.h

.PHONY: ../lib/constants.h

../lib/constants.h:
	$(MAKE) -C ../lib constants.h
