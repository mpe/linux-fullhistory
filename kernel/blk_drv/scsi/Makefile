# 1 "tmp_make.c"

# Makefile for kernel/blk_drv/scsi

# Note! This Makefile is automatically generated from makefile.in.
# DO NOT edit makefile, edit makefile.in instead.

# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DONT put your own dependencies here
# unless its something special (ie not a .c file).

# 1 "/usr/include/linux/config.h" 1 3

 

 

 

 

 

 

 

 

 

# 101 "/usr/include/linux/config.h" 3

 

# 1 "/usr/include/linux/config.dist.h" 1 3

# 115 "/usr/include/linux/config.h" 2 3

 

# 12 "tmp_make.c" 2

.c.s:
	$(CC) $(CFLAGS) $(DEBUG) -S $<

.s.o:
	$(AS) -c -o $*.o $<

.c.o:
	$(CC) $(CFLAGS) $(DEBUG) -c $<

ST_CSRC = st.c
ST_OBJS = st.o

SD_CSRC = sd.c sd_ioctl.c
SD_OBJS = sd.o sd_ioctl.o

SR_CSRC = sr.c sr_ioctl.c
SR_OBJS = sr.o sr_ioctl.o

AHA1542_OBJS = aha1542.o

AHA1740_OBJS = aha1740.o

FDOMAIN_OBJS = fdomain.o

ULTRASTOR_OBJS = ultrastor.o

WD7000_OBJS = wd7000.o

SEAGATE_OBJS = seagate.o

SCSI_OBJS = hosts.o scsi.o scsi_ioctl.o

OBJS =	$(SCSI_OBJS) $(SD_OBJS) $(ST_OBJS) $(SR_OBJS) 	$(AHA1542_OBJS) $(AHA1740_OBJS) $(FDOMAIN_OBJS) $(SEAGATE_OBJS) 	$(ULTRASTOR_OBJS) $(WD7000_OBJS) $(SCSI_DEBUG_OBJS)

all: 
	$(MAKE) Makefile
	$(MAKE) scsi.a

figure : hosts.h $(KERNELHDRS)/linux/config.h hosts.c
	$(HOSTCC) -I$(KERNELHDRS) -DFIGURE_MAX_SCSI_HOSTS hosts.c -o figure

max_hosts.h : figure
	(echo "#ifndef _MAX_HOSTS_H"; 	 echo "#define _MAX_HOSTS_H"; 	 echo "#define MAX_SCSI_HOSTS `./figure`";	 echo "#endif") > tmp_max

	cp tmp_max max_hosts.h

have_makefile:
	touch have_makefile

Makefile: ../../../include/linux/config.h 	../../../include/linux/config.site.h 	../../../include/linux/config.dist.h 	 Makefile.in have_makefile

	cp Makefile.in tmp_make.c
	$(CPP) -E tmp_make.c 		| uniq > tmp_make

	rm tmp_make.c
	cp tmp_make Makefile
	rm -f tmp_make*
	$(MAKE) dep

scsi.a: $(OBJS)
	rm -f scsi.a
	$(AR) rcs scsi.a $(OBJS)
	sync

clean:
	rm -f core *.o *.a tmp_make tmp_max figure max_hosts.h have_makefile

seagate.o: seagate.c
	$(CC) -Wall -I$(KERNELHDRS) -c seagate.c 

dep:   max_hosts.h
	sed '/\# \X\XDependencies/q' < Makefile > tmp_make
	$(CPP) -M *.c >> tmp_make
	cp tmp_make Makefile

# XXDependencies:
aha1542.o : aha1542.c /usr/include/linux/kernel.h /usr/include/linux/head.h \
  /usr/include/linux/types.h /usr/include/linux/string.h /usr/include/linux/sched.h \
  /usr/include/linux/fs.h /usr/include/linux/limits.h /usr/include/linux/wait.h \
  /usr/include/linux/dirent.h /usr/include/linux/vfs.h /usr/include/linux/pipe_fs_i.h \
  /usr/include/linux/minix_fs_i.h /usr/include/linux/ext_fs_i.h /usr/include/linux/msdos_fs_i.h \
  /usr/include/linux/iso_fs_i.h /usr/include/linux/minix_fs_sb.h /usr/include/linux/ext_fs_sb.h \
  /usr/include/linux/msdos_fs_sb.h /usr/include/linux/iso_fs_sb.h /usr/include/linux/mm.h \
  /usr/include/linux/signal.h /usr/include/linux/time.h /usr/include/linux/param.h \
  /usr/include/linux/resource.h /usr/include/linux/vm86.h /usr/include/linux/math_emu.h \
  /usr/include/asm/dma.h /usr/include/asm/io.h /usr/include/asm/system.h ../blk.h \
  scsi.h hosts.h aha1542.h 
