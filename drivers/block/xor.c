/*
 * xor.c : Multiple Devices driver for Linux
 *
 * Copyright (C) 1996, 1997, 1998, 1999 Ingo Molnar, Matti Aarnio, Jakub Jelinek
 *
 *
 * optimized RAID-5 checksumming functions.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * You should have received a copy of the GNU General Public License
 * (for example /usr/src/linux/COPYING); if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/config.h>
#define BH_TRACE 0
#include <linux/module.h>
#include <linux/raid/md.h>
#ifdef __sparc_v9__
#include <asm/head.h>
#include <asm/asi.h>
#include <asm/visasm.h>
#endif

/*
 * we use the 'XOR function template' to register multiple xor
 * functions runtime. The kernel measures their speed upon bootup
 * and decides which one to use. (compile-time registration is
 * not enough as certain CPU features like MMX can only be detected
 * runtime)
 *
 * this architecture makes it pretty easy to add new routines
 * that are faster on certain CPUs, without killing other CPU's
 * 'native' routine. Although the current routines are belived
 * to be the physically fastest ones on all CPUs tested, but
 * feel free to prove me wrong and add yet another routine =B-)
 * --mingo
 */

#define MAX_XOR_BLOCKS 5

#define XOR_ARGS (unsigned int count, struct buffer_head **bh_ptr)

typedef void (*xor_block_t) XOR_ARGS;
xor_block_t xor_block = NULL;

#ifndef __sparc_v9__

struct xor_block_template;

struct xor_block_template {
	char * name;
	xor_block_t xor_block;
	int speed;
	struct xor_block_template * next;
};

struct xor_block_template * xor_functions = NULL;

