#ifndef _SPARC_ASI4M_H
#define _SPARC_ASI4M_H

/* asi4m.h: Address Space Identifier values for sun4m
 
   Copyright (C) 1995 Paul Hatchman (paul@sfe.com.au)
*/

#define ASI_PTE 0x0

#define ASI_NULL1			0x0
#define ASI_NULL2			0x1
#define	ASI_CONTROL			0x4	/* hmm? */
#define	ASI_USERTXT			0x8	/* user text */
#define	ASI_KERNELTXT		0x9	/* kernel text */
#define	ASI_USERDATA		0xA	/* user data */
#define	ASI_KERNELDATA		0xB	/* kernel data */

/* cache flushing */
#define ASI_FLUSHPG			0x10 
#define ASI_FLUSHSEG		0x11
#define ASI_FLUSHRGN		0x12
#define ASI_FLUSHCTX		0x13

/* MMU REGS */
#define SRMMU_CTL 0x000
#define SRMMU_CTP 0x100		/* set/get context pointer */
#define SRMMU_CTX 0x200 	/* get/set context */
#endif _SPARC_ASI4M_H