aha1740.o : aha1740.c /usr/include/linux/kernel.h /usr/include/linux/head.h \
  /usr/include/linux/types.h /usr/include/linux/string.h /usr/include/linux/sched.h \
  /usr/include/linux/fs.h /usr/include/linux/limits.h /usr/include/linux/wait.h \
  /usr/include/linux/dirent.h /usr/include/linux/vfs.h /usr/include/linux/pipe_fs_i.h \
  /usr/include/linux/minix_fs_i.h /usr/include/linux/ext_fs_i.h /usr/include/linux/msdos_fs_i.h \
  /usr/include/linux/iso_fs_i.h /usr/include/linux/minix_fs_sb.h /usr/include/linux/ext_fs_sb.h \
  /usr/include/linux/msdos_fs_sb.h /usr/include/linux/iso_fs_sb.h /usr/include/linux/mm.h \
  /usr/include/linux/signal.h /usr/include/linux/time.h /usr/include/linux/param.h \
  /usr/include/linux/resource.h /usr/include/linux/vm86.h /usr/include/linux/math_emu.h \
  /usr/include/asm/dma.h /usr/include/asm/io.h /usr/include/asm/system.h ../blk.h \
  scsi.h hosts.h aha1740.h 
fdomain.o : fdomain.c /usr/include/linux/sched.h /usr/include/linux/head.h /usr/include/linux/fs.h \
  /usr/include/linux/limits.h /usr/include/linux/wait.h /usr/include/linux/types.h \
  /usr/include/linux/dirent.h /usr/include/linux/vfs.h /usr/include/linux/pipe_fs_i.h \
  /usr/include/linux/minix_fs_i.h /usr/include/linux/ext_fs_i.h /usr/include/linux/msdos_fs_i.h \
  /usr/include/linux/iso_fs_i.h /usr/include/linux/minix_fs_sb.h /usr/include/linux/ext_fs_sb.h \
  /usr/include/linux/msdos_fs_sb.h /usr/include/linux/iso_fs_sb.h /usr/include/linux/mm.h \
  /usr/include/linux/kernel.h /usr/include/linux/signal.h /usr/include/linux/time.h \
  /usr/include/linux/param.h /usr/include/linux/resource.h /usr/include/linux/vm86.h \
  /usr/include/linux/math_emu.h /usr/include/asm/io.h ../blk.h scsi.h hosts.h \
  fdomain.h /usr/include/asm/system.h /usr/include/linux/errno.h 
hosts.o : hosts.c /usr/include/linux/config.h /usr/include/linux/config.dist.h \
  ../blk.h /usr/include/linux/kernel.h scsi.h max_hosts.h hosts.h aha1542.h /usr/include/linux/types.h \
  aha1740.h fdomain.h seagate.h ultrastor.h wd7000.h 
scsi.o : scsi.c /usr/include/asm/system.h /usr/include/linux/sched.h /usr/include/linux/head.h \
  /usr/include/linux/fs.h /usr/include/linux/limits.h /usr/include/linux/wait.h \
  /usr/include/linux/types.h /usr/include/linux/dirent.h /usr/include/linux/vfs.h \
  /usr/include/linux/pipe_fs_i.h /usr/include/linux/minix_fs_i.h /usr/include/linux/ext_fs_i.h \
  /usr/include/linux/msdos_fs_i.h /usr/include/linux/iso_fs_i.h /usr/include/linux/minix_fs_sb.h \
  /usr/include/linux/ext_fs_sb.h /usr/include/linux/msdos_fs_sb.h /usr/include/linux/iso_fs_sb.h \
  /usr/include/linux/mm.h /usr/include/linux/kernel.h /usr/include/linux/signal.h \
  /usr/include/linux/time.h /usr/include/linux/param.h /usr/include/linux/resource.h \
  /usr/include/linux/vm86.h /usr/include/linux/math_emu.h /usr/include/linux/timer.h \
  /usr/include/linux/string.h ../blk.h scsi.h hosts.h 
