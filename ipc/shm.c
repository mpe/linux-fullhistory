/*
 * linux/ipc/shm.c
 * Copyright (C) 1992, 1993 Krishna Balasubramanian
 *         Many improvements/fixes by Bruno Haible.
 * Replaced `struct shm_desc' by `struct vm_area_struct', July 1994.
 * Fixed the shm swap deallocation (shm_unuse()), August 1998 Andrea Arcangeli.
 */

#include <linux/malloc.h>
#include <linux/shm.h>
#include <linux/swap.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <linux/vmalloc.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>

extern int ipcperms (struct ipc_perm *ipcp, short shmflg);
extern unsigned long get_swap_page (void);
static int findkey (key_t key);
static int newseg (key_t key, int shmflg, int size);
static int shm_map (struct vm_area_struct *shmd);
static void killseg (int id);
static void shm_open (struct vm_area_struct *shmd);
static void shm_close (struct vm_area_struct *shmd);
static pte_t shm_swap_in(struct vm_area_struct *, unsigned long, unsigned long);

static int shm_tot = 0; /* total number of shared memory pages */
static int shm_rss = 0; /* number of shared memory pages that are in memory */
static int shm_swp = 0; /* number of shared memory pages that are in swap */
static int max_shmid = 0; /* every used id is <= max_shmid */
static struct wait_queue *shm_lock = NULL; /* calling findkey() may need to wait */
static struct shmid_kernel *shm_segs[SHMMNI];

static unsigned short shm_seq = 0; /* incremented, for recognizing stale ids */

/* some statistics */
static ulong swap_attempts = 0;
static ulong swap_successes = 0;
static ulong used_segs = 0;

void __init shm_init (void)
{
	int id;

	for (id = 0; id < SHMMNI; id++)
		shm_segs[id] = (struct shmid_kernel *) IPC_UNUSED;
	shm_tot = shm_rss = shm_seq = max_shmid = used_segs = 0;
	shm_lock = NULL;
	return;
}

static int findkey (key_t key)
{
	int id;
	struct shmid_kernel *shp;

	for (id = 0; id <= max_shmid; id++) {
		while ((shp = shm_segs[id]) == IPC_NOID)
			sleep_on (&shm_lock);
		if (shp == IPC_UNUSED)
			continue;
		if (key == shp->u.shm_perm.key)
			return id;
	}
	return -1;
}

/*
 * allocate new shmid_kernel and pgtable. protected by shm_segs[id] = NOID.
 */
static int newseg (key_t key, int shmflg, int size)
{
	struct shmid_kernel *shp;
	int numpages = (size + PAGE_SIZE -1) >> PAGE_SHIFT;
	int id, i;

	if (size < SHMMIN)
		return -EINVAL;
	if (shm_tot + numpages >= SHMALL)
		return -ENOSPC;
	for (id = 0; id < SHMMNI; id++)
		if (shm_segs[id] == IPC_UNUSED) {
			shm_segs[id] = (struct shmid_kernel *) IPC_NOID;
			goto found;
		}
	return -ENOSPC;

found:
	shp = (struct shmid_kernel *) kmalloc (sizeof (*shp), GFP_KERNEL);
	if (!shp) {
		shm_segs[id] = (struct shmid_kernel *) IPC_UNUSED;
		wake_up (&shm_lock);
		return -ENOMEM;
	}

	shp->shm_pages = (ulong *) vmalloc (numpages*sizeof(ulong));
	if (!shp->shm_pages) {
		shm_segs[id] = (struct shmid_kernel *) IPC_UNUSED;
		wake_up (&shm_lock);
		kfree(shp);
		return -ENOMEM;
	}

	for (i = 0; i < numpages; shp->shm_pages[i++] = 0);
	shm_tot += numpages;
	shp->u.shm_perm.key = key;
	shp->u.shm_perm.mode = (shmflg & S_IRWXUGO);
	shp->u.shm_perm.cuid = shp->u.shm_perm.uid = current->euid;
	shp->u.shm_perm.cgid = shp->u.shm_perm.gid = current->egid;
	shp->u.shm_perm.seq = shm_seq;
	shp->u.shm_segsz = size;
	shp->u.shm_cpid = current->pid;
	shp->attaches = NULL;
	shp->u.shm_lpid = shp->u.shm_nattch = 0;
	shp->u.shm_atime = shp->u.shm_dtime = 0;
	shp->u.shm_ctime = CURRENT_TIME;
	shp->shm_npages = numpages;

	if (id > max_shmid)
		max_shmid = id;
	shm_segs[id] = shp;
	used_segs++;
	wake_up (&shm_lock);
	return (unsigned int) shp->u.shm_perm.seq * SHMMNI + id;
}

