/*
 * include/asm-mips/init.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * $Id: init.h,v 1.3 1998/05/01 01:35:53 ralf Exp $
 */
#ifndef __MIPS_INIT_H
#define __MIPS_INIT_H

#define __init __attribute__ ((__section__ (".text.init")))
#define __initdata __attribute__ ((__section__ (".data.init")))
#define __initfunc(__arginit) \
	__arginit __init; \
	__arginit

#if __GNUC__ >= 2 && __GNUC_MINOR__ >= 8
#define __initlocaldata  __initdata
#else
#define __initlocaldata
#endif

/* For assembly routines */
#define __INIT		.section	.text.init,"ax"
#define __FINIT		.previous
#define __INITDATA	.section	.data.init,"a"

#endif /* __MIPS_INIT_H */
