#
# Makefile for the Acorn block device drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now inherited from the
# parent makefile.
#

L_TARGET := acorn-block.a
L_OBJS   :=
M_OBJS   :=
MOD_LIST_NAME := ACORN_BLOCK_MODULES

ifeq ($(CONFIG_BLK_DEV_FD1772),y)
  L_OBJS += fd1772.o fd1772dma.o
else
  ifeq ($(CONFIG_BLK_DEV_FD1772),m)
    M_OBJS += fd1772_mod.o
  endif
endif

ifeq ($(CONFIG_BLK_DEV_MFM),y)
  L_OBJS += mfmhd.o mfm.o
else
  ifeq ($(CONFIG_BLK_DEV_MFM),m)
    M_OBJS += mfmhd_mod.o
  endif
endif

include $(TOPDIR)/Rules.make

fd1772_mod.o: $(FLOPPY)
	$(LD) -r -o $@ $(FLOPPY)

mfmhd_mod.o: mfmhd.o mfm.o
	$(LD) -r -o $@ mfmhd.o mfm.o

%.o: %.S
ifndef $(CONFIG_BINUTILS_NEW)
	$(CC) $(CFLAGS) -D__ASSEMBLY__ -E $< | tr ';$$' '\n#' > ..tmp.s
	$(CC) $(CFLAGS) -c -o $@ ..tmp.s
	$(RM) ..tmp.s
else
	$(CC) $(CFLAGS) -D__ASSEMBLY__ -c -o $@ $<
endif