scsi_debug.o : scsi_debug.c /usr/include/linux/kernel.h /usr/include/linux/sched.h \
  /usr/include/linux/head.h /usr/include/linux/fs.h /usr/include/linux/limits.h \
  /usr/include/linux/wait.h /usr/include/linux/types.h /usr/include/linux/dirent.h \
  /usr/include/linux/vfs.h /usr/include/linux/pipe_fs_i.h /usr/include/linux/minix_fs_i.h \
  /usr/include/linux/ext_fs_i.h /usr/include/linux/msdos_fs_i.h /usr/include/linux/iso_fs_i.h \
  /usr/include/linux/minix_fs_sb.h /usr/include/linux/ext_fs_sb.h /usr/include/linux/msdos_fs_sb.h \
  /usr/include/linux/iso_fs_sb.h /usr/include/linux/mm.h /usr/include/linux/signal.h \
  /usr/include/linux/time.h /usr/include/linux/param.h /usr/include/linux/resource.h \
  /usr/include/linux/vm86.h /usr/include/linux/math_emu.h /usr/include/linux/timer.h \
  /usr/include/linux/string.h /usr/include/linux/genhd.h /usr/include/asm/system.h \
  /usr/include/asm/io.h ../blk.h scsi.h hosts.h scsi_debug.h 
scsi_ioctl.o : scsi_ioctl.c /usr/include/asm/io.h /usr/include/asm/segment.h \
  /usr/include/asm/system.h /usr/include/linux/errno.h /usr/include/linux/kernel.h \
  /usr/include/linux/sched.h /usr/include/linux/head.h /usr/include/linux/fs.h \
  /usr/include/linux/limits.h /usr/include/linux/wait.h /usr/include/linux/types.h \
  /usr/include/linux/dirent.h /usr/include/linux/vfs.h /usr/include/linux/pipe_fs_i.h \
  /usr/include/linux/minix_fs_i.h /usr/include/linux/ext_fs_i.h /usr/include/linux/msdos_fs_i.h \
  /usr/include/linux/iso_fs_i.h /usr/include/linux/minix_fs_sb.h /usr/include/linux/ext_fs_sb.h \
  /usr/include/linux/msdos_fs_sb.h /usr/include/linux/iso_fs_sb.h /usr/include/linux/mm.h \
  /usr/include/linux/signal.h /usr/include/linux/time.h /usr/include/linux/param.h \
  /usr/include/linux/resource.h /usr/include/linux/vm86.h /usr/include/linux/math_emu.h \
  /usr/include/linux/string.h ../blk.h scsi.h hosts.h scsi_ioctl.h 
sd.o : sd.c /usr/include/linux/fs.h /usr/include/linux/limits.h /usr/include/linux/wait.h \
  /usr/include/linux/types.h /usr/include/linux/dirent.h /usr/include/linux/vfs.h \
  /usr/include/linux/pipe_fs_i.h /usr/include/linux/minix_fs_i.h /usr/include/linux/ext_fs_i.h \
  /usr/include/linux/msdos_fs_i.h /usr/include/linux/iso_fs_i.h /usr/include/linux/minix_fs_sb.h \
  /usr/include/linux/ext_fs_sb.h /usr/include/linux/msdos_fs_sb.h /usr/include/linux/iso_fs_sb.h \
  /usr/include/linux/kernel.h /usr/include/linux/sched.h /usr/include/linux/head.h \
  /usr/include/linux/mm.h /usr/include/linux/signal.h /usr/include/linux/time.h \
  /usr/include/linux/param.h /usr/include/linux/resource.h /usr/include/linux/vm86.h \
  /usr/include/linux/math_emu.h /usr/include/linux/string.h /usr/include/linux/errno.h \
  /usr/include/asm/system.h ../blk.h scsi.h hosts.h sd.h /usr/include/linux/genhd.h \
  scsi_ioctl.h 
