/*
 *	linux/mm/mmap.c
 *
 * Written by obz.
 */
#include <linux/slab.h>
#include <linux/shm.h>
#include <linux/mman.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/swapctl.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <linux/file.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>

/* description of effects of mapping type and prot in current implementation.
 * this is due to the limited x86 page protection hardware.  The expected
 * behavior is in parens:
 *
 * map_type	prot
 *		PROT_NONE	PROT_READ	PROT_WRITE	PROT_EXEC
 * MAP_SHARED	r: (no) no	r: (yes) yes	r: (no) yes	r: (no) yes
 *		w: (no) no	w: (no) no	w: (yes) yes	w: (no) no
 *		x: (no) no	x: (no) yes	x: (no) yes	x: (yes) yes
 *		
 * MAP_PRIVATE	r: (no) no	r: (yes) yes	r: (no) yes	r: (no) yes
 *		w: (no) no	w: (no) no	w: (copy) copy	w: (no) no
 *		x: (no) no	x: (no) yes	x: (no) yes	x: (yes) yes
 *
 */
pgprot_t protection_map[16] = {
	__P000, __P001, __P010, __P011, __P100, __P101, __P110, __P111,
	__S000, __S001, __S010, __S011, __S100, __S101, __S110, __S111
};

/* SLAB cache for vm_area_struct's. */
kmem_cache_t *vm_area_cachep;

int sysctl_overcommit_memory;

/* Check that a process has enough memory to allocate a
 * new virtual mapping.
 */
int vm_enough_memory(long pages)
{
	/* Stupid algorithm to decide if we have enough memory: while
	 * simple, it hopefully works in most obvious cases.. Easy to
	 * fool it, but this should catch most mistakes.
	 */
	/* 23/11/98 NJC: Somewhat less stupid version of algorithm,
	 * which tries to do "TheRightThing".  Instead of using half of
	 * (buffers+cache), use the minimum values.  Allow an extra 2%
	 * of num_physpages for safety margin.
	 */

	long free;
	
        /* Sometimes we want to use more memory than we have. */
	if (sysctl_overcommit_memory)
	    return 1;

	free = buffermem >> PAGE_SHIFT;
	free += page_cache_size;
	free += nr_free_pages;
	free += nr_swap_pages;
	free -= (page_cache.min_percent + buffer_mem.min_percent + 2)*num_physpages/100; 
	return free > pages;
}

/* Remove one vm structure from the inode's i_mmap ring. */
static inline void remove_shared_vm_struct(struct vm_area_struct *vma)
{
	struct file * file = vma->vm_file;

	if (file) {
		if (vma->vm_flags & VM_DENYWRITE)
			file->f_dentry->d_inode->i_writecount++;
		if(vma->vm_next_share)
			vma->vm_next_share->vm_pprev_share = vma->vm_pprev_share;
		*vma->vm_pprev_share = vma->vm_next_share;
	}
}

asmlinkage unsigned long sys_brk(unsigned long brk)
{
	unsigned long rlim, retval;
	unsigned long newbrk, oldbrk;
	struct mm_struct *mm = current->mm;

	down(&mm->mmap_sem);

	/*
	 * This lock-kernel is one of the main contention points for
	 * certain normal loads.  And it really should not be here: almost
	 * everything in brk()/mmap()/munmap() is protected sufficiently by
	 * the mmap semaphore that we got above.
	 *
	 * We should move this into the few things that really want the
	 * lock, namely anything that actually touches a file descriptor
	 * etc.  We can do all the normal anonymous mapping cases without
	 * ever getting the lock at all - the actual memory management
	 * code is already completely thread-safe.
	 */
	lock_kernel();

	if (brk < mm->end_code)
		goto out;
	newbrk = PAGE_ALIGN(brk);
	oldbrk = PAGE_ALIGN(mm->brk);
	if (oldbrk == newbrk)
		goto set_brk;

	/* Always allow shrinking brk. */
	if (brk <= mm->brk) {
		if (!do_munmap(newbrk, oldbrk-newbrk))
			goto set_brk;
		goto out;
	}

	/* Check against rlimit and stack.. */
	rlim = current->rlim[RLIMIT_DATA].rlim_cur;
	if (rlim < RLIM_INFINITY && brk - mm->end_code > rlim)
		goto out;

	/* Check against existing mmap mappings. */
	if (find_vma_intersection(mm, oldbrk, newbrk+PAGE_SIZE))
		goto out;

	/* Check if we have enough memory.. */
	if (!vm_enough_memory((newbrk-oldbrk) >> PAGE_SHIFT))
		goto out;

	/* Ok, looks good - let it rip. */
	if (do_mmap(NULL, oldbrk, newbrk-oldbrk,
		   PROT_READ|PROT_WRITE|PROT_EXEC,
		   MAP_FIXED|MAP_PRIVATE, 0) != oldbrk)
		goto out;
set_brk:
	mm->brk = brk;
out:
	retval = mm->brk;
	unlock_kernel();
	up(&mm->mmap_sem);
	return retval;
}

