#ifndef _MMAN_H
#define _MMAN_H

#define PROT_READ        0x1       /* page can be read */
#define PROT_WRITE       0x2       /* page can be written */
#define PROT_EXEC        0x4       /* page can be executed */
#define PROT_NONE        0x0       /* page can not be accessed */

#define MAP_SHARED       1         /* Share changes */
#define MAP_PRIVATE      2         /* Changes are private */
#define MAP_TYPE         0xf       /* Mask for type of mapping */
#define MAP_FIXED        0x10      /* Interpret addr exactly */

extern caddr_t mmap(caddr_t addr, size_t len, int prot, int flags, int fd,
		    off_t off);
extern int munmap(caddr_t addr, size_t len);

#endif /* _MMAN_H */
