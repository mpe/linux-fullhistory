/* $Id: sys32.S,v 1.4 1997/09/09 17:13:29 jj Exp $
 * sys32.S: I-cache tricks for 32-bit compatability layer simple
 *          conversions.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

	.text

	.align		32
	.globl		sys32_mmap
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

	.align		32
	.globl		sys32_lseek
	.globl		sys32_chmod, sys32_chown, sys32_mknod
sys32_lseek:
	sra		%o1, 0, %o1
	mov		%o7, %g1
	call		sys_lseek
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
sys32_mknod:
	sll		%o2, 16, %o2
	mov		%o7, %g1
	srl		%o0, 0, %o0
	srl		%o2, 16, %o2
	call		sys_mknod
	 mov		%g1, %o7

	.align		32
	.globl		sys32_sendto, sys32_recvfrom, sys32_getsockopt
sys32_sendto:
	srl		%o1, 0, %o1
	mov		%o7, %g1
	srl		%o2, 0, %o2
	srl		%o4, 0, %o4
	call		sys_sendto
	 mov		%g1, %o7
sys32_recvfrom:
	srl		%o1, 0, %o1
	mov		%o7, %g1
	srl		%o2, 0, %o2
	srl		%o4, 0, %o4
	srl		%o5, 0, %o5
	call		sys_recvfrom
	 mov		%g1, %o7
sys32_getsockopt:
	srl		%o3, 0, %o3
	mov		%o7, %g1
	srl		%o4, 0, %o4
	call		sys_setsockopt
	 mov		%g1, %o7

	.globl		sys32_bdflush
sys32_bdflush:
	sra		%o1, 0, %o1
	mov		%o7, %g1
	call		sys_bdflush
	 mov		%g1, %o7
