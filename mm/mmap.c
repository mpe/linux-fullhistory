/*
 *	linux/mm/mmap.c
 *
 * Written by obz.
 */
#include <linux/stat.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/shm.h>
#include <linux/errno.h>
#include <linux/mman.h>
#include <linux/string.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>

#include <asm/uaccess.h>
#include <asm/system.h>
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
static inline int vm_enough_memory(long pages)
{
	/* Stupid algorithm to decide if we have enough memory: while
	 * simple, it hopefully works in most obvious cases.. Easy to
	 * fool it, but this should catch most mistakes.
	 */
	long freepages;
	
        /* Sometimes we want to use more memory than we have. */
	if (sysctl_overcommit_memory)
	    return 1;

	freepages = buffermem >> PAGE_SHIFT;
	freepages += page_cache_size;
	freepages >>= 1;
	freepages += nr_free_pages;
	freepages += nr_swap_pages;
	freepages -= num_physpages >> 4;
	return freepages > pages;
}

/* Remove one vm structure from the inode's i_mmap ring. */
static inline void remove_shared_vm_struct(struct vm_area_struct *vma)
{
	struct inode * inode = vma->vm_inode;

	if (inode) {
		if (vma->vm_flags & VM_DENYWRITE)
			inode->i_writecount++;
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

	lock_kernel();
	retval = mm->brk;
	if (brk < mm->end_code)
		goto out;
	newbrk = PAGE_ALIGN(brk);
	oldbrk = PAGE_ALIGN(mm->brk);
	if (oldbrk == newbrk) {
		retval = mm->brk = brk;
		goto out;
	}

	/* Always allow shrinking brk. */
	if (brk <= mm->brk) {
		retval = mm->brk = brk;
		do_munmap(newbrk, oldbrk-newbrk);
		goto out;
	}

	/* Check against rlimit and stack.. */
	retval = mm->brk;
	rlim = current->rlim[RLIMIT_DATA].rlim_cur;
	if (rlim >= RLIM_INFINITY)
		rlim = ~0;
	if (brk - mm->end_code > rlim)
		goto out;

	/* Check against existing mmap mappings. */
	if (find_vma_intersection(mm, oldbrk, newbrk+PAGE_SIZE))
		goto out;

	/* Check if we have enough memory.. */
	if (!vm_enough_memory((newbrk-oldbrk) >> PAGE_SHIFT))
		goto out;

	/* Ok, looks good - let it rip. */
	if(do_mmap(NULL, oldbrk, newbrk-oldbrk,
		   PROT_READ|PROT_WRITE|PROT_EXEC,
		   MAP_FIXED|MAP_PRIVATE, 0) == oldbrk)
		mm->brk = brk;
	retval = mm->brk;
out:
	unlock_kernel();
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
	int correct_wcount = 0;

	if ((len = PAGE_ALIGN(len)) == 0)
		return addr;

	if (len > TASK_SIZE || addr > TASK_SIZE-len)
		return -EINVAL;

	/* offset overflow? */
	if (off + len < off)
		return -EINVAL;

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

			/* make sure there are no mandatory locks on the file. */
			if (locks_verify_locked(file->f_inode))
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
	vma->vm_inode = NULL;
	vma->vm_pte = 0;

	do_munmap(addr, len);	/* Clear old maps */

	/* Check against address space limit. */
	if ((mm->total_vm << PAGE_SHIFT) + len
	    > current->rlim[RLIMIT_AS].rlim_cur) {
		kmem_cache_free(vm_area_cachep, vma);
		return -ENOMEM;
	}

	/* Private writable mapping? Check memory availability.. */
	if ((vma->vm_flags & (VM_SHARED | VM_WRITE)) == VM_WRITE) {
		if (!(flags & MAP_NORESERVE) &&
		    !vm_enough_memory(len >> PAGE_SHIFT)) {
			kmem_cache_free(vm_area_cachep, vma);
			return -ENOMEM;
		}
	}

	if (file) {
		int error = 0;
		if (vma->vm_flags & VM_DENYWRITE) {
			if (file->f_inode->i_writecount > 0)
				error = -ETXTBSY;
			else {
	        		/* f_op->mmap might possibly sleep
				 * (generic_file_mmap doesn't, but other code
				 * might). In any case, this takes care of any
				 * race that this might cause.
				 */
				file->f_inode->i_writecount--;
				correct_wcount = 1;
			}
		}
		if (!error)
			error = file->f_op->mmap(file->f_inode, file, vma);
	
		if (error) {
			if (correct_wcount)
				file->f_inode->i_writecount++;
			kmem_cache_free(vm_area_cachep, vma);
			return error;
		}
	}

	flags = vma->vm_flags;
	insert_vm_struct(mm, vma);
	if (correct_wcount)
		file->f_inode->i_writecount++;
	merge_segments(mm, vma->vm_start, vma->vm_end);

	/* merge_segments might have merged our vma, so we can't use it any more */
	mm->total_vm += len >> PAGE_SHIFT;
	if ((flags & VM_LOCKED) && !(flags & VM_IO)) {
		unsigned long start = addr;
		mm->locked_vm += len >> PAGE_SHIFT;
		do {
			char c;
			get_user(c,(char *) start);
			len -= PAGE_SIZE;
			start += PAGE_SIZE;
			__asm__ __volatile__("": :"r" (c));
		} while (len > 0);
	}
	return addr;
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
 * the hole.
 */
static void unmap_fixup(struct vm_area_struct *area,
		 unsigned long addr, size_t len)
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
		if (area->vm_inode)
			iput(area->vm_inode);
		return;
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
		mpnt = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);

		if (!mpnt)
			return;
		*mpnt = *area;
		mpnt->vm_offset += (end - area->vm_start);
		mpnt->vm_start = end;
		if (mpnt->vm_inode)
			mpnt->vm_inode->i_count++;
		if (mpnt->vm_ops && mpnt->vm_ops->open)
			mpnt->vm_ops->open(mpnt);
		area->vm_end = addr;	/* Truncate area */
		insert_vm_struct(current->mm, mpnt);
	}

	/* Construct whatever mapping is needed. */
	mpnt = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
	if (!mpnt)
		return;
	*mpnt = *area;
	if (mpnt->vm_ops && mpnt->vm_ops->open)
		mpnt->vm_ops->open(mpnt);
	if (area->vm_ops && area->vm_ops->close) {
		area->vm_end = area->vm_start;
		area->vm_ops->close(area);
	}
	insert_vm_struct(current->mm, mpnt);
}

