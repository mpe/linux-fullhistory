#ifndef __ALPHA_MMAN_H__
#define __ALPHA_MMAN_H__

#define PROT_READ	0x1		/* page can be read */
#define PROT_WRITE	0x2		/* page can be written */
#define PROT_EXEC	0x4		/* page can be executed */
#define PROT_NONE	0x0		/* page can not be accessed */

#define MAP_SHARED	0x01		/* Share changes */
#define MAP_PRIVATE	0x02		/* Changes are private */
#define MAP_TYPE	0x0f		/* Mask for type of mapping (OSF/1 is _wrong_) */
#define MAP_FIXED	0x100		/* Interpret addr exactly */
#define MAP_ANONYMOUS	0x10		/* don't use a file */

/* not used by linux, but here to make sure we don't clash with OSF/1 defines */
#define MAP_HASSEMAPHORE 0x0200
#define MAP_INHERIT	0x0400
#define MAP_UNALIGNED	0x0800

/* These are linux-specific */
#define MAP_GROWSDOWN	0x1000		/* stack-like segment */
#define MAP_DENYWRITE	0x2000		/* ETXTBSY */
#define MAP_EXECUTABLE	0x4000		/* mark it as a executable */
#define MAP_LOCKED	0x8000		/* lock the mapping */

#define MS_ASYNC	1		/* sync memory asynchronously */
#define MS_SYNC		2		/* synchronous memory sync */
#define MS_INVALIDATE	4		/* invalidate the caches */

#define MCL_CURRENT	 8192		/* lock all currently mapped pages */
#define MCL_FUTURE	16384		/* lock all additions to address space */

/* compatibility flags */
#define MAP_ANON	MAP_ANONYMOUS
#define MAP_FILE	0

#endif /* __ALPHA_MMAN_H__ */
