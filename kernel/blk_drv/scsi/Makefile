#
# Makefile for the linux kernel block device drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

#DEBUG = -DDEBUG=0xffffffff -DDEBUG_NO_CMD

.c.s:
	$(CC) $(CFLAGS) $(DEBUG) -S $<

.s.o:
	$(AS) -c -o $*.o $<

.c.o:
	$(CC) $(CFLAGS) $(DEBUG) -c $<


LOWLEVELSSRC = seagate2.s
LOWLEVELCSRC = aha1542.c fdomain.c seagate.c ultrastor.c 7000fasst.c
LOWLEVELHSRC = aha1542.h fdomain.h seagate.h ultrastor.h 7000fasst.o
 
CSRC = hosts.c sd.c sd_ioctl.c st.c st_ioctl.c scsi.c scsi_ioctl.c $(LOWLEVELCSRC)
HSRC = hosts.h sd.h st.h scsi.h scsi_ioctl.h $(LOWLEVELHSRC)

OBJS =	scsi.o hosts.o scsi_ioctl.o sd.o sd_ioctl.o st.o st_ioctl.o \
	aha1542.o fdomain.o seagate.o seagate2.o ultrastor.o 7000fasst.o

all: scsi.a

figure : hosts.h $(KERNELHDRS)/linux/config.h hosts.c 
	$(HOSTCC) -N -DFIGURE_MAX_SCSI_HOSTS hosts.c -o figure

max_hosts.h : figure
	(echo "#ifndef _MAX_HOSTS_H"; \
	 echo "#define _MAX_HOSTS_H"; \
	 echo "#define MAX_SCSI_HOSTS `./figure`";\
	 echo "#endif") > tmp_max
	cp tmp_max max_hosts.h
		
scsi.a: $(OBJS)
	$(AR) rcs scsi.a $(OBJS)
	sync

clean:
	rm -f core *.o *.a tmp_make tmp_max figure max_hosts.h

seagate2.o : seagate2.s

seagate.o: seagate.c
	$(CC) -Wall -c seagate.c 

dep:
	touch max_hosts.h
	sed '/\#\#\# Dependencies/q' < Makefile > tmp_make
	for i in *.c;do $(CPP) -M $$i;done >> tmp_make
	rm max_hosts.h
	cp tmp_make Makefile

### Dependencies:
7000fasst.o : 7000fasst.c /usr/src/linux/include/linux/config.h /usr/src/linux/include/linux/config_rel.h \
  /usr/src/linux/include/linux/config_ver.h /usr/src/linux/include/linux/config.dist.h \
  /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/linux/head.h /usr/src/linux/include/linux/types.h \
  /usr/src/linux/include/linux/string.h /usr/src/linux/include/asm/system.h /usr/src/linux/include/asm/io.h \
  scsi.h hosts.h max_hosts.h 7000fasst.h 
aha1542.o : aha1542.c /usr/src/linux/include/linux/config.h /usr/src/linux/include/linux/config_rel.h \
  /usr/src/linux/include/linux/config_ver.h /usr/src/linux/include/linux/config.dist.h \
  /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/linux/head.h /usr/src/linux/include/linux/types.h \
  /usr/src/linux/include/linux/string.h /usr/src/linux/include/asm/system.h /usr/src/linux/include/asm/io.h \
  scsi.h hosts.h max_hosts.h aha1542.h 
fdomain.o : fdomain.c /usr/src/linux/include/linux/config.h /usr/src/linux/include/linux/config_rel.h \
  /usr/src/linux/include/linux/config_ver.h /usr/src/linux/include/linux/config.dist.h \
  /usr/src/linux/include/linux/sched.h /usr/src/linux/include/linux/head.h /usr/src/linux/include/linux/fs.h \
  /usr/src/linux/include/linux/limits.h /usr/src/linux/include/linux/wait.h /usr/src/linux/include/linux/types.h \
  /usr/src/linux/include/linux/dirent.h /usr/src/linux/include/linux/vfs.h /usr/src/linux/include/linux/minix_fs_sb.h \
  /usr/src/linux/include/linux/ext_fs_sb.h /usr/src/linux/include/linux/msdos_fs_sb.h \
  /usr/src/linux/include/linux/mm.h /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/linux/signal.h \
  /usr/src/linux/include/linux/time.h /usr/src/linux/include/linux/param.h /usr/src/linux/include/linux/resource.h \
  /usr/src/linux/include/asm/io.h fdomain.h scsi.h hosts.h max_hosts.h /usr/src/linux/include/asm/system.h \
  /usr/src/linux/include/linux/errno.h 
hosts.o : hosts.c /usr/src/linux/include/linux/config.h /usr/src/linux/include/linux/config_rel.h \
  /usr/src/linux/include/linux/config_ver.h /usr/src/linux/include/linux/config.dist.h \
  /usr/src/linux/include/linux/kernel.h scsi.h hosts.h max_hosts.h aha1542.h /usr/src/linux/include/linux/types.h \
  fdomain.h seagate.h ultrastor.h 7000fasst.h 
