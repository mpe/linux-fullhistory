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
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -traditional -E -o $*.o $<
.S.o:
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -traditional -c -o $*.o $<

CFLAGS	= $(CPPFLAGS) -O -fno-builtin -DSTDC_HEADERS
LD_ARGS = -Ttext 0x00400000
OBJCOPY = $(CROSS_COMPILE)objcopy

OBJS = crt0.o start.o main.o misc.o ../coffboot/string.o ../coffboot/zlib.o image.o sysmap.o
LIBS = $(TOPDIR)/lib/lib.a

ifeq ($(CONFIG_PPC64),y)
MSIZE=.64
else
MSIZE=
endif

ifeq ($(CONFIG_ALL_PPC),y)
# yes, we want to build chrp stuff
CONFIG_CHRP = y
endif

ifeq ($(CONFIG_SMP),y)
TFTPIMAGE=/tftpboot/zImage.chrp.smp$(MSIZE)
else
TFTPIMAGE=/tftpboot/zImage.chrp$(MSIZE)
endif

all:	$(TOPDIR)/zImage

#
# Only build anything here if we're configured for CHRP
# -- cort
#
ifeq ($(CONFIG_CHRP),y)
znetboot: zImage
	cp zImage $(TFTPIMAGE)

znetboot.initrd: zImage.initrd
	cp zImage.initrd $(TFTPIMAGE)

floppy: zImage
	mcopy zImage a:zImage

piggyback: piggyback.c
	$(HOSTCC) $(HOSTCFLAGS) -DKERNELBASE=$(KERNELBASE) -o piggyback piggyback.c

mknote: mknote.c
	$(HOSTCC) $(HOSTCFLAGS) -o mknote mknote.c

image.o: piggyback ../coffboot/vmlinux.gz
	./piggyback image < ../coffboot/vmlinux.gz | $(AS) -o image.o

sysmap.o: piggyback ../../../System.map
	./piggyback sysmap < ../../../System.map | $(AS) -o sysmap.o

initrd.o: ramdisk.image.gz piggyback
	./piggyback initrd < ramdisk.image.gz | $(AS) -o initrd.o

zImage: $(OBJS) no_initrd.o mknote
	$(LD) $(LD_ARGS) -o $@ $(OBJS) no_initrd.o $(LIBS)
	./mknote > note
	$(OBJCOPY) $@ $@ --add-section=.note=note -R .comment

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
	rm -f piggyback note mknote $(OBJS) zImage

fastdep:
	$(TOPDIR)/scripts/mkdep *.[Sch] > .depend

dep:
	$(CPP) $(CPPFLAGS) -M *.S *.c > .depend

