# Makefile for making XCOFF bootable images for booting on PowerMacs
# using Open Firmware.
#
# Paul Mackerras	January 1997

HOSTCC	= gcc
HOSTCFLAGS = -O -I$(TOPDIR)/include

CC	= $(CROSS_COMPILE)gcc
LD	= $(CROSS_COMPILE)ld
CFLAGS	= -O -fno-builtin -I$(TOPDIR)/include
OBJCOPY = $(CROSS_COMPILE)objcopy
OBJCOPY_ARGS = -O aixcoff-rs6000 -R .stab -R .stabstr -R .comment \
	--add-section=image=vmlinux.gz
LD_ARGS = -e _start -T ld.script -Ttext 500000 -Tdata 510000 -Bstatic
GZ = gzip -9

OBJS = crt0.o start.o main.o misc.o string.o zlib.o
LIBS = $(TOPDIR)/lib/lib.a

ifeq ($(CONFIG_ALL_PPC),y)
# yes, we want to build pmac stuff
CONFIG_PMAC = y
endif

ifeq ($(CONFIG_PMAC),y)
hack-coff: hack-coff.c
	$(HOSTCC) $(HOSTCFLAGS) -o hack-coff hack-coff.c

floppy: zImage
#	mount -t hfs /dev/fd0 /mnt
#	cp vmlinux.coff /mnt
#	umount /mnt

znetboot: vmlinux.coff
	cp vmlinux.coff /tftpboot/zImage.pmac

znetboot.initrd: vmlinux.coff.initrd
	cp vmlinux.coff.initrd /tftpboot/zImage.pmac

coffboot: $(OBJS) ld.script
	$(LD) -o coffboot $(LD_ARGS) $(OBJS) $(LIBS)

zImage: vmlinux.coff

zImage.initrd: vmlinux.coff.initrd

vmlinux.coff: coffboot hack-coff vmlinux.gz
	$(OBJCOPY) $(OBJCOPY_ARGS) coffboot $@
	./hack-coff $@
	ln -sf vmlinux.coff zImage

vmlinux.coff.initrd: coffboot hack-coff vmlinux.gz ramdisk.image.gz
	$(OBJCOPY) $(OBJCOPY_ARGS) --add-section=initrd=ramdisk.image.gz \
		coffboot $@
	./hack-coff $@

else
znetboot: vmlinux.gz

znetboot.initrd: vmlinux.gz

coffboot: vmlinux.gz

zImage: vmlinux.gz

zImage.initrd: vmlinux.gz

vmlinux.coff: vmlinux.gz

vmlinux.coff.initrd: vmlinux.gz

floppy: vmlinux.gz

endif

vmlinux.gz: $(TOPDIR)/vmlinux
	$(OBJCOPY) -S -O binary $(TOPDIR)/vmlinux vmlinux
	gzip -vf9 vmlinux

clean:
	rm -f hack-coff coffboot zImage vmlinux.coff vmlinux.gz

fastdep:
