#ifndef _LINUX_SHM_H_
#define _LINUX_SHM_H_

#include <linux/ipc.h>

#include <asm/shmparam.h>

struct shmid_ds {
	struct ipc_perm shm_perm;	/* operation perms */
	int	shm_segsz;		/* size of segment (bytes) */
	time_t	shm_atime;		/* last attach time */
	time_t	shm_dtime;		/* last detach time */
	time_t	shm_ctime;		/* last change time */
	unsigned short	shm_cpid;	/* pid of creator */
	unsigned short	shm_lpid;	/* pid of last operator */
	short	shm_nattch;		/* no. of current attaches */
	/* the following are private */
	unsigned short   shm_npages;	/* size of segment (pages) */
	unsigned long   *shm_pages;	/* array of ptrs to frames -> SHMMAX */ 
	struct vm_area_struct *attaches; /* descriptors for attaches */
};

/* permission flag for shmget */
#define SHM_R		0400	/* or S_IRUGO from <linux/stat.h> */
#define SHM_W		0200	/* or S_IWUGO from <linux/stat.h> */

/* mode for attach */
#define	SHM_RDONLY	010000	/* read-only access */
#define	SHM_RND		020000	/* round attach address to SHMLBA boundary */
#define	SHM_REMAP	040000	/* take-over region on attach */

/* super user shmctl commands */
#define SHM_LOCK 	11
#define SHM_UNLOCK 	12

struct	shminfo {
    int shmmax;	
    int shmmin;	
    int shmmni;	
    int shmseg;	
    int shmall;	
};

#ifdef __KERNEL__

/* shm_mode upper byte flags */
#define	SHM_DEST	01000	/* segment will be destroyed on last detach */
#define SHM_LOCKED      02000   /* segment will not be swapped */

/* ipcs ctl commands */
#define SHM_STAT 	13
#define SHM_INFO 	14
struct shm_info {
	int   used_ids;
	ulong shm_tot; /* total allocated shm */
	ulong shm_rss; /* total resident shm */
	ulong shm_swp; /* total swapped shm */
	ulong swap_attempts;
	ulong swap_successes;
};

asmlinkage int sys_shmget (key_t key, int size, int flag);
asmlinkage int sys_shmat (int shmid, char *shmaddr, int shmflg, ulong *addr);
asmlinkage int sys_shmdt (char *shmaddr);
asmlinkage int sys_shmctl (int shmid, int cmd, struct shmid_ds *buf);

#endif /* __KERNEL__ */

#endif /* _LINUX_SHM_H_ */

