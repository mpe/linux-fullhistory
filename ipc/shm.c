/*
 * linux/ipc/shm.c
 * Copyright (C) 1992, 1993 Krishna Balasubramanian
 *	 Many improvements/fixes by Bruno Haible.
 * Replaced `struct shm_desc' by `struct vm_area_struct', July 1994.
 * Fixed the shm swap deallocation (shm_unuse()), August 1998 Andrea Arcangeli.
 *
 * /proc/sysvipc/shm support (c) 1999 Dragos Acostachioaie <dragos@iname.com>
 * BIGMEM support, Andrea Arcangeli <andrea@suse.de>
 * SMP thread shm, Jean-Luc Boyard <jean-luc.boyard@siemens.fr>
 * HIGHMEM support, Ingo Molnar <mingo@redhat.com>
 * avoid vmalloc and make shmmax, shmall, shmmni sysctl'able,
 *                         Christoph Rohland <hans-christoph.rohland@sap.com>
 * Shared /dev/zero support, Kanoj Sarcar <kanoj@sgi.com>
 */

#include <linux/config.h>
#include <linux/malloc.h>
#include <linux/shm.h>
#include <linux/swap.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <linux/vmalloc.h>
#include <linux/pagemap.h>
#include <linux/proc_fs.h>
#include <linux/highmem.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>

#include "util.h"

struct shmid_kernel /* private to the kernel */
{	
	struct kern_ipc_perm	shm_perm;
	size_t			shm_segsz;
	time_t			shm_atime;
	time_t			shm_dtime;
	time_t			shm_ctime;
	pid_t			shm_cpid;
	pid_t			shm_lpid;
	unsigned long		shm_nattch;
	unsigned long		shm_npages; /* size of segment (pages) */
	pte_t			**shm_dir;  /* ptr to array of ptrs to frames -> SHMMAX */ 
	struct vm_area_struct	*attaches;  /* descriptors for attaches */
	int                     id; /* backreference to id for shm_close */
	struct semaphore sem;
};

static struct ipc_ids shm_ids;

#define shm_lock(id)	((struct shmid_kernel*)ipc_lock(&shm_ids,id))
#define shm_unlock(id)	ipc_unlock(&shm_ids,id)
#define shm_lockall()	ipc_lockall(&shm_ids)
#define shm_unlockall()	ipc_unlockall(&shm_ids)
#define shm_get(id)	((struct shmid_kernel*)ipc_get(&shm_ids,id))
#define shm_rmid(id)	((struct shmid_kernel*)ipc_rmid(&shm_ids,id))
#define shm_checkid(s, id)	\
	ipc_checkid(&shm_ids,&s->shm_perm,id)
#define shm_buildid(id, seq) \
	ipc_buildid(&shm_ids, id, seq)

static int newseg (key_t key, int shmflg, size_t size);
static int shm_map (struct vm_area_struct *shmd);
static void killseg (int shmid);
static void shm_open (struct vm_area_struct *shmd);
static void shm_close (struct vm_area_struct *shmd);
static struct page * shm_nopage(struct vm_area_struct *, unsigned long, int);
static int shm_swapout(struct page *, struct file *);
#ifdef CONFIG_PROC_FS
static int sysvipc_shm_read_proc(char *buffer, char **start, off_t offset, int length, int *eof, void *data);
#endif

static void zshm_swap (int prio, int gfp_mask, zone_t *zone);
static void zmap_unuse(swp_entry_t entry, struct page *page);
static void shmzero_open(struct vm_area_struct *shmd);
static void shmzero_close(struct vm_area_struct *shmd);
static int zero_id;
static struct shmid_kernel zshmid_kernel;

size_t shm_ctlmax = SHMMAX;
int shm_ctlall = SHMALL;
int shm_ctlmni = SHMMNI;

static int shm_tot = 0; /* total number of shared memory pages */
static int shm_rss = 0; /* number of shared memory pages that are in memory */
static int shm_swp = 0; /* number of shared memory pages that are in swap */

/* locks order:
	pagecache_lock
	shm_lock()/shm_lockall()
	kernel lock
	shp->sem
	sem_ids.sem
	mmap_sem

  SMP assumptions:
  - swap_free() never sleeps
  - add_to_swap_cache() never sleeps
  - add_to_swap_cache() doesn't acquire the big kernel lock.
  - shm_unuse() is called with the kernel lock acquired.
 */

/* some statistics */
static ulong swap_attempts = 0;
static ulong swap_successes = 0;

void __init shm_init (void)
{
	ipc_init_ids(&shm_ids, shm_ctlmni);
#ifdef CONFIG_PROC_FS
	create_proc_read_entry("sysvipc/shm", 0, 0, sysvipc_shm_read_proc, NULL);
#endif
	zero_id = ipc_addid(&shm_ids, &zshmid_kernel.shm_perm, shm_ctlmni);
	shm_unlock(zero_id);
	return;
}

#define SHM_ENTRY(shp, index) (shp)->shm_dir[(index)/PTRS_PER_PTE][(index)%PTRS_PER_PTE]

static pte_t **shm_alloc(unsigned long pages)
{
	unsigned short dir  = pages / PTRS_PER_PTE;
	unsigned short last = pages % PTRS_PER_PTE;
	pte_t **ret, **ptr;

	ret = kmalloc ((dir+1) * sizeof(pte_t *), GFP_KERNEL);
	if (!ret)
		goto out;

	for (ptr = ret; ptr < ret+dir ; ptr++)
	{
		*ptr = (pte_t *)__get_free_page (GFP_KERNEL);
		if (!*ptr)
			goto free;
		memset (*ptr, 0, PAGE_SIZE); 
	}

	/* The last one is probably not of PAGE_SIZE: we use kmalloc */
	if (last) {
		*ptr = kmalloc (last*sizeof(pte_t), GFP_KERNEL);
		if (!*ptr)
			goto free;
		memset (*ptr, 0, last*sizeof(pte_t));
	}
out:	
	return ret;

free:
	/* The last failed: we decrement first */
	while (--ptr >= ret)
		free_page ((unsigned long)*ptr);

	kfree (ret);
	return NULL;
}


static void shm_free(pte_t** dir, unsigned long pages)
{
	pte_t **ptr = dir+pages/PTRS_PER_PTE;

	/* first the last page */
	if (pages%PTRS_PER_PTE)
		kfree (*ptr);
	/* now the whole pages */
	while (--ptr >= dir)
		free_page ((unsigned long)*ptr);

	/* Now the indirect block */
	kfree (dir);
}

static int shm_revalidate(struct shmid_kernel* shp, int shmid, int pagecount, int flg)
{
	struct shmid_kernel* new;
	new = shm_lock(shmid);
	if(new==NULL) {
		return -EIDRM;
	}
	if(new!=shp || shm_checkid(shp, shmid) || shp->shm_npages != pagecount) {
		shm_unlock(shmid);
		return -EIDRM;
	}
	if (ipcperms(&shp->shm_perm, flg)) {
		shm_unlock(shmid);
		return -EACCES;
	}
	return 0;
}

static inline struct shmid_kernel *newseg_alloc(int numpages)
{
	struct shmid_kernel *shp;

	shp = (struct shmid_kernel *) kmalloc (sizeof (*shp), GFP_KERNEL);
	if (!shp)
		return 0;

	shp->shm_dir = shm_alloc (numpages);
	if (!shp->shm_dir) {
		kfree(shp);
		return 0;
	}
	shp->shm_npages = numpages;
	shp->attaches = NULL;
	shp->shm_nattch = 0;
	init_MUTEX(&shp->sem);
	return(shp);
}

static int newseg (key_t key, int shmflg, size_t size)
{
	struct shmid_kernel *shp;
	int numpages = (size + PAGE_SIZE -1) >> PAGE_SHIFT;
	int id;

	if (size < SHMMIN)
		return -EINVAL;

	if (size > shm_ctlmax)
		return -EINVAL;
	if (shm_tot + numpages >= shm_ctlall)
		return -ENOSPC;

	if (!(shp = newseg_alloc(numpages)))
		return -ENOMEM;
	id = ipc_addid(&shm_ids, &shp->shm_perm, shm_ctlmni);
	if(id == -1) {
		shm_free(shp->shm_dir,numpages);
		kfree(shp);
		return -ENOSPC;
	}
	shp->shm_perm.key = key;
	shp->shm_perm.mode = (shmflg & S_IRWXUGO);
	shp->shm_segsz = size;
	shp->shm_cpid = current->pid;
	shp->shm_lpid = 0;
	shp->shm_atime = shp->shm_dtime = 0;
	shp->shm_ctime = CURRENT_TIME;
	shp->id = shm_buildid(id,shp->shm_perm.seq);

	shm_tot += numpages;
	shm_unlock(id);

	return shm_buildid(id,shp->shm_perm.seq);
}

asmlinkage long sys_shmget (key_t key, size_t size, int shmflg)
{
	struct shmid_kernel *shp;
	int err, id = 0;

	down(&shm_ids.sem);
	if (key == IPC_PRIVATE) {
		err = newseg(key, shmflg, size);
	} else if ((id = ipc_findkey(&shm_ids,key)) == -1) {
		if (!(shmflg & IPC_CREAT))
			err = -ENOENT;
		else
			err = newseg(key, shmflg, size);
	} else if ((shmflg & IPC_CREAT) && (shmflg & IPC_EXCL)) {
		err = -EEXIST;
	} else {
		shp = shm_lock(id);
		if(shp==NULL)
			BUG();
		if (ipcperms(&shp->shm_perm, shmflg))
			err = -EACCES;
		else
			err = shm_buildid(id, shp->shm_perm.seq);
		shm_unlock(id);
	}
	up(&shm_ids.sem);
	return err;
}

static void killseg_core(struct shmid_kernel *shp, int doacc)
{
	int i, numpages, rss, swp;

	numpages = shp->shm_npages;
	for (i = 0, rss = 0, swp = 0; i < numpages ; i++) {
		pte_t pte;
		pte = SHM_ENTRY (shp,i);
		if (pte_none(pte))
			continue;
		if (pte_present(pte)) {
			__free_page (pte_page(pte));
			rss++;
		} else {
			swap_free(pte_to_swp_entry(pte));
			swp++;
		}
	}
	shm_free (shp->shm_dir, numpages);
	kfree(shp);
	if (doacc) {
		shm_lockall();
		shm_rss -= rss;
		shm_swp -= swp;
		shm_tot -= numpages;
		shm_unlockall();
	}
}

/*
 * Only called after testing nattch and SHM_DEST.
 * Here pages, pgtable and shmid_kernel are freed.
 */
static void killseg (int shmid)
{
	struct shmid_kernel *shp;

	down(&shm_ids.sem);
	shp = shm_lock(shmid);
	if(shp==NULL) {
out_up:
		up(&shm_ids.sem);
		return;
	}
	if(shm_checkid(shp,shmid) || shp->shm_nattch > 0 ||
	    !(shp->shm_perm.mode & SHM_DEST)) {
		shm_unlock(shmid);
		goto out_up;
	}
	shp = shm_rmid(shmid);
	if(shp==NULL)
		BUG();
	if (!shp->shm_dir)
		BUG();
	shm_unlock(shmid);
	up(&shm_ids.sem);
	killseg_core(shp, 1);

	return;
}

static inline unsigned long copy_shmid_to_user(void *buf, struct shmid64_ds *in, int version)
{
	switch(version) {
	case IPC_64:
		return copy_to_user(buf, in, sizeof(*in));
	case IPC_OLD:
	    {
		struct shmid_ds out;

		ipc64_perm_to_ipc_perm(&in->shm_perm, &out.shm_perm);
		out.shm_segsz	= in->shm_segsz;
		out.shm_atime	= in->shm_atime;
		out.shm_dtime	= in->shm_dtime;
		out.shm_ctime	= in->shm_ctime;
		out.shm_cpid	= in->shm_cpid;
		out.shm_lpid	= in->shm_lpid;
		out.shm_nattch	= in->shm_nattch;

		return copy_to_user(buf, &out, sizeof(out));
	    }
	default:
		return -EINVAL;
	}
}

struct shm_setbuf {
	uid_t	uid;
	gid_t	gid;
	mode_t	mode;
};	

static inline unsigned long copy_shmid_from_user(struct shm_setbuf *out, void *buf, int version)
{
	switch(version) {
	case IPC_64:
	    {
		struct shmid64_ds tbuf;

		if (copy_from_user(&tbuf, buf, sizeof(tbuf)))
			return -EFAULT;

		out->uid	= tbuf.shm_perm.uid;
		out->gid	= tbuf.shm_perm.gid;
		out->mode	= tbuf.shm_perm.mode;

		return 0;
	    }
	case IPC_OLD:
	    {
		struct shmid_ds tbuf_old;

		if (copy_from_user(&tbuf_old, buf, sizeof(tbuf_old)))
			return -EFAULT;

		out->uid	= tbuf_old.shm_perm.uid;
		out->gid	= tbuf_old.shm_perm.gid;
		out->mode	= tbuf_old.shm_perm.mode;

		return 0;
	    }
	default:
		return -EINVAL;
	}
}

static inline unsigned long copy_shminfo_to_user(void *buf, struct shminfo64 *in, int version)
{
	switch(version) {
	case IPC_64:
		return copy_to_user(buf, in, sizeof(*in));
	case IPC_OLD:
	    {
		struct shminfo out;

		if(in->shmmax > INT_MAX)
			out.shmmax = INT_MAX;
		else
			out.shmmax = (int)in->shmmax;

		out.shmmin	= in->shmmin;
		out.shmmni	= in->shmmni;
		out.shmseg	= in->shmseg;
		out.shmall	= in->shmall; 

		return copy_to_user(buf, &out, sizeof(out));
	    }
	default:
		return -EINVAL;
	}
}