/* Combine the mmap "prot" and "flags" argument into one "vm_flags" used
 * internally. Essentially, translate the "PROT_xxx" and "MAP_xxx" bits
 * into "VM_xxx".
 */
static inline unsigned long vm_flags(unsigned long prot, unsigned long flags)
{
#define _trans(x,bit1,bit2) \
((bit1==bit2)?(x&bit1):(x&bit1)?bit2:0)

	unsigned long prot_bits, flag_bits;
	prot_bits =
		_trans(prot, PROT_READ, VM_READ) |
		_trans(prot, PROT_WRITE, VM_WRITE) |
		_trans(prot, PROT_EXEC, VM_EXEC);
	flag_bits =
		_trans(flags, MAP_GROWSDOWN, VM_GROWSDOWN) |
		_trans(flags, MAP_DENYWRITE, VM_DENYWRITE) |
		_trans(flags, MAP_EXECUTABLE, VM_EXECUTABLE);
	return prot_bits | flag_bits;
#undef _trans
}

unsigned long do_mmap(struct file * file, unsigned long addr, unsigned long len,
	unsigned long prot, unsigned long flags, unsigned long off)
{
	struct mm_struct * mm = current->mm;
	struct vm_area_struct * vma;
	int error;

	if ((len = PAGE_ALIGN(len)) == 0)
		return addr;

	if (len > TASK_SIZE || addr > TASK_SIZE-len)
		return -EINVAL;

	/* offset overflow? */
	if (off + len < off)
		return -EINVAL;

	/* Too many mappings? */
	if (mm->map_count > MAX_MAP_COUNT)
		return -ENOMEM;

	/* mlock MCL_FUTURE? */
	if (mm->def_flags & VM_LOCKED) {
		unsigned long locked = mm->locked_vm << PAGE_SHIFT;
		locked += len;
		if (locked > current->rlim[RLIMIT_MEMLOCK].rlim_cur)
			return -EAGAIN;
	}

	/* Do simple checking here so the lower-level routines won't have
	 * to. we assume access permissions have been handled by the open
	 * of the memory object, so we don't do any here.
	 */
	if (file != NULL) {
		switch (flags & MAP_TYPE) {
		case MAP_SHARED:
			if ((prot & PROT_WRITE) && !(file->f_mode & 2))
				return -EACCES;

			/* Make sure we don't allow writing to an append-only file.. */
			if (IS_APPEND(file->f_dentry->d_inode) && (file->f_mode & 2))
				return -EACCES;

			/* make sure there are no mandatory locks on the file. */
			if (locks_verify_locked(file->f_dentry->d_inode))
				return -EAGAIN;

			/* fall through */
		case MAP_PRIVATE:
			if (!(file->f_mode & 1))
				return -EACCES;
			break;

		default:
			return -EINVAL;
		}
	} else if ((flags & MAP_TYPE) != MAP_PRIVATE)
		return -EINVAL;

	/* Obtain the address to map to. we verify (or select) it and ensure
	 * that it represents a valid section of the address space.
	 */
	if (flags & MAP_FIXED) {
		if (addr & ~PAGE_MASK)
			return -EINVAL;
	} else {
		addr = get_unmapped_area(addr, len);
		if (!addr)
			return -ENOMEM;
	}

	/* Determine the object being mapped and call the appropriate
	 * specific mapper. the address has already been validated, but
	 * not unmapped, but the maps are removed from the list.
	 */
	if (file && (!file->f_op || !file->f_op->mmap))
		return -ENODEV;

	vma = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
	if (!vma)
		return -ENOMEM;

	vma->vm_mm = mm;
	vma->vm_start = addr;
	vma->vm_end = addr + len;
	vma->vm_flags = vm_flags(prot,flags) | mm->def_flags;

	if (file) {
		if (file->f_mode & 1)
			vma->vm_flags |= VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC;
		if (flags & MAP_SHARED) {
			vma->vm_flags |= VM_SHARED | VM_MAYSHARE;

			/* This looks strange, but when we don't have the file open
			 * for writing, we can demote the shared mapping to a simpler
			 * private mapping. That also takes care of a security hole
			 * with ptrace() writing to a shared mapping without write
			 * permissions.
			 *
			 * We leave the VM_MAYSHARE bit on, just to get correct output
			 * from /proc/xxx/maps..
			 */
			if (!(file->f_mode & 2))
				vma->vm_flags &= ~(VM_MAYWRITE | VM_SHARED);
		}
	} else
		vma->vm_flags |= VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC;
	vma->vm_page_prot = protection_map[vma->vm_flags & 0x0f];
	vma->vm_ops = NULL;
	vma->vm_offset = off;
	vma->vm_file = NULL;
	vma->vm_pte = 0;

	/* Clear old maps */
	error = -ENOMEM;
	if (do_munmap(addr, len))
		goto free_vma;

	/* Check against address space limit. */
	if ((mm->total_vm << PAGE_SHIFT) + len
	    > current->rlim[RLIMIT_AS].rlim_cur)
		goto free_vma;

	/* Private writable mapping? Check memory availability.. */
	if ((vma->vm_flags & (VM_SHARED | VM_WRITE)) == VM_WRITE &&
	    !(flags & MAP_NORESERVE)				 &&
	    !vm_enough_memory(len >> PAGE_SHIFT))
		goto free_vma;

	if (file) {
		int correct_wcount = 0;
		if (vma->vm_flags & VM_DENYWRITE) {
			if (file->f_dentry->d_inode->i_writecount > 0) {
				error = -ETXTBSY;
				goto free_vma;
			}
	        	/* f_op->mmap might possibly sleep
			 * (generic_file_mmap doesn't, but other code
			 * might). In any case, this takes care of any
			 * race that this might cause.
			 */
			file->f_dentry->d_inode->i_writecount--;
			correct_wcount = 1;
		}
		error = file->f_op->mmap(file, vma);
		/* Fix up the count if necessary, then check for an error */
		if (correct_wcount)
			file->f_dentry->d_inode->i_writecount++;
		if (error)
			goto unmap_and_free_vma;
	}

	/*
	 * merge_segments may merge our vma, so we can't refer to it
	 * after the call.  Save the values we need now ...
	 */
	flags = vma->vm_flags;
	addr = vma->vm_start; /* can addr have changed?? */
	insert_vm_struct(mm, vma);
	merge_segments(mm, vma->vm_start, vma->vm_end);
	
	mm->total_vm += len >> PAGE_SHIFT;
	if (flags & VM_LOCKED) {
		mm->locked_vm += len >> PAGE_SHIFT;
		make_pages_present(addr, addr + len);
	}
	return addr;

unmap_and_free_vma:
	/* Undo any partial mapping done by a device driver. */
	flush_cache_range(mm, vma->vm_start, vma->vm_end);
	zap_page_range(mm, vma->vm_start, vma->vm_end - vma->vm_start);
	flush_tlb_range(mm, vma->vm_start, vma->vm_end);
free_vma:
	kmem_cache_free(vm_area_cachep, vma);
	return error;
}

