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

struct shmid_kernel /* extend struct shmis_ds with private fields */
{	
	struct shmid_ds		u;
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
	ipc_checkid(&shm_ids,&s->u.shm_perm,id)
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
static ulong used_segs = 0;

void __init shm_init (void)
{
	ipc_init_ids(&shm_ids, shm_ctlmni);
#ifdef CONFIG_PROC_FS
	create_proc_read_entry("sysvipc/shm", 0, 0, sysvipc_shm_read_proc, NULL);
#endif
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
	if (ipcperms(&shp->u.shm_perm, flg)) {
		shm_unlock(shmid);
		return -EACCES;
	}
	return 0;
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

	shp = (struct shmid_kernel *) kmalloc (sizeof (*shp), GFP_KERNEL);
	if (!shp)
		return -ENOMEM;

	shp->shm_dir = shm_alloc (numpages);
	if (!shp->shm_dir) {
		kfree(shp);
		return -ENOMEM;
	}
	id = ipc_addid(&shm_ids, &shp->u.shm_perm, shm_ctlmni);
	if(id == -1) {
		shm_free(shp->shm_dir,numpages);
		kfree(shp);
		return -ENOSPC;
	}
	shp->u.shm_perm.key = key;
	shp->u.shm_perm.mode = (shmflg & S_IRWXUGO);
	shp->u.shm_segsz = size;
	shp->u.shm_cpid = current->pid;
	shp->attaches = NULL;
	shp->u.shm_lpid = shp->u.shm_nattch = 0;
	shp->u.shm_atime = shp->u.shm_dtime = 0;
	shp->u.shm_ctime = CURRENT_TIME;
	shp->shm_npages = numpages;
	shp->id = shm_buildid(id,shp->u.shm_perm.seq);
	init_MUTEX(&shp->sem);

	shm_tot += numpages;
	shm_unlock(id);

	return shm_buildid(id,shp->u.shm_perm.seq);
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
		if (ipcperms(&shp->u.shm_perm, shmflg))
			err = -EACCES;
		else
			err = shm_buildid(id, shp->u.shm_perm.seq);
		shm_unlock(id);
	}
	up(&shm_ids.sem);
	return err;
}

/*
 * Only called after testing nattch and SHM_DEST.
 * Here pages, pgtable and shmid_kernel are freed.
 */
