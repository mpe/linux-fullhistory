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

ifeq ($(CONFIG_ARCH_ARC),y)
  ifeq ($(CONFIG_BLK_DEV_FD),y)
    L_OBJS += fd1772.o fd1772dma.o
  else
    ifeq ($(CONFIG_BLK_DEV_FD),m)
      M_OBJS += fd1772_mod.o
    endif
  endif
endif

ifeq ($(CONFIG_BLK_DEV_IDE_ICSIDE),y)
  L_OBJS += ide-ics.o
else
  ifeq ($(CONFIG_BLK_DEV_IDE_ICSIDE),m)
    M_OBJS += ide-ics.o
  endif
endif

ifeq ($(CONFIG_BLK_DEV_IDE_RAPIDE),y)
  L_OBJS += ide-rapide.o
else
  ifeq ($(CONFIG_BLK_DEV_IDE_RAPIDE),m)
    M_OBJS += ide-rapide.o
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
