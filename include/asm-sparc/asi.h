#ifndef _SPARC_ASI_H
#define _SPARC_ASI_H

/* asi.h:  Address Space Identifier values for the sparc.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 *
 * Pioneer work for sun4m: Paul Hatchman (paul@sfe.com.au)
 * Joint edition for sun4c+sun4m: Pete A. Zaitcev <zaitcev@ipmce.su>
 */

/* These are sun4c, beware on other architectures. Although things should
 * be similar under regular sun4's.
 */

#define ASI_NULL1        0x0
#define ASI_NULL2        0x1

/* sun4c and sun4 control registers and mmu/vac ops */
#define ASI_CONTROL          0x2
#define ASI_SEGMAP           0x3
#define ASI_PTE              0x4
#define ASI_HWFLUSHSEG       0x5      /* These are to initiate hw flushes of the cache */
#define ASI_HWFLUSHPAGE      0x6
#define ASI_REGMAP           0x6      /* Top level segmaps on Sun4's with MUTANT MMU */
#define ASI_HWFLUSHCONTEXT   0x7


#define ASI_USERTXT      0x8
#define ASI_KERNELTXT    0x9
#define ASI_USERDATA     0xa
#define ASI_KERNELDATA   0xb

/* VAC Cache flushing on sun4c and sun4 */

#define ASI_FLUSHSEG     0xc      /* These are for "software" flushes of the cache */
#define ASI_FLUSHPG      0xd
#define ASI_FLUSHCTX     0xe

/* The following are now not so SS5 specific any more, it is pretty
 * much a complete generic sun4m/V8 ASI assignment listing now.
 *
 * -- davem@caip.rutgers.edu
 */

/* SPARCstation-5: only 6 bits are decoded. */
/* wo = Write Only, rw = Read Write;        */
/* ss = Single Size, as = All Sizes;        */
#define ASI_M_RES00         0x00   /* Don't touch... */
#define ASI_M_UNA01         0x01   /* Same here... */
#define ASI_M_MXCC          0x02   /* Access to TI VIKING MXCC registers */
#define ASI_M_FLUSH_PROBE   0x03   /* Reference MMU Flush/Probe; rw, ss */
#define ASI_M_MMUREGS       0x04   /* MMU Registers; rw, ss */
#define ASI_M_TLBDIAG       0x05   /* MMU TLB only Diagnostics */
#define ASI_M_DIAGS         0x06   /* Reference MMU Diagnostics */
#define ASI_M_IODIAG        0x07   /* MMU I/O TLB only Diagnostics */
#define ASI_M_USERTXT       0x08   /* Same as ASI_USERTXT; rw, as */
#define ASI_M_KERNELTXT     0x09   /* Same as ASI_KERNELTXT; rw, as */
#define ASI_M_USERDATA      0x0A   /* Same as ASI_USERDATA; rw, as */
#define ASI_M_KERNELDATA    0x0B   /* Same as ASI_KERNELDATA; rw, as */
#define ASI_M_TXTC_TAG      0x0C   /* Instruction Cache Tag; rw, ss */
#define ASI_M_TXTC_DATA     0x0D   /* Instruction Cache Data; rw, ss */
#define ASI_M_DATAC_TAG     0x0E   /* Data Cache Tag; rw, ss */
#define ASI_M_DATAC_DATA    0x0F   /* Data Cache Data; rw, ss */

/* The following cache flushing ASIs work only with the 'sta'
 * instruction results are unpredictable for 'swap' and 'ldstuba' etc.
 * So don't do it.
 */

/* These ASI flushes affect external caches too. */
#define ASI_M_FLUSH_PAGE    0x10   /* Flush I&D Cache Line (page); wo, ss */
#define ASI_M_FLUSH_SEG     0x11   /* Flush I&D Cache Line (seg); wo, ss */
#define ASI_M_FLUSH_REGION  0x12   /* Flush I&D Cache Line (region); wo, ss */
#define ASI_M_FLUSH_CTX     0x13   /* Flush I&D Cache Line (context); wo, ss */
#define ASI_M_FLUSH_USER    0x14   /* Flush I&D Cache Line (user); wo, ss */

/* Block-copy operations are available on certain V8 cpus */
#define ASI_M_BCOPY         0x17   /* Block copy */

/* These affect only the ICACHE and are Ross HyperSparc specific. */
#define ASI_M_IFLUSH_PAGE   0x18   /* Flush I Cache Line (page); wo, ss */
#define ASI_M_IFLUSH_SEG    0x19   /* Flush I Cache Line (seg); wo, ss */
#define ASI_M_IFLUSH_REGION 0x1A   /* Flush I Cache Line (region); wo, ss */
#define ASI_M_IFLUSH_CTX    0x1B   /* Flush I Cache Line (context); wo, ss */
#define ASI_M_IFLUSH_USER   0x1C   /* Flush I Cache Line (user); wo, ss */

/* Block-fill operations are available on certain V8 cpus */
#define ASI_M_BFILL         0x1F