sd_ioctl.o : sd_ioctl.c /usr/include/linux/kernel.h /usr/include/linux/sched.h \
  /usr/include/linux/head.h /usr/include/linux/fs.h /usr/include/linux/limits.h \
  /usr/include/linux/wait.h /usr/include/linux/types.h /usr/include/linux/dirent.h \
  /usr/include/linux/vfs.h /usr/include/linux/pipe_fs_i.h /usr/include/linux/minix_fs_i.h \
  /usr/include/linux/ext_fs_i.h /usr/include/linux/msdos_fs_i.h /usr/include/linux/iso_fs_i.h \
  /usr/include/linux/minix_fs_sb.h /usr/include/linux/ext_fs_sb.h /usr/include/linux/msdos_fs_sb.h \
  /usr/include/linux/iso_fs_sb.h /usr/include/linux/mm.h /usr/include/linux/signal.h \
  /usr/include/linux/time.h /usr/include/linux/param.h /usr/include/linux/resource.h \
  /usr/include/linux/vm86.h /usr/include/linux/math_emu.h /usr/include/linux/hdreg.h \
  /usr/include/linux/errno.h /usr/include/asm/segment.h ../blk.h scsi.h hosts.h \
  sd.h /usr/include/linux/genhd.h 
seagate.o : seagate.c /usr/include/linux/config.h /usr/include/linux/config.dist.h \
  /usr/include/asm/io.h /usr/include/asm/system.h /usr/include/linux/signal.h \
  /usr/include/linux/sched.h /usr/include/linux/head.h /usr/include/linux/fs.h \
  /usr/include/linux/limits.h /usr/include/linux/wait.h /usr/include/linux/types.h \
  /usr/include/linux/dirent.h /usr/include/linux/vfs.h /usr/include/linux/pipe_fs_i.h \
  /usr/include/linux/minix_fs_i.h /usr/include/linux/ext_fs_i.h /usr/include/linux/msdos_fs_i.h \
  /usr/include/linux/iso_fs_i.h /usr/include/linux/minix_fs_sb.h /usr/include/linux/ext_fs_sb.h \
  /usr/include/linux/msdos_fs_sb.h /usr/include/linux/iso_fs_sb.h /usr/include/linux/mm.h \
  /usr/include/linux/kernel.h /usr/include/linux/time.h /usr/include/linux/param.h \
  /usr/include/linux/resource.h /usr/include/linux/vm86.h /usr/include/linux/math_emu.h \
  ../blk.h scsi.h hosts.h seagate.h 
sr.o : sr.c /usr/include/linux/fs.h /usr/include/linux/limits.h /usr/include/linux/wait.h \
  /usr/include/linux/types.h /usr/include/linux/dirent.h /usr/include/linux/vfs.h \
  /usr/include/linux/pipe_fs_i.h /usr/include/linux/minix_fs_i.h /usr/include/linux/ext_fs_i.h \
  /usr/include/linux/msdos_fs_i.h /usr/include/linux/iso_fs_i.h /usr/include/linux/minix_fs_sb.h \
  /usr/include/linux/ext_fs_sb.h /usr/include/linux/msdos_fs_sb.h /usr/include/linux/iso_fs_sb.h \
  /usr/include/linux/kernel.h /usr/include/linux/sched.h /usr/include/linux/head.h \
  /usr/include/linux/mm.h /usr/include/linux/signal.h /usr/include/linux/time.h \
  /usr/include/linux/param.h /usr/include/linux/resource.h /usr/include/linux/vm86.h \
  /usr/include/linux/math_emu.h /usr/include/linux/string.h /usr/include/linux/errno.h \
  /usr/include/asm/system.h ../blk.h scsi.h hosts.h sr.h scsi_ioctl.h 
sr_ioctl.o : sr_ioctl.c /usr/include/linux/kernel.h /usr/include/linux/sched.h \
  /usr/include/linux/head.h /usr/include/linux/fs.h /usr/include/linux/limits.h \
  /usr/include/linux/wait.h /usr/include/linux/types.h /usr/include/linux/dirent.h \
  /usr/include/linux/vfs.h /usr/include/linux/pipe_fs_i.h /usr/include/linux/minix_fs_i.h \
  /usr/include/linux/ext_fs_i.h /usr/include/linux/msdos_fs_i.h /usr/include/linux/iso_fs_i.h \
  /usr/include/linux/minix_fs_sb.h /usr/include/linux/ext_fs_sb.h /usr/include/linux/msdos_fs_sb.h \
  /usr/include/linux/iso_fs_sb.h /usr/include/linux/mm.h /usr/include/linux/signal.h \
  /usr/include/linux/time.h /usr/include/linux/param.h /usr/include/linux/resource.h \
  /usr/include/linux/vm86.h /usr/include/linux/math_emu.h /usr/include/asm/segment.h \
  /usr/include/linux/errno.h ../blk.h scsi.h sr.h scsi_ioctl.h /usr/include/linux/cdrom.h 
