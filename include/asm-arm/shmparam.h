#ifndef _ASMARM_SHMPARAM_H
#define _ASMARM_SHMPARAM_H

/*
 * Include the machine specific shm parameters before the processor
 * dependent parameters so that the machine parameters can override
 * the processor parameters
 */
#include <asm/arch/shmparam.h>
#include <asm/proc/shmparam.h>

/*
 * Format of a swap-entry for shared memory pages currently out in
 * swap space (see also mm/swap.c).
 *
 * SWP_TYPE = SHM_SWP_TYPE
 * SWP_OFFSET is used as follows:
 *
 *  bits 0..6 : id of shared memory segment page belongs to (SHM_ID)
 *  bits 7..21: index of page within shared memory segment (SHM_IDX)
 *		(actually fewer bits get used since SHMMAX is so low)
 */

/*
 * Keep _SHM_ID_BITS as low as possible since SHMMNI depends on it and
 * there is a static array of size SHMMNI.
 */
#define _SHM_ID_BITS	7
#define SHM_ID_MASK	((1<<_SHM_ID_BITS)-1)

#define SHM_IDX_SHIFT	(_SHM_ID_BITS)
#define _SHM_IDX_BITS	15
#define SHM_IDX_MASK	((1<<_SHM_IDX_BITS)-1)

/*
 * _SHM_ID_BITS + _SHM_IDX_BITS must be <= 24 on the i386 and
 * SHMMAX <= (PAGE_SIZE << _SHM_IDX_BITS).
 */

#define SHMMIN 1 /* really PAGE_SIZE */	/* min shared seg size (bytes) */
#define SHMMNI (1<<_SHM_ID_BITS)	/* max num of segs system wide */
#define SHMALL				/* max shm system wide (pages) */ \
	(1<<(_SHM_IDX_BITS+_SHM_ID_BITS))
#define	SHMLBA PAGE_SIZE		/* attach addr a multiple of this */
#define SHMSEG SHMMNI			/* max shared segs per process */

#endif /* _ASMARM_SHMPARAM_H */
