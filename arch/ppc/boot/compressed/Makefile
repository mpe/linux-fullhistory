#
# linux/arch/ppc/boot/compressed/Makefile
#
# create a compressed vmlinux image from the original vmlinux
#

TOPDIR = ../../../..
CC = cc.ppc
AS = as.ppc
LD = ld.ppc
ZLINKFLAGS = -T ../../ld.script -Ttext 0x00400000
HOSTCC = cc
#GZIP_FLAGS = -9
GZIP_FLAGS =

HEAD = head.o
SYSTEM = $(TOPDIR)/vmlinux

OBJECTS = $(HEAD) inflate.o unzip.o misc.o

CFLAGS = -O2 -DSTDC_HEADERS

.c.s:
	$(CC) $(CFLAGS) -S $<
.s.o:
	$(AS) -o $*.o $<
.c.o:
	$(CC) $(CFLAGS) -c $<

all: vmlinux

vmlinux: $(OBJECTS) $(SYSTEM) piggyback
	mkboot $(TOPDIR)/vmlinux xx_boot 0
	gzip ${GZIP_FLAGS} <xx_boot | ./piggyback | $(AS) -o piggy.o
	$(LD) $(ZLINKFLAGS) -o vmlinux $(OBJECTS) piggy.o
	rm -f piggy.o xx_boot

vmlinux.no_ramdisk: $(OBJECTS) piggyback
	mkboot $(TOPDIR)/vmlinux.no_ramdisk xx_boot 0
	gzip ${GZIP_FLAGS} <xx_boot | ./piggyback | $(AS) -o piggy.o
	$(LD) $(ZLINKFLAGS) -o vmlinux.no_ramdisk $(OBJECTS) piggy.o
	rm -f piggy.o xx_boot

head.o:	head.s

head.s: head.S $(TOPDIR)/include/linux/tasks.h
	$(CPP) -traditional head.S -o head.s

piggyback: piggyback.c
	$(HOSTCC) $(CFLAGS) -o piggyback piggyback.c

clean:
	rm -f piggyback vmlinux