/* Get an address range which is currently unmapped.
 * For mmap() without MAP_FIXED and shmat() with addr=0.
 * Return value 0 means ENOMEM.
 */
unsigned long get_unmapped_area(unsigned long addr, unsigned long len)
{
	struct vm_area_struct * vmm;

	if (len > TASK_SIZE)
		return 0;
	if (!addr)
		addr = TASK_UNMAPPED_BASE;
	addr = PAGE_ALIGN(addr);

	for (vmm = find_vma(current->mm, addr); ; vmm = vmm->vm_next) {
		/* At this point:  (!vmm || addr < vmm->vm_end). */
		if (TASK_SIZE - len < addr)
			return 0;
		if (!vmm || addr + len <= vmm->vm_start)
			return addr;
		addr = vmm->vm_end;
	}
}

#define vm_avl_empty	(struct vm_area_struct *) NULL

#include "mmap_avl.c"

/* Look up the first VMA which satisfies  addr < vm_end,  NULL if none. */
struct vm_area_struct * find_vma(struct mm_struct * mm, unsigned long addr)
{
	struct vm_area_struct *vma = NULL;

	if (mm) {
		/* Check the cache first. */
		/* (Cache hit rate is typically around 35%.) */
		vma = mm->mmap_cache;
		if (!(vma && vma->vm_end > addr && vma->vm_start <= addr)) {
			if (!mm->mmap_avl) {
				/* Go through the linear list. */
				vma = mm->mmap;
				while (vma && vma->vm_end <= addr)
					vma = vma->vm_next;
			} else {
				/* Then go through the AVL tree quickly. */
				struct vm_area_struct * tree = mm->mmap_avl;
				for (;;) {
					if (tree == vm_avl_empty)
						break;
					if (tree->vm_end > addr) {
						vma = tree;
						if (tree->vm_start <= addr)
							break;
						tree = tree->vm_avl_left;
					} else
						tree = tree->vm_avl_right;
				}
			}
			if (vma)
				mm->mmap_cache = vma;
		}
	}
	return vma;
}

