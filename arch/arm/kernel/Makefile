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
	    signal.o sys_arm.o time.o traps.o

DMA_OBJS_arc      = dma-arc.o
DMA_OBJS_a5k      = dma-a5k.o
DMA_OBJS_rpc      = dma-rpc.o
DMA_OBJS_ebsa110  = dma-dummy.o
DMA_OBJS_ebsa285  = dma-ebsa285.o
DMA_OBJS_nexuspci =
DMA_OBJS_vnc      = dma-vnc.o

O_OBJS_arc        = ecard.o iic.o fiq.o oldlatches.o
O_OBJS_a5k        = ecard.o iic.o fiq.o
O_OBJS_rpc        = ecard.o iic.o fiq.o
O_OBJS_ebsa110    = leds-ebsa110.o
O_OBJS_ebsa285    = leds-ebsa285.o hw-ebsa285.o
O_OBJS_nexuspci   =
O_OBJS_vnc        = leds-ebsa285.o hw-vnc.o

all: lib kernel.o $(HEAD_OBJ) init_task.o

ifeq ($(CONFIG_MODULES),y)
  OX_OBJS = armksyms.o
else
  O_OBJS += armksyms.o
endif

ifeq ($(MACHINE),nexuspci)
  ifdef CONFIG_PCI
    O_OBJS += plx9080.o
  endif
else
  ifdef CONFIG_PCI
    O_OBJS += dec21285.o
  endif
endif

ifneq ($(DMA_OBJS_$(MACHINE)),)
  OX_OBJS += dma.o
  O_OBJS  += $(DMA_OBJS_$(MACHINE))
  ifeq ($(CONFIG_ISA_DMA),y)
    O_OBJS += dma-isa.o
  endif
else
  O_OBJS += dma-dummy.o
endif

O_OBJS += $(O_OBJS_$(MACHINE))

$(HEAD_OBJ): $(HEAD_OBJ:.o=.S)
	$(CC) -D__ASSEMBLY__ -DTEXTADDR=$(TEXTADDR) -traditional -c $(HEAD_OBJ:.o=.S) -o $@

$(ENTRY_OBJ): $(ENTRY_OBJ:.o=.S)
	$(CC) $(CFLAGS) -D__ASSEMBLY__ -c $(ENTRY_OBJ:.o=.S) -o $@

include $(TOPDIR)/Rules.make

$(ENTRY_OBJ): ../lib/constants.h

.PHONY: lib

lib:
	$(MAKE) -C ../lib constants.h