asmlinkage long sys_shmctl (int shmid, int cmd, struct shmid_ds *buf)
{
	struct shm_setbuf setbuf;
	struct shmid_kernel *shp;
	int err, version;

	if (cmd < 0 || shmid < 0)
		return -EINVAL;

	version = ipc_parse_version(&cmd);

	switch (cmd) { /* replace with proc interface ? */
	case IPC_INFO:
	{
		struct shminfo64 shminfo;

		memset(&shminfo,0,sizeof(shminfo));
		shminfo.shmmni = shminfo.shmseg = shm_ctlmni;
		shminfo.shmmax = shm_ctlmax;
		shminfo.shmall = shm_ctlall;

		shminfo.shmmin = SHMMIN;
		if(copy_shminfo_to_user (buf, &shminfo, version))
			return -EFAULT;
		/* reading a integer is always atomic */
		err= shm_ids.max_id;
		if(err<0)
			err = 0;
		return err;
	}
	case SHM_INFO:
	{
		struct shm_info shm_info;

		memset(&shm_info,0,sizeof(shm_info));
		shm_lockall();
		shm_info.used_ids = shm_ids.in_use;
		shm_info.shm_rss = shm_rss;
		shm_info.shm_tot = shm_tot;
		shm_info.shm_swp = shm_swp;
		shm_info.swap_attempts = swap_attempts;
		shm_info.swap_successes = swap_successes;
		err = shm_ids.max_id;
		shm_unlockall();
		if(copy_to_user (buf, &shm_info, sizeof(shm_info)))
			return -EFAULT;

		return err < 0 ? 0 : err;
	}
	case SHM_STAT:
	case IPC_STAT:
	{
		struct shmid64_ds tbuf;
		int result;
		memset(&tbuf, 0, sizeof(tbuf));
		shp = shm_lock(shmid);
		if(shp==NULL)
			return -EINVAL;
		if (shp == &zshmid_kernel) {
			shm_unlock(shmid);
			return -EINVAL;
		}
		if(cmd==SHM_STAT) {
			err = -EINVAL;
			if (shmid > shm_ids.max_id)
				goto out_unlock;
			result = shm_buildid(shmid, shp->shm_perm.seq);
		} else {
			err = -EIDRM;
			if(shm_checkid(shp,shmid))
				goto out_unlock;
			result = 0;
		}
		err=-EACCES;
		if (ipcperms (&shp->shm_perm, S_IRUGO))
			goto out_unlock;
		kernel_to_ipc64_perm(&shp->shm_perm, &tbuf.shm_perm);
		tbuf.shm_segsz	= shp->shm_segsz;
		tbuf.shm_atime	= shp->shm_atime;
		tbuf.shm_dtime	= shp->shm_dtime;
		tbuf.shm_ctime	= shp->shm_ctime;
		tbuf.shm_cpid	= shp->shm_cpid;
		tbuf.shm_lpid	= shp->shm_lpid;
		tbuf.shm_nattch	= shp->shm_nattch;
		shm_unlock(shmid);
		if(copy_shmid_to_user (buf, &tbuf, version))
			return -EFAULT;
		return result;
	}
	case SHM_LOCK:
	case SHM_UNLOCK:
	{
/* Allow superuser to lock segment in memory */
/* Should the pages be faulted in here or leave it to user? */
/* need to determine interaction with current->swappable */
		struct kern_ipc_perm *ipcp;
		if (!capable(CAP_IPC_LOCK))
			return -EPERM;

		shp = shm_lock(shmid);
		if(shp==NULL)
			return -EINVAL;
		if (shp == &zshmid_kernel) {
			shm_unlock(shmid);
			return -EINVAL;
		}
		err=-EIDRM;
		if(shm_checkid(shp,shmid))
			goto out_unlock;
		ipcp = &shp->shm_perm;
		if(cmd==SHM_LOCK) {
			if (!(ipcp->mode & SHM_LOCKED)) {
				ipcp->mode |= SHM_LOCKED;
				err = 0;
			}
		} else {
			if (ipcp->mode & SHM_LOCKED) {
				ipcp->mode &= ~SHM_LOCKED;
				err = 0;
			}
		}
		shm_unlock(shmid);
		return err;
	}
	case IPC_RMID:
	case IPC_SET:
		break;
	default:
		return -EINVAL;
	}

	if (cmd == IPC_SET) {
		if(copy_shmid_from_user (&setbuf, buf, version))
			return -EFAULT;
	}
	down(&shm_ids.sem);
	shp = shm_lock(shmid);
	err=-EINVAL;
	if(shp==NULL)
		goto out_up;
	if (shp == &zshmid_kernel)
		goto out_unlock_up;
	err=-EIDRM;
	if(shm_checkid(shp,shmid))
		goto out_unlock_up;
	err=-EPERM;
	if (current->euid != shp->shm_perm.uid &&
	    current->euid != shp->shm_perm.cuid && 
	    !capable(CAP_SYS_ADMIN)) {
		goto out_unlock_up;
	}

	switch (cmd) {
	case IPC_SET:
		shp->shm_perm.uid = setbuf.uid;
		shp->shm_perm.gid = setbuf.gid;
		shp->shm_perm.mode = (shp->shm_perm.mode & ~S_IRWXUGO)
			| (setbuf.mode & S_IRWXUGO);
		shp->shm_ctime = CURRENT_TIME;
		break;
	case IPC_RMID:
		shp->shm_perm.mode |= SHM_DEST;
		if (shp->shm_nattch <= 0) {
			shm_unlock(shmid);
			up(&shm_ids.sem);
			killseg (shmid);
			return 0;
		}
	}
	err = 0;
out_unlock_up:
	shm_unlock(shmid);
out_up:
	up(&shm_ids.sem);
	return err;
out_unlock:
	shm_unlock(shmid);
	return err;
}

/*
 * The per process internal structure for managing segments is
 * `struct vm_area_struct'.
 * A shmat will add to and shmdt will remove from the list.
 * shmd->vm_mm		the attacher
 * shmd->vm_start	virt addr of attach, multiple of SHMLBA
 * shmd->vm_end		multiple of SHMLBA
 * shmd->vm_next	next attach for task
 * shmd->vm_next_share	next attach for segment
 * shmd->vm_pgoff	offset into segment (in pages)
 * shmd->vm_private_data		signature for this attach
 */

static struct vm_operations_struct shm_vm_ops = {
	open:		shm_open,	/* open - callback for a new vm-area open */
	close:		shm_close,	/* close - callback for when the vm-area is released */
	nopage:		shm_nopage,
	swapout:	shm_swapout,
};

/* Insert shmd into the list shp->attaches */
static inline void insert_attach (struct shmid_kernel * shp, struct vm_area_struct * shmd)
{
	if((shmd->vm_next_share = shp->attaches) != NULL)
		shp->attaches->vm_pprev_share = &shmd->vm_next_share;
	shp->attaches = shmd;
	shmd->vm_pprev_share = &shp->attaches;
}

/* Remove shmd from list shp->attaches */
static inline void remove_attach (struct shmid_kernel * shp, struct vm_area_struct * shmd)
{
	if(shmd->vm_next_share)
		shmd->vm_next_share->vm_pprev_share = shmd->vm_pprev_share;
	*shmd->vm_pprev_share = shmd->vm_next_share;
}

