#
# Makefile for the agpgart device driver.  This driver adds a user
# space ioctl interface to use agp memory.  It also adds a kernel interface
# that other drivers could use to manipulate agp memory.

O_TARGET	:= agp.o

ifeq ($(CONFIG_AGP),y)
  O_OBJS += agpgart.o
else
  ifeq ($(CONFIG_AGP), m)
    M_OBJS += agpgart.o
  endif
endif

include $(TOPDIR)/Rules.make

agpgart.o: agpgart_be.o agpgart_fe.o
	$(LD) $(LD_RFLAG) -r -o $@ agpgart_be.o agpgart_fe.o
