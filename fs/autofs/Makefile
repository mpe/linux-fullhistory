#
# Makefile for the linux autofs-filesystem routines.
#
# We can build this either out of the kernel tree or the autofs tools tree.
#

O_TARGET := autofs.o
O_OBJS   := dir.o dirhash.o init.o inode.o root.o symlink.o waitq.o

M_OBJS   := $(O_TARGET)

ifdef TOPDIR
#
# Part of the kernel code
#
include $(TOPDIR)/Rules.make
else
#
# Standalone (handy for development)
#
include ../Makefile.rules

CFLAGS += -D__KERNEL__ -DMODULE $(KFLAGS) -I../include -I$(KINCLUDE) $(MODFLAGS)

all: $(O_TARGET)

$(O_TARGET): $(O_OBJS)
	$(LD) -r -o $(O_TARGET) $(O_OBJS)

install: $(O_TARGET)
	install -c $(O_TARGET) /lib/modules/`uname -r`/fs

clean:
	rm -f *.o *.s
endif
