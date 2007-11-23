#ifndef _LINUX_MMAN_H
#define _LINUX_MMAN_H

#define PROT_READ        0x1       /* page can be read */
#define PROT_WRITE       0x2       /* page can be written */
#define PROT_EXEC        0x4       /* page can be executed */
#define PROT_NONE        0x0       /* page can not be accessed */

#define MAP_SHARED       1         /* Share changes */
#define MAP_PRIVATE      2         /* Changes are private */
#define MAP_TYPE         0xf       /* Mask for type of mapping */
#define MAP_FIXED        0x10      /* Interpret addr exactly */
#define MAP_ANONYMOUS    0x20      /* don't use a file */

#define MAP_GROWSDOWN	0x0100		/* stack-like segment */
#define MAP_DENYWRITE	0x0800		/* ETXTBSY */
#define MAP_EXECUTABLE	0x1000		/* mark it as a executable */

#define MS_ASYNC	1	/* sync memory asynchronously */
#define MS_INVALIDATE	2	/* invalidate the caches */
#define MS_SYNC		4	/* synchronous memory sync */

#endif /* _LINUX_MMAN_H */
