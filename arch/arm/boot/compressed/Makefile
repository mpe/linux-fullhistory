#
# linux/arch/arm/boot/compressed/Makefile
#
# create a compressed vmlinuz image from the original vmlinux

HEAD		 = head.o
OBJS		 = misc.o
SYSTEM		 = $(TOPDIR)/vmlinux
CFLAGS		 = $(CPPFLAGS) -O2 -DSTDC_HEADERS $(CFLAGS_PROC)
FONTC		 = $(TOPDIR)/drivers/video/font_acorn_8x8.c
ZLDFLAGS	 = -p -X -T vmlinux.lds

#
# Architecture dependencies
#
ifeq ($(CONFIG_ARCH_ACORN),y)
OBJS		+= ll_char_wr.o font.o
endif

ifeq ($(CONFIG_CPU_26),y)
ZTEXTADDR	 = 0x02080000
endif

ifeq ($(CONFIG_ARCH_RPC),y)
ZTEXTADDR	 = 0x10008000
endif

ifeq ($(CONFIG_ARCH_CLPS7500),y)
ZTEXTADDR	 = 0x10008000
endif

ifeq ($(CONFIG_ARCH_EBSA110),y)
ZTEXTADDR	 = 0x00008000
endif

ifeq ($(CONFIG_FOOTBRIDGE),y)
ZTEXTADDR	 = 0x00008000
endif

ifeq ($(CONFIG_ARCH_NETWINDER),y)
OBJS		+= head-netwinder.o
endif

ifeq ($(CONFIG_ARCH_NEXUSPCI),y)
HEAD		 = head-nexuspci.o
ZTEXTADDR	 = 0x40200000
ZRELADDR	 = 0x40008000
endif

ifeq ($(CONFIG_ARCH_SA1100),y)
OBJS		+= head-sa1100.o
ZTEXTADDR	 = 0xc0008000
ZRELADDR	 = 0xc0008000
ifeq ($(CONFIG_SA1100_VICTOR),y)
  ZTEXTADDR	 = 0x00002000
  ZBSSADDR	 = 0xc0100000
endif
endif

#
# If you don't define ZRELADDR above,
# then it defaults to ZTEXTADDR
#
ifeq ($(ZRELADDR),)
ZRELADDR	= $(ZTEXTADDR)
endif

SEDFLAGS	= s/TEXT_START/$(ZTEXTADDR)/;s/LOAD_ADDR/$(ZRELADDR)/;

ifneq ($(ZBSSADDR),)
SEDFLAGS	+= s/BSS_START/$(ZBSSADDR)/
else
SEDFLAGS	+= s/BSS_START/ALIGN(4)/
endif

all:		vmlinux

vmlinux:	$(HEAD) $(OBJS) piggy.o vmlinux.lds
		$(LD) $(ZLDFLAGS) $(HEAD) $(OBJS) piggy.o $(LIBGCC) -o vmlinux

$(HEAD): 	$(HEAD:.o=.S)
		$(CC) $(AFLAGS) -traditional -c $(HEAD:.o=.S)

piggy.o:	$(SYSTEM)
		$(OBJCOPY) $(SYSTEM) piggy
		gzip -9 < piggy > piggy.gz
		$(LD) -r -o $@ -b binary piggy.gz
		rm -f piggy piggy.gz

font.o:		$(FONTC)
		$(CC) $(CFLAGS) -Dstatic= -c -o $@ $(FONTC)

vmlinux.lds:	vmlinux.lds.in
		@sed "$(SEDFLAGS)" < vmlinux.lds.in > $@

clean:;		rm -f vmlinux core piggy* vmlinux.lds

.PHONY:	vmlinux.lds clean

misc.o: misc.c $(TOPDIR)/include/asm/arch/uncompress.h $(TOPDIR)/lib/inflate.c
