
HEAD = head.o
SYSTEM = ../tools/zSystem
#LD = gcc
#TEST = -DTEST_DRIVER

zOBJECTS = $(HEAD) inflate.o unzip.o misc.o

CFLAGS = -O2 -DSTDC_HEADERS $(TEST)

.c.s:
	$(CC) $(CFLAGS) -S $<
.s.o:
	$(AS) -o $*.o $<
.c.o:
	$(CC) $(CFLAGS) -c $<

all:	zSystem

zSystem:	piggy.o $(zOBJECTS)
		$(LD) $(LOWLDFLAGS) -o zSystem $(zOBJECTS) piggy.o

head.o:	head.s

head.s: head.S ../include/linux/tasks.h
	$(CPP) -traditional head.S -o head.s

piggy.o:	$(SYSTEM) xtract piggyback
		./xtract $(SYSTEM) | gzip -9 | ./piggyback > piggy.o

$(SYSTEM):
		$(MAKE) -C .. tools/zSystem