int shmmax = SHMMAX;

asmlinkage int sys_shmget (key_t key, int size, int shmflg)
{
	struct shmid_kernel *shp;
	int err, id = 0;

	down(&current->mm->mmap_sem);
	lock_kernel();
	if (size < 0 || size > shmmax) {
		err = -EINVAL;
	} else if (key == IPC_PRIVATE) {
		err = newseg(key, shmflg, size);
	} else if ((id = findkey (key)) == -1) {
		if (!(shmflg & IPC_CREAT))
			err = -ENOENT;
		else
			err = newseg(key, shmflg, size);
	} else if ((shmflg & IPC_CREAT) && (shmflg & IPC_EXCL)) {
		err = -EEXIST;
	} else {
		shp = shm_segs[id];
		if (shp->u.shm_perm.mode & SHM_DEST)
			err = -EIDRM;
		else if (size > shp->u.shm_segsz)
			err = -EINVAL;
		else if (ipcperms (&shp->u.shm_perm, shmflg))
			err = -EACCES;
		else
			err = (int) shp->u.shm_perm.seq * SHMMNI + id;
	}
	unlock_kernel();
	up(&current->mm->mmap_sem);
	return err;
}

/*
 * Only called after testing nattch and SHM_DEST.
 * Here pages, pgtable and shmid_kernel are freed.
 */
static void killseg (int id)
{
	struct shmid_kernel *shp;
	int i, numpages;

	shp = shm_segs[id];
	if (shp == IPC_NOID || shp == IPC_UNUSED) {
		printk ("shm nono: killseg called on unused seg id=%d\n", id);
		return;
	}
	shp->u.shm_perm.seq++;     /* for shmat */
	shm_seq = (shm_seq+1) % ((unsigned)(1<<31)/SHMMNI); /* increment, but avoid overflow */
	shm_segs[id] = (struct shmid_kernel *) IPC_UNUSED;
	used_segs--;
	if (id == max_shmid)
		while (max_shmid && (shm_segs[--max_shmid] == IPC_UNUSED));
	if (!shp->shm_pages) {
		printk ("shm nono: killseg shp->pages=NULL. id=%d\n", id);
		return;
	}
	numpages = shp->shm_npages;
	for (i = 0; i < numpages ; i++) {
		pte_t pte;
		pte = __pte(shp->shm_pages[i]);
		if (pte_none(pte))
			continue;
		if (pte_present(pte)) {
			free_page (pte_page(pte));
			shm_rss--;
		} else {
			swap_free(pte_val(pte));
			shm_swp--;
		}
	}
	vfree(shp->shm_pages);
	shm_tot -= numpages;
	kfree(shp);
	return;
}