asmlinkage int sys_munmap(unsigned long addr, size_t len)
{
	int ret;

	lock_kernel();
	ret = do_munmap(addr, len);
	unlock_kernel();
	return ret;
}

/* Munmap is split into 2 main parts -- this part which finds
 * what needs doing, and the areas themselves, which do the
 * work.  This now handles partial unmappings.
 * Jeremy Fitzhardine <jeremy@sw.oz.au>
 */
int do_munmap(unsigned long addr, size_t len)
{
	struct vm_area_struct *mpnt, *next, *free;

	if ((addr & ~PAGE_MASK) || addr > TASK_SIZE || len > TASK_SIZE-addr)
		return -EINVAL;

	if ((len = PAGE_ALIGN(len)) == 0)
		return 0;

	/* Check if this memory area is ok - put it on the temporary
	 * list if so..  The checks here are pretty simple --
	 * every area affected in some way (by any overlap) is put
	 * on the list.  If nothing is put on, nothing is affected.
	 */
	mpnt = current->mm->mmap;
	while(mpnt && mpnt->vm_end <= addr)
		mpnt = mpnt->vm_next;
	if (!mpnt)
		return 0;

	next = mpnt->vm_next;

	/* we have mpnt->vm_next = next and addr < mpnt->vm_end */
	free = NULL;
	for ( ; mpnt && mpnt->vm_start < addr+len; ) {
		struct vm_area_struct *next = mpnt->vm_next;

		if(mpnt->vm_next)
			mpnt->vm_next->vm_pprev = mpnt->vm_pprev;
		*mpnt->vm_pprev = mpnt->vm_next;

		mpnt->vm_next = free;
		free = mpnt;
		mpnt = next;
	}
	if (free == NULL)
		return 0;

	/* Ok - we have the memory areas we should free on the 'free' list,
	 * so release them, and unmap the page range..
	 * If the one of the segments is only being partially unmapped,
	 * it will put new vm_area_struct(s) into the address space.
	 */
	do {
		unsigned long st, end, size;

		mpnt = free;
		free = free->vm_next;

		remove_shared_vm_struct(mpnt);

		st = addr < mpnt->vm_start ? mpnt->vm_start : addr;
		end = addr+len;
		end = end > mpnt->vm_end ? mpnt->vm_end : end;
		size = end - st;

		if (mpnt->vm_ops && mpnt->vm_ops->unmap)
			mpnt->vm_ops->unmap(mpnt, st, size);

		flush_cache_range(current->mm, st, end);
		zap_page_range(current->mm, st, size);
		flush_tlb_range(current->mm, st, end);

		unmap_fixup(mpnt, st, size);

		kmem_cache_free(vm_area_cachep, mpnt);
	} while (free);

	current->mm->mmap_cache = NULL;		/* Kill the cache. */
	return 0;
}