/* Same as find_vma, but also return a pointer to the previous VMA in *pprev. */
struct vm_area_struct * find_vma_prev(struct mm_struct * mm, unsigned long addr,
				      struct vm_area_struct **pprev)
{
	if (mm) {
		if (!mm->mmap_avl) {
			/* Go through the linear list. */
			struct vm_area_struct * prev = NULL;
			struct vm_area_struct * vma = mm->mmap;
			while (vma && vma->vm_end <= addr) {
				prev = vma;
				vma = vma->vm_next;
			}
			*pprev = prev;
			return vma;
		} else {
			/* Go through the AVL tree quickly. */
			struct vm_area_struct * vma = NULL;
			struct vm_area_struct * last_turn_right = NULL;
			struct vm_area_struct * prev = NULL;
			struct vm_area_struct * tree = mm->mmap_avl;
			for (;;) {
				if (tree == vm_avl_empty)
					break;
				if (tree->vm_end > addr) {
					vma = tree;
					prev = last_turn_right;
					if (tree->vm_start <= addr)
						break;
					tree = tree->vm_avl_left;
				} else {
					last_turn_right = tree;
					tree = tree->vm_avl_right;
				}
			}
			if (vma) {
				if (vma->vm_avl_left != vm_avl_empty) {
					prev = vma->vm_avl_left;
					while (prev->vm_avl_right != vm_avl_empty)
						prev = prev->vm_avl_right;
				}
				if ((prev ? prev->vm_next : mm->mmap) != vma)
					printk("find_vma_prev: tree inconsistent with list\n");
				*pprev = prev;
				return vma;
			}
		}
	}
	*pprev = NULL;
	return NULL;
}

/* Normal function to fix up a mapping
 * This function is the default for when an area has no specific
 * function.  This may be used as part of a more specific routine.
 * This function works out what part of an area is affected and
 * adjusts the mapping information.  Since the actual page
 * manipulation is done in do_mmap(), none need be done here,
 * though it would probably be more appropriate.
 *
 * By the time this function is called, the area struct has been
 * removed from the process mapping list, so it needs to be
 * reinserted if necessary.
 *
 * The 4 main cases are:
 *    Unmapping the whole area
 *    Unmapping from the start of the segment to a point in it
 *    Unmapping from an intermediate point to the end
 *    Unmapping between to intermediate points, making a hole.
 *
 * Case 4 involves the creation of 2 new areas, for each side of
 * the hole.  If possible, we reuse the existing area rather than
 * allocate a new one, and the return indicates whether the old
 * area was reused.
 */