/*
 * ensure page tables exist
 * mark page table entries with shm_sgn.
 */
static int shm_map (struct vm_area_struct *shmd)
{
	unsigned long tmp;

	/* clear old mappings */
	do_munmap(shmd->vm_start, shmd->vm_end - shmd->vm_start);

	/* add new mapping */
	tmp = shmd->vm_end - shmd->vm_start;
	if((current->mm->total_vm << PAGE_SHIFT) + tmp
	   > (unsigned long) current->rlim[RLIMIT_AS].rlim_cur)
		return -ENOMEM;
	current->mm->total_vm += tmp >> PAGE_SHIFT;
	vmlist_modify_lock(current->mm);
	insert_vm_struct(current->mm, shmd);
	merge_segments(current->mm, shmd->vm_start, shmd->vm_end);
	vmlist_modify_unlock(current->mm);

	return 0;
}

/*
 * Fix shmaddr, allocate descriptor, map shm, add attach descriptor to lists.
 */
asmlinkage long sys_shmat (int shmid, char *shmaddr, int shmflg, ulong *raddr)
{
	struct shmid_kernel *shp;
	struct vm_area_struct *shmd;
	int err;
	unsigned long addr;
	unsigned long len;
	short flg = shmflg & SHM_RDONLY ? S_IRUGO : S_IRUGO|S_IWUGO;


	if (shmid < 0)
		return -EINVAL;

	down(&current->mm->mmap_sem);
	err = -EINVAL;
	shp = shm_lock(shmid);
	if (!shp)
		goto out_up;
	if (shp == &zshmid_kernel)
		goto out_unlock_up;

	err = -EACCES;
	if (ipcperms(&shp->shm_perm, flg))
		goto out_unlock_up;

	err = -EIDRM;
	if (shm_checkid(shp,shmid))
		goto out_unlock_up;

	if (!(addr = (ulong) shmaddr)) {
		if (shmflg & SHM_REMAP)
			goto out_unlock_up;
		err = -ENOMEM;
		addr = 0;
	again:
		if (!(addr = get_unmapped_area(addr, (unsigned long)shp->shm_segsz)))
			goto out_unlock_up;
		if(addr & (SHMLBA - 1)) {
			addr = (addr + (SHMLBA - 1)) & ~(SHMLBA - 1);
			goto again;
		}
	} else if (addr & (SHMLBA-1)) {
		err=-EINVAL;
		if (shmflg & SHM_RND)
			addr &= ~(SHMLBA-1);       /* round down */
		else
			goto out_unlock_up;
	}
	/*
	 * Check if addr exceeds TASK_SIZE (from do_mmap)
	 */
	len = PAGE_SIZE*shp->shm_npages;
	err = -EINVAL;
	if (addr >= TASK_SIZE || len > TASK_SIZE  || addr > TASK_SIZE - len)
		goto out_unlock_up;
	/*
	 * If shm segment goes below stack, make sure there is some
	 * space left for the stack to grow (presently 4 pages).
	 */
	if (addr < current->mm->start_stack &&
	    addr > current->mm->start_stack - PAGE_SIZE*(shp->shm_npages + 4))
		goto out_unlock_up;
	if (!(shmflg & SHM_REMAP) && find_vma_intersection(current->mm, addr, addr + (unsigned long)shp->shm_segsz))
		goto out_unlock_up;

	shm_unlock(shmid);
	err = -ENOMEM;
	shmd = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
	err = shm_revalidate(shp, shmid, len/PAGE_SIZE,flg);
	if(err)	{
		kmem_cache_free(vm_area_cachep, shmd);
		goto out_up;
	}

	shmd->vm_private_data = shp;
	shmd->vm_start = addr;
	shmd->vm_end = addr + shp->shm_npages * PAGE_SIZE;
	shmd->vm_mm = current->mm;
	shmd->vm_page_prot = (shmflg & SHM_RDONLY) ? PAGE_READONLY : PAGE_SHARED;
	shmd->vm_flags = VM_SHM | VM_MAYSHARE | VM_SHARED
			 | VM_MAYREAD | VM_MAYEXEC | VM_READ | VM_EXEC
			 | ((shmflg & SHM_RDONLY) ? 0 : VM_MAYWRITE | VM_WRITE);
	shmd->vm_file = NULL;
	shmd->vm_pgoff = 0;
	shmd->vm_ops = &shm_vm_ops;

	shp->shm_nattch++;	    /* prevent destruction */
	shm_unlock(shp->id);
	err = shm_map (shmd);
	shm_lock(shmid); /* cannot fail */
	if (err)
		goto failed_shm_map;

	insert_attach(shp,shmd);  /* insert shmd into shp->attaches */

	shp->shm_lpid = current->pid;
	shp->shm_atime = CURRENT_TIME;

	*raddr = addr;
	err = 0;
out_unlock_up:
	shm_unlock(shmid);
out_up:
	up(&current->mm->mmap_sem);
	return err;

failed_shm_map:
	{
		int delete = 0;
		if (--shp->shm_nattch <= 0 && shp->shm_perm.mode & SHM_DEST)
			delete = 1;
		shm_unlock(shmid);
		up(&current->mm->mmap_sem);
		kmem_cache_free(vm_area_cachep, shmd);
		if(delete)
			killseg(shmid);
		return err;
	}
}

