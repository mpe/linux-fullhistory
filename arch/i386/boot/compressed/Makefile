#
# linux/arch/i386/boot/compressed/Makefile
#
# create a compressed vmlinux image from the original vmlinux
#

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

vmlinux:	piggy.o $(OBJECTS)
		$(LD) $(ZLINKFLAGS) -o vmlinux $(OBJECTS) piggy.o

head.o:	head.s

head.s: head.S $(TOPDIR)/include/linux/tasks.h
	$(CPP) -traditional head.S -o head.s

piggy.o:	$(SYSTEM) xtract piggyback
		./xtract $(SYSTEM) | gzip -9 | ./piggyback > piggy.o

xtract: xtract.c
	$(HOSTCC) $(CFLAGS) -o xtract xtract.c

piggyback: piggyback.c
	$(HOSTCC) $(CFLAGS) -o piggyback piggyback.c

clean:
	rm -f xtract piggyback vmlinux
