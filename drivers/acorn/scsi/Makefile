#
# Makefile for drivers/acorn/scsi
#

L_TARGET := acorn-scsi.a
L_OBJS   :=
LX_OBJS  :=
M_OBJS   :=
MX_OBJS  :=
MOD_LIST_NAME := ACORN_SCSI_MODULES

CONFIG_QUEUE_BUILTIN :=
CONFIG_FAS216_BUILTIN :=
CONFIG_QUEUE_MODULE :=
CONFIG_FAS216_MODULE :=

ifeq ($(CONFIG_SCSI_ACORNSCSI_3),y)
  L_OBJS += acornscsi.o acornscsi-io.o
  CONFIG_QUEUE_BUILTIN=y
else
  ifeq ($(CONFIG_SCSI_ACORNSCSI_3),m)
    M_OBJS += acornscsi_mod.o
    CONFIG_QUEUE_MODULE=y
  endif
endif

ifeq ($(CONFIG_SCSI_CUMANA_1),y)
  L_OBJS += cumana_1.o
else
  ifeq ($(CONFIG_SCSI_CUMANA_1),m)
    M_OBJS += cumana_1.o
  endif
endif

ifeq ($(CONFIG_SCSI_CUMANA_2),y)
  L_OBJS += cumana_2.o
  CONFIG_QUEUE_BUILTIN=y
  CONFIG_FAS216_BUILTIN=y
else
  ifeq ($(CONFIG_SCSI_CUMANA_2),m)
    M_OBJS += cumana_2.o
    CONFIG_QUEUE_MODULE=y
    CONFIG_FAS216_MODULE=y
  endif
endif

ifeq ($(CONFIG_SCSI_ECOSCSI),y)
  L_OBJS += ecoscsi.o
else
  ifeq ($(CONFIG_SCSI_ECOSCSI),m)
    M_OBJS += ecoscsi.o
  endif
endif

ifeq ($(CONFIG_SCSI_OAK1),y)
  L_OBJS += oak.o
else
  ifeq ($(CONFIG_SCSI_OAK1),m)
    M_OBJS += oak.o
  endif
endif

ifeq ($(CONFIG_SCSI_POWERTECSCSI),y)
  L_OBJS += powertec.o
  CONFIG_QUEUE_BUILTIN=y
  CONFIG_FAS216_BUILTIN=y
else
  ifeq ($(CONFIG_SCSI_POWERTECSCSI),m)
    M_OBJS += powertec.o
    CONFIG_QUEUE_MODULE=y
    CONFIG_FAS216_MODULE=y
  endif
endif

ifeq ($(CONFIG_SCSI_EESOXSCSI),y)
  L_OBJS += eesox.o
  CONFIG_QUEUE_BUILTIN=y
  CONFIG_FAS216_BUILTIN=y
else
  ifeq ($(CONFIG_SCSI_EESOXSCSI),m)
    M_OBJS += eesox.o
    CONFIG_QUEUE_MODULE=y
    CONFIG_FAS216_MODULE=y
  endif
endif

ifeq ($(CONFIG_QUEUE_BUILTIN),y)
  LX_OBJS += queue.o msgqueue.o
else
  ifeq ($(CONFIG_QUEUE_MODULE),y)
    MX_OBJS += queue.o msgqueue.o
  endif
endif

ifeq ($(CONFIG_FAS216_BUILTIN),y)
  LX_OBJS += fas216.o
else
  ifeq ($(CONFIG_FAS216_MODULE),y)
    MX_OBJS += fas216.o
  endif
endif

include $(TOPDIR)/Rules.make

acornscsi_mod.o: acornscsi.o acornscsi-io.o
	$(LD) $(LD_RFLAG) -r -o $@ acornscsi.o acornscsi-io.o

%.o: %.S
ifndef $(CONFIG_BINUTILS_NEW)
	$(CC) $(CFLAGS) -D__ASSEMBLY__ -E $< | tr ';$$' '\n#' > ..tmp.$<.s
	$(CC) $(CFLAGS:-pipe=) -c -o $@ ..tmp.$<.s
	$(RM) ..tmp.$<.s
else
	$(CC) $(CFLAGS) -D__ASSEMBLY__ -c -o $@ $<
endif
