/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1992-1999,2001-2004 Silicon Graphics, Inc. All rights reserved.
 */

#ifndef _ASM_IA64_SN_ADDRS_H
#define _ASM_IA64_SN_ADDRS_H

#include <asm/percpu.h>
#include <asm/sn/types.h>
#include <asm/sn/pda.h>

/*
 *  Memory/SHUB Address Format:
 *  +-+---------+--+--------------+
 *  |0|  NASID  |AS| NodeOffset   |
 *  +-+---------+--+--------------+
 *
 *  NASID: (low NASID bit is 0) Memory and SHUB MMRs
 *   AS: 2-bit Address Space Identifier. Used only if low NASID bit is 0
 *     00: Local Resources and MMR space
 *           Top bit of NodeOffset
 *               0: Local resources space
 *                  node id:
 *                        0: IA64/NT compatibility space
 *                        2: Local MMR Space
 *                        4: Local memory, regardless of local node id
 *               1: Global MMR space
 *     01: GET space.
 *     10: AMO space.
 *     11: Cacheable memory space.
 *
 *   NodeOffset: byte offset
 *
 *
 *  TIO address format:
 *  +-+----------+--+--------------+
 *  |0|  NASID   |AS| Nodeoffset   |
 *  +-+----------+--+--------------+
 *
 *  NASID: (low NASID bit is 1) TIO
 *   AS: 2-bit Chiplet Identifier
 *     00: TIO LB (Indicates TIO MMR access.)
 *     01: TIO ICE (indicates coretalk space access.)
 * 
 *   NodeOffset: top bit must be set.
 *
 *
 * Note that in both of the above address formats, the low
 * NASID bit indicates if the reference is to the SHUB or TIO MMRs.
 */


/*
 * Define basic shift & mask constants for manipulating NASIDs and AS values.
 */
#define NASID_BITMASK		(pda->nasid_bitmask)
#define NASID_SHIFT		(pda->nasid_shift)
#define AS_SHIFT		(pda->as_shift)
#define AS_BITMASK		0x3UL

#define NASID_MASK              ((u64)NASID_BITMASK << NASID_SHIFT)
#define AS_MASK			((u64)AS_BITMASK << AS_SHIFT)
#define REGION_BITS		0xe000000000000000UL


/*
 * AS values. These are the same on both SHUB1 & SHUB2.
 */
#define AS_GET_VAL		1UL
#define AS_AMO_VAL		2UL
#define AS_CAC_VAL		3UL
#define AS_GET_SPACE		(AS_GET_VAL << AS_SHIFT)
#define AS_AMO_SPACE		(AS_AMO_VAL << AS_SHIFT)
#define AS_CAC_SPACE		(AS_CAC_VAL << AS_SHIFT)


/*
 * Base addresses for various address ranges.
 */
#define CACHED			0xe000000000000000UL
#define UNCACHED                0xc000000000000000UL
#define UNCACHED_PHYS           0x8000000000000000UL


/* 
 * Virtual Mode Local & Global MMR space.  
 */
#define SH1_LOCAL_MMR_OFFSET	0x8000000000UL
#define SH2_LOCAL_MMR_OFFSET	0x0200000000UL
#define LOCAL_MMR_OFFSET	(is_shub2() ? SH2_LOCAL_MMR_OFFSET : SH1_LOCAL_MMR_OFFSET)
#define LOCAL_MMR_SPACE		(UNCACHED | LOCAL_MMR_OFFSET)
#define LOCAL_PHYS_MMR_SPACE	(UNCACHED_PHYS | LOCAL_MMR_OFFSET)

#define SH1_GLOBAL_MMR_OFFSET	0x0800000000UL
#define SH2_GLOBAL_MMR_OFFSET	0x0300000000UL
#define GLOBAL_MMR_OFFSET	(is_shub2() ? SH2_GLOBAL_MMR_OFFSET : SH1_GLOBAL_MMR_OFFSET)
#define GLOBAL_MMR_SPACE	(UNCACHED | GLOBAL_MMR_OFFSET)

/*
 * Physical mode addresses
 */
#define GLOBAL_PHYS_MMR_SPACE	(UNCACHED_PHYS | GLOBAL_MMR_OFFSET)


/*
 * Clear region & AS bits.
 */
#define TO_PHYS_MASK		(~(REGION_BITS | AS_MASK))


/*
 * Misc NASID manipulation.
 */
#define NASID_SPACE(n)		((u64)(n) << NASID_SHIFT)
#define REMOTE_ADDR(n,a)	(NASID_SPACE(n) | (a))
#define NODE_OFFSET(x)		((x) & (NODE_ADDRSPACE_SIZE - 1))
#define NODE_ADDRSPACE_SIZE     (1UL << AS_SHIFT)
#define NASID_GET(x)		(int) (((u64) (x) >> NASID_SHIFT) & NASID_BITMASK)
#define LOCAL_MMR_ADDR(a)	(LOCAL_MMR_SPACE | (a))
#define GLOBAL_MMR_ADDR(n,a)	(GLOBAL_MMR_SPACE | REMOTE_ADDR(n,a))
#define GLOBAL_MMR_PHYS_ADDR(n,a) (GLOBAL_PHYS_MMR_SPACE | REMOTE_ADDR(n,a))
#define GLOBAL_CAC_ADDR(n,a)	(CAC_BASE | REMOTE_ADDR(n,a))
#define CHANGE_NASID(n,x)	((void *)(((u64)(x) & ~NASID_MASK) | NASID_SPACE(n)))