static void killseg (int shmid)
{
	struct shmid_kernel *shp;
	int i, numpages;
	int rss, swp;

	down(&shm_ids.sem);
	shp = shm_lock(shmid);
	if(shp==NULL) {
out_up:
		up(&shm_ids.sem);
		return;
	}
	if(shm_checkid(shp,shmid) || shp->u.shm_nattch > 0 ||
	    !(shp->u.shm_perm.mode & SHM_DEST)) {
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
	shm_lockall();
	shm_rss -= rss;
	shm_swp -= swp;
	shm_tot -= numpages;
	shm_unlockall();
	return;
}

asmlinkage long sys_shmctl (int shmid, int cmd, struct shmid_ds *buf)
{
	struct shmid_ds tbuf;
	struct shmid_kernel *shp;
	int err;

	if (cmd < 0 || shmid < 0)
		return -EINVAL;

	switch (cmd) { /* replace with proc interface ? */
	case IPC_INFO:
	{
		struct shminfo shminfo;

		memset(&shminfo,0,sizeof(shminfo));
		shminfo.shmmni = shminfo.shmseg = shm_ctlmni;
		shminfo.shmmax = shm_ctlmax;
		shminfo.shmall = shm_ctlall;

		shminfo.shmmin = SHMMIN;
		if(copy_to_user (buf, &shminfo, sizeof(struct shminfo)))
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
		shm_info.used_ids = used_segs;
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
		struct shmid_ds tmp;
		int result;
		shp = shm_lock(shmid);
		if(shp==NULL)
			return -EINVAL;
		if(cmd==SHM_STAT) {
			err = -EINVAL;
			if (shmid > shm_ids.max_id)
				goto out_unlock;
			result = shm_buildid(shmid, shp->u.shm_perm.seq);
		} else {
			err = -EIDRM;
			if(shm_checkid(shp,shmid))
				goto out_unlock;
			result = 0;
		}
		err=-EACCES;
		if (ipcperms (&shp->u.shm_perm, S_IRUGO))
			goto out_unlock;
		memcpy(&tmp,&shp->u,sizeof(tmp));
		shm_unlock(shmid);
		if(copy_to_user (buf, &tmp, sizeof(tmp)))
			return -EFAULT;
		return result;
	}
	case SHM_LOCK:
	case SHM_UNLOCK:
	{
/* Allow superuser to lock segment in memory */
/* Should the pages be faulted in here or leave it to user? */
/* need to determine interaction with current->swappable */
		struct ipc_perm *ipcp;
		if (!capable(CAP_IPC_LOCK))
			return -EPERM;

		shp = shm_lock(shmid);
		if(shp==NULL)
			return -EINVAL;
		err=-EIDRM;
		if(shm_checkid(shp,shmid))
			goto out_unlock;
		ipcp = &shp->u.shm_perm;
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
		if(copy_from_user (&tbuf, buf, sizeof (*buf)))
			return -EFAULT;
	}
	down(&shm_ids.sem);
	shp = shm_lock(shmid);
	err=-EINVAL;
	if(shp==NULL)
		goto out_up;
	err=-EIDRM;
	if(shm_checkid(shp,shmid))
		goto out_unlock_up;
	err=-EPERM;
	if (current->euid != shp->u.shm_perm.uid &&
	    current->euid != shp->u.shm_perm.cuid && 
	    !capable(CAP_SYS_ADMIN)) {
		goto out_unlock_up;
	}

	switch (cmd) {
	case IPC_SET:
		shp->u.shm_perm.uid = tbuf.shm_perm.uid;
		shp->u.shm_perm.gid = tbuf.shm_perm.gid;
		shp->u.shm_perm.mode = (shp->u.shm_perm.mode & ~S_IRWXUGO)
			| (tbuf.shm_perm.mode & S_IRWXUGO);
		shp->u.shm_ctime = CURRENT_TIME;
		break;
	case IPC_RMID:
		shp->u.shm_perm.mode |= SHM_DEST;
		if (shp->u.shm_nattch <= 0) {
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
	shm_open,		/* open - callback for a new vm-area open */
	shm_close,		/* close - callback for when the vm-area is released */
	NULL,			/* no need to sync pages at unmap */
	NULL,			/* protect */
	NULL,			/* sync */
	NULL,			/* advise */
	shm_nopage,		/* nopage */
	NULL,			/* wppage */
	shm_swapout		/* swapout */
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
	if(shp == NULL)
		goto out_unlock_up;

	err = -EACCES;
	if (ipcperms(&shp->u.shm_perm, flg))
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
		if (!(addr = get_unmapped_area(addr, (unsigned long)shp->u.shm_segsz)))
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
	if (!(shmflg & SHM_REMAP) && find_vma_intersection(current->mm, addr, addr + (unsigned long)shp->u.shm_segsz))
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

	shp->u.shm_nattch++;	    /* prevent destruction */
	shm_unlock(shp->id);
	err = shm_map (shmd);
	shm_lock(shmid); /* cannot fail */
	if (err)
		goto failed_shm_map;

	insert_attach(shp,shmd);  /* insert shmd into shp->attaches */

	shp->u.shm_lpid = current->pid;
	shp->u.shm_atime = CURRENT_TIME;

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
		if (--shp->u.shm_nattch <= 0 && shp->u.shm_perm.mode & SHM_DEST)
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
	shp->u.shm_nattch++;
	shp->u.shm_atime = CURRENT_TIME;
	shp->u.shm_lpid = current->pid;
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
  	shp->u.shm_lpid = current->pid;
	shp->u.shm_dtime = CURRENT_TIME;
	id=-1;
	if (--shp->u.shm_nattch <= 0 && shp->u.shm_perm.mode & SHM_DEST)
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

	shp = (struct shmid_kernel *) shmd->vm_private_data;
	idx = (address - shmd->vm_start) >> PAGE_SHIFT;
	idx += shmd->vm_pgoff;

	down(&shp->sem);
	if(shp != shm_lock(shp->id))
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
			if(shp != shm_lock(shp->id))
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
			if(shp != shm_lock(shp->id))
				BUG();
			shm_swp--;
		}
		shm_rss++;
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

/*
 * Goes through counter = (shm_rss >> prio) present shm pages.
 */
static unsigned long swap_id = 0; /* currently being swapped */
static unsigned long swap_idx = 0; /* next to swap */

int shm_swap (int prio, int gfp_mask)
{
	pte_t page;
	struct shmid_kernel *shp;
	swp_entry_t swap_entry;
	unsigned long id, idx;
	int loop = 0;
	int counter;
	struct page * page_map;
	
	counter = shm_rss >> prio;
	if (!counter)
		return 0;
	lock_kernel();
	/* subtle: preload the swap count for the swap cache. We can't
	   increase the count inside the critical section as we can't release
	   the shm_lock there. And we can't acquire the big lock with the
	   shm_lock held (otherwise we would deadlock too easily). */
	swap_entry = __get_swap_page(2);
	if (!swap_entry.val) {
		unlock_kernel();
		return 0;
	}
	unlock_kernel();

	shm_lockall();
check_id:
	shp = shm_get(swap_id);
	if(shp==NULL || shp->u.shm_perm.mode & SHM_LOCKED) {
next_id:
		swap_idx = 0;
		if (++swap_id > shm_ids.max_id) {
			swap_id = 0;
			if (loop)
				goto failed;
			loop = 1;
		}
		goto check_id;
	}
	id = swap_id;

check_table:
	idx = swap_idx++;
	if (idx >= shp->shm_npages)
		goto next_id;

	page = SHM_ENTRY(shp, idx);
	if (!pte_present(page))
		goto check_table;
	page_map = pte_page(page);
	if ((gfp_mask & __GFP_DMA) && !PageDMA(page_map))
		goto check_table;
	if (!(gfp_mask & __GFP_HIGHMEM) && PageHighMem(page_map))
		goto check_table;
	swap_attempts++;

	if (--counter < 0) { /* failed */
failed:
		shm_unlockall();
		__swap_free(swap_entry, 2);
		return 0;
	}
	if (page_count(page_map) != 1)
		goto check_table;

	if (!(page_map = prepare_highmem_swapout(page_map)))
		goto failed;
	SHM_ENTRY (shp, idx) = swp_entry_to_pte(swap_entry);
	swap_successes++;
	shm_swp++;
	shm_rss--;

	/* add the locked page to the swap cache before allowing
	   the swapin path to run lookup_swap_cache(). This avoids
	   reading a not yet uptodate block from disk.
	   NOTE: we just accounted the swap space reference for this
	   swap cache page at __get_swap_page() time. */
	add_to_swap_cache(page_map, swap_entry);
	shm_unlockall();

	lock_kernel();
	rw_swap_page(WRITE, page_map, 0);
	unlock_kernel();

	__free_page(page_map);
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

/*
 * unuse_shm() search for an eventually swapped out shm page.
 */
void shm_unuse(swp_entry_t entry, struct page *page)
{
	int i, n;

	shm_lockall();
	for (i = 0; i <= shm_ids.max_id; i++) {
		struct shmid_kernel *shp = shm_get(i);
		if(shp==NULL)
			continue;
		for (n = 0; n < shp->shm_npages; n++) {
			if (pte_none(SHM_ENTRY(shp,n)))
				continue;
			if (pte_present(SHM_ENTRY(shp,n)))
				continue;
			if (pte_to_swp_entry(SHM_ENTRY(shp,n)).val == entry.val) {
				shm_unuse_page(shp, n, entry, page);
				goto out;
			}
		}
	}
out:
	shm_unlockall();
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
		if(shp!=NULL) {
#define SMALL_STRING "%10d %10d  %4o %10u %5u %5u  %5d %5u %5u %5u %5u %10lu %10lu %10lu\n"
#define BIG_STRING   "%10d %10d  %4o %21u %5u %5u  %5d %5u %5u %5u %5u %10lu %10lu %10lu\n"
			char *format;

			if (sizeof(size_t) <= sizeof(int))
				format = SMALL_STRING;
			else
				format = BIG_STRING;
	    		len += sprintf(buffer + len, format,
				shp->u.shm_perm.key,
				shm_buildid(i, shp->u.shm_perm.seq),
				shp->u.shm_perm.mode,
				shp->u.shm_segsz,
				shp->u.shm_cpid,
				shp->u.shm_lpid,
				shp->u.shm_nattch,
				shp->u.shm_perm.uid,
				shp->u.shm_perm.gid,
				shp->u.shm_perm.cuid,
				shp->u.shm_perm.cgid,
				shp->u.shm_atime,
				shp->u.shm_dtime,
				shp->u.shm_ctime);
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
