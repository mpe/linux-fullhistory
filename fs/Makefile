#
# Makefile for the linux filesystem.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

L_TARGET := filesystems.a
L_OBJS    = $(join $(SUB_DIRS),$(SUB_DIRS:%=/%.o))
O_TARGET := fs.o
O_OBJS    = open.o read_write.o inode.o devices.o file_table.o buffer.o \
		super.o  block_dev.o stat.o exec.o pipe.o namei.o fcntl.o \
		ioctl.o readdir.o select.o fifo.o locks.o filesystems.o \
		dcache.o $(BINFMTS) 

MOD_LIST_NAME := FS_MODULES
ALL_SUB_DIRS = minix ext ext2 msdos proc isofs nfs xiafs umsdos hpfs sysv smbfs

ifdef CONFIG_MINIX_FS
SUB_DIRS += minix
else
MOD_SUB_DIRS += minix
endif

ifdef CONFIG_EXT_FS
SUB_DIRS += ext
endif

ifdef CONFIG_EXT2_FS
SUB_DIRS += ext2
endif

ifdef CONFIG_MSDOS_FS
SUB_DIRS += msdos
else
MOD_SUB_DIRS += msdos
endif

ifdef CONFIG_PROC_FS
SUB_DIRS += proc
endif

ifdef CONFIG_ISO9660_FS
SUB_DIRS += isofs
else
MOD_SUB_DIRS += isofs
endif

ifdef CONFIG_NFS_FS
SUB_DIRS += nfs
else
MOD_SUB_DIRS += nfs
endif

ifdef CONFIG_XIA_FS
SUB_DIRS += xiafs
else
MOD_SUB_DIRS += xiafs
endif

ifdef CONFIG_UMSDOS_FS
SUB_DIRS += umsdos
else
MOD_SUB_DIRS += umsdos
endif

ifdef CONFIG_SYSV_FS
SUB_DIRS += sysv
else
MOD_SUB_DIRS += sysv
endif

ifdef CONFIG_SMB_FS
SUB_DIRS += smbfs
else
MOD_SUB_DIRS += smbfs
endif

ifdef CONFIG_HPFS_FS
SUB_DIRS += hpfs
else
MOD_SUB_DIRS += hpfs
endif

ifdef CONFIG_BINFMT_ELF
BINFMTS += binfmt_elf.o
else
MOD_SUB_DIRS += binfmt_elf.o
endif

include $(TOPDIR)/Rules.make
