L_OBJS :=
M_OBJS :=
LX_OBJS :=
MX_OBJS :=
O_OBJS :=
OX_OBJS :=
L_TARGET :=
O_TARGET :=

ifeq ($(CONFIG_ISDN_DRV_EICON_STANDALONE),y)

 ifeq ($(CONFIG_PCI),y)
   O_OBJS += common.o idi.o bri.o pri.o log.o xlog.o kprintf.o fpga.o fourbri.o
   O_OBJS += lincfg.o linchr.o linsys.o linio.o
   O_OBJS += fcheck.o
   OX_OBJS += Divas_mod.o
 endif

else

 OX_OBJS += eicon_mod.o
 O_OBJS := eicon_isa.o eicon_pci.o eicon_idi.o eicon_io.o
 O_OBJS += fcheck.o
 ifeq ($(CONFIG_PCI),y)
  ifeq ($(CONFIG_ISDN_DRV_EICON_PCI),y)
   O_OBJS += common.o idi.o bri.o pri.o log.o xlog.o kprintf.o fpga.o fourbri.o
   O_OBJS += lincfg.o linchr.o linsys.o linio.o
  endif
 endif

endif

O_TARGET :=

ifeq ($(CONFIG_ISDN_DRV_EICON),y)
  O_TARGET += eicon.o
else
  O_TARGET += eicon.o
  M_OBJS   = eicon.o
endif


include $(TOPDIR)/Rules.make

MD5FILES += common.c idi.c bri.c pri.c log.c xlog.c kprintf.c fpga.c \
            fourbri.c fcheck.c

FCHECK = $(shell md5sum -c md5sums.asc >> /dev/null;echo $$?)

fcheck.o: $(MD5FILES)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -D FILECHECK=$(FCHECK) -c -o fcheck.o fcheck.c