/* non-II mmr's start at top of big window space (4G) */
#define BWIN_TOP		0x0000000100000000UL

/*
 * general address defines
 */
#define CAC_BASE		(CACHED   | AS_CAC_SPACE)
#define AMO_BASE		(UNCACHED | AS_AMO_SPACE)
#define GET_BASE		(CACHED   | AS_GET_SPACE)

/*
 * Convert Memory addresses between various addressing modes.
 */
#define TO_PHYS(x)		(TO_PHYS_MASK & (x))
#define TO_CAC(x)		(CAC_BASE     | TO_PHYS(x))
#define TO_AMO(x)		(AMO_BASE     | TO_PHYS(x))
#define TO_GET(x)		(GET_BASE     | TO_PHYS(x))


/*
 * Covert from processor physical address to II/TIO physical address:
 *	II - squeeze out the AS bits
 *	TIO- requires a chiplet id in bits 38-39.  For DMA to memory,
 *           the chiplet id is zero.  If we implement TIO-TIO dma, we might need
 *           to insert a chiplet id into this macro.  However, it is our belief
 *           right now that this chiplet id will be ICE, which is also zero.
 */
#define PHYS_TO_TIODMA(x)	( (((u64)(x) & NASID_MASK) << 2) | NODE_OFFSET(x))
#define PHYS_TO_DMA(x)          ( (((u64)(x) & NASID_MASK) >> 2) | NODE_OFFSET(x))


/*
 * The following definitions pertain to the IO special address
 * space.  They define the location of the big and little windows
 * of any given node.
 */
#define BWIN_SIZE_BITS			29	/* big window size: 512M */
#define TIO_BWIN_SIZE_BITS		30	/* big window size: 1G */
#define NODE_SWIN_BASE(n, w)		((w == 0) ? NODE_BWIN_BASE((n), SWIN0_BIGWIN) \
		: RAW_NODE_SWIN_BASE(n, w))
#define NODE_IO_BASE(n)			(GLOBAL_MMR_SPACE | NASID_SPACE(n))
#define BWIN_SIZE			(1UL << BWIN_SIZE_BITS)
#define NODE_BWIN_BASE0(n)		(NODE_IO_BASE(n) + BWIN_SIZE)
#define NODE_BWIN_BASE(n, w)		(NODE_BWIN_BASE0(n) + ((u64) (w) << BWIN_SIZE_BITS))
#define RAW_NODE_SWIN_BASE(n, w)	(NODE_IO_BASE(n) + ((u64) (w) << SWIN_SIZE_BITS))
#define BWIN_WIDGET_MASK		0x7
#define BWIN_WINDOWNUM(x)		(((x) >> BWIN_SIZE_BITS) & BWIN_WIDGET_MASK)

#define TIO_BWIN_WINDOW_SELECT_MASK	0x7
#define TIO_BWIN_WINDOWNUM(x)		(((x) >> TIO_BWIN_SIZE_BITS) & TIO_BWIN_WINDOW_SELECT_MASK)



/*
 * The following definitions pertain to the IO special address
 * space.  They define the location of the big and little windows
 * of any given node.
 */

#define SWIN_SIZE_BITS			24
#define	SWIN_WIDGET_MASK		0xF

#define TIO_SWIN_SIZE_BITS		28
#define TIO_SWIN_SIZE			(1UL << TIO_SWIN_SIZE_BITS)
#define TIO_SWIN_WIDGET_MASK		0x3

/*
 * Convert smallwindow address to xtalk address.
 *
 * 'addr' can be physical or virtual address, but will be converted
 * to Xtalk address in the range 0 -> SWINZ_SIZEMASK
 */
#define	SWIN_WIDGETNUM(x)		(((x)  >> SWIN_SIZE_BITS) & SWIN_WIDGET_MASK)
#define TIO_SWIN_WIDGETNUM(x)		(((x)  >> TIO_SWIN_SIZE_BITS) & TIO_SWIN_WIDGET_MASK)


/*
 * The following macros produce the correct base virtual address for
 * the hub registers. The REMOTE_HUB_* macro produce
 * the address for the specified hub's registers.  The intent is
 * that the appropriate PI, MD, NI, or II register would be substituted
 * for x.
 *
 *   WARNING:
 *	When certain Hub chip workaround are defined, it's not sufficient
 *	to dereference the *_HUB_ADDR() macros.  You should instead use
 *	HUB_L() and HUB_S() if you must deal with pointers to hub registers.
 *	Otherwise, the recommended approach is to use *_HUB_L() and *_HUB_S().
 *	They're always safe.
 */
#define REMOTE_HUB_ADDR(n,x)						\
	((n & 1) ?							\
	/* TIO: */							\
	((volatile u64 *)(GLOBAL_MMR_ADDR(n,x)))			\
	: /* SHUB: */							\
	(((x) & BWIN_TOP) ? ((volatile u64 *)(GLOBAL_MMR_ADDR(n,x)))\
	: ((volatile u64 *)(NODE_SWIN_BASE(n,1) + 0x800000 + (x)))))



#define HUB_L(x)			(*((volatile typeof(*x) *)x))
#define	HUB_S(x,d)			(*((volatile typeof(*x) *)x) = (d))

#define REMOTE_HUB_L(n, a)		HUB_L(REMOTE_HUB_ADDR((n), (a)))
#define REMOTE_HUB_S(n, a, d)		HUB_S(REMOTE_HUB_ADDR((n), (a)), (d))


#endif /* _ASM_IA64_SN_ADDRS_H */