#define XORBLOCK_TEMPLATE(x) \
static void xor_block_##x XOR_ARGS; \
static struct xor_block_template t_xor_block_##x = \
				 { #x, xor_block_##x, 0, NULL }; \
static void xor_block_##x XOR_ARGS

#ifdef __i386__

#ifdef CONFIG_X86_XMM
/*
 * Cache avoiding checksumming functions utilizing KNI instructions
 * Copyright (C) 1999 Zach Brown (with obvious credit due Ingo)
 */

XORBLOCK_TEMPLATE(pIII_kni)
{
	char xmm_save[16*4];
	int cr0;
        int lines = (bh_ptr[0]->b_size>>8);

	__asm__ __volatile__ ( 
		"movl %%cr0,%0		;\n\t"
		"clts			;\n\t"
		"movups %%xmm0,(%1)	;\n\t"
		"movups %%xmm1,0x10(%1)	;\n\t"
		"movups %%xmm2,0x20(%1)	;\n\t"
		"movups %%xmm3,0x30(%1)	;\n\t"
		: "=r" (cr0)
		: "r" (xmm_save) 
		: "memory" );

#define OFFS(x) "8*("#x"*2)"
#define	PF0(x) \
	"	prefetcht0  "OFFS(x)"(%1)   ;\n"
#define LD(x,y) \
        "       movaps   "OFFS(x)"(%1), %%xmm"#y"   ;\n"
#define ST(x,y) \
        "       movaps %%xmm"#y",   "OFFS(x)"(%1)   ;\n"
#define PF1(x) \
	"	prefetchnta "OFFS(x)"(%2)   ;\n"
#define PF2(x) \
	"	prefetchnta "OFFS(x)"(%3)   ;\n"
#define PF3(x) \
	"	prefetchnta "OFFS(x)"(%4)   ;\n"
#define PF4(x) \
	"	prefetchnta "OFFS(x)"(%5)   ;\n"
#define PF5(x) \
	"	prefetchnta "OFFS(x)"(%6)   ;\n"
#define XO1(x,y) \
        "       xorps   "OFFS(x)"(%2), %%xmm"#y"   ;\n"
#define XO2(x,y) \
        "       xorps   "OFFS(x)"(%3), %%xmm"#y"   ;\n"
#define XO3(x,y) \
        "       xorps   "OFFS(x)"(%4), %%xmm"#y"   ;\n"
#define XO4(x,y) \
        "       xorps   "OFFS(x)"(%5), %%xmm"#y"   ;\n"
#define XO5(x,y) \
        "       xorps   "OFFS(x)"(%6), %%xmm"#y"   ;\n"

	switch(count) {
		case 2:
		        __asm__ __volatile__ (
#undef BLOCK
#define BLOCK(i) \
		LD(i,0)					\
			LD(i+1,1)			\
		PF1(i)					\
				PF1(i+2)		\
				LD(i+2,2)		\
					LD(i+3,3)	\
		PF0(i+4)				\
				PF0(i+6)		\
		XO1(i,0)				\
			XO1(i+1,1)			\
				XO1(i+2,2)		\
					XO1(i+3,3)	\
		ST(i,0)					\
			ST(i+1,1)			\
				ST(i+2,2)		\
					ST(i+3,3)	\


		PF0(0)
				PF0(2)

	" .align 32,0x90		;\n"
        " 1:                            ;\n"

		BLOCK(0)
		BLOCK(4)
		BLOCK(8)
		BLOCK(12)

        "       addl $256, %1           ;\n"
        "       addl $256, %2           ;\n"
        "       decl %0                 ;\n"
        "       jnz 1b                  ;\n"

        		:
			: "r" (lines),
			  "r" (bh_ptr[0]->b_data),
        		  "r" (bh_ptr[1]->b_data)
		        : "memory" );
			break;
		case 3:
		        __asm__ __volatile__ (
#undef BLOCK
#define BLOCK(i) \
		PF1(i)					\
				PF1(i+2)		\
		LD(i,0)					\
			LD(i+1,1)			\
				LD(i+2,2)		\
					LD(i+3,3)	\
		PF2(i)					\
				PF2(i+2)		\
		PF0(i+4)				\
				PF0(i+6)		\
		XO1(i,0)				\
			XO1(i+1,1)			\
				XO1(i+2,2)		\
					XO1(i+3,3)	\
		XO2(i,0)				\
			XO2(i+1,1)			\
				XO2(i+2,2)		\
					XO2(i+3,3)	\
		ST(i,0)					\
			ST(i+1,1)			\
				ST(i+2,2)		\
					ST(i+3,3)	\


		PF0(0)
				PF0(2)

	" .align 32,0x90		;\n"
        " 1:                            ;\n"

		BLOCK(0)
		BLOCK(4)
		BLOCK(8)
		BLOCK(12)

        "       addl $256, %1           ;\n"
        "       addl $256, %2           ;\n"
        "       addl $256, %3           ;\n"
        "       decl %0                 ;\n"
        "       jnz 1b                  ;\n"
        		:
			: "r" (lines),
			  "r" (bh_ptr[0]->b_data),
        		  "r" (bh_ptr[1]->b_data),
			  "r" (bh_ptr[2]->b_data)
		        : "memory" );
			break;
		case 4:
		        __asm__ __volatile__ (
#undef BLOCK
#define BLOCK(i) \
		PF1(i)					\
				PF1(i+2)		\
		LD(i,0)					\
			LD(i+1,1)			\
				LD(i+2,2)		\
					LD(i+3,3)	\
		PF2(i)					\
				PF2(i+2)		\
		XO1(i,0)				\
			XO1(i+1,1)			\
				XO1(i+2,2)		\
					XO1(i+3,3)	\
		PF3(i)					\
				PF3(i+2)		\
		PF0(i+4)				\
				PF0(i+6)		\
		XO2(i,0)				\
			XO2(i+1,1)			\
				XO2(i+2,2)		\
					XO2(i+3,3)	\
		XO3(i,0)				\
			XO3(i+1,1)			\
				XO3(i+2,2)		\
					XO3(i+3,3)	\
		ST(i,0)					\
			ST(i+1,1)			\
				ST(i+2,2)		\
					ST(i+3,3)	\


		PF0(0)
				PF0(2)

	" .align 32,0x90		;\n"
        " 1:                            ;\n"

		BLOCK(0)
		BLOCK(4)
		BLOCK(8)
		BLOCK(12)

        "       addl $256, %1           ;\n"
        "       addl $256, %2           ;\n"
        "       addl $256, %3           ;\n"
        "       addl $256, %4           ;\n"
        "       decl %0                 ;\n"
        "       jnz 1b                  ;\n"

        		:
			: "r" (lines),
			  "r" (bh_ptr[0]->b_data),
        		  "r" (bh_ptr[1]->b_data),
			  "r" (bh_ptr[2]->b_data),
			  "r" (bh_ptr[3]->b_data)
		        : "memory" );
			break;
		case 5:
		        __asm__ __volatile__ (
#undef BLOCK
#define BLOCK(i) \
		PF1(i)					\
				PF1(i+2)		\
		LD(i,0)					\
			LD(i+1,1)			\
				LD(i+2,2)		\
					LD(i+3,3)	\
		PF2(i)					\
				PF2(i+2)		\
		XO1(i,0)				\
			XO1(i+1,1)			\
				XO1(i+2,2)		\
					XO1(i+3,3)	\
		PF3(i)					\
				PF3(i+2)		\
		XO2(i,0)				\
			XO2(i+1,1)			\
				XO2(i+2,2)		\
					XO2(i+3,3)	\
		PF4(i)					\
				PF4(i+2)		\
		PF0(i+4)				\
				PF0(i+6)		\
		XO3(i,0)				\
			XO3(i+1,1)			\
				XO3(i+2,2)		\
					XO3(i+3,3)	\
		XO4(i,0)				\
			XO4(i+1,1)			\
				XO4(i+2,2)		\
					XO4(i+3,3)	\
		ST(i,0)					\
			ST(i+1,1)			\
				ST(i+2,2)		\
					ST(i+3,3)	\


		PF0(0)
				PF0(2)

	" .align 32,0x90		;\n"
        " 1:                            ;\n"

		BLOCK(0)
		BLOCK(4)
		BLOCK(8)
		BLOCK(12)

        "       addl $256, %1           ;\n"
        "       addl $256, %2           ;\n"
        "       addl $256, %3           ;\n"
        "       addl $256, %4           ;\n"
        "       addl $256, %5           ;\n"
        "       decl %0                 ;\n"
        "       jnz 1b                  ;\n"

        		:
			: "r" (lines),
			  "r" (bh_ptr[0]->b_data),
        		  "r" (bh_ptr[1]->b_data),
			  "r" (bh_ptr[2]->b_data),
			  "r" (bh_ptr[3]->b_data),
			  "r" (bh_ptr[4]->b_data)
			: "memory");
			break;
	}

	__asm__ __volatile__ ( 
		"sfence			;\n\t"
		"movups (%1),%%xmm0	;\n\t"
		"movups 0x10(%1),%%xmm1	;\n\t"
		"movups 0x20(%1),%%xmm2	;\n\t"
		"movups 0x30(%1),%%xmm3	;\n\t"
		"movl 	%0,%%cr0	;\n\t"
		:
		: "r" (cr0), "r" (xmm_save)
		: "memory" );
}

#undef OFFS
#undef LD
#undef ST
#undef PF0
#undef PF1
#undef PF2
#undef PF3
#undef PF4
#undef PF5
#undef XO1
#undef XO2
#undef XO3
#undef XO4
#undef XO5
#undef BLOCK

#endif /* CONFIG_X86_XMM */

/*
 * high-speed RAID5 checksumming functions utilizing MMX instructions
 * Copyright (C) 1998 Ingo Molnar
 */
XORBLOCK_TEMPLATE(pII_mmx)
{
	char fpu_save[108];
        int lines = (bh_ptr[0]->b_size>>7);

	if (!(current->flags & PF_USEDFPU))
		__asm__ __volatile__ ( " clts;\n");

	__asm__ __volatile__ ( " fsave %0; fwait\n"::"m"(fpu_save[0]) );

#define LD(x,y) \
        "       movq   8*("#x")(%1), %%mm"#y"   ;\n"
#define ST(x,y) \
        "       movq %%mm"#y",   8*("#x")(%1)   ;\n"
#define XO1(x,y) \
        "       pxor   8*("#x")(%2), %%mm"#y"   ;\n"
#define XO2(x,y) \
        "       pxor   8*("#x")(%3), %%mm"#y"   ;\n"
#define XO3(x,y) \
        "       pxor   8*("#x")(%4), %%mm"#y"   ;\n"
#define XO4(x,y) \
        "       pxor   8*("#x")(%5), %%mm"#y"   ;\n"

	switch(count) {
		case 2:
			__asm__ __volatile__ (
#undef BLOCK
#define BLOCK(i) \
			LD(i,0)					\
				LD(i+1,1)			\
					LD(i+2,2)		\
						LD(i+3,3)	\
			XO1(i,0)				\
			ST(i,0)					\
				XO1(i+1,1)			\
				ST(i+1,1)			\
					XO1(i+2,2)		\
					ST(i+2,2)		\
						XO1(i+3,3)	\
						ST(i+3,3)

			" .align 32,0x90		;\n"
  			" 1:                            ;\n"

			BLOCK(0)
			BLOCK(4)
			BLOCK(8)
			BLOCK(12)

		        "       addl $128, %1         ;\n"
		        "       addl $128, %2         ;\n"
		        "       decl %0               ;\n"
		        "       jnz 1b                ;\n"
	        	:
			: "r" (lines),
			  "r" (bh_ptr[0]->b_data),
			  "r" (bh_ptr[1]->b_data)
			: "memory");
			break;
		case 3:
			__asm__ __volatile__ (
#undef BLOCK
#define BLOCK(i) \
			LD(i,0)					\
				LD(i+1,1)			\
					LD(i+2,2)		\
						LD(i+3,3)	\
			XO1(i,0)				\
				XO1(i+1,1)			\
					XO1(i+2,2)		\
						XO1(i+3,3)	\
			XO2(i,0)				\
			ST(i,0)					\
				XO2(i+1,1)			\
				ST(i+1,1)			\
					XO2(i+2,2)		\
					ST(i+2,2)		\
						XO2(i+3,3)	\
						ST(i+3,3)

			" .align 32,0x90		;\n"
  			" 1:                            ;\n"

			BLOCK(0)
			BLOCK(4)
			BLOCK(8)
			BLOCK(12)

		        "       addl $128, %1         ;\n"
		        "       addl $128, %2         ;\n"
		        "       addl $128, %3         ;\n"
		        "       decl %0               ;\n"
		        "       jnz 1b                ;\n"
	        	:
			: "r" (lines),
			  "r" (bh_ptr[0]->b_data),
			  "r" (bh_ptr[1]->b_data),
			  "r" (bh_ptr[2]->b_data)
			: "memory");
			break;
		case 4:
			__asm__ __volatile__ (
#undef BLOCK
#define BLOCK(i) \
			LD(i,0)					\
				LD(i+1,1)			\
					LD(i+2,2)		\
						LD(i+3,3)	\
			XO1(i,0)				\
				XO1(i+1,1)			\
					XO1(i+2,2)		\
						XO1(i+3,3)	\
			XO2(i,0)				\
				XO2(i+1,1)			\
					XO2(i+2,2)		\
						XO2(i+3,3)	\
			XO3(i,0)				\
			ST(i,0)					\
				XO3(i+1,1)			\
				ST(i+1,1)			\
					XO3(i+2,2)		\
					ST(i+2,2)		\
						XO3(i+3,3)	\
						ST(i+3,3)

			" .align 32,0x90		;\n"
  			" 1:                            ;\n"

			BLOCK(0)
			BLOCK(4)
			BLOCK(8)
			BLOCK(12)

		        "       addl $128, %1         ;\n"
		        "       addl $128, %2         ;\n"
		        "       addl $128, %3         ;\n"
		        "       addl $128, %4         ;\n"
		        "       decl %0               ;\n"
		        "       jnz 1b                ;\n"
	        	:
			: "r" (lines),
			  "r" (bh_ptr[0]->b_data),
			  "r" (bh_ptr[1]->b_data),
			  "r" (bh_ptr[2]->b_data),
			  "r" (bh_ptr[3]->b_data)
			: "memory");
			break;
		case 5:
			__asm__ __volatile__ (
#undef BLOCK
#define BLOCK(i) \
			LD(i,0)					\
				LD(i+1,1)			\
					LD(i+2,2)		\
						LD(i+3,3)	\
			XO1(i,0)				\
				XO1(i+1,1)			\
					XO1(i+2,2)		\
						XO1(i+3,3)	\
			XO2(i,0)				\
				XO2(i+1,1)			\
					XO2(i+2,2)		\
						XO2(i+3,3)	\
			XO3(i,0)				\
				XO3(i+1,1)			\
					XO3(i+2,2)		\
						XO3(i+3,3)	\
			XO4(i,0)				\
			ST(i,0)					\
				XO4(i+1,1)			\
				ST(i+1,1)			\
					XO4(i+2,2)		\
					ST(i+2,2)		\
						XO4(i+3,3)	\
						ST(i+3,3)

			" .align 32,0x90		;\n"
  			" 1:                            ;\n"

			BLOCK(0)
			BLOCK(4)
			BLOCK(8)
			BLOCK(12)

		        "       addl $128, %1         ;\n"
		        "       addl $128, %2         ;\n"
		        "       addl $128, %3         ;\n"
		        "       addl $128, %4         ;\n"
		        "       addl $128, %5         ;\n"
		        "       decl %0               ;\n"
		        "       jnz 1b                ;\n"
	        	:
			: "g" (lines),
			  "r" (bh_ptr[0]->b_data),
			  "r" (bh_ptr[1]->b_data),
			  "r" (bh_ptr[2]->b_data),
			  "r" (bh_ptr[3]->b_data),
			  "r" (bh_ptr[4]->b_data)
			: "memory");
			break;
	}

	__asm__ __volatile__ ( " frstor %0;\n"::"m"(fpu_save[0]) );

	if (!(current->flags & PF_USEDFPU))
		stts();
}

#undef LD
#undef XO1
#undef XO2
#undef XO3
#undef XO4
#undef ST
#undef BLOCK

XORBLOCK_TEMPLATE(p5_mmx)
{
	char fpu_save[108];
        int lines = (bh_ptr[0]->b_size>>6);

	if (!(current->flags & PF_USEDFPU))
		__asm__ __volatile__ ( " clts;\n");

	__asm__ __volatile__ ( " fsave %0; fwait\n"::"m"(fpu_save[0]) );

	switch(count) {
		case 2:
		        __asm__ __volatile__ (

			        " .align 32,0x90             ;\n"
			        " 1:                         ;\n"
			        "       movq   (%1), %%mm0   ;\n"
			        "       movq  8(%1), %%mm1   ;\n"
			        "       pxor   (%2), %%mm0   ;\n"
			        "       movq 16(%1), %%mm2   ;\n"
			        "       movq %%mm0,   (%1)   ;\n"
			        "       pxor  8(%2), %%mm1   ;\n"
			        "       movq 24(%1), %%mm3   ;\n"
			        "       movq %%mm1,  8(%1)   ;\n"
			        "       pxor 16(%2), %%mm2   ;\n"
			        "       movq 32(%1), %%mm4   ;\n"
			        "       movq %%mm2, 16(%1)   ;\n"
			        "       pxor 24(%2), %%mm3   ;\n"
			        "       movq 40(%1), %%mm5   ;\n"
			        "       movq %%mm3, 24(%1)   ;\n"
			        "       pxor 32(%2), %%mm4   ;\n"
			        "       movq 48(%1), %%mm6   ;\n"
			        "       movq %%mm4, 32(%1)   ;\n"
			        "       pxor 40(%2), %%mm5   ;\n"
			        "       movq 56(%1), %%mm7   ;\n"
			        "       movq %%mm5, 40(%1)   ;\n"
			        "       pxor 48(%2), %%mm6   ;\n"
			        "       pxor 56(%2), %%mm7   ;\n"
			        "       movq %%mm6, 48(%1)   ;\n"
			        "       movq %%mm7, 56(%1)   ;\n"
        
			        "       addl $64, %1         ;\n"
			        "       addl $64, %2         ;\n"
			        "       decl %0              ;\n"
			        "       jnz 1b               ;\n"

			        : 
			        : "r" (lines),
				  "r" (bh_ptr[0]->b_data),
				  "r" (bh_ptr[1]->b_data)
			        : "memory" );
			break;
		case 3:
			__asm__ __volatile__ (

			        " .align 32,0x90             ;\n"
			        " 1:                         ;\n"
			        "       movq   (%1), %%mm0   ;\n"
			        "       movq  8(%1), %%mm1   ;\n"
			        "       pxor   (%2), %%mm0   ;\n"
			        "       movq 16(%1), %%mm2   ;\n"
			        "       pxor  8(%2), %%mm1   ;\n"
			        "       pxor   (%3), %%mm0   ;\n"
			        "       pxor 16(%2), %%mm2   ;\n"
			        "       movq %%mm0,   (%1)   ;\n"
			        "       pxor  8(%3), %%mm1   ;\n"
			        "       pxor 16(%3), %%mm2   ;\n"
			        "       movq 24(%1), %%mm3   ;\n"
			        "       movq %%mm1,  8(%1)   ;\n"
			        "       movq 32(%1), %%mm4   ;\n"
			        "       movq 40(%1), %%mm5   ;\n"
			        "       pxor 24(%2), %%mm3   ;\n"
			        "       movq %%mm2, 16(%1)   ;\n"
			        "       pxor 32(%2), %%mm4   ;\n"
			        "       pxor 24(%3), %%mm3   ;\n"
			        "       pxor 40(%2), %%mm5   ;\n"
			        "       movq %%mm3, 24(%1)   ;\n"
			        "       pxor 32(%3), %%mm4   ;\n"
			        "       pxor 40(%3), %%mm5   ;\n"
			        "       movq 48(%1), %%mm6   ;\n"
			        "       movq %%mm4, 32(%1)   ;\n"
			        "       movq 56(%1), %%mm7   ;\n"
			        "       pxor 48(%2), %%mm6   ;\n"
			        "       movq %%mm5, 40(%1)   ;\n"
			        "       pxor 56(%2), %%mm7   ;\n"
			        "       pxor 48(%3), %%mm6   ;\n"
			        "       pxor 56(%3), %%mm7   ;\n"
			        "       movq %%mm6, 48(%1)   ;\n"
			        "       movq %%mm7, 56(%1)   ;\n"
        
			        "       addl $64, %1         ;\n"
			        "       addl $64, %2         ;\n"
			        "       addl $64, %3         ;\n"
			        "       decl %0              ;\n"
			        "       jnz 1b               ;\n"

			        : 
			        : "r" (lines),
				  "r" (bh_ptr[0]->b_data),
				  "r" (bh_ptr[1]->b_data),
				  "r" (bh_ptr[2]->b_data)
			        : "memory" );
			break;
		case 4:
			__asm__ __volatile__ (

			        " .align 32,0x90             ;\n"
			        " 1:                         ;\n"
			        "       movq   (%1), %%mm0   ;\n"
			        "       movq  8(%1), %%mm1   ;\n"
			        "       pxor   (%2), %%mm0   ;\n"
			        "       movq 16(%1), %%mm2   ;\n"
			        "       pxor  8(%2), %%mm1   ;\n"
			        "       pxor   (%3), %%mm0   ;\n"
			        "       pxor 16(%2), %%mm2   ;\n"
			        "       pxor  8(%3), %%mm1   ;\n"
			        "       pxor   (%4), %%mm0   ;\n"
			        "       movq 24(%1), %%mm3   ;\n"
			        "       pxor 16(%3), %%mm2   ;\n"
			        "       pxor  8(%4), %%mm1   ;\n"
			        "       movq %%mm0,   (%1)   ;\n"
			        "       movq 32(%1), %%mm4   ;\n"
			        "       pxor 24(%2), %%mm3   ;\n"
			        "       pxor 16(%4), %%mm2   ;\n"
			        "       movq %%mm1,  8(%1)   ;\n"
			        "       movq 40(%1), %%mm5   ;\n"
			        "       pxor 32(%2), %%mm4   ;\n"
			        "       pxor 24(%3), %%mm3   ;\n"
			        "       movq %%mm2, 16(%1)   ;\n"
			        "       pxor 40(%2), %%mm5   ;\n"
			        "       pxor 32(%3), %%mm4   ;\n"
			        "       pxor 24(%4), %%mm3   ;\n"
			        "       movq %%mm3, 24(%1)   ;\n"
			        "       movq 56(%1), %%mm7   ;\n"
			        "       movq 48(%1), %%mm6   ;\n"
			        "       pxor 40(%3), %%mm5   ;\n"
			        "       pxor 32(%4), %%mm4   ;\n"
			        "       pxor 48(%2), %%mm6   ;\n"
			        "       movq %%mm4, 32(%1)   ;\n"
			        "       pxor 56(%2), %%mm7   ;\n"
			        "       pxor 40(%4), %%mm5   ;\n"
			        "       pxor 48(%3), %%mm6   ;\n"
			        "       pxor 56(%3), %%mm7   ;\n"
			        "       movq %%mm5, 40(%1)   ;\n"
			        "       pxor 48(%4), %%mm6   ;\n"
			        "       pxor 56(%4), %%mm7   ;\n"
			        "       movq %%mm6, 48(%1)   ;\n"
			        "       movq %%mm7, 56(%1)   ;\n"
        
			        "       addl $64, %1         ;\n"
			        "       addl $64, %2         ;\n"
			        "       addl $64, %3         ;\n"
			        "       addl $64, %4         ;\n"
			        "       decl %0              ;\n"
			        "       jnz 1b               ;\n"

			        : 
			        : "r" (lines),
				  "r" (bh_ptr[0]->b_data),
				  "r" (bh_ptr[1]->b_data),
				  "r" (bh_ptr[2]->b_data),
				  "r" (bh_ptr[3]->b_data)
			        : "memory" );
			break;
		case 5:
			__asm__ __volatile__ (

			        " .align 32,0x90             ;\n"
			        " 1:                         ;\n"
			        "       movq   (%1), %%mm0   ;\n"
			        "       movq  8(%1), %%mm1   ;\n"
			        "       pxor   (%2), %%mm0   ;\n"
			        "       pxor  8(%2), %%mm1   ;\n"
			        "       movq 16(%1), %%mm2   ;\n"
			        "       pxor   (%3), %%mm0   ;\n"
			        "       pxor  8(%3), %%mm1   ;\n"
			        "       pxor 16(%2), %%mm2   ;\n"
			        "       pxor   (%4), %%mm0   ;\n"
			        "       pxor  8(%4), %%mm1   ;\n"
			        "       pxor 16(%3), %%mm2   ;\n"
			        "       movq 24(%1), %%mm3   ;\n"
			        "       pxor   (%5), %%mm0   ;\n"
			        "       pxor  8(%5), %%mm1   ;\n"
			        "       movq %%mm0,   (%1)   ;\n"
			        "       pxor 16(%4), %%mm2   ;\n"
			        "       pxor 24(%2), %%mm3   ;\n"
			        "       movq %%mm1,  8(%1)   ;\n"
			        "       pxor 16(%5), %%mm2   ;\n"
			        "       pxor 24(%3), %%mm3   ;\n"
			        "       movq 32(%1), %%mm4   ;\n"
			        "       movq %%mm2, 16(%1)   ;\n"
			        "       pxor 24(%4), %%mm3   ;\n"
			        "       pxor 32(%2), %%mm4   ;\n"
			        "       movq 40(%1), %%mm5   ;\n"
			        "       pxor 24(%5), %%mm3   ;\n"
			        "       pxor 32(%3), %%mm4   ;\n"
			        "       pxor 40(%2), %%mm5   ;\n"
			        "       movq %%mm3, 24(%1)   ;\n"
			        "       pxor 32(%4), %%mm4   ;\n"
			        "       pxor 40(%3), %%mm5   ;\n"
			        "       movq 48(%1), %%mm6   ;\n"
			        "       movq 56(%1), %%mm7   ;\n"
			        "       pxor 32(%5), %%mm4   ;\n"
			        "       pxor 40(%4), %%mm5   ;\n"
			        "       pxor 48(%2), %%mm6   ;\n"
			        "       pxor 56(%2), %%mm7   ;\n"
			        "       movq %%mm4, 32(%1)   ;\n"
			        "       pxor 48(%3), %%mm6   ;\n"
			        "       pxor 56(%3), %%mm7   ;\n"
			        "       pxor 40(%5), %%mm5   ;\n"
			        "       pxor 48(%4), %%mm6   ;\n"
			        "       pxor 56(%4), %%mm7   ;\n"
			        "       movq %%mm5, 40(%1)   ;\n"
			        "       pxor 48(%5), %%mm6   ;\n"
			        "       pxor 56(%5), %%mm7   ;\n"
			        "       movq %%mm6, 48(%1)   ;\n"
			        "       movq %%mm7, 56(%1)   ;\n"
        
			        "       addl $64, %1         ;\n"
			        "       addl $64, %2         ;\n"
			        "       addl $64, %3         ;\n"
			        "       addl $64, %4         ;\n"
			        "       addl $64, %5         ;\n"
			        "       decl %0              ;\n"
			        "       jnz 1b               ;\n"

			        : 
			        : "g" (lines),
				  "r" (bh_ptr[0]->b_data),
				  "r" (bh_ptr[1]->b_data),
				  "r" (bh_ptr[2]->b_data),
				  "r" (bh_ptr[3]->b_data),
				  "r" (bh_ptr[4]->b_data)
			        : "memory" );
			break;
	}

	__asm__ __volatile__ ( " frstor %0;\n"::"m"(fpu_save[0]) );

	if (!(current->flags & PF_USEDFPU))
		stts();
}
#endif /* __i386__ */
#endif /* !__sparc_v9__ */

#ifdef __sparc_v9__
/*
 * High speed xor_block operation for RAID4/5 utilizing the
 * UltraSparc Visual Instruction Set.
 *
 * Copyright (C) 1997, 1999 Jakub Jelinek (jj@ultra.linux.cz)
 *
 *	Requirements:
 *	!(((long)dest | (long)sourceN) & (64 - 1)) &&
 *	!(len & 127) && len >= 256
 *
 * It is done in pure assembly, as otherwise gcc makes it
 * a non-leaf function, which is not what we want.
 * Also, we don't measure the speeds as on other architectures,
 * as the measuring routine does not take into account cold caches
 * and the fact that xor_block_VIS bypasses the caches.
 * xor_block_32regs might be 5% faster for count 2 if caches are hot
 * and things just right (for count 3 VIS is about as fast as 32regs for
 * hot caches and for count 4 and 5 VIS is faster by good margin always),
 * but I think it is better not to pollute the caches.
 * Actually, if I'd just fight for speed for hot caches, I could
 * write a hybrid VIS/integer routine, which would do always two
 * 64B blocks in VIS and two in IEUs, but I really care more about
 * caches.
 */
extern void *VISenter(void);
extern void xor_block_VIS XOR_ARGS;

void __xor_block_VIS(void)
{
__asm__ ("
	.globl xor_block_VIS
xor_block_VIS:
	ldx	[%%o1 + 0], %%o4
	ldx	[%%o1 + 8], %%o3
	ldx	[%%o4 + %1], %%g5
	ldx	[%%o4 + %0], %%o4
	ldx	[%%o3 + %0], %%o3
	rd	%%fprs, %%o5
	andcc	%%o5, %2, %%g0
	be,pt	%%icc, 297f
	 sethi	%%hi(%5), %%g1
	jmpl	%%g1 + %%lo(%5), %%g7
	 add	%%g7, 8, %%g7
297:	wr	%%g0, %4, %%fprs
	membar	#LoadStore|#StoreLoad|#StoreStore
	sub	%%g5, 64, %%g5
	ldda	[%%o4] %3, %%f0
	ldda	[%%o3] %3, %%f16
	cmp	%%o0, 4
	bgeu,pt	%%xcc, 10f
	 cmp	%%o0, 3
	be,pn	%%xcc, 13f
	 mov	-64, %%g1
	sub	%%g5, 64, %%g5
	rd	%%asi, %%g1
	wr	%%g0, %3, %%asi

2:	ldda	[%%o4 + 64] %%asi, %%f32
	fxor	%%f0, %%f16, %%f16
	fxor	%%f2, %%f18, %%f18
	fxor	%%f4, %%f20, %%f20
	fxor	%%f6, %%f22, %%f22
	fxor	%%f8, %%f24, %%f24
	fxor	%%f10, %%f26, %%f26
	fxor	%%f12, %%f28, %%f28
	fxor	%%f14, %%f30, %%f30
	stda	%%f16, [%%o4] %3
	ldda	[%%o3 + 64] %%asi, %%f48
	ldda	[%%o4 + 128] %%asi, %%f0
	fxor	%%f32, %%f48, %%f48
	fxor	%%f34, %%f50, %%f50
	add	%%o4, 128, %%o4
	fxor	%%f36, %%f52, %%f52
	add	%%o3, 128, %%o3
	fxor	%%f38, %%f54, %%f54
	subcc	%%g5, 128, %%g5
	fxor	%%f40, %%f56, %%f56
	fxor	%%f42, %%f58, %%f58
	fxor	%%f44, %%f60, %%f60
	fxor	%%f46, %%f62, %%f62
	stda	%%f48, [%%o4 - 64] %%asi
	bne,pt	%%xcc, 2b
	 ldda	[%%o3] %3, %%f16

	ldda	[%%o4 + 64] %%asi, %%f32
	fxor	%%f0, %%f16, %%f16
	fxor	%%f2, %%f18, %%f18
	fxor	%%f4, %%f20, %%f20
	fxor	%%f6, %%f22, %%f22
	fxor	%%f8, %%f24, %%f24
	fxor	%%f10, %%f26, %%f26
	fxor	%%f12, %%f28, %%f28
	fxor	%%f14, %%f30, %%f30
	stda	%%f16, [%%o4] %3
	ldda	[%%o3 + 64] %%asi, %%f48
	membar	#Sync
	fxor	%%f32, %%f48, %%f48
	fxor	%%f34, %%f50, %%f50
	fxor	%%f36, %%f52, %%f52
	fxor	%%f38, %%f54, %%f54
	fxor	%%f40, %%f56, %%f56
	fxor	%%f42, %%f58, %%f58
	fxor	%%f44, %%f60, %%f60
	fxor	%%f46, %%f62, %%f62
	stda	%%f48, [%%o4 + 64] %%asi
	membar	#Sync|#StoreStore|#StoreLoad
	wr	%%g0, 0, %%fprs
	retl
	 wr	%%g1, %%g0, %%asi

13:	ldx	[%%o1 + 16], %%o2
	ldx	[%%o2 + %0], %%o2

3:	ldda	[%%o2] %3, %%f32
	fxor	%%f0, %%f16, %%f48
	fxor	%%f2, %%f18, %%f50
	add	%%o4, 64, %%o4
	fxor	%%f4, %%f20, %%f52
	fxor	%%f6, %%f22, %%f54
	add	%%o3, 64, %%o3
	fxor	%%f8, %%f24, %%f56
	fxor	%%f10, %%f26, %%f58
	fxor	%%f12, %%f28, %%f60
	fxor	%%f14, %%f30, %%f62
	ldda	[%%o4] %3, %%f0
	fxor	%%f48, %%f32, %%f48
	fxor	%%f50, %%f34, %%f50
	fxor	%%f52, %%f36, %%f52
	fxor	%%f54, %%f38, %%f54
	add	%%o2, 64, %%o2
	fxor	%%f56, %%f40, %%f56
	fxor	%%f58, %%f42, %%f58
	subcc	%%g5, 64, %%g5
	fxor	%%f60, %%f44, %%f60
	fxor	%%f62, %%f46, %%f62
	stda	%%f48, [%%o4 + %%g1] %3
	bne,pt	%%xcc, 3b
	 ldda	[%%o3] %3, %%f16

	ldda	[%%o2] %3, %%f32
	fxor	%%f0, %%f16, %%f48
	fxor	%%f2, %%f18, %%f50
	fxor	%%f4, %%f20, %%f52
	fxor	%%f6, %%f22, %%f54
	fxor	%%f8, %%f24, %%f56
	fxor	%%f10, %%f26, %%f58
	fxor	%%f12, %%f28, %%f60
	fxor	%%f14, %%f30, %%f62
	membar	#Sync
	fxor	%%f48, %%f32, %%f48
	fxor	%%f50, %%f34, %%f50
	fxor	%%f52, %%f36, %%f52
	fxor	%%f54, %%f38, %%f54
	fxor	%%f56, %%f40, %%f56
	fxor	%%f58, %%f42, %%f58
	fxor	%%f60, %%f44, %%f60
	fxor	%%f62, %%f46, %%f62
	stda	%%f48, [%%o4] %3
	membar	#Sync|#StoreStore|#StoreLoad
	retl
	 wr	%%g0, 0, %%fprs

10:	cmp	%%o0, 5
	be,pt	%%xcc, 15f
	 mov	-64, %%g1

14:	ldx	[%%o1 + 16], %%o2
	ldx	[%%o1 + 24], %%o0
	ldx	[%%o2 + %0], %%o2
	ldx	[%%o0 + %0], %%o0

4:	ldda	[%%o2] %3, %%f32
	fxor	%%f0, %%f16, %%f16
	fxor	%%f2, %%f18, %%f18
	add	%%o4, 64, %%o4
	fxor	%%f4, %%f20, %%f20
	fxor	%%f6, %%f22, %%f22
	add	%%o3, 64, %%o3
	fxor	%%f8, %%f24, %%f24
	fxor	%%f10, %%f26, %%f26
	fxor	%%f12, %%f28, %%f28
	fxor	%%f14, %%f30, %%f30
	ldda	[%%o0] %3, %%f48
	fxor	%%f16, %%f32, %%f32
	fxor	%%f18, %%f34, %%f34
	fxor	%%f20, %%f36, %%f36
	fxor	%%f22, %%f38, %%f38
	add	%%o2, 64, %%o2
	fxor	%%f24, %%f40, %%f40
	fxor	%%f26, %%f42, %%f42
	fxor	%%f28, %%f44, %%f44
	fxor	%%f30, %%f46, %%f46
	ldda	[%%o4] %3, %%f0
	fxor	%%f32, %%f48, %%f48
	fxor	%%f34, %%f50, %%f50
	fxor	%%f36, %%f52, %%f52
	add	%%o0, 64, %%o0
	fxor	%%f38, %%f54, %%f54
	fxor	%%f40, %%f56, %%f56
	fxor	%%f42, %%f58, %%f58
	subcc	%%g5, 64, %%g5
	fxor	%%f44, %%f60, %%f60
	fxor	%%f46, %%f62, %%f62
	stda	%%f48, [%%o4 + %%g1] %3
	bne,pt	%%xcc, 4b
	 ldda	[%%o3] %3, %%f16

	ldda	[%%o2] %3, %%f32
	fxor	%%f0, %%f16, %%f16
	fxor	%%f2, %%f18, %%f18
	fxor	%%f4, %%f20, %%f20
	fxor	%%f6, %%f22, %%f22
	fxor	%%f8, %%f24, %%f24
	fxor	%%f10, %%f26, %%f26
	fxor	%%f12, %%f28, %%f28
	fxor	%%f14, %%f30, %%f30
	ldda	[%%o0] %3, %%f48
	fxor	%%f16, %%f32, %%f32
	fxor	%%f18, %%f34, %%f34
	fxor	%%f20, %%f36, %%f36
	fxor	%%f22, %%f38, %%f38
	fxor	%%f24, %%f40, %%f40
	fxor	%%f26, %%f42, %%f42
	fxor	%%f28, %%f44, %%f44
	fxor	%%f30, %%f46, %%f46
	membar	#Sync
	fxor	%%f32, %%f48, %%f48
	fxor	%%f34, %%f50, %%f50
	fxor	%%f36, %%f52, %%f52
	fxor	%%f38, %%f54, %%f54
	fxor	%%f40, %%f56, %%f56
	fxor	%%f42, %%f58, %%f58
	fxor	%%f44, %%f60, %%f60
	fxor	%%f46, %%f62, %%f62
	stda	%%f48, [%%o4] %3
	membar	#Sync|#StoreStore|#StoreLoad
	retl
	 wr	%%g0, 0, %%fprs

15:	ldx	[%%o1 + 16], %%o2
	ldx	[%%o1 + 24], %%o0
	ldx	[%%o1 + 32], %%o1
	ldx	[%%o2 + %0], %%o2
	ldx	[%%o0 + %0], %%o0
	ldx	[%%o1 + %0], %%o1

5:	ldda	[%%o2] %3, %%f32
	fxor	%%f0, %%f16, %%f48
	fxor	%%f2, %%f18, %%f50
	add	%%o4, 64, %%o4
	fxor	%%f4, %%f20, %%f52
	fxor	%%f6, %%f22, %%f54
	add	%%o3, 64, %%o3
	fxor	%%f8, %%f24, %%f56
	fxor	%%f10, %%f26, %%f58
	fxor	%%f12, %%f28, %%f60
	fxor	%%f14, %%f30, %%f62
	ldda	[%%o0] %3, %%f16
	fxor	%%f48, %%f32, %%f48
	fxor	%%f50, %%f34, %%f50
	fxor	%%f52, %%f36, %%f52
	fxor	%%f54, %%f38, %%f54
	add	%%o2, 64, %%o2
	fxor	%%f56, %%f40, %%f56
	fxor	%%f58, %%f42, %%f58
	fxor	%%f60, %%f44, %%f60
	fxor	%%f62, %%f46, %%f62
	ldda	[%%o1] %3, %%f32
	fxor	%%f48, %%f16, %%f48
	fxor	%%f50, %%f18, %%f50
	add	%%o0, 64, %%o0
	fxor	%%f52, %%f20, %%f52
	fxor	%%f54, %%f22, %%f54
	add	%%o1, 64, %%o1
	fxor	%%f56, %%f24, %%f56
	fxor	%%f58, %%f26, %%f58
	fxor	%%f60, %%f28, %%f60
	fxor	%%f62, %%f30, %%f62
	ldda	[%%o4] %3, %%f0
	fxor	%%f48, %%f32, %%f48
	fxor	%%f50, %%f34, %%f50
	fxor	%%f52, %%f36, %%f52
	fxor	%%f54, %%f38, %%f54
	fxor	%%f56, %%f40, %%f56
	fxor	%%f58, %%f42, %%f58
	subcc	%%g5, 64, %%g5
	fxor	%%f60, %%f44, %%f60
	fxor	%%f62, %%f46, %%f62
	stda	%%f48, [%%o4 + %%g1] %3
	bne,pt	%%xcc, 5b
	 ldda	[%%o3] %3, %%f16

	ldda	[%%o2] %3, %%f32
	fxor	%%f0, %%f16, %%f48
	fxor	%%f2, %%f18, %%f50
	fxor	%%f4, %%f20, %%f52
	fxor	%%f6, %%f22, %%f54
	fxor	%%f8, %%f24, %%f56
	fxor	%%f10, %%f26, %%f58
	fxor	%%f12, %%f28, %%f60
	fxor	%%f14, %%f30, %%f62
	ldda	[%%o0] %3, %%f16
	fxor	%%f48, %%f32, %%f48
	fxor	%%f50, %%f34, %%f50
	fxor	%%f52, %%f36, %%f52
	fxor	%%f54, %%f38, %%f54
	fxor	%%f56, %%f40, %%f56
	fxor	%%f58, %%f42, %%f58
	fxor	%%f60, %%f44, %%f60
	fxor	%%f62, %%f46, %%f62
	ldda	[%%o1] %3, %%f32
	fxor	%%f48, %%f16, %%f48
	fxor	%%f50, %%f18, %%f50
	fxor	%%f52, %%f20, %%f52
	fxor	%%f54, %%f22, %%f54
	fxor	%%f56, %%f24, %%f56
	fxor	%%f58, %%f26, %%f58
	fxor	%%f60, %%f28, %%f60
	fxor	%%f62, %%f30, %%f62
	membar	#Sync
	fxor	%%f48, %%f32, %%f48
	fxor	%%f50, %%f34, %%f50
	fxor	%%f52, %%f36, %%f52
	fxor	%%f54, %%f38, %%f54
	fxor	%%f56, %%f40, %%f56
	fxor	%%f58, %%f42, %%f58
	fxor	%%f60, %%f44, %%f60
	fxor	%%f62, %%f46, %%f62
	stda	%%f48, [%%o4] %3
	membar	#Sync|#StoreStore|#StoreLoad
	retl
	 wr	%%g0, 0, %%fprs
	" : :
	"i" (&((struct buffer_head *)0)->b_data),
	"i" (&((struct buffer_head *)0)->b_data),
	"i" (FPRS_FEF|FPRS_DU), "i" (ASI_BLK_P),
	"i" (FPRS_FEF), "i" (VISenter));
}
#endif /* __sparc_v9__ */

#if defined(__sparc__) && !defined(__sparc_v9__)
/*
 * High speed xor_block operation for RAID4/5 utilizing the
 * ldd/std SPARC instructions.
 *
 * Copyright (C) 1999 Jakub Jelinek (jj@ultra.linux.cz)
 *
 */

XORBLOCK_TEMPLATE(SPARC)
{
	int size  = bh_ptr[0]->b_size;
	int lines = size / (sizeof (long)) / 8, i;
	long *destp   = (long *) bh_ptr[0]->b_data;
	long *source1 = (long *) bh_ptr[1]->b_data;
	long *source2, *source3, *source4;

	switch (count) {
	case 2:
		for (i = lines; i > 0; i--) {
		  __asm__ __volatile__("
		  ldd [%0 + 0x00], %%g2
		  ldd [%0 + 0x08], %%g4
		  ldd [%0 + 0x10], %%o0
		  ldd [%0 + 0x18], %%o2
		  ldd [%1 + 0x00], %%o4
		  ldd [%1 + 0x08], %%l0
		  ldd [%1 + 0x10], %%l2
		  ldd [%1 + 0x18], %%l4
		  xor %%g2, %%o4, %%g2
		  xor %%g3, %%o5, %%g3
		  xor %%g4, %%l0, %%g4
		  xor %%g5, %%l1, %%g5
		  xor %%o0, %%l2, %%o0
		  xor %%o1, %%l3, %%o1
		  xor %%o2, %%l4, %%o2
		  xor %%o3, %%l5, %%o3
		  std %%g2, [%0 + 0x00]
		  std %%g4, [%0 + 0x08]
		  std %%o0, [%0 + 0x10]
		  std %%o2, [%0 + 0x18]
		  " : : "r" (destp), "r" (source1) : "g2", "g3", "g4", "g5", "o0", 
		  "o1", "o2", "o3", "o4", "o5", "l0", "l1", "l2", "l3", "l4", "l5");
		  destp += 8;
		  source1 += 8;
		}
		break;
	case 3:
		source2 = (long *) bh_ptr[2]->b_data;
		for (i = lines; i > 0; i--) {
		  __asm__ __volatile__("
		  ldd [%0 + 0x00], %%g2
		  ldd [%0 + 0x08], %%g4
		  ldd [%0 + 0x10], %%o0
		  ldd [%0 + 0x18], %%o2
		  ldd [%1 + 0x00], %%o4
		  ldd [%1 + 0x08], %%l0
		  ldd [%1 + 0x10], %%l2
		  ldd [%1 + 0x18], %%l4
		  xor %%g2, %%o4, %%g2
		  xor %%g3, %%o5, %%g3
		  ldd [%2 + 0x00], %%o4
		  xor %%g4, %%l0, %%g4
		  xor %%g5, %%l1, %%g5
		  ldd [%2 + 0x08], %%l0
		  xor %%o0, %%l2, %%o0
		  xor %%o1, %%l3, %%o1
		  ldd [%2 + 0x10], %%l2
		  xor %%o2, %%l4, %%o2
		  xor %%o3, %%l5, %%o3
		  ldd [%2 + 0x18], %%l4
		  xor %%g2, %%o4, %%g2
		  xor %%g3, %%o5, %%g3
		  xor %%g4, %%l0, %%g4
		  xor %%g5, %%l1, %%g5
		  xor %%o0, %%l2, %%o0
		  xor %%o1, %%l3, %%o1
		  xor %%o2, %%l4, %%o2
		  xor %%o3, %%l5, %%o3
		  std %%g2, [%0 + 0x00]
		  std %%g4, [%0 + 0x08]
		  std %%o0, [%0 + 0x10]
		  std %%o2, [%0 + 0x18]
		  " : : "r" (destp), "r" (source1), "r" (source2)
		  : "g2", "g3", "g4", "g5", "o0", "o1", "o2", "o3", "o4", "o5",
		  "l0", "l1", "l2", "l3", "l4", "l5");
		  destp += 8;
		  source1 += 8;
		  source2 += 8;
		}
		break;
	case 4:
		source2 = (long *) bh_ptr[2]->b_data;
		source3 = (long *) bh_ptr[3]->b_data;
		for (i = lines; i > 0; i--) {
		  __asm__ __volatile__("
		  ldd [%0 + 0x00], %%g2
		  ldd [%0 + 0x08], %%g4
		  ldd [%0 + 0x10], %%o0
		  ldd [%0 + 0x18], %%o2
		  ldd [%1 + 0x00], %%o4
		  ldd [%1 + 0x08], %%l0
		  ldd [%1 + 0x10], %%l2
		  ldd [%1 + 0x18], %%l4
		  xor %%g2, %%o4, %%g2
		  xor %%g3, %%o5, %%g3
		  ldd [%2 + 0x00], %%o4
		  xor %%g4, %%l0, %%g4
		  xor %%g5, %%l1, %%g5
		  ldd [%2 + 0x08], %%l0
		  xor %%o0, %%l2, %%o0
		  xor %%o1, %%l3, %%o1
		  ldd [%2 + 0x10], %%l2
		  xor %%o2, %%l4, %%o2
		  xor %%o3, %%l5, %%o3
		  ldd [%2 + 0x18], %%l4
		  xor %%g2, %%o4, %%g2
		  xor %%g3, %%o5, %%g3
		  ldd [%3 + 0x00], %%o4
		  xor %%g4, %%l0, %%g4
		  xor %%g5, %%l1, %%g5
		  ldd [%3 + 0x08], %%l0
		  xor %%o0, %%l2, %%o0
		  xor %%o1, %%l3, %%o1
		  ldd [%3 + 0x10], %%l2
		  xor %%o2, %%l4, %%o2
		  xor %%o3, %%l5, %%o3
		  ldd [%3 + 0x18], %%l4
		  xor %%g2, %%o4, %%g2
		  xor %%g3, %%o5, %%g3
		  xor %%g4, %%l0, %%g4
		  xor %%g5, %%l1, %%g5
		  xor %%o0, %%l2, %%o0
		  xor %%o1, %%l3, %%o1
		  xor %%o2, %%l4, %%o2
		  xor %%o3, %%l5, %%o3
		  std %%g2, [%0 + 0x00]
		  std %%g4, [%0 + 0x08]
		  std %%o0, [%0 + 0x10]
		  std %%o2, [%0 + 0x18]
		  " : : "r" (destp), "r" (source1), "r" (source2), "r" (source3)
		  : "g2", "g3", "g4", "g5", "o0", "o1", "o2", "o3", "o4", "o5",
		  "l0", "l1", "l2", "l3", "l4", "l5");
		  destp += 8;
		  source1 += 8;
		  source2 += 8;
		  source3 += 8;
		}
		break;
	case 5:
		source2 = (long *) bh_ptr[2]->b_data;
		source3 = (long *) bh_ptr[3]->b_data;
		source4 = (long *) bh_ptr[4]->b_data;
		for (i = lines; i > 0; i--) {
		  __asm__ __volatile__("
		  ldd [%0 + 0x00], %%g2
		  ldd [%0 + 0x08], %%g4
		  ldd [%0 + 0x10], %%o0
		  ldd [%0 + 0x18], %%o2
		  ldd [%1 + 0x00], %%o4
		  ldd [%1 + 0x08], %%l0
		  ldd [%1 + 0x10], %%l2
		  ldd [%1 + 0x18], %%l4
		  xor %%g2, %%o4, %%g2
		  xor %%g3, %%o5, %%g3
		  ldd [%2 + 0x00], %%o4
		  xor %%g4, %%l0, %%g4
		  xor %%g5, %%l1, %%g5
		  ldd [%2 + 0x08], %%l0
		  xor %%o0, %%l2, %%o0
		  xor %%o1, %%l3, %%o1
		  ldd [%2 + 0x10], %%l2
		  xor %%o2, %%l4, %%o2
		  xor %%o3, %%l5, %%o3
		  ldd [%2 + 0x18], %%l4
		  xor %%g2, %%o4, %%g2
		  xor %%g3, %%o5, %%g3
		  ldd [%3 + 0x00], %%o4
		  xor %%g4, %%l0, %%g4
		  xor %%g5, %%l1, %%g5
		  ldd [%3 + 0x08], %%l0
		  xor %%o0, %%l2, %%o0
		  xor %%o1, %%l3, %%o1
		  ldd [%3 + 0x10], %%l2
		  xor %%o2, %%l4, %%o2
		  xor %%o3, %%l5, %%o3
		  ldd [%3 + 0x18], %%l4
		  xor %%g2, %%o4, %%g2
		  xor %%g3, %%o5, %%g3
		  ldd [%4 + 0x00], %%o4
		  xor %%g4, %%l0, %%g4
		  xor %%g5, %%l1, %%g5
		  ldd [%4 + 0x08], %%l0
		  xor %%o0, %%l2, %%o0
		  xor %%o1, %%l3, %%o1
		  ldd [%4 + 0x10], %%l2
		  xor %%o2, %%l4, %%o2
		  xor %%o3, %%l5, %%o3
		  ldd [%4 + 0x18], %%l4
		  xor %%g2, %%o4, %%g2
		  xor %%g3, %%o5, %%g3
		  xor %%g4, %%l0, %%g4
		  xor %%g5, %%l1, %%g5
		  xor %%o0, %%l2, %%o0
		  xor %%o1, %%l3, %%o1
		  xor %%o2, %%l4, %%o2
		  xor %%o3, %%l5, %%o3
		  std %%g2, [%0 + 0x00]
		  std %%g4, [%0 + 0x08]
		  std %%o0, [%0 + 0x10]
		  std %%o2, [%0 + 0x18]
		  " : : "r" (destp), "r" (source1), "r" (source2), "r" (source3), "r" (source4)
		  : "g2", "g3", "g4", "g5", "o0", "o1", "o2", "o3", "o4", "o5",
		  "l0", "l1", "l2", "l3", "l4", "l5");
		  destp += 8;
		  source1 += 8;
		  source2 += 8;
		  source3 += 8;
		  source4 += 8;
		}
		break;
	}
}
#endif /* __sparc_v[78]__ */

#ifndef __sparc_v9__

/*
 * this one works reasonably on any x86 CPU
 * (send me an assembly version for inclusion if you can make it faster)
 *
 * this one is just as fast as written in pure assembly on x86.
 * the reason for this separate version is that the
 * fast open-coded xor routine "32reg" produces suboptimal code
 * on x86, due to lack of registers.
 */
XORBLOCK_TEMPLATE(8regs)
{
	int len  = bh_ptr[0]->b_size;
	long *destp   = (long *) bh_ptr[0]->b_data;
	long *source1, *source2, *source3, *source4;
	long lines = len / (sizeof (long)) / 8, i;

	switch(count) {
		case 2:
			source1 = (long *) bh_ptr[1]->b_data;
			for (i = lines; i > 0; i--) {
				*(destp + 0) ^= *(source1 + 0);
				*(destp + 1) ^= *(source1 + 1);
				*(destp + 2) ^= *(source1 + 2);
				*(destp + 3) ^= *(source1 + 3);
				*(destp + 4) ^= *(source1 + 4);
				*(destp + 5) ^= *(source1 + 5);
				*(destp + 6) ^= *(source1 + 6);
				*(destp + 7) ^= *(source1 + 7);
				source1 += 8;
				destp += 8;
			}
			break;
		case 3:
			source2 = (long *) bh_ptr[2]->b_data;
			source1 = (long *) bh_ptr[1]->b_data;
			for (i = lines; i > 0; i--) {
				*(destp + 0) ^= *(source1 + 0);
				*(destp + 0) ^= *(source2 + 0);
				*(destp + 1) ^= *(source1 + 1);
				*(destp + 1) ^= *(source2 + 1);
				*(destp + 2) ^= *(source1 + 2);
				*(destp + 2) ^= *(source2 + 2);
				*(destp + 3) ^= *(source1 + 3);
				*(destp + 3) ^= *(source2 + 3);
				*(destp + 4) ^= *(source1 + 4);
				*(destp + 4) ^= *(source2 + 4);
				*(destp + 5) ^= *(source1 + 5);
				*(destp + 5) ^= *(source2 + 5);
				*(destp + 6) ^= *(source1 + 6);
				*(destp + 6) ^= *(source2 + 6);
				*(destp + 7) ^= *(source1 + 7);
				*(destp + 7) ^= *(source2 + 7);
				source1 += 8;
				source2 += 8;
				destp += 8;
			}
			break;
		case 4:
			source3 = (long *) bh_ptr[3]->b_data;
			source2 = (long *) bh_ptr[2]->b_data;
			source1 = (long *) bh_ptr[1]->b_data;
			for (i = lines; i > 0; i--) {
				*(destp + 0) ^= *(source1 + 0);
				*(destp + 0) ^= *(source2 + 0);
				*(destp + 0) ^= *(source3 + 0);
				*(destp + 1) ^= *(source1 + 1);
				*(destp + 1) ^= *(source2 + 1);
				*(destp + 1) ^= *(source3 + 1);
				*(destp + 2) ^= *(source1 + 2);
				*(destp + 2) ^= *(source2 + 2);
				*(destp + 2) ^= *(source3 + 2);
				*(destp + 3) ^= *(source1 + 3);
				*(destp + 3) ^= *(source2 + 3);
				*(destp + 3) ^= *(source3 + 3);
				*(destp + 4) ^= *(source1 + 4);
				*(destp + 4) ^= *(source2 + 4);
				*(destp + 4) ^= *(source3 + 4);
				*(destp + 5) ^= *(source1 + 5);
				*(destp + 5) ^= *(source2 + 5);
				*(destp + 5) ^= *(source3 + 5);
				*(destp + 6) ^= *(source1 + 6);
				*(destp + 6) ^= *(source2 + 6);
				*(destp + 6) ^= *(source3 + 6);
				*(destp + 7) ^= *(source1 + 7);
				*(destp + 7) ^= *(source2 + 7);
				*(destp + 7) ^= *(source3 + 7);
				source1 += 8;
				source2 += 8;
				source3 += 8;
				destp += 8;
			}
			break;
		case 5:
			source4 = (long *) bh_ptr[4]->b_data;
			source3 = (long *) bh_ptr[3]->b_data;
			source2 = (long *) bh_ptr[2]->b_data;
			source1 = (long *) bh_ptr[1]->b_data;
			for (i = lines; i > 0; i--) {
				*(destp + 0) ^= *(source1 + 0);
				*(destp + 0) ^= *(source2 + 0);
				*(destp + 0) ^= *(source3 + 0);
				*(destp + 0) ^= *(source4 + 0);
				*(destp + 1) ^= *(source1 + 1);
				*(destp + 1) ^= *(source2 + 1);
				*(destp + 1) ^= *(source3 + 1);
				*(destp + 1) ^= *(source4 + 1);
				*(destp + 2) ^= *(source1 + 2);
				*(destp + 2) ^= *(source2 + 2);
				*(destp + 2) ^= *(source3 + 2);
				*(destp + 2) ^= *(source4 + 2);
				*(destp + 3) ^= *(source1 + 3);
				*(destp + 3) ^= *(source2 + 3);
				*(destp + 3) ^= *(source3 + 3);
				*(destp + 3) ^= *(source4 + 3);
				*(destp + 4) ^= *(source1 + 4);
				*(destp + 4) ^= *(source2 + 4);
				*(destp + 4) ^= *(source3 + 4);
				*(destp + 4) ^= *(source4 + 4);
				*(destp + 5) ^= *(source1 + 5);
				*(destp + 5) ^= *(source2 + 5);
				*(destp + 5) ^= *(source3 + 5);
				*(destp + 5) ^= *(source4 + 5);
				*(destp + 6) ^= *(source1 + 6);
				*(destp + 6) ^= *(source2 + 6);
				*(destp + 6) ^= *(source3 + 6);
				*(destp + 6) ^= *(source4 + 6);
				*(destp + 7) ^= *(source1 + 7);
				*(destp + 7) ^= *(source2 + 7);
				*(destp + 7) ^= *(source3 + 7);
				*(destp + 7) ^= *(source4 + 7);
				source1 += 8;
				source2 += 8;
				source3 += 8;
				source4 += 8;
				destp += 8;
			}
			break;
	}
}

/*
 * platform independent RAID5 checksum calculation, this should
 * be very fast on any platform that has a decent amount of
 * registers. (32 or more)
 */
XORBLOCK_TEMPLATE(32regs)
{
	int size  = bh_ptr[0]->b_size;
	int lines = size / (sizeof (long)) / 8, i;
	long *destp   = (long *) bh_ptr[0]->b_data;
	long *source1, *source2, *source3, *source4;
	
	  /* LOTS of registers available...
	     We do explicite loop-unrolling here for code which
	     favours RISC machines.  In fact this is almoast direct
	     RISC assembly on Alpha and SPARC :-)  */


	switch(count) {
		case 2:
			source1 = (long *) bh_ptr[1]->b_data;
			for (i = lines; i > 0; i--) {
	  			register long d0, d1, d2, d3, d4, d5, d6, d7;
				d0 = destp[0];	/* Pull the stuff into registers	*/
				d1 = destp[1];	/*  ... in bursts, if possible.		*/
				d2 = destp[2];
				d3 = destp[3];
				d4 = destp[4];
				d5 = destp[5];
				d6 = destp[6];
				d7 = destp[7];
				d0 ^= source1[0];
				d1 ^= source1[1];
				d2 ^= source1[2];
				d3 ^= source1[3];
				d4 ^= source1[4];
				d5 ^= source1[5];
				d6 ^= source1[6];
				d7 ^= source1[7];
				destp[0] = d0;	/* Store the result (in burts)		*/
				destp[1] = d1;
				destp[2] = d2;
				destp[3] = d3;
				destp[4] = d4;	/* Store the result (in burts)		*/
				destp[5] = d5;
				destp[6] = d6;
				destp[7] = d7;
				source1 += 8;
				destp += 8;
			}
			break;
	  	case 3:
			source2 = (long *) bh_ptr[2]->b_data;
			source1 = (long *) bh_ptr[1]->b_data;
			for (i = lines; i > 0; i--) {
	  			register long d0, d1, d2, d3, d4, d5, d6, d7;
				d0 = destp[0];	/* Pull the stuff into registers	*/
				d1 = destp[1];	/*  ... in bursts, if possible.		*/
				d2 = destp[2];
				d3 = destp[3];
				d4 = destp[4];
				d5 = destp[5];
				d6 = destp[6];
				d7 = destp[7];
				d0 ^= source1[0];
				d1 ^= source1[1];
				d2 ^= source1[2];
				d3 ^= source1[3];
				d4 ^= source1[4];
				d5 ^= source1[5];
				d6 ^= source1[6];
				d7 ^= source1[7];
				d0 ^= source2[0];
				d1 ^= source2[1];
				d2 ^= source2[2];
				d3 ^= source2[3];
				d4 ^= source2[4];
				d5 ^= source2[5];
				d6 ^= source2[6];
				d7 ^= source2[7];
				destp[0] = d0;	/* Store the result (in burts)		*/
				destp[1] = d1;
				destp[2] = d2;
				destp[3] = d3;
				destp[4] = d4;	/* Store the result (in burts)		*/
				destp[5] = d5;
				destp[6] = d6;
				destp[7] = d7;
				source1 += 8;
				source2 += 8;
				destp += 8;
			}
			break;
		case 4:
			source3 = (long *) bh_ptr[3]->b_data;
			source2 = (long *) bh_ptr[2]->b_data;
			source1 = (long *) bh_ptr[1]->b_data;
			for (i = lines; i > 0; i--) {
	  			register long d0, d1, d2, d3, d4, d5, d6, d7;
				d0 = destp[0];	/* Pull the stuff into registers	*/
				d1 = destp[1];	/*  ... in bursts, if possible.		*/
				d2 = destp[2];
				d3 = destp[3];
				d4 = destp[4];
				d5 = destp[5];
				d6 = destp[6];
				d7 = destp[7];
				d0 ^= source1[0];
				d1 ^= source1[1];
				d2 ^= source1[2];
				d3 ^= source1[3];
				d4 ^= source1[4];
				d5 ^= source1[5];
				d6 ^= source1[6];
				d7 ^= source1[7];
				d0 ^= source2[0];
				d1 ^= source2[1];
				d2 ^= source2[2];
				d3 ^= source2[3];
				d4 ^= source2[4];
				d5 ^= source2[5];
				d6 ^= source2[6];
				d7 ^= source2[7];
				d0 ^= source3[0];
				d1 ^= source3[1];
				d2 ^= source3[2];
				d3 ^= source3[3];
				d4 ^= source3[4];
				d5 ^= source3[5];
				d6 ^= source3[6];
				d7 ^= source3[7];
				destp[0] = d0;	/* Store the result (in burts)		*/
				destp[1] = d1;
				destp[2] = d2;
				destp[3] = d3;
				destp[4] = d4;	/* Store the result (in burts)		*/
				destp[5] = d5;
				destp[6] = d6;
				destp[7] = d7;
				source1 += 8;
				source2 += 8;
				source3 += 8;
				destp += 8;
			}
			break;
		case 5:
			source4 = (long *) bh_ptr[4]->b_data;
			source3 = (long *) bh_ptr[3]->b_data;
			source2 = (long *) bh_ptr[2]->b_data;
			source1 = (long *) bh_ptr[1]->b_data;
			for (i = lines; i > 0; i--) {
	  			register long d0, d1, d2, d3, d4, d5, d6, d7;
				d0 = destp[0];	/* Pull the stuff into registers	*/
				d1 = destp[1];	/*  ... in bursts, if possible.		*/
				d2 = destp[2];
				d3 = destp[3];
				d4 = destp[4];
				d5 = destp[5];
				d6 = destp[6];
				d7 = destp[7];
				d0 ^= source1[0];
				d1 ^= source1[1];
				d2 ^= source1[2];
				d3 ^= source1[3];
				d4 ^= source1[4];
				d5 ^= source1[5];
				d6 ^= source1[6];
				d7 ^= source1[7];
				d0 ^= source2[0];
				d1 ^= source2[1];
				d2 ^= source2[2];
				d3 ^= source2[3];
				d4 ^= source2[4];
				d5 ^= source2[5];
				d6 ^= source2[6];
				d7 ^= source2[7];
				d0 ^= source3[0];
				d1 ^= source3[1];
				d2 ^= source3[2];
				d3 ^= source3[3];
				d4 ^= source3[4];
				d5 ^= source3[5];
				d6 ^= source3[6];
				d7 ^= source3[7];
				d0 ^= source4[0];
				d1 ^= source4[1];
				d2 ^= source4[2];
				d3 ^= source4[3];
				d4 ^= source4[4];
				d5 ^= source4[5];
				d6 ^= source4[6];
				d7 ^= source4[7];
				destp[0] = d0;	/* Store the result (in burts)		*/
				destp[1] = d1;
				destp[2] = d2;
				destp[3] = d3;
				destp[4] = d4;	/* Store the result (in burts)		*/
				destp[5] = d5;
				destp[6] = d6;
				destp[7] = d7;
				source1 += 8;
				source2 += 8;
				source3 += 8;
				source4 += 8;
				destp += 8;
			}
			break;
	}
}

/*
 * (the -6*32 shift factor colors the cache)
 */
#define SIZE (PAGE_SIZE-6*32)

static void xor_speed ( struct xor_block_template * func, 
	struct buffer_head *b1, struct buffer_head *b2)
{
	int speed;
	unsigned long now;
	int i, count, max;
	struct buffer_head *bh_ptr[6];

	func->next = xor_functions;
	xor_functions = func;
	bh_ptr[0] = b1;
	bh_ptr[1] = b2;

	/*
	 * count the number of XORs done during a whole jiffy.
	 * calculate the speed of checksumming from this.
	 * (we use a 2-page allocation to have guaranteed
	 * color L1-cache layout)
	 */
	max = 0;
	for (i = 0; i < 5; i++) {
		now = jiffies;
		count = 0;
		while (jiffies == now) {
			mb();
			func->xor_block(2,bh_ptr);
			mb();
			count++;
			mb();
		}
		if (count > max)
			max = count;
	}

	speed = max * (HZ*SIZE/1024);
	func->speed = speed;

	printk( "   %-10s: %5d.%03d MB/sec\n", func->name,
		speed / 1000, speed % 1000);
}

static inline void pick_fastest_function(void)
{
	struct xor_block_template *f, *fastest;

	fastest = xor_functions;
	for (f = fastest; f; f = f->next) {
		if (f->speed > fastest->speed)
			fastest = f;
	}
#ifdef CONFIG_X86_XMM 
	if (cpu_has_xmm) {
		fastest = &t_xor_block_pIII_kni;
	}
#endif
	xor_block = fastest->xor_block;
	printk( "using fastest function: %s (%d.%03d MB/sec)\n", fastest->name,
		fastest->speed / 1000, fastest->speed % 1000);
}
 
static struct buffer_head b1, b2;

void calibrate_xor_block(void)
{
	if (xor_block)
		return;
	memset(&b1,0,sizeof(b1));
	b2 = b1;

	b1.b_data = (char *) md__get_free_pages(GFP_KERNEL,2);
	if (!b1.b_data) {
		pick_fastest_function();
		return;
	}
	b2.b_data = b1.b_data + 2*PAGE_SIZE + SIZE;

	b1.b_size = SIZE;

	printk(KERN_INFO "raid5: measuring checksumming speed\n");

	sti(); /* should be safe */

#if defined(__sparc__) && !defined(__sparc_v9__)
	printk(KERN_INFO "raid5: trying high-speed SPARC checksum routine\n");
	xor_speed(&t_xor_block_SPARC,&b1,&b2);
#endif

#ifdef CONFIG_X86_XMM 
	if (cpu_has_xmm) {
		printk(KERN_INFO
			"raid5: KNI detected, trying cache-avoiding KNI checksum routine\n");
		/* we force the use of the KNI xor block because it
			can write around l2.  we may also be able
			to load into the l1 only depending on how
			the cpu deals with a load to a line that is
			being prefetched.
		*/
		xor_speed(&t_xor_block_pIII_kni,&b1,&b2);
	}
#endif /* CONFIG_X86_XMM */

#ifdef __i386__

	if (md_cpu_has_mmx()) {
		printk(KERN_INFO
			"raid5: MMX detected, trying high-speed MMX checksum routines\n");
		xor_speed(&t_xor_block_pII_mmx,&b1,&b2);
		xor_speed(&t_xor_block_p5_mmx,&b1,&b2);
	}

#endif /* __i386__ */
	
	
	xor_speed(&t_xor_block_8regs,&b1,&b2);
	xor_speed(&t_xor_block_32regs,&b1,&b2);

	free_pages((unsigned long)b1.b_data,2);
	pick_fastest_function();
}

#else /* __sparc_v9__ */

void calibrate_xor_block(void)
{
	if (xor_block)
		return;
	printk(KERN_INFO "raid5: using high-speed VIS checksum routine\n");
	xor_block = xor_block_VIS;
}

#endif /* __sparc_v9__ */

MD_EXPORT_SYMBOL(xor_block);
MD_EXPORT_SYMBOL(calibrate_xor_block);

#ifdef MODULE
int init_module(void)
{
	calibrate_xor_block();
	return 0;
}
#endif
