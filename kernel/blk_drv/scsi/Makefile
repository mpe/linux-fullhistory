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

LOWLEVELCSRC = aha1542.c seagate.c ultrastor.c 
LOWLEVELHSRC = aha1542.c seagate.h ultrastor.h
 
CSRC = hosts.c sd.c st.c scsi.c $(LOWLEVELCSRC)
HSRC = hosts.h sd.h st.h scsi.h $(LOWLEVELHSRC)

OBJS =	scsi.o hosts.o scsi_ioctl.o sd.o sd_ioctl.o st.o st_ioctl.o \
	aha1542.o seagate.o ultrastor.o

all: scsi.a

config.out : config.in $(KERNELHDRS)/linux/config.h
	rm -f  foo.c
	ln -s config.in foo.c	
	$(CPP) foo.c | grep '\.o' > config.out 
	rm foo.c

figure : hosts.h $(KERNELHDRS)/linux/config.h hosts.c config.out
	$(HOSTCC) -I$(KERNELHDRS) -DFIGURE_MAX_SCSI_HOSTS hosts.c -o figure

max_hosts.h : figure
	(echo "#ifndef _MAX_HOSTS_H"; \
	 echo "#define _MAX_HOSTS_H"; \
	 echo "#define MAX_SCSI_HOSTS `./figure`";\
	 echo "#endif") > tmp_max
	cp tmp_max max_hosts.h
		
scsi.a: $(OBJS)
	$(AR) rcs scsi.a $(OBJS)
	sync

scsi.shar: Makefile scsi.doc $(CSRC) $(HSRC) ../ll_rw_blk.c ../blk.h all.diff
	(cd ..; shar scsi/scsi.doc scsi/Makefile scsi/*.{c,h} scsi/all.diff blk.h ll_rw_blk.c) > scsi.shar;

clean:
	rm -f core *.o *.a tmp_make tmp_max figure config.out Makefile.tag max_hosts.h

seagate.s seagate.o : seagate.c ../../../include/linux/config.h \
  ../../../include/linux/config.dist.h ../../../include/linux/sched.h \
  ../../../include/linux/head.h ../../../include/linux/fs.h \
  ../../../include/sys/types.h ../../../include/linux/mm.h \
  ../../../include/linux/kernel.h ../../../include/signal.h \
  ../../../include/sys/param.h ../../../include/sys/time.h \
  ../../../include/time.h ../../../include/sys/resource.h \
  ../../../include/linux/string.h seagate.h scsi.h hosts.h max_hosts.h 
	$(CC) -Wall -c seagate.c $(DEBUG) 

dep:
	sed '/\#\#\# Dependencies/q' < Makefile > tmp_make
	for i in *.c ;do $(CPP) -M $$i;done >> tmp_make
	cp tmp_make Makefile

### Dependencies:
aha1542.s aha1542.o : aha1542.c ../../../include/linux/config.h ../../../include/linux/config_rel.h \
  ../../../include/linux/config_ver.h ../../../include/linux/config.dist.h ../../../include/linux/kernel.h \
  ../../../include/linux/head.h ../../../include/linux/string.h ../../../include/asm/system.h \
  ../../../include/asm/io.h scsi.h hosts.h max_hosts.h aha1542.h 
hosts.s hosts.o : hosts.c ../../../include/linux/config.h ../../../include/linux/config_rel.h \
  ../../../include/linux/config_ver.h ../../../include/linux/config.dist.h ../../../include/linux/kernel.h \
  scsi.h hosts.h max_hosts.h aha1542.h seagate.h ultrastor.h 
scsi.s scsi.o : scsi.c ../../../include/linux/config.h ../../../include/linux/config_rel.h \
  ../../../include/linux/config_ver.h ../../../include/linux/config.dist.h ../../../include/asm/system.h \
  ../../../include/linux/sched.h ../../../include/linux/head.h ../../../include/linux/fs.h \
  ../../../include/sys/types.h ../../../include/sys/dirent.h ../../../include/limits.h \
  ../../../include/linux/mm.h ../../../include/linux/kernel.h ../../../include/signal.h \
  ../../../include/sys/param.h ../../../include/sys/time.h ../../../include/time.h \
  ../../../include/sys/resource.h ../../../include/linux/timer.h ../../../include/linux/string.h \
  scsi.h hosts.h max_hosts.h sd.h st.h 
scsi_ioctl.s scsi_ioctl.o : scsi_ioctl.c ../../../include/linux/config.h ../../../include/linux/config_rel.h \
  ../../../include/linux/config_ver.h ../../../include/linux/config.dist.h ../../../include/errno.h \
  ../../../include/asm/io.h ../../../include/asm/segment.h ../../../include/asm/system.h \
  ../../../include/linux/kernel.h ../../../include/linux/sched.h ../../../include/linux/head.h \
  ../../../include/linux/fs.h ../../../include/sys/types.h ../../../include/sys/dirent.h \
  ../../../include/limits.h ../../../include/linux/mm.h ../../../include/signal.h \
  ../../../include/sys/param.h ../../../include/sys/time.h ../../../include/time.h \
  ../../../include/sys/resource.h ../../../include/linux/string.h scsi.h hosts.h \
  max_hosts.h scsi_ioctl.h 
sd.s sd.o : sd.c ../../../include/linux/config.h ../../../include/linux/config_rel.h \
  ../../../include/linux/config_ver.h ../../../include/linux/config.dist.h ../../../include/linux/string.h \
  ../../../include/linux/fs.h ../../../include/sys/types.h ../../../include/sys/dirent.h \
  ../../../include/limits.h ../../../include/linux/kernel.h ../../../include/linux/sched.h \
  ../../../include/linux/head.h ../../../include/linux/mm.h ../../../include/signal.h \
  ../../../include/sys/param.h ../../../include/sys/time.h ../../../include/time.h \
  ../../../include/sys/resource.h scsi.h sd.h ../blk.h 
sd_ioctl.s sd_ioctl.o : sd_ioctl.c ../../../include/linux/config.h ../../../include/linux/config_rel.h \
  ../../../include/linux/config_ver.h ../../../include/linux/config.dist.h ../../../include/linux/kernel.h \
  ../../../include/linux/sched.h ../../../include/linux/head.h ../../../include/linux/fs.h \
  ../../../include/sys/types.h ../../../include/sys/dirent.h ../../../include/limits.h \
  ../../../include/linux/mm.h ../../../include/signal.h ../../../include/sys/param.h \
  ../../../include/sys/time.h ../../../include/time.h ../../../include/sys/resource.h \
  scsi.h sd.h 
seagate.s seagate.o : seagate.c ../../../include/linux/config.h ../../../include/linux/config_rel.h \
  ../../../include/linux/config_ver.h ../../../include/linux/config.dist.h ../../../include/linux/sched.h \
  ../../../include/linux/head.h ../../../include/linux/fs.h ../../../include/sys/types.h \
  ../../../include/sys/dirent.h ../../../include/limits.h ../../../include/linux/mm.h \
  ../../../include/linux/kernel.h ../../../include/signal.h ../../../include/sys/param.h \
  ../../../include/sys/time.h ../../../include/time.h ../../../include/sys/resource.h \
  seagate.h scsi.h hosts.h max_hosts.h 
st.s st.o : st.c ../../../include/linux/config.h ../../../include/linux/config_rel.h \
  ../../../include/linux/config_ver.h ../../../include/linux/config.dist.h scsi.h \
  st.h ../../../include/linux/fs.h ../../../include/sys/types.h ../../../include/sys/dirent.h \
  ../../../include/limits.h ../../../include/linux/kernel.h ../../../include/linux/sched.h \
  ../../../include/linux/head.h ../../../include/linux/mm.h ../../../include/signal.h \
  ../../../include/sys/param.h ../../../include/sys/time.h ../../../include/time.h \
  ../../../include/sys/resource.h ../blk.h 
st_ioctl.s st_ioctl.o : st_ioctl.c ../../../include/linux/config.h ../../../include/linux/config_rel.h \
  ../../../include/linux/config_ver.h ../../../include/linux/config.dist.h ../../../include/linux/kernel.h \
  ../../../include/linux/sched.h ../../../include/linux/head.h ../../../include/linux/fs.h \
  ../../../include/sys/types.h ../../../include/sys/dirent.h ../../../include/limits.h \
  ../../../include/linux/mm.h ../../../include/signal.h ../../../include/sys/param.h \
  ../../../include/sys/time.h ../../../include/time.h ../../../include/sys/resource.h \
  st.h scsi.h 
ultrastor.s ultrastor.o : ultrastor.c ../../../include/linux/config.h ../../../include/linux/config_rel.h \
  ../../../include/linux/config_ver.h ../../../include/linux/config.dist.h ../../../include/stddef.h \
  ../../../include/linux/string.h ../../../include/linux/sched.h ../../../include/linux/head.h \
  ../../../include/linux/fs.h ../../../include/sys/types.h ../../../include/sys/dirent.h \
  ../../../include/limits.h ../../../include/linux/mm.h ../../../include/linux/kernel.h \
  ../../../include/signal.h ../../../include/sys/param.h ../../../include/sys/time.h \
  ../../../include/time.h ../../../include/sys/resource.h ../../../include/linux/hdreg.h \
  ../../../include/asm/system.h ../../../include/asm/io.h ../../../include/asm/segment.h \
  ultrastor.h scsi.h hosts.h max_hosts.h 
