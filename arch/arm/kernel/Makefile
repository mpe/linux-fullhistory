#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).

HEAD_OBJ  = head-$(PROCESSOR).o
ENTRY_OBJ = entry-$(PROCESSOR).o

ifeq ($(CONFIG_ISA_DMA),y)
  ISA_DMA_OBJS += dma-isa.o
endif

O_OBJS_arc        = dma-arc.o oldlatches.o
O_OBJS_a5k        = dma-a5k.o
O_OBJS_rpc        = dma-rpc.o
O_OBJS_ebsa110    = dma-dummy.o
O_OBJS_footbridge = dma.o dma-footbridge.o $(ISA_DMA_OBJS) hw-footbridge.o isa.o
O_OBJS_clps7500   = dma-dummy.o
O_OBJS_nexuspci   = dma-dummy.o
O_OBJS_sa1100     = dma-dummy.o hw-sa1100.o
O_OBJS_l7200      = dma-dummy.o fiq.o

O_TARGET := kernel.o

# Object file lists.

obj-y			:= arch.o $(ENTRY_OBJ) irq.o process.o ptrace.o \
			   semaphore.o setup.o signal.o sys_arm.o time.o \
			   traps.o $(O_OBJS_$(MACHINE))
obj-m			:=
obj-n			:=
obj-			:=

export-objs		:= armksyms.o dma.o ecard.o hw-footbridge.o hw-sa1100.o leds-$(MACHINE).o

obj-$(CONFIG_ARCH_ACORN) += dma.o ecard.o fiq.o time-acorn.o
obj-$(CONFIG_DEBUG_LL)	+= debug-$(PROCESSOR).o
obj-$(CONFIG_MODULES)	+= armksyms.o
obj-$(CONFIG_LEDS)	+= leds-$(MACHINE).o
obj-$(CONFIG_ARTHUR)	+= arthur.o

ifeq ($(MACHINE),nexuspci)
  obj-$(CONFIG_PCI)	+= plx9080.o
else
  obj-$(CONFIG_PCI)	+= bios32.o dec21285.o
endif

# Files that are both resident and modular; remove from modular.

obj-m		:= $(filter-out $(obj-y), $(obj-m))

# Translate to Rules.make lists.

O_OBJS		:= $(filter-out $(export-objs), $(obj-y))
OX_OBJS 	:= $(filter	$(export-objs), $(obj-y))
M_OBJS		:= $(sort $(filter-out $(export-objs), $(obj-m)))
MX_OBJS 	:= $(sort $(filter     $(export-objs), $(obj-m)))

all: kernel.o $(HEAD_OBJ) init_task.o

$(HEAD_OBJ): $(HEAD_OBJ:.o=.S)
	$(CC) $(AFLAGS) -DTEXTADDR=$(TEXTADDR) -traditional -c $(HEAD_OBJ:.o=.S) -o $@

include $(TOPDIR)/Rules.make

.S.o:
	$(CC) $(AFLAGS) $(AFLAGS_$@) -c -o $*.o $<

# Spell out some dependencies that `make dep' doesn't spot
entry-armv.o: calls.S ../lib/constants.h
entry-armo.o: calls.S ../lib/constants.h
