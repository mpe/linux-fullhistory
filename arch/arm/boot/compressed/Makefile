#
# linux/arch/arm/boot/compressed/Makefile
#
# create a compressed vmlinuz image from the original vmlinux
#
# With this config, max compressed image size = 640k
# Uncompressed image size = 1.3M (text+data)

SYSTEM	=$(TOPDIR)/vmlinux
HEAD	=$(COMPRESSED_HEAD)
OBJS	=$(HEAD) misc.o $(COMPRESSED_EXTRA)
CFLAGS	=-O2 -DSTDC_HEADERS $(CFLAGS_PROC)
ARFLAGS =rc

all:		vmlinux

vmlinux:	piggy.o $(OBJS)
		$(LD) $(ZLINKFLAGS) -o vmlinux $(OBJS) piggy.o

$(HEAD): 	$(HEAD:.o=.S)
		$(CC) -traditional -DLOADADDR=$(ZRELADDR) -c $(HEAD:.o=.S)

piggy.o:	$(SYSTEM)
		tmppiggy=_tmp_$$$$piggy; \
		rm -f $$tmppiggy $$tmppiggy.gz $$tmppiggy.lnk; \
		$(OBJCOPY) $(SYSTEM) $$tmppiggy; \
		gzip -f -9 < $$tmppiggy > $$tmppiggy.gz; \
		echo "SECTIONS { .data : { input_len = .; LONG(input_data_end - input_data) input_data = .; *(.data) input_data_end = .; }}" > $$tmppiggy.lnk; \
		$(LD) -r -o piggy.o -b binary $$tmppiggy.gz -b elf32-arm -T $$tmppiggy.lnk; \
		rm -f $$tmppiggy $$tmppiggy.gz $$tmppiggy.lnk;

clean:;		rm -f vmlinux core

