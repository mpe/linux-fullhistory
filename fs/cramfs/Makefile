#
# Makefile for the linux cramfs routines.
#

O_TARGET := cramfs.o

O_OBJS := inode.o uncompress.o inflate/zlib.o

include $(TOPDIR)/Rules.make

inflate/zlib.o:
	make -C inflate
