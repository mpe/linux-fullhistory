#
# Makefile for the Linux IrDA IrCOMM protocol layer.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET := ircomm_and_tty.o
O_OBJS	 := ircomm_param.o ircomm_core.o ircomm_event.o ircomm_lmp.o ircomm_ttp.o ircomm_tty.o ircomm_tty_attach.o ircomm_tty_ioctl.o
M_OBJS   := ircomm.o ircomm-tty.o
MI_OBJS  := ircomm_param.o ircomm_core.o ircomm_event.o ircomm_lmp.o ircomm_ttp.o ircomm_tty.o ircomm_tty_attach.o ircomm_tty_ioctl.o

OX_OBJS  += 

# Special rule to build the composite modules

ifeq ($(CONFIG_IRCOMM),m)
ircomm.o:  $(MI_OBJS)
	$(LD) $(LD_RFLAG) -r -o $@ ircomm_core.o ircomm_event.o ircomm_lmp.o ircomm_ttp.o

ircomm-tty.o: $(MI_OBJS)
	$(LD) $(LD_RFLAG) -r -o $@ ircomm_tty.o ircomm_tty_attach.o ircomm_tty_ioctl.o ircomm_param.o


endif

include $(TOPDIR)/Rules.make

tar:
		tar -cvf /dev/f1 .