/* This is called by fork, once for every shm attach. */
static void shm_open (struct vm_area_struct *shmd)
{
	struct shmid_kernel *shp;

	shp = (struct shmid_kernel *) shmd->vm_private_data;
	if(shp != shm_lock(shp->id))
		BUG();
	insert_attach(shp,shmd);  /* insert shmd into shp->attaches */
	shp->shm_nattch++;
	shp->shm_atime = CURRENT_TIME;
	shp->shm_lpid = current->pid;
	shm_unlock(shp->id);
}

/*
 * remove the attach descriptor shmd.
 * free memory for segment if it is marked destroyed.
 * The descriptor has already been removed from the current->mm->mmap list
 * and will later be kfree()d.
 */
static void shm_close (struct vm_area_struct *shmd)
{
	struct shmid_kernel *shp;
	int id;

	/* remove from the list of attaches of the shm segment */
	shp = (struct shmid_kernel *) shmd->vm_private_data;
	if(shp != shm_lock(shp->id))
		BUG();
	remove_attach(shp,shmd);  /* remove from shp->attaches */
  	shp->shm_lpid = current->pid;
	shp->shm_dtime = CURRENT_TIME;
	id=-1;
	if (--shp->shm_nattch <= 0 && shp->shm_perm.mode & SHM_DEST)
		id=shp->id;
	shm_unlock(shp->id);
	if(id!=-1)
		killseg(id);
}

/*
 * detach and kill segment if marked destroyed.
 * The work is done in shm_close.
 */
asmlinkage long sys_shmdt (char *shmaddr)
{
	struct vm_area_struct *shmd, *shmdnext;

	down(&current->mm->mmap_sem);
	for (shmd = current->mm->mmap; shmd; shmd = shmdnext) {
		shmdnext = shmd->vm_next;
		if (shmd->vm_ops == &shm_vm_ops
		    && shmd->vm_start - (shmd->vm_pgoff << PAGE_SHIFT) == (ulong) shmaddr)
			do_munmap(shmd->vm_start, shmd->vm_end - shmd->vm_start);
	}
	up(&current->mm->mmap_sem);
	return 0;
}

/*
 * Enter the shm page into the SHM data structures.
 *
 * The way "nopage" is done, we don't actually have to
 * do anything here: nopage will have filled in the shm
 * data structures already, and shm_swap_out() will just
 * work off them..
 */
static int shm_swapout(struct page * page, struct file *file)
{
	return 0;
}

/*
 * page not present ... go through shm_dir
 */
static struct page * shm_nopage(struct vm_area_struct * shmd, unsigned long address, int no_share)
{
	pte_t pte;
	struct shmid_kernel *shp;
	unsigned int idx;
	struct page * page;
	int is_shmzero;

	shp = (struct shmid_kernel *) shmd->vm_private_data;
	idx = (address - shmd->vm_start) >> PAGE_SHIFT;
	idx += shmd->vm_pgoff;
	is_shmzero = (shp->id == zero_id);

	/*
	 * A shared mapping past the last page of the file is an error
	 * and results in a SIGBUS, so logically a shared mapping past 
	 * the end of a shared memory segment should result in SIGBUS
	 * as well.
	 */
	if (idx >= shp->shm_npages) { 
		return NULL;
	}
	down(&shp->sem);
	if ((shp != shm_lock(shp->id)) && (is_shmzero == 0))
		BUG();

	pte = SHM_ENTRY(shp,idx);
	if (!pte_present(pte)) {
		/* page not present so shm_swap can't race with us
		   and the semaphore protects us by other tasks that
		   could potentially fault on our pte under us */
		if (pte_none(pte)) {
			shm_unlock(shp->id);
			page = alloc_page(GFP_HIGHUSER);
			if (!page)
				goto oom;
			clear_highpage(page);
			if ((shp != shm_lock(shp->id)) && (is_shmzero == 0))
				BUG();
		} else {
			swp_entry_t entry = pte_to_swp_entry(pte);

			shm_unlock(shp->id);
			page = lookup_swap_cache(entry);
			if (!page) {
				lock_kernel();
				swapin_readahead(entry);
				page = read_swap_cache(entry);
				unlock_kernel();
				if (!page)
					goto oom;
			}
			delete_from_swap_cache(page);
			page = replace_with_highmem(page);
			swap_free(entry);
			if ((shp != shm_lock(shp->id)) && (is_shmzero == 0))
				BUG();
			if (is_shmzero) shm_swp--;
		}
		if (is_shmzero) shm_rss++;
		pte = pte_mkdirty(mk_pte(page, PAGE_SHARED));
		SHM_ENTRY(shp, idx) = pte;
	} else
		--current->maj_flt;  /* was incremented in do_no_page */

	/* pte_val(pte) == SHM_ENTRY (shp, idx) */
	get_page(pte_page(pte));
	shm_unlock(shp->id);
	up(&shp->sem);
	current->min_flt++;
	return pte_page(pte);

oom:
	up(&shp->sem);
	return NOPAGE_OOM;
}

#define OKAY	0
#define RETRY	1
#define FAILED	2

