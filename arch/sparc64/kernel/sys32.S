/* $Id: sys32.S,v 1.8 1998/10/28 08:10:37 jj Exp $
 * sys32.S: I-cache tricks for 32-bit compatability layer simple
 *          conversions.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1998 Jakub Jelinek   (jj@ultra.linux.cz)
 */

/* NOTE: call as jump breaks return stack, we have to avoid that */

	.text

	.align		32
	.globl		sys32_mmap
sys32_mmap:
	srl		%o4, 0, %o4
	sethi		%hi(sys_mmap), %g1
	jmpl		%g1 + %lo(sys_mmap), %g0
	 srl		%o5, 0, %o5

	.align		32
	.globl		sys32_lseek
	.globl		sys32_chmod, sys32_chown, sys32_lchown, sys32_mknod
sys32_lseek:
	sra		%o1, 0, %o1
	sethi		%hi(sys_lseek), %g1
	jmpl		%g1 + %lo(sys_lseek), %g0
	 nop
sys32_chmod:
	sethi		%hi(0xffff), %g2
	sethi		%hi(sys_chmod), %g1
	orcc		%g2, %lo(0xffff), %g2
	jmpl		%g1 + %lo(sys_chmod), %g0
	 and		%o1, %g2, %o1
sys32_chown:
	sethi		%hi(0xffff), %g2
	sethi		%hi(sys_chown), %g1
	orcc		%g2, %lo(0xffff), %g2
	and		%o1, %g2, %o1
	jmpl		%g1 + %lo(sys_chown), %g0
	 and		%o2, %g2, %o2
sys32_lchown:
	sethi		%hi(0xffff), %g2
	sethi		%hi(sys_lchown), %g1
	orcc		%g2, %lo(0xffff), %g2
	and		%o1, %g2, %o1
	jmpl		%g1 + %lo(sys_lchown), %g0
	 and		%o2, %g2, %o2
sys32_mknod:
	sethi		%hi(0xffff), %g2
	sethi		%hi(sys_mknod), %g1
	orcc		%g2, %lo(0xffff), %g2
	jmpl		%g1 + %lo(sys_mknod), %g0
	 and		%o2, %g2, %o2

	.align		32
	.globl		sys32_sendto, sys32_recvfrom, sys32_getsockopt
sys32_sendto:
	sethi		%hi(sys_sendto), %g1
	jmpl		%g1 + %lo(sys_sendto), %g0
	 srl		%o4, 0, %o4
sys32_recvfrom:
	srl		%o4, 0, %o4
	sethi		%hi(sys_recvfrom), %g1
	jmpl		%g1 + %lo(sys_recvfrom), %g0
	 srl		%o5, 0, %o5
sys32_getsockopt:
	sethi		%hi(sys_getsockopt), %g1
	jmpl		%g1 + %lo(sys_getsockopt), %g0
	 srl		%o4, 0, %o4

	.globl		sys32_bdflush
sys32_bdflush:
	sethi		%hi(sys_bdflush), %g1
	jmpl		%g1 + %lo(sys_bdflush), %g0
	 sra		%o1, 0, %o1
