#
# Makefile for the Linux IrDA IrCOMM protocol layer.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET := ircomm_n_vtd.o
O_OBJS	 := ircomm_common.o attach.o irvtd.o irvtd_driver.o
M_OBJS   := ircomm.o ircomm_tty.o
MI_OBJS  := ircomm_common.o attach.o irvtd.o irvtd_driver.o

OX_OBJS  += 

# Special rule to build the composite modules

ifeq ($(CONFIG_IRCOMM),m)
ircomm.o:  $(MI_OBJS)
	$(LD) $(LD_RFLAG) -r -o $@ ircomm_common.o attach.o

ircomm_tty.o: $(MI_OBJS)
	$(LD) $(LD_RFLAG) -r -o $@ irvtd.o irvtd_driver.o
endif

include $(TOPDIR)/Rules.make

tar:
		tar -cvf /dev/f1 .

