/*
** bootstrap.h -- This file is a part of the Amiga bootloader.
**
** Copyright 1993, 1994 by Hamish Macdonald
**
** Some minor additions by Michael Rausch 1-11-94
** Modified 11-May-94 by Geert Uytterhoeven
**                      (Geert.Uytterhoeven@cs.kuleuven.ac.be)
**     - inline Supervisor() call
**
** This file is subject to the terms and conditions of the GNU General Public
** License.  See the file README.legal in the main directory of this archive
** for more details.
**
*/

#ifndef BOOTSTRAP_H
#define BOOTSTRAP_H

#include <asm/amigatypes.h>
#include <asm/amigahw.h>

struct List {
    struct Node *l_head;
    struct Node *l_tail;
    struct Node *l_tailpred;
    u_char  l_type;
    u_char  l_pad;
};

struct MemChunk {
    struct  MemChunk *mc_Next;	/* pointer to next chunk */
    u_long   mc_Bytes;		/* chunk byte size	*/
};

#define MEMF_CHIP  (1<<1)
#define MEMF_FAST  (1<<2)
#define MEMF_LOCAL (1<<8)
#define MEMF_CLEAR (1<<16)

struct MemHeader {
    struct  Node mh_Node;
    u_short   mh_Attributes;	/* characteristics of this region */
    struct  MemChunk *mh_First; /* first free region		*/
    void    *mh_Lower;		/* lower memory bound		*/
    void    *mh_Upper;		/* upper memory bound+1 */
    u_long   mh_Free;		/* total number of free bytes	*/
};

struct ExecBase {
    u_char	fill1[296];
    u_short	AttnFlags;
    u_char	fill2[24];
    struct List MemList;
    u_char	fill3[194];
    u_char	VBlankFrequency;
    u_char	PowerSupplyFrequency;
    u_char	fill4[36];
    u_long	EClockFrequency;
};

#ifndef AFF_68020
#define AFB_68020 1
#define AFF_68020 (1<<AFB_68020)
#endif

#ifndef AFF_68030
#define AFB_68030 2
#define AFF_68030 (1<<AFB_68030)
#endif

#ifndef AFF_68040
#define AFB_68040 3
#define AFF_68040 (1<<AFB_68040)
#endif

#ifndef AFF_68881
#define AFB_68881 4
#define AFF_68881 (1<<AFB_68881)
#endif

#ifndef AFF_68882
#define AFB_68882 5
#define AFF_68882 (1<<AFB_68882)
#endif

#ifndef AFF_FPU40
#define AFB_FPU40 6
#define AFF_FPU40 (1<<AFB_FPU40)
#endif

/*
 *  GfxBase is now used to determine if AGA or ECS is present
 */

struct GfxBase {
	u_char	unused1[0xec];
	u_char	ChipRevBits0;
	u_char	unused2[5];
	u_short	monitor_id;
};

#ifndef	GFXB_HR_AGNUS
#define	GFXB_HR_AGNUS	0
#define	GFXF_HR_AGNUS	(1<<GFXB_HR_AGNUS)
#endif

#ifndef	GFXB_HR_DENISE
#define GFXB_HR_DENISE	1
#define GFXF_HR_DENISE	(1<<GFXB_HR_DENISE)
#endif

#ifndef	GFXB_AA_ALICE
#define GFXB_AA_ALICE	2
#define GFXF_AA_ALICE	(1<<GFXB_AA_ALICE)
#endif

#ifndef	GFXB_AA_LISA
#define GFXB_AA_LISA	3
#define GFXF_AA_LISA	(1<<GFXB_AA_LISA)
#endif

/*
 *  HiRes(=Big) Agnus present; i.e. 
 *  1MB chipmem, big blits (none of interest so far) and programmable sync
 */
#define GFXG_OCS	(GFXF_HR_AGNUS)
/*
 *  HiRes Agnus/Denise present; we are running on ECS
 */
#define GFXG_ECS	(GFXF_HR_AGNUS|GFXF_HR_DENISE)
/*
 *  Alice and Lisa present; we are running on AGA
 */
#define GFXG_AGA	(GFXF_AA_ALICE|GFXF_AA_LISA)

struct Library;

extern struct ExecBase *SysBase;

