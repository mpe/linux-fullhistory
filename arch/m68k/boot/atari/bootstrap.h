/*
** bootstrap.h -- This file is a part of the Atari bootloader.
**
** Copyright 1993 by Arjan Knor
**
** Modified by Andreas Schwab
**     - clear transparent translation registers
** Modified 18-Aug-96 by Geert Uytterhoeven
**     - Updated for the new boot information structure (untested!)
** Modified 1996-11-12 by Andreas Schwab
**     - Fixed and tested previous change
**
** This file is subject to the terms and conditions of the GNU General Public
** License.  See the file COPYING in the main directory of this archive
** for more details.
**
*/

#ifndef BOOTSTRAP_H
#define BOOTSTRAP_H

     /*
     *  Atari Bootinfo Definitions
     *
     *  All limits herein are `soft' limits, i.e. they don't put constraints
     *  on the actual parameters in the kernel.
     */

struct atari_bootinfo {
    unsigned long machtype;		    /* machine type */
    unsigned long cputype;		    /* system CPU */
    unsigned long fputype;		    /* system FPU */
    unsigned long mmutype;		    /* system MMU */
    int num_memory;			    /* # of memory blocks found */
    struct mem_info memory[NUM_MEMINFO];    /* memory description */
    struct mem_info ramdisk;		    /* ramdisk description */
    char command_line[CL_SIZE];		    /* kernel command line parameters */
    unsigned long mch_cookie;		    /* _MCH cookie from TOS */
};


/* _MCH cookie values */
#define MACH_ST  0
#define MACH_STE 1
#define MACH_TT  2
#define MACH_FALCON 3

/* some constants for memory handling */
#define ST_RAM 0
#define TT_RAM 1
#define TT_RAM_BASE  (u_long)(0x01000000)
#define MB           (1024 * 1024)
#define START_MEM    (bi.memory[0].addr)
#define MEM_SIZE     (bi.memory[0].size)

/* the various CPU- and FPU-types */
#define AFF_68000 (1)
#define AFF_68020 (2)
#define AFF_68030 (4)
#define AFF_68040 (8)
#define AFF_68881 (16)
#define AFF_68882 (32)

/* the possible OS-languages */
#define USA 0
#define FRG 1
#define FRA 2
#define UK  3
#define SPA 4
#define ITA 5
#define SWE 6
#define SWF 7
#define SWG 8
#define TUR 9
#define FIN 10
#define NOR 11
#define DEN 12
#define SAU 13
#define HOL 14

/* some inline functions */

static __inline int fpu_idle_frame_size (void)
{
  char fpu_frame[216];
  __asm__ __volatile__ ("fnop"::);
  __asm__ __volatile__ ("fsave %0@" : : "a" (fpu_frame));
  return fpu_frame[1];
}

static __inline void change_stack (u_long *stackp)
{
    __asm__ volatile ("movel %0,sp\n\t" :: "g" (stackp) : "sp");
}

static __inline void disable_interrupts (void)
{
  __asm__ volatile ("orw #0x700,sr":);
}

extern struct atari_bootinfo bi;
static __inline void disable_cache (void)
{
    __asm__ volatile ("movec %0,cacr" :: "d" (0));
    if (bi.cputype & CPU_68060) {
	/* '060: clear branch cache after disabling it;
	 * disable superscalar operation (and enable FPU) */
	__asm__ volatile ("movec %0,cacr" :: "d" (0x00400000));
	__asm__ volatile ("moveq #0,d0;"
			  ".long 0x4e7b0808"	/* movec d0,pcr */
			  : /* no outputs */
			  : /* no inputs */
			  : "d0");
    }
}

static __inline void disable_mmu (void)
{
	if (bi.cputype & (CPU_68040|CPU_68060)) {
	    __asm__ volatile ("moveq #0,d0;"
						  ".long 0x4e7b0003;"	/* movec d0,tc */
						  ".long 0x4e7b0004;"	/* movec d0,itt0 */
						  ".long 0x4e7b0005;"	/* movec d0,itt1 */
						  ".long 0x4e7b0006;"	/* movec d0,dtt0 */
						  ".long 0x4e7b0007"	/* movec d0,dtt1 */
						  : /* no outputs */
						  : /* no inputs */
						  : "d0");
	}
	else {
		__asm__ volatile ("subl  #4,sp\n\t"
						  "pmove tc,sp@\n\t"
						  "bclr  #7,sp@\n\t"
						  "pmove sp@,tc\n\t"
						  "addl  #4,sp");
		if (bi.cputype & CPU_68030) {
			__asm__ volatile ("clrl sp@-\n\t"
							  ".long 0xf0170800\n\t" /* pmove sp@,tt0 */
							  ".long 0xf0170c00\n\t" /* pmove sp@,tt1 */
							  "addl  #4,sp\n");
		}
	}
}

static __inline void jump_to_mover (void *, void *, void *, void *, int, int,
				    void *) __attribute__ ((noreturn));
static __inline void jump_to_mover (void *kernel_start, void *mem_start,
				    void *ramdisk_end, void *mem_end,
				    int kernel_size, int ramdisk_size,
				    void *mover_addr)
{
    asm volatile ("movel %0,a0\n\t"
		  "movel %1,a1\n\t"
		  "movel %2,a2\n\t"
		  "movel %3,a3\n\t"
		  "movel %4,d0\n\t"
		  "movel %5,d1\n\t"
		  "jmp   %6@\n"
		  : /* no outputs */
		  : "g" (kernel_start), "g" (mem_start),
		    "g" (ramdisk_end), "g" (mem_end),
		    "g" (kernel_size), "g" (ramdisk_size),
		    "a" (mover_addr)
		  : "a0", "a1", "a2", "a3", "d0", "d1");

	/* Avoid warning that function may return */
	for (;;) ;
}

#endif /* BOOTSTRAP_H */

