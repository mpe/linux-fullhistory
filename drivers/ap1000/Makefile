# File: drivers/ap1000/Makefile
#
# Makefile for the AP1000 drivers
#

L_TARGET := ap1000.a
L_OBJS   := bif.o apfddi.o mac.o plc.o ringbuf.o

ifeq ($(CONFIG_APBLOCK),y)
L_OBJS += ap.o
else
  ifeq ($(CONFIG_APBLOCK),m)
  M_OBJS += ap.o
  endif
endif

ifeq ($(CONFIG_DDV),y)
L_OBJS += ddv.o ddv_util.o
else
  ifeq ($(CONFIG_DDV),m)
  M_OBJS += ddv.o ddv_util.o
  endif
endif

include $(TOPDIR)/Rules.make

clean:
	rm -f core *.o *.a *.s