static int unmap_fixup(struct vm_area_struct *area, unsigned long addr,
			 size_t len, struct vm_area_struct **extra)
{
	struct vm_area_struct *mpnt;
	unsigned long end = addr + len;

	area->vm_mm->total_vm -= len >> PAGE_SHIFT;
	if (area->vm_flags & VM_LOCKED)
		area->vm_mm->locked_vm -= len >> PAGE_SHIFT;

	/* Unmapping the whole area. */
	if (addr == area->vm_start && end == area->vm_end) {
		if (area->vm_ops && area->vm_ops->close)
			area->vm_ops->close(area);
		if (area->vm_file)
			fput(area->vm_file);
		return 0;
	}

	/* Work out to one of the ends. */
	if (end == area->vm_end)
		area->vm_end = addr;
	else if (addr == area->vm_start) {
		area->vm_offset += (end - area->vm_start);
		area->vm_start = end;
	} else {
	/* Unmapping a hole: area->vm_start < addr <= end < area->vm_end */
		/* Add end mapping -- leave beginning for below */
		mpnt = *extra;
		*extra = NULL;

		mpnt->vm_mm = area->vm_mm;
		mpnt->vm_start = end;
		mpnt->vm_end = area->vm_end;
		mpnt->vm_page_prot = area->vm_page_prot;
		mpnt->vm_flags = area->vm_flags;
		mpnt->vm_ops = area->vm_ops;
		mpnt->vm_offset = area->vm_offset + (end - area->vm_start);
		mpnt->vm_file = area->vm_file;
		mpnt->vm_pte = area->vm_pte;
		if (mpnt->vm_file)
			mpnt->vm_file->f_count++;
		if (mpnt->vm_ops && mpnt->vm_ops->open)
			mpnt->vm_ops->open(mpnt);
		area->vm_end = addr;	/* Truncate area */
		insert_vm_struct(current->mm, mpnt);
	}

	insert_vm_struct(current->mm, area);
	return 1;
}

/*
 * Try to free as many page directory entries as we can,
 * without having to work very hard at actually scanning
 * the page tables themselves.
 *
 * Right now we try to free page tables if we have a nice
 * PGDIR-aligned area that got free'd up. We could be more
 * granular if we want to, but this is fast and simple,
 * and covers the bad cases.
 *
 * "prev", if it exists, points to a vma before the one
 * we just free'd - but there's no telling how much before.
 */
static void free_pgtables(struct mm_struct * mm, struct vm_area_struct *prev,
	unsigned long start, unsigned long end)
{
	unsigned long first = start & PGDIR_MASK;
	unsigned long last = (end & PGDIR_MASK) + PGDIR_SIZE;

	if (!prev) {
		prev = mm->mmap;
		if (!prev)
			goto no_mmaps;
		if (prev->vm_end > start) {
			if (last > prev->vm_end)
				last = prev->vm_end;
			goto no_mmaps;
		}
	}
	for (;;) {
		struct vm_area_struct *next = prev->vm_next;

		if (next) {
			if (next->vm_start < start) {
				prev = next;
				continue;
			}
			if (last > next->vm_start)
				last = next->vm_start;
		}
		if (prev->vm_end > first)
			first = prev->vm_end + PGDIR_SIZE - 1;
		break;
	}
no_mmaps:
	first = first >> PGDIR_SHIFT;
	last = last >> PGDIR_SHIFT;
	if (last > first)
		clear_page_tables(mm, first, last-first);
}

/* Munmap is split into 2 main parts -- this part which finds
 * what needs doing, and the areas themselves, which do the
 * work.  This now handles partial unmappings.
 * Jeremy Fitzhardine <jeremy@sw.oz.au>
 */