/* Release all mmaps. */
void exit_mmap(struct mm_struct * mm)
{
	struct vm_area_struct * mpnt;

	mpnt = mm->mmap;
	mm->mmap = mm->mmap_cache = NULL;
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
		remove_shared_vm_struct(mpnt);
		zap_page_range(mm, start, size);
		if (mpnt->vm_inode)
			iput(mpnt->vm_inode);
		kmem_cache_free(vm_area_cachep, mpnt);
		mpnt = next;
	}
}

/* Insert vm structure into process list sorted by address
 * and into the inode's i_mmap ring.
 */
void insert_vm_struct(struct mm_struct *mm, struct vm_area_struct *vmp)
{
	struct vm_area_struct **pprev = &mm->mmap;
	struct inode * inode;

	/* Find where to link it in. */
	while(*pprev && (*pprev)->vm_start <= vmp->vm_start)
		pprev = &(*pprev)->vm_next;

	/* Insert it. */
	if((vmp->vm_next = *pprev) != NULL)
		(*pprev)->vm_pprev = &vmp->vm_next;
	*pprev = vmp;
	vmp->vm_pprev = pprev;

	inode = vmp->vm_inode;
	if (inode) {
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
 */
void merge_segments (struct mm_struct * mm, unsigned long start_addr, unsigned long end_addr)
{
	struct vm_area_struct *prev, *mpnt, *next;

	down(&mm->mmap_sem);

	prev = NULL;
	mpnt = mm->mmap;
	while(mpnt && mpnt->vm_end <= start_addr) {
		prev = mpnt;
		mpnt = mpnt->vm_next;
	}
	if (!mpnt)
		goto no_vma;

	next = mpnt->vm_next;

	/* we have prev->vm_next == mpnt && mpnt->vm_next = next */
	if (!prev) {
		prev = mpnt;
		mpnt = next;
	}

	/* prev and mpnt cycle through the list, as long as
	 * start_addr < mpnt->vm_end && prev->vm_start < end_addr
	 */
	for ( ; mpnt && prev->vm_start < end_addr ; prev = mpnt, mpnt = next) {
		next = mpnt->vm_next;

		/* To share, we must have the same inode, operations.. */
		if ((mpnt->vm_inode != prev->vm_inode)	||
		    (mpnt->vm_pte != prev->vm_pte)	||
		    (mpnt->vm_ops != prev->vm_ops)	||
		    (mpnt->vm_flags != prev->vm_flags)	||
		    (prev->vm_end != mpnt->vm_start))
			continue;

		/* and if we have an inode, the offsets must be contiguous.. */
		if ((mpnt->vm_inode != NULL) || (mpnt->vm_flags & VM_SHM)) {
			unsigned long off = prev->vm_offset+prev->vm_end-prev->vm_start;
			if (off != mpnt->vm_offset)
				continue;
		}

		/* merge prev with mpnt and set up pointers so the new
		 * big segment can possibly merge with the next one.
		 * The old unused mpnt is freed.
		 */
		if(mpnt->vm_next)
			mpnt->vm_next->vm_pprev = mpnt->vm_pprev;
		*mpnt->vm_pprev = mpnt->vm_next;

		prev->vm_end = mpnt->vm_end;
		if (mpnt->vm_ops && mpnt->vm_ops->close) {
			mpnt->vm_offset += mpnt->vm_end - mpnt->vm_start;
			mpnt->vm_start = mpnt->vm_end;
			mpnt->vm_ops->close(mpnt);
		}
		remove_shared_vm_struct(mpnt);
		if (mpnt->vm_inode)
			mpnt->vm_inode->i_count--;
		kmem_cache_free(vm_area_cachep, mpnt);
		mpnt = prev;
	}
	mm->mmap_cache = NULL;		/* Kill the cache. */
no_vma:
	up(&mm->mmap_sem);
}

void vma_init(void)
{
	vm_area_cachep = kmem_cache_create("vm_area_struct",
					   sizeof(struct vm_area_struct),
					   sizeof(long)*8, SLAB_HWCACHE_ALIGN,
					   NULL, NULL);
	if(!vm_area_cachep)
		panic("vma_init: Cannot alloc vm_area_struct cache.");

	mm_cachep = kmem_cache_create("mm_struct",
				      sizeof(struct mm_struct),
				      sizeof(long) * 4, SLAB_HWCACHE_ALIGN,
				      NULL, NULL);
	if(!mm_cachep)
		panic("vma_init: Cannot alloc mm_struct cache.");
}
