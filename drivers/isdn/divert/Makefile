#
# Makefile for the dss1_divert ISDN module
#

# The target object and module list name.

O_TARGET	:=

# Objects that export symbols.

export-objs	:=

# Multipart objects.

list-multi		:= dss1_divert.o
dss1_divert-objs	:= isdn_divert.o divert_procfs.o divert_init.o

# Object file lists.

obj-y	:=
obj-m	:=
obj-n	:=
obj-	:=

# Each configuration option enables a list of files.

obj-m	+= dss1_divert.o

include $(TOPDIR)/Rules.make

# Link rules for multi-part drivers.

dss1_divert.o: $(dss1_divert-objs)
	$(LD) -r -o $@ $(dss1_divert-objs)