int do_munmap(unsigned long addr, size_t len)
{
	struct mm_struct * mm;
	struct vm_area_struct *mpnt, *prev, **npp, *free, *extra;

	if ((addr & ~PAGE_MASK) || addr > TASK_SIZE || len > TASK_SIZE-addr)
		return -EINVAL;

	if ((len = PAGE_ALIGN(len)) == 0)
		return 0;

	/* Check if this memory area is ok - put it on the temporary
	 * list if so..  The checks here are pretty simple --
	 * every area affected in some way (by any overlap) is put
	 * on the list.  If nothing is put on, nothing is affected.
	 */
	mm = current->mm;
	mpnt = find_vma_prev(mm, addr, &prev);
	if (!mpnt)
		return 0;
	/* we have  addr < mpnt->vm_end  */

	if (mpnt->vm_start >= addr+len)
		return 0;

	/* If we'll make "hole", check the vm areas limit */
	if ((mpnt->vm_start < addr && mpnt->vm_end > addr+len)
	    && mm->map_count >= MAX_MAP_COUNT)
		return -ENOMEM;

	/*
	 * We may need one additional vma to fix up the mappings ... 
	 * and this is the last chance for an easy error exit.
	 */
	extra = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
	if (!extra)
		return -ENOMEM;

	npp = (prev ? &prev->vm_next : &mm->mmap);
	free = NULL;
	for ( ; mpnt && mpnt->vm_start < addr+len; mpnt = *npp) {
		*npp = mpnt->vm_next;
		mpnt->vm_next = free;
		free = mpnt;
		if (mm->mmap_avl)
			avl_remove(mpnt, &mm->mmap_avl);
	}

	/* Ok - we have the memory areas we should free on the 'free' list,
	 * so release them, and unmap the page range..
	 * If the one of the segments is only being partially unmapped,
	 * it will put new vm_area_struct(s) into the address space.
	 */
	while ((mpnt = free) != NULL) {
		unsigned long st, end, size;

		free = free->vm_next;

		st = addr < mpnt->vm_start ? mpnt->vm_start : addr;
		end = addr+len;
		end = end > mpnt->vm_end ? mpnt->vm_end : end;
		size = end - st;

		if (mpnt->vm_ops && mpnt->vm_ops->unmap)
			mpnt->vm_ops->unmap(mpnt, st, size);

		remove_shared_vm_struct(mpnt);
		mm->map_count--;

		flush_cache_range(mm, st, end);
		zap_page_range(mm, st, size);
		flush_tlb_range(mm, st, end);

		/*
		 * Fix the mapping, and free the old area if it wasn't reused.
		 */
		if (!unmap_fixup(mpnt, st, size, &extra))
			kmem_cache_free(vm_area_cachep, mpnt);
	}

	/* Release the extra vma struct if it wasn't used */
	if (extra)
		kmem_cache_free(vm_area_cachep, extra);

	free_pgtables(mm, prev, addr, addr+len);

	mm->mmap_cache = NULL;	/* Kill the cache. */
	return 0;
}

asmlinkage int sys_munmap(unsigned long addr, size_t len)
{
	int ret;

	down(&current->mm->mmap_sem);
	lock_kernel();
	ret = do_munmap(addr, len);
	unlock_kernel();
	up(&current->mm->mmap_sem);
	return ret;
}

/* Build the AVL tree corresponding to the VMA list. */
void build_mmap_avl(struct mm_struct * mm)
{
	struct vm_area_struct * vma;

	mm->mmap_avl = NULL;
	for (vma = mm->mmap; vma; vma = vma->vm_next)
		avl_insert(vma, &mm->mmap_avl);
}

/* Release all mmaps. */
void exit_mmap(struct mm_struct * mm)
{
	struct vm_area_struct * mpnt;

	mpnt = mm->mmap;
	mm->mmap = mm->mmap_avl = mm->mmap_cache = NULL;
	mm->rss = 0;
	mm->total_vm = 0;
	mm->locked_vm = 0;
	while (mpnt) {
		struct vm_area_struct * next = mpnt->vm_next;
		unsigned long start = mpnt->vm_start;
		unsigned long end = mpnt->vm_end;
		unsigned long size = end - start;

		if (mpnt->vm_ops) {
			if (mpnt->vm_ops->unmap)
				mpnt->vm_ops->unmap(mpnt, start, size);
			if (mpnt->vm_ops->close)
				mpnt->vm_ops->close(mpnt);
		}
		mm->map_count--;
		remove_shared_vm_struct(mpnt);
		zap_page_range(mm, start, size);
		if (mpnt->vm_file)
			fput(mpnt->vm_file);
		kmem_cache_free(vm_area_cachep, mpnt);
		mpnt = next;
	}

	/* This is just debugging */
	if (mm->map_count)
		printk("exit_mmap: map count is %d\n", mm->map_count);

	clear_page_tables(mm, 0, USER_PTRS_PER_PGD);
}

/* Insert vm structure into process list sorted by address
 * and into the inode's i_mmap ring.
 */