scsi.o : scsi.c /usr/src/linux/include/linux/config.h /usr/src/linux/include/linux/config_rel.h \
  /usr/src/linux/include/linux/config_ver.h /usr/src/linux/include/linux/config.dist.h \
  /usr/src/linux/include/asm/system.h /usr/src/linux/include/linux/sched.h /usr/src/linux/include/linux/head.h \
  /usr/src/linux/include/linux/fs.h /usr/src/linux/include/linux/limits.h /usr/src/linux/include/linux/wait.h \
  /usr/src/linux/include/linux/types.h /usr/src/linux/include/linux/dirent.h /usr/src/linux/include/linux/vfs.h \
  /usr/src/linux/include/linux/minix_fs_sb.h /usr/src/linux/include/linux/ext_fs_sb.h \
  /usr/src/linux/include/linux/msdos_fs_sb.h /usr/src/linux/include/linux/mm.h \
  /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/linux/signal.h \
  /usr/src/linux/include/linux/time.h /usr/src/linux/include/linux/param.h /usr/src/linux/include/linux/resource.h \
  /usr/src/linux/include/linux/timer.h /usr/src/linux/include/linux/string.h scsi.h \
  hosts.h max_hosts.h sd.h /usr/src/linux/include/linux/genhd.h st.h 
scsi_ioctl.o : scsi_ioctl.c /usr/src/linux/include/linux/config.h /usr/src/linux/include/linux/config_rel.h \
  /usr/src/linux/include/linux/config_ver.h /usr/src/linux/include/linux/config.dist.h \
  /usr/src/linux/include/asm/io.h /usr/src/linux/include/asm/segment.h /usr/src/linux/include/asm/system.h \
  /usr/src/linux/include/linux/errno.h /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/linux/sched.h \
  /usr/src/linux/include/linux/head.h /usr/src/linux/include/linux/fs.h /usr/src/linux/include/linux/limits.h \
  /usr/src/linux/include/linux/wait.h /usr/src/linux/include/linux/types.h /usr/src/linux/include/linux/dirent.h \
  /usr/src/linux/include/linux/vfs.h /usr/src/linux/include/linux/minix_fs_sb.h \
  /usr/src/linux/include/linux/ext_fs_sb.h /usr/src/linux/include/linux/msdos_fs_sb.h \
  /usr/src/linux/include/linux/mm.h /usr/src/linux/include/linux/signal.h /usr/src/linux/include/linux/time.h \
  /usr/src/linux/include/linux/param.h /usr/src/linux/include/linux/resource.h \
  /usr/src/linux/include/linux/string.h scsi.h hosts.h max_hosts.h scsi_ioctl.h 
sd.o : sd.c /usr/src/linux/include/linux/config.h /usr/src/linux/include/linux/config_rel.h \
  /usr/src/linux/include/linux/config_ver.h /usr/src/linux/include/linux/config.dist.h \
  /usr/src/linux/include/linux/fs.h /usr/src/linux/include/linux/limits.h /usr/src/linux/include/linux/wait.h \
  /usr/src/linux/include/linux/types.h /usr/src/linux/include/linux/dirent.h /usr/src/linux/include/linux/vfs.h \
  /usr/src/linux/include/linux/minix_fs_sb.h /usr/src/linux/include/linux/ext_fs_sb.h \
  /usr/src/linux/include/linux/msdos_fs_sb.h /usr/src/linux/include/linux/kernel.h \
  /usr/src/linux/include/linux/sched.h /usr/src/linux/include/linux/head.h /usr/src/linux/include/linux/mm.h \
  /usr/src/linux/include/linux/signal.h /usr/src/linux/include/linux/time.h /usr/src/linux/include/linux/param.h \
  /usr/src/linux/include/linux/resource.h /usr/src/linux/include/linux/string.h \
  scsi.h sd.h /usr/src/linux/include/linux/genhd.h ../blk.h 
sd_ioctl.o : sd_ioctl.c /usr/src/linux/include/linux/config.h /usr/src/linux/include/linux/config_rel.h \
  /usr/src/linux/include/linux/config_ver.h /usr/src/linux/include/linux/config.dist.h \
  /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/linux/sched.h /usr/src/linux/include/linux/head.h \
  /usr/src/linux/include/linux/fs.h /usr/src/linux/include/linux/limits.h /usr/src/linux/include/linux/wait.h \
  /usr/src/linux/include/linux/types.h /usr/src/linux/include/linux/dirent.h /usr/src/linux/include/linux/vfs.h \
  /usr/src/linux/include/linux/minix_fs_sb.h /usr/src/linux/include/linux/ext_fs_sb.h \
  /usr/src/linux/include/linux/msdos_fs_sb.h /usr/src/linux/include/linux/mm.h \
  /usr/src/linux/include/linux/signal.h /usr/src/linux/include/linux/time.h /usr/src/linux/include/linux/param.h \
  /usr/src/linux/include/linux/resource.h scsi.h sd.h /usr/src/linux/include/linux/genhd.h 