st.o : st.c /usr/include/linux/fs.h /usr/include/linux/limits.h /usr/include/linux/wait.h \
  /usr/include/linux/types.h /usr/include/linux/dirent.h /usr/include/linux/vfs.h \
  /usr/include/linux/pipe_fs_i.h /usr/include/linux/minix_fs_i.h /usr/include/linux/ext_fs_i.h \
  /usr/include/linux/msdos_fs_i.h /usr/include/linux/iso_fs_i.h /usr/include/linux/minix_fs_sb.h \
  /usr/include/linux/ext_fs_sb.h /usr/include/linux/msdos_fs_sb.h /usr/include/linux/iso_fs_sb.h \
  /usr/include/linux/kernel.h /usr/include/linux/sched.h /usr/include/linux/head.h \
  /usr/include/linux/mm.h /usr/include/linux/signal.h /usr/include/linux/time.h \
  /usr/include/linux/param.h /usr/include/linux/resource.h /usr/include/linux/vm86.h \
  /usr/include/linux/math_emu.h /usr/include/linux/string.h /usr/include/linux/errno.h \
  /usr/include/linux/mtio.h /usr/include/linux/ioctl.h /usr/include/linux/fcntl.h \
  /usr/include/asm/segment.h /usr/include/asm/system.h ../blk.h scsi.h st.h 
ultrastor.o : ultrastor.c /usr/include/linux/stddef.h /usr/include/linux/string.h \
  /usr/include/linux/types.h /usr/include/linux/sched.h /usr/include/linux/head.h \
  /usr/include/linux/fs.h /usr/include/linux/limits.h /usr/include/linux/wait.h \
  /usr/include/linux/dirent.h /usr/include/linux/vfs.h /usr/include/linux/pipe_fs_i.h \
  /usr/include/linux/minix_fs_i.h /usr/include/linux/ext_fs_i.h /usr/include/linux/msdos_fs_i.h \
  /usr/include/linux/iso_fs_i.h /usr/include/linux/minix_fs_sb.h /usr/include/linux/ext_fs_sb.h \
  /usr/include/linux/msdos_fs_sb.h /usr/include/linux/iso_fs_sb.h /usr/include/linux/mm.h \
  /usr/include/linux/kernel.h /usr/include/linux/signal.h /usr/include/linux/time.h \
  /usr/include/linux/param.h /usr/include/linux/resource.h /usr/include/linux/vm86.h \
  /usr/include/linux/math_emu.h /usr/include/asm/io.h /usr/include/asm/system.h \
  /usr/include/asm/dma.h ../blk.h scsi.h hosts.h ultrastor.h 
wd7000.o : wd7000.c /usr/lib/gcc-lib/i386-linux/2.3.2/include/stdarg.h /usr/include/linux/kernel.h \
  /usr/include/linux/head.h /usr/include/linux/types.h /usr/include/linux/string.h \
  /usr/include/linux/sched.h /usr/include/linux/fs.h /usr/include/linux/limits.h \
  /usr/include/linux/wait.h /usr/include/linux/dirent.h /usr/include/linux/vfs.h \
  /usr/include/linux/pipe_fs_i.h /usr/include/linux/minix_fs_i.h /usr/include/linux/ext_fs_i.h \
  /usr/include/linux/msdos_fs_i.h /usr/include/linux/iso_fs_i.h /usr/include/linux/minix_fs_sb.h \
  /usr/include/linux/ext_fs_sb.h /usr/include/linux/msdos_fs_sb.h /usr/include/linux/iso_fs_sb.h \
  /usr/include/linux/mm.h /usr/include/linux/signal.h /usr/include/linux/time.h \
  /usr/include/linux/param.h /usr/include/linux/resource.h /usr/include/linux/vm86.h \
  /usr/include/linux/math_emu.h /usr/include/asm/system.h /usr/include/asm/dma.h \
  /usr/include/asm/io.h ../blk.h scsi.h hosts.h wd7000.h 