static int shm_swap_core(struct shmid_kernel *shp, unsigned long idx, swp_entry_t swap_entry, zone_t *zone, int *counter, struct page **outpage)
{
	pte_t page;
	struct page *page_map;

	page = SHM_ENTRY(shp, idx);
	if (!pte_present(page))
		return RETRY;
	page_map = pte_page(page);
	if (zone && (!memclass(page_map->zone, zone)))
		return RETRY;
	if (shp->id != zero_id) swap_attempts++;

	if (--counter < 0) /* failed */
		return FAILED;
	if (page_count(page_map) != 1)
		return RETRY;

	if (!(page_map = prepare_highmem_swapout(page_map)))
		return FAILED;
	SHM_ENTRY (shp, idx) = swp_entry_to_pte(swap_entry);

	/* add the locked page to the swap cache before allowing
	   the swapin path to run lookup_swap_cache(). This avoids
	   reading a not yet uptodate block from disk.
	   NOTE: we just accounted the swap space reference for this
	   swap cache page at __get_swap_page() time. */
	add_to_swap_cache(*outpage = page_map, swap_entry);
	return OKAY;
}

static void shm_swap_postop(struct page *page)
{
	lock_kernel();
	rw_swap_page(WRITE, page, 0);
	unlock_kernel();
	__free_page(page);
}

static int shm_swap_preop(swp_entry_t *swap_entry)
{
	lock_kernel();
	/* subtle: preload the swap count for the swap cache. We can't
	   increase the count inside the critical section as we can't release
	   the shm_lock there. And we can't acquire the big lock with the
	   shm_lock held (otherwise we would deadlock too easily). */
	*swap_entry = __get_swap_page(2);
	if (!(*swap_entry).val) {
		unlock_kernel();
		return 1;
	}
	unlock_kernel();
	return 0;
}

/*
 * Goes through counter = (shm_rss >> prio) present shm pages.
 */
static unsigned long swap_id = 0; /* currently being swapped */
static unsigned long swap_idx = 0; /* next to swap */

int shm_swap (int prio, int gfp_mask, zone_t *zone)
{
	struct shmid_kernel *shp;
	swp_entry_t swap_entry;
	unsigned long id, idx;
	int loop = 0;
	int counter;
	struct page * page_map;

	zshm_swap(prio, gfp_mask, zone);
	counter = shm_rss >> prio;
	if (!counter)
		return 0;
	if (shm_swap_preop(&swap_entry))
		return 0;

	shm_lockall();
check_id:
	shp = shm_get(swap_id);
	if(shp==NULL || shp->shm_perm.mode & SHM_LOCKED) {
next_id:
		swap_idx = 0;
		if (++swap_id > shm_ids.max_id) {
			swap_id = 0;
			if (loop) {
failed:
				shm_unlockall();
				__swap_free(swap_entry, 2);
				return 0;
			}
			loop = 1;
		}
		goto check_id;
	}
	id = swap_id;

check_table:
	idx = swap_idx++;
	if (idx >= shp->shm_npages)
		goto next_id;

	switch (shm_swap_core(shp, idx, swap_entry, zone, &counter, &page_map)) {
		case RETRY: goto check_table;
		case FAILED: goto failed;
	}
	swap_successes++;
	shm_swp++;
	shm_rss--;
	shm_unlockall();

	shm_swap_postop(page_map);
	return 1;
}

/*
 * Free the swap entry and set the new pte for the shm page.
 */
static void shm_unuse_page(struct shmid_kernel *shp, unsigned long idx,
			   swp_entry_t entry, struct page *page)
{
	pte_t pte;

	pte = pte_mkdirty(mk_pte(page, PAGE_SHARED));
	SHM_ENTRY(shp, idx) = pte;
	get_page(page);
	shm_rss++;

	shm_swp--;

	swap_free(entry);
}

static int shm_unuse_core(struct shmid_kernel *shp, swp_entry_t entry, struct page *page)
{
	int n;

	for (n = 0; n < shp->shm_npages; n++) {
		if (pte_none(SHM_ENTRY(shp,n)))
			continue;
		if (pte_present(SHM_ENTRY(shp,n)))
			continue;
		if (pte_to_swp_entry(SHM_ENTRY(shp,n)).val == entry.val) {
			shm_unuse_page(shp, n, entry, page);
			return 1;
		}
	}
	return 0;
}

/*
 * unuse_shm() search for an eventually swapped out shm page.
 */
void shm_unuse(swp_entry_t entry, struct page *page)
{
	int i;

	shm_lockall();
	for (i = 0; i <= shm_ids.max_id; i++) {
		struct shmid_kernel *shp = shm_get(i);
		if(shp==NULL)
			continue;
		if (shm_unuse_core(shp, entry, page))
			goto out;
	}
out:
	shm_unlockall();
	zmap_unuse(entry, page);
}

