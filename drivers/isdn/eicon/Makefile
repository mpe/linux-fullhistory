L_OBJS :=
M_OBJS :=
O_OBJS := eicon_mod.o eicon_isa.o eicon_pci.o eicon_idi.o eicon_io.o

O_TARGET :=
ifeq ($(CONFIG_ISDN_DRV_EICON),y)
  O_TARGET += eicon.o
else
  O_TARGET += eicon.o
  M_OBJS   = eicon.o
endif

include $(TOPDIR)/Rules.make