/* This allows direct access to main memory, actually 0x20 to 0x2f are
 * the available ASI's for physical ram pass-through, but I don't have
 * any idea what the other ones do....
 */

#define ASI_M_BYPASS       0x20   /* Reference MMU bypass; rw, as */
#define ASI_M_FBMEM        0x29   /* Graphics card frame buffer access */
#define ASI_M_VMEUS        0x2A   /* VME user 16-bit access */
#define ASI_M_VMEPS        0x2B   /* VME priv 16-bit access */
#define ASI_M_VMEUT        0x2C   /* VME user 32-bit access */
#define ASI_M_VMEPT        0x2D   /* VME priv 32-bit access */
#define ASI_M_SBUS         0x2E   /* Direct SBus access */
#define ASI_M_CTL          0x2F   /* Control Space (ECC and MXCC are here) */


/* This is ROSS HyperSparc only. */
#define ASI_M_FLUSH_IWHOLE 0x31   /* Flush entire ICACHE; wo, ss */

#define ASI_M_DCDR         0x39   /* Data Cache Diagnostics Registerl rw, ss */

/* Sparc V9 TI UltraSparc ASI's */

/* ASIs 0x0-0x7f are Supervisor Only.  0x80-0xff are for anyone. */

/* You will notice that there are a lot of places where if a normal
 * ASI is available on the V9, there is also a little-endian version.
 */

#define ASI_V9_RESV0       0x00   /* Don't touch... */
#define ASI_V9_RESV1       0x01   /* Not here */
#define ASI_V9_RESV2       0x02   /* Or here */
#define ASI_V9_RESV3       0x03   /* nor here. */
#define ASI_V9_NUCLEUS     0x04   /* Impl-dep extra virtual access context */
#define ASI_V9_NUCLEUSL    0x0C   /* Nucleus context, little-endian */
#define ASI_V9_USER_PRIM   0x10   /* User primary address space */
#define ASI_V9_USER_SEC    0x11   /* User secondary address space */

#define ASI_V9_MMUPASS     0x14   /* OBMEM (external cache, no data cache) */
#define ASI_V9_IOPASS      0x15   /* Like MMUPASS but for I/O areas (uncached) */
#define ASI_V9_USER_PRIML  0x18   /* User primary address space, little-endian. */
#define ASI_V9_USER_SECL   0x19   /* User secondary address space, little-endian. */
#define ASI_V9_MMUPASSL    0x1C   /* OBMEM little-endian */
#define ASI_V9_IOPASSL     0x1D   /* Like IOPASS but little-endian */
#define ASI_V9_ATOMICQ     0x24   /* Atomic 128-bit load address space */
#define ASI_V9_ATOMICQL    0x2C   /* Atomic 128-bit load little-endian */
#define ASI_V9_LSTORECTL   0x45   /* ld/st control unit */
#define ASI_V9_DCACHE_ENT  0x46   /* Data cache entries */
#define ASI_V9_DCACHE_TAG  0x47   /* Data cache tags */
#define ASI_V9_IRQDISPS    0x48   /* IRQ dispatch status registers */
#define ASI_V9_IRQRECVS    0x49   /* IRQ receive status registers */
#define ASI_V9_MMUREGS     0x4A   /* Spitfire MMU control register */
#define ASI_V9_ESTATE      0x4B   /* Error state enable register */
#define ASI_V9_ASYNC_FSR   0x4C   /* Asynchronous Fault Status reg */
#define ASI_V9_ASYNC_FAR   0x4D   /* Asynchronous Fault Address reg */

#define ASI_V9_ECACHE_DIAG 0x4E   /* External Cache diagnostics */

#define ASI_V9_TXTMMU      0x50   /* MMU for program text */
#define ASI_V9_TXTMMU_D1   0x51   /* XXX */
#define ASI_V9_TXTMMU_D2   0x52   /* XXX */
#define ASI_V9_TXTMMU_TDI  0x54   /* Text MMU TLB data in */
#define ASI_V9_TXTMMU_TDA  0x55   /* Text MMU TLB data access */
#define ASI_V9_TXTMMU_TTR  0x56   /* Text MMU TLB tag read */
#define ASI_V9_TXTMMU_TDM  0x57   /* Text MMU TLB de-map */

#define ASI_V9_DATAMMU     0x58   /* MMU for program data */
#define ASI_V9_DATAMMU_D1  0x59   /* XXX */
#define ASI_V9_DATAMMU_D2  0x5A   /* XXX */
#define ASI_V9_DATAMMU_DD  0x5B   /* XXX */
#define ASI_V9_DATAMMU_TDI 0x5C   /* Data MMU TLB data in */
#define ASI_V9_DATAMMU_TDA 0x5D   /* Data MMU TLB data access */
#define ASI_V9_DATAMMU_TTR 0x5E   /* Data MMU TLB tag read */
#define ASI_V9_DATAMMU_TDM 0x5F   /* Data MMU TLB de-map */