asmlinkage int sys_shmctl (int shmid, int cmd, struct shmid_ds *buf)
{
	struct shmid_ds tbuf;
	struct shmid_kernel *shp;
	struct ipc_perm *ipcp;
	int id, err = -EINVAL;

	lock_kernel();
	if (cmd < 0 || shmid < 0)
		goto out;
	if (cmd == IPC_SET) {
		err = -EFAULT;
		if(copy_from_user (&tbuf, buf, sizeof (*buf)))
			goto out;
	}

	switch (cmd) { /* replace with proc interface ? */
	case IPC_INFO:
	{
		struct shminfo shminfo;
		err = -EFAULT;
		if (!buf)
			goto out;
		shminfo.shmmni = SHMMNI;
		shminfo.shmmax = shmmax;
		shminfo.shmmin = SHMMIN;
		shminfo.shmall = SHMALL;
		shminfo.shmseg = SHMSEG;
		if(copy_to_user (buf, &shminfo, sizeof(struct shminfo)))
			goto out;
		err = max_shmid;
		goto out;
	}
	case SHM_INFO:
	{
		struct shm_info shm_info;
		err = -EFAULT;
		shm_info.used_ids = used_segs;
		shm_info.shm_rss = shm_rss;
		shm_info.shm_tot = shm_tot;
		shm_info.shm_swp = shm_swp;
		shm_info.swap_attempts = swap_attempts;
		shm_info.swap_successes = swap_successes;
		if(copy_to_user (buf, &shm_info, sizeof(shm_info)))
			goto out;
		err = max_shmid;
		goto out;
	}
	case SHM_STAT:
		err = -EINVAL;
		if (shmid > max_shmid)
			goto out;
		shp = shm_segs[shmid];
		if (shp == IPC_UNUSED || shp == IPC_NOID)
			goto out;
		if (ipcperms (&shp->u.shm_perm, S_IRUGO))
			goto out;
		id = (unsigned int) shp->u.shm_perm.seq * SHMMNI + shmid;
		err = -EFAULT;
		if(copy_to_user (buf, &shp->u, sizeof(*buf)))
			goto out;
		err = id;
		goto out;
	}

	shp = shm_segs[id = (unsigned int) shmid % SHMMNI];
	err = -EINVAL;
	if (shp == IPC_UNUSED || shp == IPC_NOID)
		goto out;
	err = -EIDRM;
	if (shp->u.shm_perm.seq != (unsigned int) shmid / SHMMNI)
		goto out;
	ipcp = &shp->u.shm_perm;

	switch (cmd) {
	case SHM_UNLOCK:
		err = -EPERM;
		if (!capable(CAP_IPC_LOCK))
			goto out;
		err = -EINVAL;
		if (!(ipcp->mode & SHM_LOCKED))
			goto out;
		ipcp->mode &= ~SHM_LOCKED;
		break;
	case SHM_LOCK:
/* Allow superuser to lock segment in memory */
/* Should the pages be faulted in here or leave it to user? */
/* need to determine interaction with current->swappable */
		err = -EPERM;
		if (!capable(CAP_IPC_LOCK))
			goto out;
		err = -EINVAL;
		if (ipcp->mode & SHM_LOCKED)
			goto out;
		ipcp->mode |= SHM_LOCKED;
		break;
	case IPC_STAT:
		err = -EACCES;
		if (ipcperms (ipcp, S_IRUGO))
			goto out;
		err = -EFAULT;
		if(copy_to_user (buf, &shp->u, sizeof(shp->u)))
			goto out;
		break;
	case IPC_SET:
		if (current->euid == shp->u.shm_perm.uid ||
		    current->euid == shp->u.shm_perm.cuid || 
		    capable(CAP_SYS_ADMIN)) {
			ipcp->uid = tbuf.shm_perm.uid;
			ipcp->gid = tbuf.shm_perm.gid;
			ipcp->mode = (ipcp->mode & ~S_IRWXUGO)
				| (tbuf.shm_perm.mode & S_IRWXUGO);
			shp->u.shm_ctime = CURRENT_TIME;
			break;
		}
		err = -EPERM;
		goto out;
	case IPC_RMID:
		if (current->euid == shp->u.shm_perm.uid ||
		    current->euid == shp->u.shm_perm.cuid || 
		    capable(CAP_SYS_ADMIN)) {
			shp->u.shm_perm.mode |= SHM_DEST;
			if (shp->u.shm_nattch <= 0)
				killseg (id);
			break;
		}
		err = -EPERM;
		goto out;
	default:
		err = -EINVAL;
		goto out;
	}
	err = 0;
out:
	unlock_kernel();
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
 * shmd->vm_offset	offset into segment
 * shmd->vm_pte		signature for this attach
 */

static struct vm_operations_struct shm_vm_ops = {
	shm_open,		/* open - callback for a new vm-area open */
	shm_close,		/* close - callback for when the vm-area is released */
	NULL,			/* no need to sync pages at unmap */
	NULL,			/* protect */
	NULL,			/* sync */
	NULL,			/* advise */
	NULL,			/* nopage (done with swapin) */
	NULL,			/* wppage */
	NULL,			/* swapout (hardcoded right now) */
	shm_swap_in		/* swapin */
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
	pgd_t *page_dir;
	pmd_t *page_middle;
	pte_t *page_table;
	unsigned long tmp, shm_sgn;
	int error;

	/* clear old mappings */
	do_munmap(shmd->vm_start, shmd->vm_end - shmd->vm_start);

	/* add new mapping */
	tmp = shmd->vm_end - shmd->vm_start;
	if((current->mm->total_vm << PAGE_SHIFT) + tmp
	   > (unsigned long) current->rlim[RLIMIT_AS].rlim_cur)
		return -ENOMEM;
	current->mm->total_vm += tmp >> PAGE_SHIFT;
	insert_vm_struct(current->mm, shmd);
	merge_segments(current->mm, shmd->vm_start, shmd->vm_end);

	/* map page range */
	error = 0;
	shm_sgn = shmd->vm_pte +
	  SWP_ENTRY(0, (shmd->vm_offset >> PAGE_SHIFT) << SHM_IDX_SHIFT);
	flush_cache_range(shmd->vm_mm, shmd->vm_start, shmd->vm_end);
	for (tmp = shmd->vm_start;
	     tmp < shmd->vm_end;
	     tmp += PAGE_SIZE, shm_sgn += SWP_ENTRY(0, 1 << SHM_IDX_SHIFT))
	{
		page_dir = pgd_offset(shmd->vm_mm,tmp);
		page_middle = pmd_alloc(page_dir,tmp);
		if (!page_middle) {
			error = -ENOMEM;
			break;
		}
		page_table = pte_alloc(page_middle,tmp);
		if (!page_table) {
			error = -ENOMEM;
			break;
		}
		set_pte(page_table, __pte(shm_sgn));
	}
	flush_tlb_range(shmd->vm_mm, shmd->vm_start, shmd->vm_end);
	return error;
}

/*
 * Fix shmaddr, allocate descriptor, map shm, add attach descriptor to lists.
 */
asmlinkage int sys_shmat (int shmid, char *shmaddr, int shmflg, ulong *raddr)
{
	struct shmid_kernel *shp;
	struct vm_area_struct *shmd;
	int err = -EINVAL;
	unsigned int id;
	unsigned long addr;
	unsigned long len;

	down(&current->mm->mmap_sem);
	lock_kernel();
	if (shmid < 0) {
		/* printk("shmat() -> EINVAL because shmid = %d < 0\n",shmid); */
		goto out;
	}

	shp = shm_segs[id = (unsigned int) shmid % SHMMNI];
	if (shp == IPC_UNUSED || shp == IPC_NOID) {
		/* printk("shmat() -> EINVAL because shmid = %d is invalid\n",shmid); */
		goto out;
	}

	if (!(addr = (ulong) shmaddr)) {
		if (shmflg & SHM_REMAP)
			goto out;
		err = -ENOMEM;
		addr = 0;
	again:
		if (!(addr = get_unmapped_area(addr, shp->u.shm_segsz)))
			goto out;
		if(addr & (SHMLBA - 1)) {
			addr = (addr + (SHMLBA - 1)) & ~(SHMLBA - 1);
			goto again;
		}
	} else if (addr & (SHMLBA-1)) {
		if (shmflg & SHM_RND)
			addr &= ~(SHMLBA-1);       /* round down */
		else
			goto out;
	}
	/*
	 * Check if addr exceeds TASK_SIZE (from do_mmap)
	 */
	len = PAGE_SIZE*shp->shm_npages;
	err = -EINVAL;
	if (addr >= TASK_SIZE || len > TASK_SIZE  || addr > TASK_SIZE - len)
		goto out;
	/*
	 * If shm segment goes below stack, make sure there is some
	 * space left for the stack to grow (presently 4 pages).
	 */
	if (addr < current->mm->start_stack &&
	    addr > current->mm->start_stack - PAGE_SIZE*(shp->shm_npages + 4))
	{
		/* printk("shmat() -> EINVAL because segment intersects stack\n"); */
		goto out;
	}
	if (!(shmflg & SHM_REMAP))
		if ((shmd = find_vma_intersection(current->mm, addr, addr + shp->u.shm_segsz))) {
			/* printk("shmat() -> EINVAL because the interval [0x%lx,0x%lx) intersects an already mapped interval [0x%lx,0x%lx).\n",
				addr, addr + shp->shm_segsz, shmd->vm_start, shmd->vm_end); */
			goto out;
		}

	err = -EACCES;
	if (ipcperms(&shp->u.shm_perm, shmflg & SHM_RDONLY ? S_IRUGO : S_IRUGO|S_IWUGO))
		goto out;
	err = -EIDRM;
	if (shp->u.shm_perm.seq != (unsigned int) shmid / SHMMNI)
		goto out;

	err = -ENOMEM;
	shmd = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
	if (!shmd)
		goto out;
	if ((shp != shm_segs[id]) || (shp->u.shm_perm.seq != (unsigned int) shmid / SHMMNI)) {
		kmem_cache_free(vm_area_cachep, shmd);
		err = -EIDRM;
		goto out;
	}

	shmd->vm_pte = SWP_ENTRY(SHM_SWP_TYPE, id);
	shmd->vm_start = addr;
	shmd->vm_end = addr + shp->shm_npages * PAGE_SIZE;
	shmd->vm_mm = current->mm;
	shmd->vm_page_prot = (shmflg & SHM_RDONLY) ? PAGE_READONLY : PAGE_SHARED;
	shmd->vm_flags = VM_SHM | VM_MAYSHARE | VM_SHARED
			 | VM_MAYREAD | VM_MAYEXEC | VM_READ | VM_EXEC
			 | ((shmflg & SHM_RDONLY) ? 0 : VM_MAYWRITE | VM_WRITE);
	shmd->vm_file = NULL;
	shmd->vm_offset = 0;
	shmd->vm_ops = &shm_vm_ops;

	shp->u.shm_nattch++;            /* prevent destruction */
	if ((err = shm_map (shmd))) {
		if (--shp->u.shm_nattch <= 0 && shp->u.shm_perm.mode & SHM_DEST)
			killseg(id);
		kmem_cache_free(vm_area_cachep, shmd);
		goto out;
	}

	insert_attach(shp,shmd);  /* insert shmd into shp->attaches */

	shp->u.shm_lpid = current->pid;
	shp->u.shm_atime = CURRENT_TIME;

	*raddr = addr;
	err = 0;
out:
	unlock_kernel();
	up(&current->mm->mmap_sem);
	return err;
}

/* This is called by fork, once for every shm attach. */
static void shm_open (struct vm_area_struct *shmd)
{
	unsigned int id;
	struct shmid_kernel *shp;

	id = SWP_OFFSET(shmd->vm_pte) & SHM_ID_MASK;
	shp = shm_segs[id];
	if (shp == IPC_UNUSED) {
		printk("shm_open: unused id=%d PANIC\n", id);
		return;
	}
	insert_attach(shp,shmd);  /* insert shmd into shp->attaches */
	shp->u.shm_nattch++;
	shp->u.shm_atime = CURRENT_TIME;
	shp->u.shm_lpid = current->pid;
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
	id = SWP_OFFSET(shmd->vm_pte) & SHM_ID_MASK;
	shp = shm_segs[id];
	remove_attach(shp,shmd);  /* remove from shp->attaches */
  	shp->u.shm_lpid = current->pid;
	shp->u.shm_dtime = CURRENT_TIME;
	if (--shp->u.shm_nattch <= 0 && shp->u.shm_perm.mode & SHM_DEST)
		killseg (id);
}

/*
 * detach and kill segment if marked destroyed.
 * The work is done in shm_close.
 */
asmlinkage int sys_shmdt (char *shmaddr)
{
	struct vm_area_struct *shmd, *shmdnext;

	down(&current->mm->mmap_sem);
	lock_kernel();
	for (shmd = current->mm->mmap; shmd; shmd = shmdnext) {
		shmdnext = shmd->vm_next;
		if (shmd->vm_ops == &shm_vm_ops
		    && shmd->vm_start - shmd->vm_offset == (ulong) shmaddr)
			do_munmap(shmd->vm_start, shmd->vm_end - shmd->vm_start);
	}
	unlock_kernel();
	up(&current->mm->mmap_sem);
	return 0;
}

/*
 * page not present ... go through shm_pages
 */
static pte_t shm_swap_in(struct vm_area_struct * shmd, unsigned long offset, unsigned long code)
{
	pte_t pte;
	struct shmid_kernel *shp;
	unsigned int id, idx;

	id = SWP_OFFSET(code) & SHM_ID_MASK;
#ifdef DEBUG_SHM
	if (id != (SWP_OFFSET(shmd->vm_pte) & SHM_ID_MASK)) {
		printk ("shm_swap_in: code id = %d and shmd id = %ld differ\n",
			id, SWP_OFFSET(shmd->vm_pte) & SHM_ID_MASK);
		return BAD_PAGE;
	}
	if (id > max_shmid) {
		printk ("shm_swap_in: id=%d too big. proc mem corrupted\n", id);
		return BAD_PAGE;
	}
#endif
	shp = shm_segs[id];

#ifdef DEBUG_SHM
	if (shp == IPC_UNUSED || shp == IPC_NOID) {
		printk ("shm_swap_in: id=%d invalid. Race.\n", id);
		return BAD_PAGE;
	}
#endif
	idx = (SWP_OFFSET(code) >> SHM_IDX_SHIFT) & SHM_IDX_MASK;
#ifdef DEBUG_SHM
	if (idx != (offset >> PAGE_SHIFT)) {
		printk ("shm_swap_in: code idx = %u and shmd idx = %lu differ\n",
			idx, offset >> PAGE_SHIFT);
		return BAD_PAGE;
	}
	if (idx >= shp->shm_npages) {
		printk ("shm_swap_in : too large page index. id=%d\n", id);
		return BAD_PAGE;
	}
#endif

	pte = __pte(shp->shm_pages[idx]);
	if (!pte_present(pte)) {
		unsigned long page = get_free_page(GFP_KERNEL);
		if (!page) {
			oom(current);
			return BAD_PAGE;
		}
		pte = __pte(shp->shm_pages[idx]);
		if (pte_present(pte)) {
			free_page (page); /* doesn't sleep */
			goto done;
		}
		if (!pte_none(pte)) {
			rw_swap_page_nocache(READ, pte_val(pte), (char *)page);
			pte = __pte(shp->shm_pages[idx]);
			if (pte_present(pte))  {
				free_page (page); /* doesn't sleep */
				goto done;
			}
			swap_free(pte_val(pte));
			shm_swp--;
		}
		shm_rss++;
		pte = pte_mkdirty(mk_pte(page, PAGE_SHARED));
		shp->shm_pages[idx] = pte_val(pte);
	} else
		--current->maj_flt;  /* was incremented in do_no_page */

done:	/* pte_val(pte) == shp->shm_pages[idx] */
	current->min_flt++;
	atomic_inc(&mem_map[MAP_NR(pte_page(pte))].count);
	return pte_modify(pte, shmd->vm_page_prot);
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
	struct vm_area_struct *shmd;
	unsigned long swap_nr;
	unsigned long id, idx;
	int loop = 0;
	int counter;
	
	counter = shm_rss >> prio;
	if (!counter || !(swap_nr = get_swap_page()))
		return 0;

 check_id:
	shp = shm_segs[swap_id];
	if (shp == IPC_UNUSED || shp == IPC_NOID || shp->u.shm_perm.mode & SHM_LOCKED ) {
		next_id:
		swap_idx = 0;
		if (++swap_id > max_shmid) {
			if (loop)
				goto failed;
			loop = 1;
			swap_id = 0;
		}
		goto check_id;
	}
	id = swap_id;

 check_table:
	idx = swap_idx++;
	if (idx >= shp->shm_npages)
		goto next_id;

	page = __pte(shp->shm_pages[idx]);
	if (!pte_present(page))
		goto check_table;
	if ((gfp_mask & __GFP_DMA) && !PageDMA(&mem_map[MAP_NR(pte_page(page))]))
		goto check_table;
	swap_attempts++;

	if (--counter < 0) { /* failed */
		failed:
		swap_free (swap_nr);
		return 0;
	}
	if (shp->attaches)
	  for (shmd = shp->attaches; ; ) {
	    do {
		pgd_t *page_dir;
		pmd_t *page_middle;
		pte_t *page_table, pte;
		unsigned long tmp;

		if ((SWP_OFFSET(shmd->vm_pte) & SHM_ID_MASK) != id) {
			printk ("shm_swap: id=%ld does not match shmd->vm_pte.id=%ld\n",
				id, SWP_OFFSET(shmd->vm_pte) & SHM_ID_MASK);
			continue;
		}
		tmp = shmd->vm_start + (idx << PAGE_SHIFT) - shmd->vm_offset;
		if (!(tmp >= shmd->vm_start && tmp < shmd->vm_end))
			continue;
		page_dir = pgd_offset(shmd->vm_mm,tmp);
		if (pgd_none(*page_dir) || pgd_bad(*page_dir)) {
			printk("shm_swap: bad pgtbl! id=%ld start=%lx idx=%ld\n",
					id, shmd->vm_start, idx);
			pgd_clear(page_dir);
			continue;
		}
		page_middle = pmd_offset(page_dir,tmp);
		if (pmd_none(*page_middle) || pmd_bad(*page_middle)) {
			printk("shm_swap: bad pgmid! id=%ld start=%lx idx=%ld\n",
					id, shmd->vm_start, idx);
			pmd_clear(page_middle);
			continue;
		}
		page_table = pte_offset(page_middle,tmp);
		pte = *page_table;
		if (!pte_present(pte))
			continue;
		if (pte_young(pte)) {
			set_pte(page_table, pte_mkold(pte));
			continue;
		}
		if (pte_page(pte) != pte_page(page))
			printk("shm_swap_out: page and pte mismatch %lx %lx\n",
			       pte_page(pte),pte_page(page));
		flush_cache_page(shmd, tmp);
		set_pte(page_table,
		  __pte(shmd->vm_pte + SWP_ENTRY(0, idx << SHM_IDX_SHIFT)));
		atomic_dec(&mem_map[MAP_NR(pte_page(pte))].count);
		if (shmd->vm_mm->rss > 0)
			shmd->vm_mm->rss--;
		flush_tlb_page(shmd, tmp);
	    /* continue looping through the linked list */
	    } while (0);
	    shmd = shmd->vm_next_share;
	    if (!shmd)
		break;
	}

	if (atomic_read(&mem_map[MAP_NR(pte_page(page))].count) != 1)
		goto check_table;
	shp->shm_pages[idx] = swap_nr;
	rw_swap_page_nocache (WRITE, swap_nr, (char *) pte_page(page));
	free_page(pte_page(page));
	swap_successes++;
	shm_swp++;
	shm_rss--;
	return 1;
}

/*
 * Free the swap entry and set the new pte for the shm page.
 */
static void shm_unuse_page(struct shmid_kernel *shp, unsigned long idx,
			   unsigned long page, unsigned long entry)
{
	pte_t pte;

	pte = pte_mkdirty(mk_pte(page, PAGE_SHARED));
	shp->shm_pages[idx] = pte_val(pte);
	atomic_inc(&mem_map[MAP_NR(page)].count);
	shm_rss++;

	swap_free(entry);
	shm_swp--;
}

/*
 * unuse_shm() search for an eventually swapped out shm page.
 */
void shm_unuse(unsigned long entry, unsigned long page)
{
	int i, n;

	for (i = 0; i < SHMMNI; i++)
		if (shm_segs[i] != IPC_UNUSED && shm_segs[i] != IPC_NOID)
			for (n = 0; n < shm_segs[i]->shm_npages; n++)
				if (shm_segs[i]->shm_pages[n] == entry)
				{
					shm_unuse_page(shm_segs[i], n,
						       page, entry);
					return;
				}
}