seagate.o : seagate.c /usr/src/linux/include/linux/config.h /usr/src/linux/include/linux/config_rel.h \
  /usr/src/linux/include/linux/config_ver.h /usr/src/linux/include/linux/config.dist.h \
  /usr/src/linux/include/asm/io.h /usr/src/linux/include/asm/system.h /usr/src/linux/include/linux/sched.h \
  /usr/src/linux/include/linux/head.h /usr/src/linux/include/linux/fs.h /usr/src/linux/include/linux/limits.h \
  /usr/src/linux/include/linux/wait.h /usr/src/linux/include/linux/types.h /usr/src/linux/include/linux/dirent.h \
  /usr/src/linux/include/linux/vfs.h /usr/src/linux/include/linux/minix_fs_sb.h \
  /usr/src/linux/include/linux/ext_fs_sb.h /usr/src/linux/include/linux/msdos_fs_sb.h \
  /usr/src/linux/include/linux/mm.h /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/linux/signal.h \
  /usr/src/linux/include/linux/time.h /usr/src/linux/include/linux/param.h /usr/src/linux/include/linux/resource.h \
  seagate.h scsi.h hosts.h max_hosts.h 
st.o : st.c /usr/src/linux/include/linux/config.h /usr/src/linux/include/linux/config_rel.h \
  /usr/src/linux/include/linux/config_ver.h /usr/src/linux/include/linux/config.dist.h \
  scsi.h st.h /usr/src/linux/include/linux/fs.h /usr/src/linux/include/linux/limits.h \
  /usr/src/linux/include/linux/wait.h /usr/src/linux/include/linux/types.h /usr/src/linux/include/linux/dirent.h \
  /usr/src/linux/include/linux/vfs.h /usr/src/linux/include/linux/minix_fs_sb.h \
  /usr/src/linux/include/linux/ext_fs_sb.h /usr/src/linux/include/linux/msdos_fs_sb.h \
  /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/linux/sched.h /usr/src/linux/include/linux/head.h \
  /usr/src/linux/include/linux/mm.h /usr/src/linux/include/linux/signal.h /usr/src/linux/include/linux/time.h \
  /usr/src/linux/include/linux/param.h /usr/src/linux/include/linux/resource.h \
  ../blk.h 
st_ioctl.o : st_ioctl.c /usr/src/linux/include/linux/config.h /usr/src/linux/include/linux/config_rel.h \
  /usr/src/linux/include/linux/config_ver.h /usr/src/linux/include/linux/config.dist.h \
  /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/linux/sched.h /usr/src/linux/include/linux/head.h \
  /usr/src/linux/include/linux/fs.h /usr/src/linux/include/linux/limits.h /usr/src/linux/include/linux/wait.h \
  /usr/src/linux/include/linux/types.h /usr/src/linux/include/linux/dirent.h /usr/src/linux/include/linux/vfs.h \
  /usr/src/linux/include/linux/minix_fs_sb.h /usr/src/linux/include/linux/ext_fs_sb.h \
  /usr/src/linux/include/linux/msdos_fs_sb.h /usr/src/linux/include/linux/mm.h \
  /usr/src/linux/include/linux/signal.h /usr/src/linux/include/linux/time.h /usr/src/linux/include/linux/param.h \
  /usr/src/linux/include/linux/resource.h st.h scsi.h 
ultrastor.o : ultrastor.c /usr/src/linux/include/linux/config.h /usr/src/linux/include/linux/config_rel.h \
  /usr/src/linux/include/linux/config_ver.h /usr/src/linux/include/linux/config.dist.h \
  /usr/src/linux/include/linux/stddef.h /usr/src/linux/include/linux/string.h \
  /usr/src/linux/include/linux/sched.h /usr/src/linux/include/linux/head.h /usr/src/linux/include/linux/fs.h \
  /usr/src/linux/include/linux/limits.h /usr/src/linux/include/linux/wait.h /usr/src/linux/include/linux/types.h \
  /usr/src/linux/include/linux/dirent.h /usr/src/linux/include/linux/vfs.h /usr/src/linux/include/linux/minix_fs_sb.h \
  /usr/src/linux/include/linux/ext_fs_sb.h /usr/src/linux/include/linux/msdos_fs_sb.h \
  /usr/src/linux/include/linux/mm.h /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/linux/signal.h \
  /usr/src/linux/include/linux/time.h /usr/src/linux/include/linux/param.h /usr/src/linux/include/linux/resource.h \
  /usr/src/linux/include/asm/io.h /usr/src/linux/include/asm/system.h ultrastor.h \
  scsi.h hosts.h max_hosts.h 
