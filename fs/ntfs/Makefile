# Rules for making the NTFS driver

O_TARGET := ntfs.o
O_OBJS   := fs.o sysctl.o support.o util.o inode.o dir.o super.o attr.o
M_OBJS   := $(O_TARGET)
EXTRA_CFLAGS = -DNTFS_IN_LINUX_KERNEL -DNTFS_VERSION=\"000410\"

include $(TOPDIR)/Rules.make

