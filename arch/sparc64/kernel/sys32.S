/* $Id: sys32.S,v 1.3 1997/08/22 20:11:47 davem Exp $
 * sys32.S: I-cache tricks for 32-bit compatability layer simple
 *          conversions.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

	.text

	.align		32
	.globl		sys32_mmap, sys32_mprotect, sys32_munmap, sys32_msync
	.globl		sys32_mlock, sys32_munlock, sys32_mremap, sparc32_brk
sys32_mmap:
	srl		%o0, 0, %o0			! IEU0	Group
	sethi		%hi(0xffffffff), %g2		! IEU1
	srl		%o1, 0, %o1			! IEU0	Group
	or		%g2, %lo(0xffffffff), %g2	! IEU1
	srl		%o2, 0, %o2			! IEU0	Group
	mov		%o7, %g1			! IEU1
	and		%o3, %g2, %o3			! IEU0	Group
	and		%o4, %g2, %o4			! IEU1
	and		%o5, %g2, %o5			! IEU0	Group
	call		sys_mmap			! CTI	Group brk forced
	 mov		%g1, %o7			! IEU0	Group (regdep)
sys32_mprotect:
	srl		%o0, 0, %o0
	mov		%o7, %g1
	srl		%o1, 0, %o1
	srl		%o2, 0, %o2
	call		sys_mprotect
	 mov		%g1, %o7
sys32_munmap:
	srl		%o0, 0, %o0
	mov		%o7, %g1
	srl		%o1, 0, %o1
	call		sys_munmap
	 mov		%g1, %o7
sparc32_brk:
	srl		%o0, 0, %o0
	mov		%o7, %g1
	call		sys_brk
	 mov		%g1, %o7
sys32_msync:
	srl		%o0, 0, %o0
	mov		%o7, %g1
	srl		%o1, 0, %o1
	call		sys_msync
	 mov		%g1, %o7
sys32_mlock:
	srl		%o0, 0, %o0
	mov		%o7, %g1
	srl		%o1, 0, %o1
	call		sys_mlock
	 mov		%g1, %o7
sys32_munlock:
	srl		%o0, 0, %o0
	mov		%o7, %g1
	srl		%o1, 0, %o1
	call		sys_munlock
	 mov		%g1, %o7
sys32_mremap:
	srl		%o0, 0, %o0
	mov		%o7, %g1
	srl		%o1, 0, %o1
	srl		%o2, 0, %o2
	srl		%o3, 0, %o3
	call		sys_mremap
	 mov		%g1, %o7

	.align		32
	.globl		sys32_read, sys32_write, sys32_open, sys32_access
	.globl		sys32_chdir, sys32_lseek, sys32_llseek, sys32_poll
	.globl		sys32_readlink, sys32_unlink, sys32_rmdir, sys32_symlink
	.globl		sys32_link, sys32_rename, sys32_truncate, sys32_ftruncate
	.globl		sys32_chroot, sys32_chmod, sys32_chown, sys32_creat
	.globl		sys32_mkdir, sys32_mknod, sys32_ustat
sys32_read:
	srl		%o1, 0, %o1
	mov		%o7, %g1
	srl		%o2, 0, %o2
	call		sys_read
	 mov		%g1, %o7
sys32_write:
	srl		%o1, 0, %o1
	mov		%o7, %g1
	srl		%o2, 0, %o2
	call		sys_write
	 mov		%g1, %o7
sys32_open:
	srl		%o0, 0, %o0
	mov		%o7, %g1
	call		sys_open
	 mov		%g1, %o7
sys32_access:
	srl		%o0, 0, %o0
	mov		%o7, %g1
	call		sys_access
	 mov		%g1, %o7
sys32_chdir:
	srl		%o0, 0, %o0
	mov		%o7, %g1
	call		sys_chdir
	 mov		%g1, %o7
sys32_lseek:
	sra		%o1, 0, %o1
	mov		%o7, %g1
	call		sys_lseek
	 mov		%g1, %o7
sys32_llseek:
	srl		%o1, 0, %o1
	mov		%o7, %g1
	srl		%o2, 0, %o2
	srl		%o3, 0, %o3
	call		sys_llseek
	 mov		%g1, %o7
sys32_poll:
	srl		%o0, 0, %o0
	mov		%o7, %g1
	call		sys_poll
	 mov		%g1, %o7
sys32_readlink:
	srl		%o0, 0, %o0
	mov		%o7, %g1
	srl		%o1, 0, %o1
	call		sys_readlink
	 mov		%g1, %o7
sys32_unlink:
	srl		%o0, 0, %o0
	mov		%o7, %g1
	call		sys_unlink
	 mov		%g1, %o7
sys32_rmdir:
	srl		%o0, 0, %o0
	mov		%o7, %g1
	call		sys_rmdir
	 mov		%g1, %o7
sys32_symlink:
	srl		%o0, 0, %o0
	mov		%o7, %g1
	srl		%o1, 0, %o1
	call		sys_symlink
	 mov		%g1, %o7
sys32_link:
	srl		%o0, 0, %o0
	mov		%o7, %g1
	srl		%o1, 0, %o1
	call		sys_link
	 mov		%g1, %o7
sys32_rename:
	srl		%o0, 0, %o0
	mov		%o7, %g1
	srl		%o1, 0, %o1
	call		sys_rename
	 mov		%g1, %o7
	nop
sys32_truncate:
	srl		%o0, 0, %o0
	mov		%o7, %g1
	srl		%o1, 0, %o1
	call		sys_truncate
	 mov		%g1, %o7
sys32_ftruncate:
	srl		%o1, 0, %o1
	mov		%o7, %g1
	call		sys_ftruncate
	 mov		%g1, %o7
sys32_chroot:
	srl		%o0, 0, %o0
	mov		%o7, %g1
	call		sys_chroot
	 mov		%g1, %o7
sys32_chmod:
	sll		%o1, 16, %o1
	mov		%o7, %g1
	srl		%o0, 0, %o0
	srl		%o1, 16, %o1
	call		sys_chmod
	 mov		%g1, %o7
sys32_chown:
	sll		%o1, 16, %o1
	mov		%o7, %g1
	sll		%o2, 16, %o2
	srl		%o0, 0, %o0
	srl		%o1, 16, %o1
	srl		%o2, 16, %o2
	call		sys_chown
	 mov		%g1, %o7
sys32_creat:
	srl		%o0, 0, %o0
	mov		%o7, %g1
	call		sys_creat
	 mov		%g1, %o7
sys32_mkdir:
	srl		%o0, 0, %o0
	mov		%o7, %g1
	call		sys_mkdir
	 mov		%g1, %o7
sys32_mknod:
	sll		%o2, 16, %o2
	mov		%o7, %g1
	srl		%o0, 0, %o0
	srl		%o2, 16, %o2
	call		sys_mknod
	 mov		%g1, %o7
sys32_ustat:
	srl		%o1, 0, %o1
	mov		%o7, %g1
	call		sys_ustat
	  mov		%g1, %o7

	.align		32
	.globl		sys32_bind, sys32_accept, sys32_connect, sys32_getsockname
	.globl		sys32_getpeername, sys32_send, sys32_sendto, sys32_recv
	.globl		sys32_recvfrom, sys32_setsockopt, sys32_getsockopt
sys32_bind:
	srl		%o1, 0, %o1
	mov		%o7, %g1
	call		sys_bind
	 mov		%g1, %o7
sys32_accept:
	srl		%o1, 0, %o1
	mov		%o7, %g1
	srl		%o2, 0, %o2
	call		sys_accept
	 mov		%g1, %o7
sys32_connect:
	srl		%o1, 0, %o1
	mov		%o7, %g1
	call		sys_connect
	 mov		%g1, %o7
sys32_getsockname:
	srl		%o1, 0, %o1
	mov		%o7, %g1
	srl		%o2, 0, %o2
	call		sys_getsockname
	 mov		%g1, %o7
sys32_getpeername:
	srl		%o1, 0, %o1
	mov		%o7, %g1
	srl		%o2, 0, %o2
	call		sys_getpeername
	 mov		%g1, %o7
sys32_send:
	srl		%o1, 0, %o1
	mov		%o7, %g1
	srl		%o2, 0, %o2
	call		sys_send
	 mov		%g1, %o7
sys32_sendto:
	srl		%o1, 0, %o1
	mov		%o7, %g1
	srl		%o2, 0, %o2
	srl		%o4, 0, %o4
	call		sys_sendto
	 mov		%g1, %o7
sys32_recv:
	srl		%o1, 0, %o1
	mov		%o7, %g1
	srl		%o2, 0, %o2
	call		sys_recv
	 mov		%g1, %o7
sys32_recvfrom:
	srl		%o1, 0, %o1
	mov		%o7, %g1
	srl		%o2, 0, %o2
	srl		%o4, 0, %o4
	srl		%o5, 0, %o5
	call		sys_recvfrom
	 mov		%g1, %o7
sys32_setsockopt:
	srl		%o3, 0, %o3
	mov		%o7, %g1
	call		sys_setsockopt
	 mov		%g1, %o7
sys32_getsockopt:
	srl		%o3, 0, %o3
	mov		%o7, %g1
	srl		%o4, 0, %o4
	call		sys_setsockopt
	 mov		%g1, %o7

	.globl		sys32_bdflush, sys32_uselib, sys32_umount, sys32_syslog
	.globl		sys32_personality, sys32_waitpid
	.globl		sys32_sched_setscheduler
	.globl		sys32_sched_setparam, sys32_sched_getparam, sys32_signal
	.globl		sys32_reboot, sys32_acct, sys32_newuname, sys32_olduname
	.globl		sys32_sethostname, sys32_gethostname, sys32_setdomainname
	.globl		sys32_time, sys32_swapoff, sys32_swapon
	.globl		sys32_create_module, sys32_init_module, sys32_delete_module
sys32_bdflush:
	sra		%o1, 0, %o1
	mov		%o7, %g1
	call		sys_bdflush
	 mov		%g1, %o7
sys32_uselib:
	srl		%o0, 0, %o0
	mov		%o7, %g1
	call		sys_uselib
	 mov		%g1, %o7
sys32_umount:
	srl		%o0, 0, %o0
	mov		%o7, %g1
	call		sys_umount
	 mov		%g1, %o7
sys32_syslog:
	srl		%o1, 0, %o1
	mov		%o7, %g1
	call		sys_syslog
	 mov		%g1, %o7
sys32_personality:
	srl		%o0, 0, %o0
	mov		%o7, %g1
	call		sys_personality
	 mov		%g1, %o7
sys32_waitpid:
	srl		%o1, 0, %o1
	mov		%o7, %g1
	call		sys_waitpid
	 mov		%g1, %o7
sys32_sched_setscheduler:
	srl		%o2, 0, %o2
	mov		%o7, %g1
	call		sys_sched_setscheduler
	 mov		%g1, %o7
sys32_sched_setparam:
	srl		%o1, 0, %o1
	mov		%o7, %g1
	call		sys_sched_setparam
	 mov		%g1, %o7
sys32_sched_getparam:
	srl		%o1, 0, %o1
	mov		%o7, %g1
	call		sys_sched_getparam
	 mov		%g1, %o7
sys32_signal:
	srl		%o1, 0, %o1
	mov		%o7, %g1
	call		sys_signal
	 mov		%g1, %o7
sys32_reboot:
	srl		%o3, 0, %o3
	mov		%o7, %g1
	call		sys_reboot
	 mov		%g1, %o7
sys32_acct:
	srl		%o0, 0, %o0
	mov		%o7, %g1
	call		sys_acct
	 mov		%g1, %o7
sys32_newuname:
	srl		%o0, 0, %o0
	mov		%o7, %g1
	call		sys_newuname
	 mov		%g1, %o7
sys32_olduname:
	srl		%o0, 0, %o0
	mov		%o7, %g1
	call		sys_olduname
	 mov		%g1, %o7
sys32_sethostname:
	srl		%o0, 0, %o0
	mov		%o7, %g1
	call		sys_sethostname
	 mov		%g1, %o7
sys32_gethostname:
	srl		%o0, 0, %o0
	mov		%o7, %g1
	call		sys_gethostname
	 mov		%g1, %o7
sys32_setdomainname:
	srl		%o0, 0, %o0
	mov		%o7, %g1
	call		sys_setdomainname
	 mov		%g1, %o7
sys32_time:
	srl		%o0, 0, %o0
	mov		%o7, %g1
	call		sys_time
	 mov		%g1, %o7
sys32_swapoff:
	srl		%o0, 0, %o0
	mov		%o7, %g1
	call		sys_swapoff
	 mov		%g1, %o7
sys32_swapon:
	srl		%o0, 0, %o0
	mov		%o7, %g1
	call		sys_swapon
	 mov		%g1, %o7