static __inline void *
AllocMem (unsigned long byteSize,unsigned long requirements)
{
  register void  *_res	__asm("d0");
  register struct ExecBase *a6 __asm("a6") = SysBase;
  register unsigned long d0 __asm("d0") = byteSize;
  register unsigned long d1 __asm("d1") = requirements;
  __asm __volatile ("jsr a6@(-0xc6)"
  : "=r" (_res)
  : "r" (a6), "r" (d0), "r" (d1)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline void
CloseLibrary (struct Library *library)
{
  register struct ExecBase *a6 __asm("a6") = SysBase;
  register struct Library *a1 __asm("a1") = library;
  __asm __volatile ("jsr a6@(-0x19e)"
  : /* no output */
  : "r" (a6), "r" (a1)
  : "a0","a1","d0","d1", "memory");
}
static __inline void
Disable (void)
{
  extern struct ExecBase *SysBase;
  register struct ExecBase *a6 __asm("a6") = SysBase;
  __asm __volatile ("jsr a6@(-0x78)"
  : /* no output */
  : "r" (a6)
  : "a0","a1","d0","d1", "memory");
}
static __inline void
Enable (void)
{
  register struct ExecBase *a6 __asm("a6") = SysBase;
  __asm __volatile ("jsr a6@(-0x7e)"
  : /* no output */
  : "r" (a6)
  : "a0","a1","d0","d1", "memory");
}
static __inline void
FreeMem (void * memoryBlock,unsigned long byteSize)
{
  register struct ExecBase *a6 __asm("a6") = SysBase;
  register void *a1 __asm("a1") = memoryBlock;
  register unsigned long d0 __asm("d0") = byteSize;
  __asm __volatile ("jsr a6@(-0xd2)"
  : /* no output */
  : "r" (a6), "r" (a1), "r" (d0)
  : "a0","a1","d0","d1", "memory");
}
static __inline struct Library *
OpenLibrary (char *libName,unsigned long version)
{
  register struct Library * _res  __asm("d0");
  register struct ExecBase *a6 __asm("a6") = SysBase;
  register u_char *a1 __asm("a1") = libName;
  register unsigned long d0 __asm("d0") = version;
  __asm __volatile ("jsr a6@(-0x228)"
  : "=r" (_res)
  : "r" (a6), "r" (a1), "r" (d0)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline void *
SuperState (void)
{
  register void  *_res	__asm("d0");
  register struct ExecBase *a6 __asm("a6") = SysBase;
  __asm __volatile ("jsr a6@(-0x96)"
  : "=r" (_res)
  : "r" (a6)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline void 
CacheClearU (void)
{
  register struct ExecBase *a6 __asm("a6") = SysBase;
  __asm __volatile ("jsr a6@(-0x27c)"
  : /* no output */
  : "r" (a6)
  : "a0","a1","d0","d1", "memory");
}
static __inline unsigned long 
CacheControl (unsigned long cacheBits,unsigned long cacheMask)
{
  register unsigned long  _res  __asm("d0");
  register struct ExecBase *a6 __asm("a6") = SysBase;
  register unsigned long d0 __asm("d0") = cacheBits;
  register unsigned long d1 __asm("d1") = cacheMask;
  __asm __volatile ("jsr a6@(-0x288)"
  : "=r" (_res)
  : "r" (a6), "r" (d0), "r" (d1)
  : "a0","a1","d0","d1", "memory");
  return _res;
}
static __inline unsigned long
Supervisor (unsigned long (*userfunc)())
{
  register unsigned long _res __asm("d0");
  register struct ExecBase *a6 __asm("a6") = SysBase;
  register unsigned long (*a0)() __asm("a0") = userfunc;
	/* gcc doesn't seem to like asm parameters in a5 */
  __asm __volatile ("movel a5,sp@-;movel a0,a5;jsr a6@(-0x1e);movel sp@+,a5"
  : "=r" (_res)
  : "r" (a6), "r" (a0)
  : "a0","a1","d0","d1","memory");
  return _res;
}


struct ExpansionBase;
extern struct ExpansionBase *ExpansionBase;

static __inline struct ConfigDev *
FindConfigDev (struct ConfigDev *oldConfigDev,long manufacturer,long product)
{
  register struct ConfigDev * _res  __asm("d0");
  register struct ExpansionBase* a6 __asm("a6") = ExpansionBase;
  register struct ConfigDev *a0 __asm("a0") = oldConfigDev;
  register long d0 __asm("d0") = manufacturer;
  register long d1 __asm("d1") = product;
  __asm __volatile ("jsr a6@(-0x48)"
  : "=r" (_res)
  : "r" (a6), "r" (a0), "r" (d0), "r" (d1)
  : "a0","a1","d0","d1", "memory");
  return _res;
}

struct GfxBase;
extern struct GfxBase *GfxBase;
struct View;
static __inline void 
LoadView (struct View *view)
{
  register struct GfxBase* a6 __asm("a6") = GfxBase;
  register struct View *a1 __asm("a1") = view;
  __asm __volatile ("jsr a6@(-0xde)"
  : /* no output */
  : "r" (a6), "r" (a1)
  : "a0","a1","d0","d1", "memory");
}

static __inline void change_stack (char *stackp)
{
    __asm__ volatile ("movel %0,sp\n\t" :: "g" (stackp) : "sp");
}

static __inline void disable_cache (void)
{
    __asm__ volatile ("movec %0,cacr" :: "d" (0));
}

static __inline void disable_mmu (void)
{
    if (SysBase->AttnFlags & AFF_68040)
	    __asm__ volatile ("moveq #0,d0;"
			      ".long 0x4e7b0003;"	/* movec d0,tc */
			      ".long 0x4e7b0004;"	/* movec d0,itt0 */
			      ".long 0x4e7b0005;"	/* movec d0,itt1 */
			      ".long 0x4e7b0006;"	/* movec d0,dtt0 */
			      ".long 0x4e7b0007"	/* movec d0,dtt1 */
			      : /* no outputs */
			      : /* no inputs */
			      : "d0");
    else {
	    __asm__ volatile ("subl  #4,sp;"
			      "pmove tc,sp@;"
			      "bclr  #7,sp@;"
			      "pmove sp@,tc;"
			      "addl  #4,sp");
	    if (SysBase->AttnFlags & AFF_68030)
		    __asm__ volatile ("clrl  sp@-;"
				      ".long 0xf0170800;" /* pmove sp@,tt0 */
				      ".long 0xf0170c00;" /* pmove sp@,tt1 */
				      "addql #4,sp");
    }
}

static __inline void jump_to (unsigned long addr)
{
    __asm__ volatile ("jmp %0@" :: "a" (addr));
    /* NOTREACHED */
}

#endif /* BOOTSTRAP_H */