#ifdef CONFIG_PROC_FS
static int sysvipc_shm_read_proc(char *buffer, char **start, off_t offset, int length, int *eof, void *data)
{
	off_t pos = 0;
	off_t begin = 0;
	int i, len = 0;

	down(&shm_ids.sem);
	len += sprintf(buffer, "       key      shmid perms       size  cpid  lpid nattch   uid   gid  cuid  cgid      atime      dtime      ctime\n");

    	for(i = 0; i <= shm_ids.max_id; i++) {
		struct shmid_kernel* shp = shm_lock(i);
		if (shp == &zshmid_kernel) {
			shm_unlock(i);
			continue;
		}
		if(shp!=NULL) {
#define SMALL_STRING "%10d %10d  %4o %10u %5u %5u  %5d %5u %5u %5u %5u %10lu %10lu %10lu\n"
#define BIG_STRING   "%10d %10d  %4o %21u %5u %5u  %5d %5u %5u %5u %5u %10lu %10lu %10lu\n"
			char *format;

			if (sizeof(size_t) <= sizeof(int))
				format = SMALL_STRING;
			else
				format = BIG_STRING;
	    		len += sprintf(buffer + len, format,
				shp->shm_perm.key,
				shm_buildid(i, shp->shm_perm.seq),
				shp->shm_perm.mode,
				shp->shm_segsz,
				shp->shm_cpid,
				shp->shm_lpid,
				shp->shm_nattch,
				shp->shm_perm.uid,
				shp->shm_perm.gid,
				shp->shm_perm.cuid,
				shp->shm_perm.cgid,
				shp->shm_atime,
				shp->shm_dtime,
				shp->shm_ctime);
			shm_unlock(i);

			pos += len;
			if(pos < offset) {
				len = 0;
				begin = pos;
			}
			if(pos > offset + length)
				goto done;
		}
	}
	*eof = 1;
done:
	up(&shm_ids.sem);
	*start = buffer + (offset - begin);
	len -= (offset - begin);
	if(len > length)
		len = length;
	if(len < 0)
		len = 0;
	return len;
}
#endif

static struct shmid_kernel *zmap_list = 0;
static spinlock_t zmap_list_lock = SPIN_LOCK_UNLOCKED;
static unsigned long zswap_idx = 0; /* next to swap */
static struct shmid_kernel *zswap_shp = 0;

static struct vm_operations_struct shmzero_vm_ops = {
	open:		shmzero_open,
	close:		shmzero_close,
	nopage:		shm_nopage,
	swapout:	shm_swapout,
};

int map_zero_setup(struct vm_area_struct *vma)
{
	struct shmid_kernel *shp;

	if (!(shp = newseg_alloc((vma->vm_end - vma->vm_start) / PAGE_SIZE)))
		return -ENOMEM;
	shp->id = zero_id;	/* hack for shm_lock et al */
	vma->vm_private_data = shp;
	vma->vm_ops = &shmzero_vm_ops;
	shmzero_open(vma);
	spin_lock(&zmap_list_lock);
	shp->attaches = (struct vm_area_struct *)zmap_list;
	zmap_list = shp;
	spin_unlock(&zmap_list_lock);
	return 0;
}

static void shmzero_open(struct vm_area_struct *shmd)
{
	struct shmid_kernel *shp;

	shp = (struct shmid_kernel *) shmd->vm_private_data;
	down(&shp->sem);
	shp->shm_nattch++;
	up(&shp->sem);
}

static void shmzero_close(struct vm_area_struct *shmd)
{
	int done = 0;
	struct shmid_kernel *shp, *prev, *cur;

	shp = (struct shmid_kernel *) shmd->vm_private_data;
	down(&shp->sem);
	if (--shp->shm_nattch == 0)
		done = 1;
	up(&shp->sem);
	if (done) {
		spin_lock(&zmap_list_lock);
		if (shp == zswap_shp)
			zswap_shp = (struct shmid_kernel *)(shp->attaches);
		if (shp == zmap_list)
			zmap_list = (struct shmid_kernel *)(shp->attaches);
		else {
			prev = zmap_list;
			cur = (struct shmid_kernel *)(prev->attaches);
			while (cur != shp) {
				prev = cur;
				cur = (struct shmid_kernel *)(prev->attaches);
			}
			prev->attaches = (struct vm_area_struct *)(shp->attaches);
		}
		spin_unlock(&zmap_list_lock);
		killseg_core(shp, 0);
	}
}

static void zmap_unuse(swp_entry_t entry, struct page *page)
{
	struct shmid_kernel *shp;

	spin_lock(&zmap_list_lock);
	shp = zmap_list;
	while (shp) {
		if (shm_unuse_core(shp, entry, page))
			break;
		shp = (struct shmid_kernel *)shp->attaches;
	}
	spin_unlock(&zmap_list_lock);
}

static void zshm_swap (int prio, int gfp_mask, zone_t *zone)
{
	struct shmid_kernel *shp;
	swp_entry_t swap_entry;
	unsigned long idx;
	int loop = 0;
	int counter;
	struct page * page_map;

	counter = 10;	/* maybe we should use zshm_rss */
	if (!counter)
		return;
next:
	if (shm_swap_preop(&swap_entry))
		return;

	spin_lock(&zmap_list_lock);
	if (zmap_list == 0)
		goto failed;
next_id:
	if ((shp = zswap_shp) == 0) {
		if (loop) {
failed:
			spin_unlock(&zmap_list_lock);
			__swap_free(swap_entry, 2);
			return;
		}
		zswap_shp = shp = zmap_list;
		zswap_idx = 0;
		loop = 1;
	}

check_table:
	idx = zswap_idx++;
	if (idx >= shp->shm_npages) {
		zswap_shp = (struct shmid_kernel *)(zswap_shp->attaches);
		zswap_idx = 0;
		goto next_id;
	}

	switch (shm_swap_core(shp, idx, swap_entry, zone, &counter, &page_map)) {
		case RETRY: goto check_table;
		case FAILED: goto failed;
	}
	spin_unlock(&zmap_list_lock);

	shm_swap_postop(page_map);
	if (counter)
		goto next;
	return;
}
