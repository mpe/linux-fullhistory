# Makefile for making XCOFF bootable images for booting on PowerMacs
# using Open Firmware.
#
# Paul Mackerras	January 1997

# PowerPC (cross) tools
ifneq ($(shell uname -m),ppc)
CROSS_COMPILE 	=powerpc-eabi-
endif

HOSTCC	= gcc
HOSTCFLAGS = -O -I$(TOPDIR)/include

CC	= $(CROSS_COMPILE)gcc
LD	= $(CROSS_COMPILE)ld
CFLAGS	= -O -fno-builtin -I$(TOPDIR)/include
OBJCOPY = $(CROSS_COMPILE)objcopy
OBJCOPY_ARGS = -O aixcoff-rs6000 -R .stab -R .stabstr -R .comment \
	--add-section=image=zImage
LD_ARGS = -e _start -T ld.script -Ttext 500000 -Tdata 510000 -Bstatic
GZIP = gzip -9

OBJS = crt0.o start.o main.o misc.o string.o zlib.o
LIBS = $(TOPDIR)/lib/lib.a

vmlinux.coff: coffboot hack-coff zImage
	$(OBJCOPY) $(OBJCOPY_ARGS) coffboot $@
	./hack-coff $@

vmlinux.coff.initrd: coffboot hack-coff zImage ramdisk.image.gz
	$(OBJCOPY) $(OBJCOPY_ARGS) --add-section=initrd=ramdisk.image.gz \
		coffboot $@
	./hack-coff $@

coffboot: $(OBJS) ld.script
	$(LD) -o coffboot $(LD_ARGS) $(OBJS) $(LIBS)

zImage: $(TOPDIR)/vmlinux elfextract
	./elfextract $(TOPDIR)/vmlinux | $(GZIP) >zImage

hack-coff: hack-coff.c
	$(HOSTCC) $(HOSTCFLAGS) -o hack-coff hack-coff.c

elfextract: elfextract.c
	$(HOSTCC) $(HOSTCFLAGS) -o elfextract elfextract.c

clean:
	rm -f elfextract hack-coff coffboot zImage vmlinux.coff

fastdep:
