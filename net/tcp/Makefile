#
# Makefile for the linux networking.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

.c.o:
	$(CC) $(CFLAGS) \
	-c -o $*.o $<
.s.o:
	$(AS) -o $*.o $<
.c.s:
	$(CC) $(CFLAGS) \
	-S -o $*.s $<

OBJS	= sock.o tcp.o ip.o timer.o we.o arp.o udp.o eth.o Space.o loopback.o \
	  icmp.o protocols.o raw.o pack_type.o dev.o packet.o

tcpip.o: $(OBJS)
	$(LD) -r -o tcpip.o $(OBJS)

subdirs: dummy
	for i in $(SUBDIRS); do (cd $$i; $(MAKE)); done


clean:
	rm -f core *.o *.a tmp_make
	for i in *.c;do rm -f `basename $$i .c`.s;done

dep:
	sed '/\#\#\# Dependencies/q' < Makefile > tmp_make
	$(CPP) -M *.c >> tmp_make
	cp tmp_make Makefile

tar:
	tar -cvf /dev/f1 .

### Dependencies:
Space.o : Space.c dev.h /usr/include/linux/stddef.h 
arp.o : arp.c /usr/include/linux/types.h /usr/include/linux/string.h /usr/include/linux/kernel.h \
  /usr/include/linux/sched.h /usr/include/linux/head.h /usr/include/linux/fs.h \
  /usr/include/linux/limits.h /usr/include/linux/wait.h /usr/include/linux/dirent.h \
  /usr/include/linux/vfs.h /usr/include/linux/pipe_fs_i.h /usr/include/linux/minix_fs_i.h \
  /usr/include/linux/ext_fs_i.h /usr/include/linux/msdos_fs_i.h /usr/include/linux/iso_fs_i.h \
  /usr/include/linux/minix_fs_sb.h /usr/include/linux/ext_fs_sb.h /usr/include/linux/msdos_fs_sb.h \
  /usr/include/linux/iso_fs_sb.h /usr/include/linux/mm.h /usr/include/linux/signal.h \
  /usr/include/linux/time.h /usr/include/linux/param.h /usr/include/linux/resource.h \
  /usr/include/linux/vm86.h /usr/include/linux/math_emu.h /usr/include/linux/socket.h \
  /usr/include/netinet/in.h /usr/include/features.h /usr/include/sys/cdefs.h /usr/include/sys/socket.h \
  /usr/include/asm/system.h timer.h ip.h dev.h /usr/include/linux/sock_ioctl.h \
  eth.h tcp.h sock.h arp.h 
dev.o : dev.c /usr/include/asm/segment.h /usr/include/asm/system.h /usr/include/linux/types.h \
  /usr/include/linux/kernel.h /usr/include/linux/sched.h /usr/include/linux/head.h \
  /usr/include/linux/fs.h /usr/include/linux/limits.h /usr/include/linux/wait.h \
  /usr/include/linux/dirent.h /usr/include/linux/vfs.h /usr/include/linux/pipe_fs_i.h \
  /usr/include/linux/minix_fs_i.h /usr/include/linux/ext_fs_i.h /usr/include/linux/msdos_fs_i.h \
  /usr/include/linux/iso_fs_i.h /usr/include/linux/minix_fs_sb.h /usr/include/linux/ext_fs_sb.h \
  /usr/include/linux/msdos_fs_sb.h /usr/include/linux/iso_fs_sb.h /usr/include/linux/mm.h \
  /usr/include/linux/signal.h /usr/include/linux/time.h /usr/include/linux/param.h \
  /usr/include/linux/resource.h /usr/include/linux/vm86.h /usr/include/linux/math_emu.h \
  /usr/include/linux/string.h /usr/include/linux/socket.h /usr/include/netinet/in.h \
  /usr/include/features.h /usr/include/sys/cdefs.h /usr/include/sys/socket.h dev.h \
  eth.h timer.h ip.h /usr/include/linux/sock_ioctl.h tcp.h sock.h /usr/include/linux/errno.h \
  arp.h 
eth.o : eth.c /usr/include/asm/segment.h /usr/include/asm/system.h /usr/include/linux/types.h \
  /usr/include/linux/kernel.h /usr/include/linux/sched.h /usr/include/linux/head.h \
  /usr/include/linux/fs.h /usr/include/linux/limits.h /usr/include/linux/wait.h \
  /usr/include/linux/dirent.h /usr/include/linux/vfs.h /usr/include/linux/pipe_fs_i.h \
  /usr/include/linux/minix_fs_i.h /usr/include/linux/ext_fs_i.h /usr/include/linux/msdos_fs_i.h \
  /usr/include/linux/iso_fs_i.h /usr/include/linux/minix_fs_sb.h /usr/include/linux/ext_fs_sb.h \
  /usr/include/linux/msdos_fs_sb.h /usr/include/linux/iso_fs_sb.h /usr/include/linux/mm.h \
  /usr/include/linux/signal.h /usr/include/linux/time.h /usr/include/linux/param.h \
  /usr/include/linux/resource.h /usr/include/linux/vm86.h /usr/include/linux/math_emu.h \
  /usr/include/linux/string.h /usr/include/linux/socket.h /usr/include/netinet/in.h \
  /usr/include/features.h /usr/include/sys/cdefs.h /usr/include/sys/socket.h dev.h \
  eth.h timer.h ip.h /usr/include/linux/sock_ioctl.h tcp.h sock.h /usr/include/linux/errno.h \
  arp.h 
icmp.o : icmp.c /usr/include/linux/types.h /usr/include/linux/sched.h /usr/include/linux/head.h \
  /usr/include/linux/fs.h /usr/include/linux/limits.h /usr/include/linux/wait.h \
  /usr/include/linux/dirent.h /usr/include/linux/vfs.h /usr/include/linux/pipe_fs_i.h \
  /usr/include/linux/minix_fs_i.h /usr/include/linux/ext_fs_i.h /usr/include/linux/msdos_fs_i.h \
  /usr/include/linux/iso_fs_i.h /usr/include/linux/minix_fs_sb.h /usr/include/linux/ext_fs_sb.h \
  /usr/include/linux/msdos_fs_sb.h /usr/include/linux/iso_fs_sb.h /usr/include/linux/mm.h \
  /usr/include/linux/kernel.h /usr/include/linux/signal.h /usr/include/linux/time.h \
  /usr/include/linux/param.h /usr/include/linux/resource.h /usr/include/linux/vm86.h \
  /usr/include/linux/math_emu.h /usr/include/linux/fcntl.h /usr/include/linux/socket.h \
  /usr/include/netinet/in.h /usr/include/features.h /usr/include/sys/cdefs.h /usr/include/sys/socket.h \
  timer.h ip.h dev.h /usr/include/linux/sock_ioctl.h eth.h tcp.h sock.h /usr/include/linux/errno.h \
  /usr/include/linux/timer.h /usr/include/asm/system.h /usr/include/asm/segment.h \
  icmp.h 
ip.o : ip.c /usr/include/asm/segment.h /usr/include/asm/system.h /usr/include/linux/types.h \
  /usr/include/linux/kernel.h /usr/include/linux/sched.h /usr/include/linux/head.h \
  /usr/include/linux/fs.h /usr/include/linux/limits.h /usr/include/linux/wait.h \
  /usr/include/linux/dirent.h /usr/include/linux/vfs.h /usr/include/linux/pipe_fs_i.h \
  /usr/include/linux/minix_fs_i.h /usr/include/linux/ext_fs_i.h /usr/include/linux/msdos_fs_i.h \
  /usr/include/linux/iso_fs_i.h /usr/include/linux/minix_fs_sb.h /usr/include/linux/ext_fs_sb.h \
  /usr/include/linux/msdos_fs_sb.h /usr/include/linux/iso_fs_sb.h /usr/include/linux/mm.h \
  /usr/include/linux/signal.h /usr/include/linux/time.h /usr/include/linux/param.h \
  /usr/include/linux/resource.h /usr/include/linux/vm86.h /usr/include/linux/math_emu.h \
  /usr/include/linux/string.h /usr/include/linux/socket.h /usr/include/netinet/in.h \
  /usr/include/features.h /usr/include/sys/cdefs.h /usr/include/sys/socket.h timer.h \
  ip.h dev.h /usr/include/linux/sock_ioctl.h eth.h tcp.h sock.h /usr/include/linux/errno.h \
  arp.h icmp.h 
loopback.o : loopback.c /usr/include/linux/config.h /usr/include/linux/config.dist.h \
  /usr/include/linux/kernel.h /usr/include/linux/sched.h /usr/include/linux/head.h \
  /usr/include/linux/fs.h /usr/include/linux/limits.h /usr/include/linux/wait.h \
  /usr/include/linux/types.h /usr/include/linux/dirent.h /usr/include/linux/vfs.h \
  /usr/include/linux/pipe_fs_i.h /usr/include/linux/minix_fs_i.h /usr/include/linux/ext_fs_i.h \
  /usr/include/linux/msdos_fs_i.h /usr/include/linux/iso_fs_i.h /usr/include/linux/minix_fs_sb.h \
  /usr/include/linux/ext_fs_sb.h /usr/include/linux/msdos_fs_sb.h /usr/include/linux/iso_fs_sb.h \
  /usr/include/linux/mm.h /usr/include/linux/signal.h /usr/include/linux/time.h \
  /usr/include/linux/param.h /usr/include/linux/resource.h /usr/include/linux/vm86.h \
  /usr/include/linux/math_emu.h /usr/include/linux/tty.h /usr/include/linux/termios.h \
  /usr/include/asm/system.h /usr/include/linux/ptrace.h /usr/include/linux/string.h \
  /usr/include/asm/segment.h /usr/include/asm/io.h /usr/include/errno.h /usr/include/features.h \
  /usr/include/sys/cdefs.h /usr/include/linux/errno.h /usr/include/linux/fcntl.h \
  /usr/include/netinet/in.h /usr/include/sys/socket.h /usr/include/linux/socket.h \
  dev.h eth.h timer.h ip.h /usr/include/linux/sock_ioctl.h tcp.h sock.h arp.h 
pack_type.o : pack_type.c /usr/include/linux/stddef.h dev.h eth.h 
packet.o : packet.c /usr/include/linux/types.h /usr/include/linux/sched.h /usr/include/linux/head.h \
  /usr/include/linux/fs.h /usr/include/linux/limits.h /usr/include/linux/wait.h \
  /usr/include/linux/dirent.h /usr/include/linux/vfs.h /usr/include/linux/pipe_fs_i.h \
  /usr/include/linux/minix_fs_i.h /usr/include/linux/ext_fs_i.h /usr/include/linux/msdos_fs_i.h \
  /usr/include/linux/iso_fs_i.h /usr/include/linux/minix_fs_sb.h /usr/include/linux/ext_fs_sb.h \
  /usr/include/linux/msdos_fs_sb.h /usr/include/linux/iso_fs_sb.h /usr/include/linux/mm.h \
  /usr/include/linux/kernel.h /usr/include/linux/signal.h /usr/include/linux/time.h \
  /usr/include/linux/param.h /usr/include/linux/resource.h /usr/include/linux/vm86.h \
  /usr/include/linux/math_emu.h /usr/include/linux/fcntl.h /usr/include/linux/socket.h \
  /usr/include/netinet/in.h /usr/include/features.h /usr/include/sys/cdefs.h /usr/include/sys/socket.h \
  timer.h ip.h dev.h /usr/include/linux/sock_ioctl.h eth.h tcp.h sock.h /usr/include/linux/errno.h \
  /usr/include/linux/timer.h /usr/include/asm/system.h /usr/include/asm/segment.h \
  ../kern_sock.h 
protocols.o : protocols.c /usr/include/asm/segment.h /usr/include/asm/system.h \
  /usr/include/linux/types.h /usr/include/linux/kernel.h /usr/include/linux/sched.h \
  /usr/include/linux/head.h /usr/include/linux/fs.h /usr/include/linux/limits.h \
  /usr/include/linux/wait.h /usr/include/linux/dirent.h /usr/include/linux/vfs.h \
  /usr/include/linux/pipe_fs_i.h /usr/include/linux/minix_fs_i.h /usr/include/linux/ext_fs_i.h \
  /usr/include/linux/msdos_fs_i.h /usr/include/linux/iso_fs_i.h /usr/include/linux/minix_fs_sb.h \
  /usr/include/linux/ext_fs_sb.h /usr/include/linux/msdos_fs_sb.h /usr/include/linux/iso_fs_sb.h \
  /usr/include/linux/mm.h /usr/include/linux/signal.h /usr/include/linux/time.h \
  /usr/include/linux/param.h /usr/include/linux/resource.h /usr/include/linux/vm86.h \
  /usr/include/linux/math_emu.h /usr/include/linux/string.h /usr/include/linux/socket.h \
  /usr/include/netinet/in.h /usr/include/features.h /usr/include/sys/cdefs.h /usr/include/sys/socket.h \
  timer.h ip.h dev.h /usr/include/linux/sock_ioctl.h eth.h tcp.h sock.h icmp.h 
