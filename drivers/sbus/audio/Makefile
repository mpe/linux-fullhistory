#
# Makefile for the kernel SPARC audio drivers.
#
# 7 October 2000, Bartlomiej Zolnierkiewicz <bkz@linux-ide.org>
# Rewritten to use lists instead of if-statements.
#

O_TARGET := sparcaudio.o

export-objs	:= audio.o amd7930.o dbri.o

obj-$(CONFIG_SPARCAUDIO)		+= audio.o
obj-$(CONFIG_SPARCAUDIO_AMD7930)	+= amd7930.o
obj-$(CONFIG_SPARCAUDIO_CS4231)		+= cs4231.o
obj-$(CONFIG_SPARCAUDIO_DBRI)		+= dbri.o
obj-$(CONFIG_SPARCAUDIO_DUMMY)		+= dmy.o

O_OBJS		:= $(filter-out	$(export-objs), $(obj-y))
OX_OBJS		:= $(filter	$(export-objs), $(obj-y))
M_OBJS		:= $(sort $(filter-out	$(export-objs), $(obj-m)))
MX_OBJS		:= $(sort $(filter	$(export-objs), $(obj-m)))

include $(TOPDIR)/Rules.make