#define ASI_V9_ICACHE_D    0x66   /* Instruction cache data */
#define ASI_V9_ICACHE_T    0x67   /* Instruction cache tags */
#define ASI_V9_ICACHE_DEC  0x6E   /* Instruction cache decode */
#define ASI_V9_ICACHE_NXT  0x6F   /* Instruction cache next ent */

#define ASI_V9_HUH1        0x70   /* XXX */
#define ASI_V9_HUH2        0x71   /* XXX */

#define ASI_V9_ECACHE_ACC  0x76   /* External cache registers */

#define ASI_V9_INTR_DISP   0x77   /* Interrupt dispatch registers */
#define ASI_V9_HUH1L       0x78   /* XXX */
#define ASI_V9_HUH2L       0x79   /* XXX */
#define ASI_V9_INTR_RECV   0x7f   /* Interrupt Receive registers */

#define ASI_V9_PRIMARY      0x80   /* Primary address space */
#define ASI_V9_SECONDARY    0x81   /* Secondary address space */
#define ASI_V9_PRIMARY_NF   0x82   /* Primary address space -- No Fault */
#define ASI_V9_SECONDARY_NF 0x83   /* Secondary address space -- No Fault */

#define ASI_V9_PRIMARYL      0x80   /* Primary address space, little-endian */
#define ASI_V9_SECONDARYL    0x81   /* Secondary address space, little-endian  */
#define ASI_V9_PRIMARY_NFL   0x82   /* Primary address space, No Fault, l-endian  */
#define ASI_V9_SECONDARY_NFL 0x83   /* Secondary address space, No Fault, l-endian  */

#define ASI_V9_XXX1        0xC0   /* XXX */
#define ASI_V9_XXX2        0xC1   /* XXX */
#define ASI_V9_XXX3        0xC2   /* XXX */
#define ASI_V9_XXX4        0xC3   /* XXX */
#define ASI_V9_XXX5        0xC4   /* XXX */
#define ASI_V9_XXX6        0xC5   /* XXX */
#define ASI_V9_XXX7        0xC8   /* XXX */
#define ASI_V9_XXX8        0xC9   /* XXX */
#define ASI_V9_XXX9        0xCA   /* XXX */
#define ASI_V9_XXX10       0xCB   /* XXX */
#define ASI_V9_XXX11       0xCC   /* XXX */
#define ASI_V9_XXX12       0xCD   /* XXX */

#define ASI_V9_XXX13       0xD0   /* XXX */
#define ASI_V9_XXX14       0xD1   /* XXX */
#define ASI_V9_XXX15       0xD2   /* XXX */
#define ASI_V9_XXX16       0xD3   /* XXX */
#define ASI_V9_XXX17       0xD8   /* XXX */
#define ASI_V9_XXX18       0xD9   /* XXX */
#define ASI_V9_XXX19       0xDA   /* XXX */
#define ASI_V9_XXX20       0xDB   /* XXX */

#define ASI_V9_XXX21       0xE0   /* XXX */
#define ASI_V9_XXX22       0xE1   /* XXX */
#define ASI_V9_XXX23       0xF0   /* XXX */
#define ASI_V9_XXX24       0xF1   /* XXX */
#define ASI_V9_XXX25       0xF8   /* XXX */
#define ASI_V9_XXX26       0xF9   /* XXX */

#ifndef __ASSEMBLY__

/* Better to do these inline with gcc __asm__ statements. */

/* The following allow you to access physical memory directly without
 * translation by the SRMMU.  The only other way to do this is to
 * turn off the SRMMU completely, and well... thats not good.
 *
 * TODO: For non-MBus SRMMU units we have to perform the following
 *       using this sequence.
 * 1) Turn off traps
 * 2) Turn on AC bit in SRMMU control register
 * 3) Do our direct physical memory access
 * 4) Restore old SRMMU control register value
 * 5) Restore old %psr value 
 */

extern __inline__ unsigned int
ldb_sun4m_bypass(unsigned int addr)
{
  unsigned int retval;

  __asm__("lduba [%2] %1, %0\n\t" :
          "=r" (retval) :
          "i" (ASI_M_BYPASS), "r" (addr));

  return retval;
}

extern __inline__ unsigned int
ldw_sun4m_bypass(unsigned int addr)
{
  unsigned int retval;

  __asm__("lda [%2] %1, %0\n\t" :
          "=r" (retval) :
          "i" (ASI_M_BYPASS), "r" (addr));

  return retval;
}

extern __inline__ void
stb_sun4m_bypass(unsigned char value, unsigned int addr)
{
  __asm__("stba %0, [%2] %1\n\t" : :
          "r" (value), "i" (ASI_M_BYPASS), "r" (addr) :
	  "memory");
}

extern __inline__ void
stw_sun4m_bypass(unsigned int value, unsigned int addr)
{
  __asm__("sta %0, [%2] %1\n\t" : :
          "r" (value), "i" (ASI_M_BYPASS), "r" (addr) :
	  "memory");
}

#endif /* !(__ASSEMBLY__) */


#endif /* _SPARC_ASI_H */