raw.o : raw.c /usr/include/linux/types.h /usr/include/linux/sched.h /usr/include/linux/head.h \
  /usr/include/linux/fs.h /usr/include/linux/limits.h /usr/include/linux/wait.h \
  /usr/include/linux/dirent.h /usr/include/linux/vfs.h /usr/include/linux/pipe_fs_i.h \
  /usr/include/linux/minix_fs_i.h /usr/include/linux/ext_fs_i.h /usr/include/linux/msdos_fs_i.h \
  /usr/include/linux/iso_fs_i.h /usr/include/linux/minix_fs_sb.h /usr/include/linux/ext_fs_sb.h \
  /usr/include/linux/msdos_fs_sb.h /usr/include/linux/iso_fs_sb.h /usr/include/linux/mm.h \
  /usr/include/linux/kernel.h /usr/include/linux/signal.h /usr/include/linux/time.h \
  /usr/include/linux/param.h /usr/include/linux/resource.h /usr/include/linux/vm86.h \
  /usr/include/linux/math_emu.h /usr/include/linux/fcntl.h /usr/include/linux/socket.h \
  /usr/include/netinet/in.h /usr/include/features.h /usr/include/sys/cdefs.h /usr/include/sys/socket.h \
  timer.h ip.h dev.h /usr/include/linux/sock_ioctl.h eth.h tcp.h sock.h /usr/include/linux/errno.h \
  /usr/include/linux/timer.h /usr/include/asm/system.h /usr/include/asm/segment.h \
  ../kern_sock.h 
sock.o : sock.c /usr/include/linux/errno.h /usr/include/linux/types.h /usr/include/linux/socket.h \
  /usr/include/netinet/in.h /usr/include/features.h /usr/include/sys/cdefs.h /usr/include/sys/socket.h \
  /usr/include/linux/kernel.h /usr/include/linux/sched.h /usr/include/linux/head.h \
  /usr/include/linux/fs.h /usr/include/linux/limits.h /usr/include/linux/wait.h \
  /usr/include/linux/dirent.h /usr/include/linux/vfs.h /usr/include/linux/pipe_fs_i.h \
  /usr/include/linux/minix_fs_i.h /usr/include/linux/ext_fs_i.h /usr/include/linux/msdos_fs_i.h \
  /usr/include/linux/iso_fs_i.h /usr/include/linux/minix_fs_sb.h /usr/include/linux/ext_fs_sb.h \
  /usr/include/linux/msdos_fs_sb.h /usr/include/linux/iso_fs_sb.h /usr/include/linux/mm.h \
  /usr/include/linux/signal.h /usr/include/linux/time.h /usr/include/linux/param.h \
  /usr/include/linux/resource.h /usr/include/linux/vm86.h /usr/include/linux/math_emu.h \
  /usr/include/linux/timer.h /usr/include/linux/string.h /usr/include/linux/sock_ioctl.h \
  ../kern_sock.h timer.h ip.h dev.h eth.h tcp.h udp.h sock.h /usr/include/asm/segment.h \
  /usr/include/asm/system.h /usr/include/linux/fcntl.h 
tcp.o : tcp.c /usr/include/linux/types.h /usr/include/linux/sched.h /usr/include/linux/head.h \
  /usr/include/linux/fs.h /usr/include/linux/limits.h /usr/include/linux/wait.h \
  /usr/include/linux/dirent.h /usr/include/linux/vfs.h /usr/include/linux/pipe_fs_i.h \
  /usr/include/linux/minix_fs_i.h /usr/include/linux/ext_fs_i.h /usr/include/linux/msdos_fs_i.h \
  /usr/include/linux/iso_fs_i.h /usr/include/linux/minix_fs_sb.h /usr/include/linux/ext_fs_sb.h \
  /usr/include/linux/msdos_fs_sb.h /usr/include/linux/iso_fs_sb.h /usr/include/linux/mm.h \
  /usr/include/linux/kernel.h /usr/include/linux/signal.h /usr/include/linux/time.h \
  /usr/include/linux/param.h /usr/include/linux/resource.h /usr/include/linux/vm86.h \
  /usr/include/linux/math_emu.h /usr/include/linux/string.h /usr/include/linux/socket.h \
  /usr/include/netinet/in.h /usr/include/features.h /usr/include/sys/cdefs.h /usr/include/sys/socket.h \
  /usr/include/linux/fcntl.h timer.h ip.h dev.h /usr/include/linux/sock_ioctl.h \
  eth.h icmp.h tcp.h sock.h /usr/include/linux/errno.h /usr/include/linux/timer.h \
  /usr/include/asm/system.h /usr/include/asm/segment.h /usr/include/linux/termios.h \
  ../kern_sock.h 