void insert_vm_struct(struct mm_struct *mm, struct vm_area_struct *vmp)
{
	struct vm_area_struct **pprev;
	struct file * file;

	if (!mm->mmap_avl) {
		pprev = &mm->mmap;
		while (*pprev && (*pprev)->vm_start <= vmp->vm_start)
			pprev = &(*pprev)->vm_next;
	} else {
		struct vm_area_struct *prev, *next;
		avl_insert_neighbours(vmp, &mm->mmap_avl, &prev, &next);
		pprev = (prev ? &prev->vm_next : &mm->mmap);
		if (*pprev != next)
			printk("insert_vm_struct: tree inconsistent with list\n");
	}
	vmp->vm_next = *pprev;
	*pprev = vmp;

	mm->map_count++;
	if (mm->map_count >= AVL_MIN_MAP_COUNT && !mm->mmap_avl)
		build_mmap_avl(mm);

	file = vmp->vm_file;
	if (file) {
		struct inode * inode = file->f_dentry->d_inode;
		if (vmp->vm_flags & VM_DENYWRITE)
			inode->i_writecount--;
      
		/* insert vmp into inode's share list */
		if((vmp->vm_next_share = inode->i_mmap) != NULL)
			inode->i_mmap->vm_pprev_share = &vmp->vm_next_share;
		inode->i_mmap = vmp;
		vmp->vm_pprev_share = &inode->i_mmap;
	}
}

/* Merge the list of memory segments if possible.
 * Redundant vm_area_structs are freed.
 * This assumes that the list is ordered by address.
 * We don't need to traverse the entire list, only those segments
 * which intersect or are adjacent to a given interval.
 *
 * We must already hold the mm semaphore when we get here..
 */
void merge_segments (struct mm_struct * mm, unsigned long start_addr, unsigned long end_addr)
{
	struct vm_area_struct *prev, *mpnt, *next, *prev1;

	mpnt = find_vma_prev(mm, start_addr, &prev1);
	if (!mpnt)
		return;

	if (prev1) {
		prev = prev1;
	} else {
		prev = mpnt;
		mpnt = mpnt->vm_next;
	}

	/* prev and mpnt cycle through the list, as long as
	 * start_addr < mpnt->vm_end && prev->vm_start < end_addr
	 */
	for ( ; mpnt && prev->vm_start < end_addr ; prev = mpnt, mpnt = next) {
		next = mpnt->vm_next;

		/* To share, we must have the same file, operations.. */
		if ((mpnt->vm_file != prev->vm_file)||
		    (mpnt->vm_pte != prev->vm_pte)	||
		    (mpnt->vm_ops != prev->vm_ops)	||
		    (mpnt->vm_flags != prev->vm_flags)	||
		    (prev->vm_end != mpnt->vm_start))
			continue;

		/*
		 * If we have a file or it's a shared memory area
		 * the offsets must be contiguous..
		 */
		if ((mpnt->vm_file != NULL) || (mpnt->vm_flags & VM_SHM)) {
			unsigned long off = prev->vm_offset+prev->vm_end-prev->vm_start;
			if (off != mpnt->vm_offset)
				continue;
		}

		/* merge prev with mpnt and set up pointers so the new
		 * big segment can possibly merge with the next one.
		 * The old unused mpnt is freed.
		 */
		if (mm->mmap_avl)
			avl_remove(mpnt, &mm->mmap_avl);
		prev->vm_end = mpnt->vm_end;
		prev->vm_next = mpnt->vm_next;
		if (mpnt->vm_ops && mpnt->vm_ops->close) {
			mpnt->vm_offset += mpnt->vm_end - mpnt->vm_start;
			mpnt->vm_start = mpnt->vm_end;
			mpnt->vm_ops->close(mpnt);
		}
		mm->map_count--;
		remove_shared_vm_struct(mpnt);
		if (mpnt->vm_file)
			fput(mpnt->vm_file);
		kmem_cache_free(vm_area_cachep, mpnt);
		mpnt = prev;
	}
	mm->mmap_cache = NULL;		/* Kill the cache. */
}

void __init vma_init(void)
{
	vm_area_cachep = kmem_cache_create("vm_area_struct",
					   sizeof(struct vm_area_struct),
					   0, SLAB_HWCACHE_ALIGN,
					   NULL, NULL);
	if(!vm_area_cachep)
		panic("vma_init: Cannot alloc vm_area_struct cache.");

	mm_cachep = kmem_cache_create("mm_struct",
				      sizeof(struct mm_struct),
				      0, SLAB_HWCACHE_ALIGN,
				      NULL, NULL);
	if(!mm_cachep)
		panic("vma_init: Cannot alloc mm_struct cache.");
}
