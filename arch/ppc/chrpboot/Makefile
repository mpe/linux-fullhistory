# Makefile for making ELF bootable images for booting on CHRP
# using Open Firmware.
#
# Geert Uytterhoeven	September 1997
#
# Based on coffboot by Paul Mackerras

.c.s:
	$(CC) $(CFLAGS) -S -o $*.s $<
.s.o:
	$(AS) -o $*.o $<
.c.o:
	$(CC) $(CFLAGS) -DKERNELBASE=$(KERNELBASE) -c -o $*.o $<
.S.s:
	$(CC) -D__ASSEMBLY__ -traditional -E -o $*.o $<
.S.o:
	$(CC) -D__ASSEMBLY__ -traditional -c -o $*.o $<

CFLAGS	= -O -fno-builtin -DSTDC_HEADERS -I$(TOPDIR)/include
LD_ARGS = -T ../vmlinux.lds -Ttext 0x00800000
OBJCOPY = $(CROSS_COMPILE)objcopy

OBJS = crt0.o start.o main.o misc.o ../coffboot/string.o ../coffboot/zlib.o image.o # initrd.o
LIBS = $(TOPDIR)/lib/lib.a

ifeq ($(CONFIG_ALL_PPC),y)
# yes, we want to build chrp stuff
CONFIG_CHRP = y
endif

all:	$(TOPDIR)/zImage

#
# Only build anything here if we're configured for CHRP
# -- cort
#
ifeq ($(CONFIG_CHRP),y)
znetboot: zImage
	cp zImage /tftpboot/zImage.chrp

znetboot.initrd: zImage.initrd
	cp zImage.initrd /tftpboot/zImage.chrp

floppy: zImage
	mcopy zImage a:zImage

piggyback: piggyback.c
	$(HOSTCC) -DKERNELBASE=$(KERNELBASE) -o piggyback piggyback.c

image.o: piggyback ../coffboot/vmlinux.gz
	./piggyback image < ../coffboot/vmlinux.gz | $(AS) -o image.o

initrd.o: ramdisk.image.gz piggyback
	./piggyback initrd < ramdisk.image.gz | $(AS) -o initrd.o

zImage: $(OBJS) no_initrd.o
	$(LD) $(LD_ARGS) -o $@ $(OBJS) no_initrd.o $(LIBS)
	objcopy zImage zImage

zImage.initrd: $(OBJS) initrd.o
	$(LD) $(LD_ARGS) -o $@ $(OBJS) initrd.o $(LIBS)

else
znetboot:

znetboot.initrd:

floppy:

zImage:

zImage.initrd:

endif

# just here to match coffboot/Makefile
vmlinux.coff:

vmlinux.coff.initrd:


clean:
	rm -f piggyback
	rm -f $(OBJS) zImage

fastdep:
	$(TOPDIR)/scripts/mkdep *.[Sch] > .depend

dep:
	$(CPP) -M *.S *.c > .depend