timer.o : timer.c /usr/include/linux/types.h /usr/include/linux/errno.h /usr/include/linux/socket.h \
  /usr/include/netinet/in.h /usr/include/features.h /usr/include/sys/cdefs.h /usr/include/sys/socket.h \
  /usr/include/linux/kernel.h /usr/include/linux/sched.h /usr/include/linux/head.h \
  /usr/include/linux/fs.h /usr/include/linux/limits.h /usr/include/linux/wait.h \
  /usr/include/linux/dirent.h /usr/include/linux/vfs.h /usr/include/linux/pipe_fs_i.h \
  /usr/include/linux/minix_fs_i.h /usr/include/linux/ext_fs_i.h /usr/include/linux/msdos_fs_i.h \
  /usr/include/linux/iso_fs_i.h /usr/include/linux/minix_fs_sb.h /usr/include/linux/ext_fs_sb.h \
  /usr/include/linux/msdos_fs_sb.h /usr/include/linux/iso_fs_sb.h /usr/include/linux/mm.h \
  /usr/include/linux/signal.h /usr/include/linux/time.h /usr/include/linux/param.h \
  /usr/include/linux/resource.h /usr/include/linux/vm86.h /usr/include/linux/math_emu.h \
  /usr/include/linux/timer.h /usr/include/asm/system.h timer.h ip.h dev.h /usr/include/linux/sock_ioctl.h \
  eth.h tcp.h sock.h arp.h ../kern_sock.h 
udp.o : udp.c /usr/include/linux/types.h /usr/include/linux/sched.h /usr/include/linux/head.h \
  /usr/include/linux/fs.h /usr/include/linux/limits.h /usr/include/linux/wait.h \
  /usr/include/linux/dirent.h /usr/include/linux/vfs.h /usr/include/linux/pipe_fs_i.h \
  /usr/include/linux/minix_fs_i.h /usr/include/linux/ext_fs_i.h /usr/include/linux/msdos_fs_i.h \
  /usr/include/linux/iso_fs_i.h /usr/include/linux/minix_fs_sb.h /usr/include/linux/ext_fs_sb.h \
  /usr/include/linux/msdos_fs_sb.h /usr/include/linux/iso_fs_sb.h /usr/include/linux/mm.h \
  /usr/include/linux/kernel.h /usr/include/linux/signal.h /usr/include/linux/time.h \
  /usr/include/linux/param.h /usr/include/linux/resource.h /usr/include/linux/vm86.h \
  /usr/include/linux/math_emu.h /usr/include/linux/fcntl.h /usr/include/linux/socket.h \
  /usr/include/netinet/in.h /usr/include/features.h /usr/include/sys/cdefs.h /usr/include/sys/socket.h \
  timer.h ip.h dev.h /usr/include/linux/sock_ioctl.h eth.h tcp.h sock.h /usr/include/linux/errno.h \
  /usr/include/linux/timer.h /usr/include/linux/termios.h /usr/include/asm/system.h \
  /usr/include/asm/segment.h ../kern_sock.h udp.h icmp.h 
we.o : we.c /usr/include/linux/config.h /usr/include/linux/config.dist.h /usr/include/linux/kernel.h \
  /usr/include/linux/sched.h /usr/include/linux/head.h /usr/include/linux/fs.h \
  /usr/include/linux/limits.h /usr/include/linux/wait.h /usr/include/linux/types.h \
  /usr/include/linux/dirent.h /usr/include/linux/vfs.h /usr/include/linux/pipe_fs_i.h \
  /usr/include/linux/minix_fs_i.h /usr/include/linux/ext_fs_i.h /usr/include/linux/msdos_fs_i.h \
  /usr/include/linux/iso_fs_i.h /usr/include/linux/minix_fs_sb.h /usr/include/linux/ext_fs_sb.h \
  /usr/include/linux/msdos_fs_sb.h /usr/include/linux/iso_fs_sb.h /usr/include/linux/mm.h \
  /usr/include/linux/signal.h /usr/include/linux/time.h /usr/include/linux/param.h \
  /usr/include/linux/resource.h /usr/include/linux/vm86.h /usr/include/linux/math_emu.h \
  /usr/include/linux/tty.h /usr/include/linux/termios.h /usr/include/asm/system.h \
  /usr/include/linux/ptrace.h /usr/include/linux/string.h /usr/include/asm/segment.h \
  /usr/include/asm/io.h /usr/include/errno.h /usr/include/features.h /usr/include/sys/cdefs.h \
  /usr/include/linux/errno.h /usr/include/linux/fcntl.h /usr/include/netinet/in.h \
  /usr/include/sys/socket.h /usr/include/linux/socket.h dev.h eth.h timer.h ip.h \
  /usr/include/linux/sock_ioctl.h tcp.h sock.h arp.h wereg.h 
