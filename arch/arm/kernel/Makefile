#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).

HEAD_OBJ  = head-$(PROCESSOR).o
ENTRY_OBJ = entry-$(PROCESSOR).o

O_TARGET := kernel.o
O_OBJS   := $(ENTRY_OBJ) ioport.o irq.o process.o ptrace.o \
	    semaphore.o setup.o signal.o sys_arm.o time.o traps.o

ifeq ($(CONFIG_ISA_DMA),y)
  ISA_DMA_OBJS += dma-isa.o
endif

O_OBJS_arc        = dma-arc.o iic.o fiq.o time-acorn.o oldlatches.o
O_OBJS_a5k        = dma-a5k.o iic.o fiq.o time-acorn.o
O_OBJS_rpc        = dma-rpc.o iic.o fiq.o time-acorn.o
O_OBJS_ebsa110    = dma-dummy.o
O_OBJS_footbridge = dma-footbridge.o $(ISA_DMA_OBJS) isa.o
O_OBJS_nexuspci   = dma-dummy.o
O_OBJS_sa1100     = dma-dummy.o fiq.o

OX_OBJS_arc	  = dma.o
OX_OBJS_a5k	  = dma.o
OX_OBJS_rpc	  = dma.o
OX_OBJS_ebsa110	  = 
OX_OBJS_footbridge= dma.o hw-footbridge.o
OX_OBJS_nexuspci  =
OX_OBJS_sa1100    =

all: kernel.o $(HEAD_OBJ) init_task.o

O_OBJS += $(O_OBJS_$(MACHINE))

ifeq ($(CONFIG_DEBUG_LL),y)
  O_OBJS += debug-$(PROCESSOR).o
endif

ifeq ($(CONFIG_MODULES),y)
  OX_OBJS = armksyms.o
endif

ifeq ($(CONFIG_ARCH_ACORN),y)
  OX_OBJS += ecard.o
endif

ifeq ($(CONFIG_PCI),y)
  ifeq ($(MACHINE),nexuspci)
    O_OBJS += plx9080.o
  else
    O_OBJS += bios32.o dec21285.o
  endif
endif

ifeq ($(CONFIG_LEDS),y)
  OX_OBJS += leds-$(MACHINE).o
endif

ifeq ($(CONFIG_MODULES),y)
  OX_OBJS += $(OX_OBJS_$(MACHINE))
else
  O_OBJS += $(OX_OBJS_$(MACHINE))
endif

ifeq ($(CONFIG_ARTHUR),y)
  O_OBJS += arthur.o
else
  ifeq ($(CONFIG_ARTHUR),m)
    M_OBJS += arthur.o
  endif
endif

$(HEAD_OBJ): $(HEAD_OBJ:.o=.S)
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -DTEXTADDR=$(TEXTADDR) -traditional -c $(HEAD_OBJ:.o=.S) -o $@

include $(TOPDIR)/Rules.make

.S.o:
	$(CC) -D__ASSEMBLY__ $(AFLAGS) $(AFLAGS_$@) -c -o $*.o $<

# Spell out some dependencies that `make dep' doesn't spot
entry-armv.o: calls.S ../lib/constants.h
entry-armo.o: calls.S ../lib/constants.h
