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
ALL_SUB_DIRS = minix ext ext2 fat msdos vfat proc isofs nfs xiafs umsdos \
		hpfs sysv smbfs ncpfs ufs affs

ifeq ($(CONFIG_QUOTA),y)
O_OBJS += dquot.o
else
O_OBJS += noquot.o
endif

ifeq ($(CONFIG_MINIX_FS),y)
SUB_DIRS += minix
else
  ifeq ($(CONFIG_MINIX_FS),m)
  MOD_SUB_DIRS += minix
  endif
endif

ifeq ($(CONFIG_EXT_FS),y)
SUB_DIRS += ext
else
  ifeq ($(CONFIG_EXT_FS),m)
  MOD_SUB_DIRS += ext
  endif
endif

ifeq ($(CONFIG_EXT2_FS),y)
SUB_DIRS += ext2
else
  ifeq ($(CONFIG_EXT2_FS),m)
  MOD_SUB_DIRS += ext2
  endif
endif

ifeq ($(CONFIG_FAT_FS),y)
SUB_DIRS += fat
else
  ifeq ($(CONFIG_FAT_FS),m)
  MOD_SUB_DIRS += fat
  endif
endif

ifeq ($(CONFIG_MSDOS_FS),y)
SUB_DIRS += msdos
else
  ifeq ($(CONFIG_MSDOS_FS),m)
  MOD_SUB_DIRS += msdos
  endif
endif

ifeq ($(CONFIG_VFAT_FS),y)
SUB_DIRS += vfat
else
  ifeq ($(CONFIG_VFAT_FS),m)
  MOD_SUB_DIRS += vfat
  endif
endif

ifdef CONFIG_PROC_FS
SUB_DIRS += proc
endif

ifeq ($(CONFIG_ISO9660_FS),y)
SUB_DIRS += isofs
else
  ifeq ($(CONFIG_ISO9660_FS),m)
  MOD_SUB_DIRS += isofs
  endif
endif

ifeq ($(CONFIG_NFS_FS),y)
SUB_DIRS += nfs
else
  ifeq ($(CONFIG_NFS_FS),m)
  MOD_SUB_DIRS += nfs
  endif
endif

ifeq ($(CONFIG_XIA_FS),y)
SUB_DIRS += xiafs
else
  ifeq ($(CONFIG_XIA_FS),m)
  MOD_SUB_DIRS += xiafs
  endif
endif

ifeq ($(CONFIG_UMSDOS_FS),y)
SUB_DIRS += umsdos
else
  ifeq ($(CONFIG_UMSDOS_FS),m)
  MOD_SUB_DIRS += umsdos
  endif
endif

ifeq ($(CONFIG_SYSV_FS),y)
SUB_DIRS += sysv
else
  ifeq ($(CONFIG_SYSV_FS),m)
  MOD_SUB_DIRS += sysv
  endif
endif

ifeq ($(CONFIG_SMB_FS),y)
SUB_DIRS += smbfs
else
  ifeq ($(CONFIG_SMB_FS),m)
  MOD_SUB_DIRS += smbfs
  endif
endif

ifeq ($(CONFIG_NCP_FS),y)
SUB_DIRS += ncpfs
else
  ifeq ($(CONFIG_NCP_FS),m)
  MOD_SUB_DIRS += ncpfs
  endif
endif

ifeq ($(CONFIG_HPFS_FS),y)
SUB_DIRS += hpfs
else
  ifeq ($(CONFIG_HPFS_FS),m)
  MOD_SUB_DIRS += hpfs
  endif
endif

ifeq ($(CONFIG_UFS_FS),y)
SUB_DIRS += ufs
else
  ifeq ($(CONFIG_UFS_FS),m)
  MOD_SUB_DIRS += ufs
  endif
endif

ifeq ($(CONFIG_AFFS_FS),y)
SUB_DIRS += affs
else
  ifeq ($(CONFIG_AFFS_FS),m)
  MOD_SUB_DIRS += affs
  endif
endif

ifeq ($(CONFIG_BINFMT_ELF),y)
BINFMTS += binfmt_elf.o
else
  ifeq ($(CONFIG_BINFMT_ELF),m)
  M_OBJS += binfmt_elf.o
  endif
endif

ifeq ($(CONFIG_BINFMT_AOUT),y)
BINFMTS += binfmt_aout.o
else
  ifeq ($(CONFIG_BINFMT_AOUT),m)
  M_OBJS += binfmt_aout.o
  endif
endif

ifeq ($(CONFIG_BINFMT_JAVA),y)
BINFMTS += binfmt_java.o
else
  ifeq ($(CONFIG_BINFMT_JAVA),m)
  M_OBJS += binfmt_java.o
  endif
endif

# binfmt_script is always there
BINFMTS += binfmt_script.o

include $(TOPDIR)/Rules.make
