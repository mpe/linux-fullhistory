/*
 *	linux/mm/mmap.c
 *
 * Written by obz.
 */
#include <linux/stat.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/shm.h>
#include <linux/errno.h>
#include <linux/mman.h>
#include <linux/string.h>
#include <linux/malloc.h>
#include <linux/pagemap.h>
#include <linux/swap.h>

#include <asm/segment.h>
#include <asm/system.h>
#include <asm/pgtable.h>

/*
 * description of effects of mapping type and prot in current implementation.
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

/*
 * Check that a process has enough memory to allocate a
 * new virtual mapping.
 */
static inline int vm_enough_memory(long pages)
{
	/*
	 * stupid algorithm to decide if we have enough memory: while
	 * simple, it hopefully works in most obvious cases.. Easy to
	 * fool it, but this should catch most mistakes.
	 */
	long freepages;
	freepages = buffermem >> PAGE_SHIFT;
	freepages += page_cache_size;
	freepages >>= 1;
	freepages += nr_free_pages;
	freepages += nr_swap_pages;
	freepages -= MAP_NR(high_memory) >> 4;
	return freepages > pages;
}

asmlinkage unsigned long sys_brk(unsigned long brk)
{
	unsigned long rlim;
	unsigned long newbrk, oldbrk;

	if (brk < current->mm->end_code)
		return current->mm->brk;
	newbrk = PAGE_ALIGN(brk);
	oldbrk = PAGE_ALIGN(current->mm->brk);
	if (oldbrk == newbrk)
		return current->mm->brk = brk;

	/*
	 * Always allow shrinking brk
	 */
	if (brk <= current->mm->brk) {
		current->mm->brk = brk;
		do_munmap(newbrk, oldbrk-newbrk);
		return brk;
	}
	/*
	 * Check against rlimit and stack..
	 */
	rlim = current->rlim[RLIMIT_DATA].rlim_cur;
	if (rlim >= RLIM_INFINITY)
		rlim = ~0;
	if (brk - current->mm->end_code > rlim)
		return current->mm->brk;

	/*
	 * Check against existing mmap mappings.
	 */
	if (find_vma_intersection(current, oldbrk, newbrk+PAGE_SIZE))
		return current->mm->brk;

	/*
	 * Check if we have enough memory..
	 */
	if (!vm_enough_memory((newbrk-oldbrk) >> PAGE_SHIFT))
		return current->mm->brk;

	/*
	 * Ok, looks good - let it rip.
	 */
	current->mm->brk = brk;
	do_mmap(NULL, oldbrk, newbrk-oldbrk,
		PROT_READ|PROT_WRITE|PROT_EXEC,
		MAP_FIXED|MAP_PRIVATE, 0);
	return brk;
}

/*
 * Combine the mmap "prot" and "flags" argument into one "vm_flags" used
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
	struct vm_area_struct * vma;

	if ((len = PAGE_ALIGN(len)) == 0)
		return addr;

	if (addr > TASK_SIZE || len > TASK_SIZE || addr > TASK_SIZE-len)
		return -EINVAL;

	/* offset overflow? */
	if (off + len < off)
		return -EINVAL;

	/* mlock MCL_FUTURE? */
	if (current->mm->def_flags & VM_LOCKED) {
		unsigned long locked = current->mm->locked_vm << PAGE_SHIFT;
		locked += len;
		if (locked > current->rlim[RLIMIT_MEMLOCK].rlim_cur)
			return -EAGAIN;
	}

	/*
	 * do simple checking here so the lower-level routines won't have
	 * to. we assume access permissions have been handled by the open
	 * of the memory object, so we don't do any here.
	 */

	if (file != NULL) {
		switch (flags & MAP_TYPE) {
		case MAP_SHARED:
			if ((prot & PROT_WRITE) && !(file->f_mode & 2))
				return -EACCES;
			/*
			 * make sure there are no mandatory locks on the file.
			 */
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
		if (flags & MAP_DENYWRITE) {
			if (file->f_inode->i_writecount > 0)
				return -ETXTBSY;
		}
	} else if ((flags & MAP_TYPE) != MAP_PRIVATE)
		return -EINVAL;

	/*
	 * obtain the address to map to. we verify (or select) it and ensure
	 * that it represents a valid section of the address space.
	 */

	if (flags & MAP_FIXED) {
		if (addr & ~PAGE_MASK)
			return -EINVAL;
		if (len > TASK_SIZE || addr > TASK_SIZE - len)
			return -EINVAL;
	} else {
		addr = get_unmapped_area(addr, len);
		if (!addr)
			return -ENOMEM;
	}

	/*
	 * determine the object being mapped and call the appropriate
	 * specific mapper. the address has already been validated, but
	 * not unmapped, but the maps are removed from the list.
	 */
	if (file && (!file->f_op || !file->f_op->mmap))
		return -ENODEV;

	vma = (struct vm_area_struct *)kmalloc(sizeof(struct vm_area_struct),
		GFP_KERNEL);
	if (!vma)
		return -ENOMEM;

	vma->vm_mm = current->mm;
	vma->vm_start = addr;
	vma->vm_end = addr + len;
	vma->vm_flags = vm_flags(prot,flags) | current->mm->def_flags;

	if (file) {
		if (file->f_mode & 1)
			vma->vm_flags |= VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC;
		if (flags & MAP_SHARED) {
			vma->vm_flags |= VM_SHARED | VM_MAYSHARE;
			/*
			 * This looks strange, but when we don't have the file open
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

	/* Private writable mapping? Check memory availability.. */
	if ((vma->vm_flags & (VM_SHARED | VM_WRITE)) == VM_WRITE) {
		if (!vm_enough_memory(len >> PAGE_SHIFT)) {
			kfree(vma);
			return -ENOMEM;
		}
	}

	if (file) {
		int error = file->f_op->mmap(file->f_inode, file, vma);
	
		if (error) {
			kfree(vma);
			return error;
		}
	}

	flags = vma->vm_flags;
	insert_vm_struct(current, vma);
	merge_segments(current, vma->vm_start, vma->vm_end);

	/* merge_segments might have merged our vma, so we can't use it any more */
	current->mm->total_vm += len >> PAGE_SHIFT;
	if (flags & VM_LOCKED) {
		unsigned long start = addr;
		current->mm->locked_vm += len >> PAGE_SHIFT;
		do {
			char c = get_user((char *) start);
			len -= PAGE_SIZE;
			start += PAGE_SIZE;
			__asm__ __volatile__("": :"r" (c));
		} while (len > 0);
	}
	return addr;
}

/*
 * Get an address range which is currently unmapped.
 * For mmap() without MAP_FIXED and shmat() with addr=0.
 * Return value 0 means ENOMEM.
 */
unsigned long get_unmapped_area(unsigned long addr, unsigned long len)
{
	struct vm_area_struct * vmm;

	if (len > TASK_SIZE)
		return 0;
	if (!addr)
		addr = TASK_SIZE / 3;
	addr = PAGE_ALIGN(addr);

	for (vmm = find_vma(current, addr); ; vmm = vmm->vm_next) {
		/* At this point:  (!vmm || addr < vmm->vm_end). */
		if (TASK_SIZE - len < addr)
			return 0;
		if (!vmm || addr + len <= vmm->vm_start)
			return addr;
		addr = vmm->vm_end;
	}
}

/*
 * Searching a VMA in the linear list task->mm->mmap is horribly slow.
 * Use an AVL (Adelson-Velskii and Landis) tree to speed up this search
 * from O(n) to O(log n), where n is the number of VMAs of the task
 * (typically around 6, but may reach 3000 in some cases).
 * Written by Bruno Haible <haible@ma2s2.mathematik.uni-karlsruhe.de>.
 */

/* We keep the list and tree sorted by address. */
#define vm_avl_key	vm_end
#define vm_avl_key_t	unsigned long	/* typeof(vma->avl_key) */

/*
 * task->mm->mmap_avl is the AVL tree corresponding to task->mm->mmap
 * or, more exactly, its root.
 * A vm_area_struct has the following fields:
 *   vm_avl_left     left son of a tree node
 *   vm_avl_right    right son of a tree node
 *   vm_avl_height   1+max(heightof(left),heightof(right))
 * The empty tree is represented as NULL.
 */

/* Since the trees are balanced, their height will never be large. */
#define avl_maxheight	41	/* why this? a small exercise */
#define heightof(tree)	((tree) == avl_empty ? 0 : (tree)->vm_avl_height)
/*
 * Consistency and balancing rules:
 * 1. tree->vm_avl_height == 1+max(heightof(tree->vm_avl_left),heightof(tree->vm_avl_right))
 * 2. abs( heightof(tree->vm_avl_left) - heightof(tree->vm_avl_right) ) <= 1
 * 3. foreach node in tree->vm_avl_left: node->vm_avl_key <= tree->vm_avl_key,
 *    foreach node in tree->vm_avl_right: node->vm_avl_key >= tree->vm_avl_key.
 */

/* Look up the nodes at the left and at the right of a given node. */
static inline void avl_neighbours (struct vm_area_struct * node, struct vm_area_struct * tree, struct vm_area_struct ** to_the_left, struct vm_area_struct ** to_the_right)
{
	vm_avl_key_t key = node->vm_avl_key;

	*to_the_left = *to_the_right = NULL;
	for (;;) {
		if (tree == avl_empty) {
			printk("avl_neighbours: node not found in the tree\n");
			return;
		}
		if (key == tree->vm_avl_key)
			break;
		if (key < tree->vm_avl_key) {
			*to_the_right = tree;
			tree = tree->vm_avl_left;
		} else {
			*to_the_left = tree;
			tree = tree->vm_avl_right;
		}
	}
	if (tree != node) {
		printk("avl_neighbours: node not exactly found in the tree\n");
		return;
	}
	if (tree->vm_avl_left != avl_empty) {
		struct vm_area_struct * node;
		for (node = tree->vm_avl_left; node->vm_avl_right != avl_empty; node = node->vm_avl_right)
			continue;
		*to_the_left = node;
	}
	if (tree->vm_avl_right != avl_empty) {
		struct vm_area_struct * node;
		for (node = tree->vm_avl_right; node->vm_avl_left != avl_empty; node = node->vm_avl_left)
			continue;
		*to_the_right = node;
	}
	if ((*to_the_left && ((*to_the_left)->vm_next != node)) || (node->vm_next != *to_the_right))
		printk("avl_neighbours: tree inconsistent with list\n");
}

/*
 * Rebalance a tree.
 * After inserting or deleting a node of a tree we have a sequence of subtrees
 * nodes[0]..nodes[k-1] such that
 * nodes[0] is the root and nodes[i+1] = nodes[i]->{vm_avl_left|vm_avl_right}.
 */
static inline void avl_rebalance (struct vm_area_struct *** nodeplaces_ptr, int count)
{
	for ( ; count > 0 ; count--) {
		struct vm_area_struct ** nodeplace = *--nodeplaces_ptr;
		struct vm_area_struct * node = *nodeplace;
		struct vm_area_struct * nodeleft = node->vm_avl_left;
		struct vm_area_struct * noderight = node->vm_avl_right;
		int heightleft = heightof(nodeleft);
		int heightright = heightof(noderight);
		if (heightright + 1 < heightleft) {
			/*                                                      */
			/*                            *                         */
			/*                          /   \                       */
			/*                       n+2      n                     */
			/*                                                      */
			struct vm_area_struct * nodeleftleft = nodeleft->vm_avl_left;
			struct vm_area_struct * nodeleftright = nodeleft->vm_avl_right;
			int heightleftright = heightof(nodeleftright);
			if (heightof(nodeleftleft) >= heightleftright) {
				/*                                                        */
				/*                *                    n+2|n+3            */
				/*              /   \                  /    \             */
				/*           n+2      n      -->      /   n+1|n+2         */
				/*           / \                      |    /    \         */
				/*         n+1 n|n+1                 n+1  n|n+1  n        */
				/*                                                        */
				node->vm_avl_left = nodeleftright; nodeleft->vm_avl_right = node;
				nodeleft->vm_avl_height = 1 + (node->vm_avl_height = 1 + heightleftright);
				*nodeplace = nodeleft;
			} else {
				/*                                                        */
				/*                *                     n+2               */
				/*              /   \                 /     \             */
				/*           n+2      n      -->    n+1     n+1           */
				/*           / \                    / \     / \           */
				/*          n  n+1                 n   L   R   n          */
				/*             / \                                        */
				/*            L   R                                       */
				/*                                                        */
				nodeleft->vm_avl_right = nodeleftright->vm_avl_left;
				node->vm_avl_left = nodeleftright->vm_avl_right;
				nodeleftright->vm_avl_left = nodeleft;
				nodeleftright->vm_avl_right = node;
				nodeleft->vm_avl_height = node->vm_avl_height = heightleftright;
				nodeleftright->vm_avl_height = heightleft;
				*nodeplace = nodeleftright;
			}
		}
		else if (heightleft + 1 < heightright) {
			/* similar to the above, just interchange 'left' <--> 'right' */
			struct vm_area_struct * noderightright = noderight->vm_avl_right;
			struct vm_area_struct * noderightleft = noderight->vm_avl_left;
			int heightrightleft = heightof(noderightleft);
			if (heightof(noderightright) >= heightrightleft) {
				node->vm_avl_right = noderightleft; noderight->vm_avl_left = node;
				noderight->vm_avl_height = 1 + (node->vm_avl_height = 1 + heightrightleft);
				*nodeplace = noderight;
			} else {
				noderight->vm_avl_left = noderightleft->vm_avl_right;
				node->vm_avl_right = noderightleft->vm_avl_left;
				noderightleft->vm_avl_right = noderight;
				noderightleft->vm_avl_left = node;
				noderight->vm_avl_height = node->vm_avl_height = heightrightleft;
				noderightleft->vm_avl_height = heightright;
				*nodeplace = noderightleft;
			}
		}
		else {
			int height = (heightleft<heightright ? heightright : heightleft) + 1;
			if (height == node->vm_avl_height)
				break;
			node->vm_avl_height = height;
		}
	}
}

/* Insert a node into a tree. */
static inline void avl_insert (struct vm_area_struct * new_node, struct vm_area_struct ** ptree)
{
	vm_avl_key_t key = new_node->vm_avl_key;
	struct vm_area_struct ** nodeplace = ptree;
	struct vm_area_struct ** stack[avl_maxheight];
	int stack_count = 0;
	struct vm_area_struct *** stack_ptr = &stack[0]; /* = &stack[stackcount] */
	for (;;) {
		struct vm_area_struct * node = *nodeplace;
		if (node == avl_empty)
			break;
		*stack_ptr++ = nodeplace; stack_count++;
		if (key < node->vm_avl_key)
			nodeplace = &node->vm_avl_left;
		else
			nodeplace = &node->vm_avl_right;
	}
	new_node->vm_avl_left = avl_empty;
	new_node->vm_avl_right = avl_empty;
	new_node->vm_avl_height = 1;
	*nodeplace = new_node;
	avl_rebalance(stack_ptr,stack_count);
}

/* Insert a node into a tree, and
 * return the node to the left of it and the node to the right of it.
 */
static inline void avl_insert_neighbours (struct vm_area_struct * new_node, struct vm_area_struct ** ptree,
	struct vm_area_struct ** to_the_left, struct vm_area_struct ** to_the_right)
{
	vm_avl_key_t key = new_node->vm_avl_key;
	struct vm_area_struct ** nodeplace = ptree;
	struct vm_area_struct ** stack[avl_maxheight];
	int stack_count = 0;
	struct vm_area_struct *** stack_ptr = &stack[0]; /* = &stack[stackcount] */
	*to_the_left = *to_the_right = NULL;
	for (;;) {
		struct vm_area_struct * node = *nodeplace;
		if (node == avl_empty)
			break;
		*stack_ptr++ = nodeplace; stack_count++;
		if (key < node->vm_avl_key) {
			*to_the_right = node;
			nodeplace = &node->vm_avl_left;
		} else {
			*to_the_left = node;
			nodeplace = &node->vm_avl_right;
		}
	}
	new_node->vm_avl_left = avl_empty;
	new_node->vm_avl_right = avl_empty;
	new_node->vm_avl_height = 1;
	*nodeplace = new_node;
	avl_rebalance(stack_ptr,stack_count);
}

/* Removes a node out of a tree. */
static inline void avl_remove (struct vm_area_struct * node_to_delete, struct vm_area_struct ** ptree)
{
	vm_avl_key_t key = node_to_delete->vm_avl_key;
	struct vm_area_struct ** nodeplace = ptree;
	struct vm_area_struct ** stack[avl_maxheight];
	int stack_count = 0;
	struct vm_area_struct *** stack_ptr = &stack[0]; /* = &stack[stackcount] */
	struct vm_area_struct ** nodeplace_to_delete;
	for (;;) {
		struct vm_area_struct * node = *nodeplace;
		if (node == avl_empty) {
			/* what? node_to_delete not found in tree? */
			printk("avl_remove: node to delete not found in tree\n");
			return;
		}
		*stack_ptr++ = nodeplace; stack_count++;
		if (key == node->vm_avl_key)
			break;
		if (key < node->vm_avl_key)
			nodeplace = &node->vm_avl_left;
		else
			nodeplace = &node->vm_avl_right;
	}
	nodeplace_to_delete = nodeplace;
	/* Have to remove node_to_delete = *nodeplace_to_delete. */
	if (node_to_delete->vm_avl_left == avl_empty) {
		*nodeplace_to_delete = node_to_delete->vm_avl_right;
		stack_ptr--; stack_count--;
	} else {
		struct vm_area_struct *** stack_ptr_to_delete = stack_ptr;
		struct vm_area_struct ** nodeplace = &node_to_delete->vm_avl_left;
		struct vm_area_struct * node;
		for (;;) {
			node = *nodeplace;
			if (node->vm_avl_right == avl_empty)
				break;
			*stack_ptr++ = nodeplace; stack_count++;
			nodeplace = &node->vm_avl_right;
		}
		*nodeplace = node->vm_avl_left;
		/* node replaces node_to_delete */
		node->vm_avl_left = node_to_delete->vm_avl_left;
		node->vm_avl_right = node_to_delete->vm_avl_right;
		node->vm_avl_height = node_to_delete->vm_avl_height;
		*nodeplace_to_delete = node; /* replace node_to_delete */
		*stack_ptr_to_delete = &node->vm_avl_left; /* replace &node_to_delete->vm_avl_left */
	}
	avl_rebalance(stack_ptr,stack_count);
}

#ifdef DEBUG_AVL

/* print a list */
static void printk_list (struct vm_area_struct * vma)
{
	printk("[");
	while (vma) {
		printk("%08lX-%08lX", vma->vm_start, vma->vm_end);
		vma = vma->vm_next;
		if (!vma)
			break;
		printk(" ");
	}
	printk("]");
}

/* print a tree */
static void printk_avl (struct vm_area_struct * tree)
{
	if (tree != avl_empty) {
		printk("(");
		if (tree->vm_avl_left != avl_empty) {
			printk_avl(tree->vm_avl_left);
			printk("<");
		}
		printk("%08lX-%08lX", tree->vm_start, tree->vm_end);
		if (tree->vm_avl_right != avl_empty) {
			printk(">");
			printk_avl(tree->vm_avl_right);
		}
		printk(")");
	}
}

static char *avl_check_point = "somewhere";

/* check a tree's consistency and balancing */
static void avl_checkheights (struct vm_area_struct * tree)
{
	int h, hl, hr;

	if (tree == avl_empty)
		return;
	avl_checkheights(tree->vm_avl_left);
	avl_checkheights(tree->vm_avl_right);
	h = tree->vm_avl_height;
	hl = heightof(tree->vm_avl_left);
	hr = heightof(tree->vm_avl_right);
	if ((h == hl+1) && (hr <= hl) && (hl <= hr+1))
		return;
	if ((h == hr+1) && (hl <= hr) && (hr <= hl+1))
		return;
	printk("%s: avl_checkheights: heights inconsistent\n",avl_check_point);
}

/* check that all values stored in a tree are < key */
static void avl_checkleft (struct vm_area_struct * tree, vm_avl_key_t key)
{
	if (tree == avl_empty)
		return;
	avl_checkleft(tree->vm_avl_left,key);
	avl_checkleft(tree->vm_avl_right,key);
	if (tree->vm_avl_key < key)
		return;
	printk("%s: avl_checkleft: left key %lu >= top key %lu\n",avl_check_point,tree->vm_avl_key,key);
}

/* check that all values stored in a tree are > key */
static void avl_checkright (struct vm_area_struct * tree, vm_avl_key_t key)
{
	if (tree == avl_empty)
		return;
	avl_checkright(tree->vm_avl_left,key);
	avl_checkright(tree->vm_avl_right,key);
	if (tree->vm_avl_key > key)
		return;
	printk("%s: avl_checkright: right key %lu <= top key %lu\n",avl_check_point,tree->vm_avl_key,key);
}

/* check that all values are properly increasing */
static void avl_checkorder (struct vm_area_struct * tree)
{
	if (tree == avl_empty)
		return;
	avl_checkorder(tree->vm_avl_left);
	avl_checkorder(tree->vm_avl_right);
	avl_checkleft(tree->vm_avl_left,tree->vm_avl_key);
	avl_checkright(tree->vm_avl_right,tree->vm_avl_key);
}

/* all checks */
static void avl_check (struct task_struct * task, char *caller)
{
	avl_check_point = caller;
/*	printk("task \"%s\", %s\n",task->comm,caller); */
/*	printk("task \"%s\" list: ",task->comm); printk_list(task->mm->mmap); printk("\n"); */
/*	printk("task \"%s\" tree: ",task->comm); printk_avl(task->mm->mmap_avl); printk("\n"); */
	avl_checkheights(task->mm->mmap_avl);
	avl_checkorder(task->mm->mmap_avl);
}

#endif


/*
 * Normal function to fix up a mapping
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

	if (addr < area->vm_start || addr >= area->vm_end ||
	    end <= area->vm_start || end > area->vm_end ||
	    end < addr)
	{
		printk("unmap_fixup: area=%lx-%lx, unmap %lx-%lx!!\n",
		       area->vm_start, area->vm_end, addr, end);
		return;
	}
	area->vm_mm->total_vm -= len >> PAGE_SHIFT;
	if (area->vm_flags & VM_LOCKED)
		area->vm_mm->locked_vm -= len >> PAGE_SHIFT;

	/* Unmapping the whole area */
	if (addr == area->vm_start && end == area->vm_end) {
		if (area->vm_ops && area->vm_ops->close)
			area->vm_ops->close(area);
		if (area->vm_inode)
			iput(area->vm_inode);
		return;
	}

	/* Work out to one of the ends */
	if (end == area->vm_end)
		area->vm_end = addr;
	else
	if (addr == area->vm_start) {
		area->vm_offset += (end - area->vm_start);
		area->vm_start = end;
	}
	else {
	/* Unmapping a hole: area->vm_start < addr <= end < area->vm_end */
		/* Add end mapping -- leave beginning for below */
		mpnt = (struct vm_area_struct *)kmalloc(sizeof(*mpnt), GFP_KERNEL);

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
		insert_vm_struct(current, mpnt);
	}

	/* construct whatever mapping is needed */
	mpnt = (struct vm_area_struct *)kmalloc(sizeof(*mpnt), GFP_KERNEL);
	if (!mpnt)
		return;
	*mpnt = *area;
	if (mpnt->vm_ops && mpnt->vm_ops->open)
		mpnt->vm_ops->open(mpnt);
	if (area->vm_ops && area->vm_ops->close) {
		area->vm_end = area->vm_start;
		area->vm_ops->close(area);
	}
	insert_vm_struct(current, mpnt);
}

asmlinkage int sys_munmap(unsigned long addr, size_t len)
{
	return do_munmap(addr, len);
}

/*
 * Munmap is split into 2 main parts -- this part which finds
 * what needs doing, and the areas themselves, which do the
 * work.  This now handles partial unmappings.
 * Jeremy Fitzhardine <jeremy@sw.oz.au>
 */
int do_munmap(unsigned long addr, size_t len)
{
	struct vm_area_struct *mpnt, *prev, *next, **npp, *free;

	if ((addr & ~PAGE_MASK) || addr > TASK_SIZE || len > TASK_SIZE-addr)
		return -EINVAL;

	if ((len = PAGE_ALIGN(len)) == 0)
		return 0;

	/*
	 * Check if this memory area is ok - put it on the temporary
	 * list if so..  The checks here are pretty simple --
	 * every area affected in some way (by any overlap) is put
	 * on the list.  If nothing is put on, nothing is affected.
	 */
	mpnt = find_vma(current, addr);
	if (!mpnt)
		return 0;
	avl_neighbours(mpnt, current->mm->mmap_avl, &prev, &next);
	/* we have  prev->vm_next == mpnt && mpnt->vm_next = next */
	/* and  addr < mpnt->vm_end  */

	npp = (prev ? &prev->vm_next : &current->mm->mmap);
	free = NULL;
	for ( ; mpnt && mpnt->vm_start < addr+len; mpnt = *npp) {
		*npp = mpnt->vm_next;
		mpnt->vm_next = free;
		free = mpnt;
		avl_remove(mpnt, &current->mm->mmap_avl);
	}

	if (free == NULL)
		return 0;

	/*
	 * Ok - we have the memory areas we should free on the 'free' list,
	 * so release them, and unmap the page range..
	 * If the one of the segments is only being partially unmapped,
	 * it will put new vm_area_struct(s) into the address space.
	 */
	while (free) {
		unsigned long st, end;

		mpnt = free;
		free = free->vm_next;

		remove_shared_vm_struct(mpnt);

		st = addr < mpnt->vm_start ? mpnt->vm_start : addr;
		end = addr+len;
		end = end > mpnt->vm_end ? mpnt->vm_end : end;

		if (mpnt->vm_ops && mpnt->vm_ops->unmap)
			mpnt->vm_ops->unmap(mpnt, st, end-st);
		zap_page_range(current->mm, st, end-st);
		unmap_fixup(mpnt, st, end-st);
		kfree(mpnt);
	}

	zap_page_range(current->mm, addr, len);
	return 0;
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
	mm->mmap = NULL;
	mm->mmap_avl = NULL;
	mm->rss = 0;
	mm->total_vm = 0;
	mm->locked_vm = 0;
	while (mpnt) {
		struct vm_area_struct * next = mpnt->vm_next;
		if (mpnt->vm_ops) {
			if (mpnt->vm_ops->unmap)
				mpnt->vm_ops->unmap(mpnt, mpnt->vm_start, mpnt->vm_end-mpnt->vm_start);
			if (mpnt->vm_ops->close)
				mpnt->vm_ops->close(mpnt);
		}
		remove_shared_vm_struct(mpnt);
		zap_page_range(mm, mpnt->vm_start, mpnt->vm_end-mpnt->vm_start);
		if (mpnt->vm_inode)
			iput(mpnt->vm_inode);
		kfree(mpnt);
		mpnt = next;
	}
}

/*
 * Insert vm structure into process list sorted by address
 * and into the inode's i_mmap ring.
 */
void insert_vm_struct(struct task_struct *t, struct vm_area_struct *vmp)
{
	struct vm_area_struct *share;
	struct inode * inode;

#if 0 /* equivalent, but slow */
	struct vm_area_struct **p, *mpnt;

	p = &t->mm->mmap;
	while ((mpnt = *p) != NULL) {
		if (mpnt->vm_start > vmp->vm_start)
			break;
		if (mpnt->vm_end > vmp->vm_start)
			printk("insert_vm_struct: overlapping memory areas\n");
		p = &mpnt->vm_next;
	}
	vmp->vm_next = mpnt;
	*p = vmp;
#else
	struct vm_area_struct * prev, * next;

	avl_insert_neighbours(vmp, &t->mm->mmap_avl, &prev, &next);
	if ((prev ? prev->vm_next : t->mm->mmap) != next)
		printk("insert_vm_struct: tree inconsistent with list\n");
	if (prev)
		prev->vm_next = vmp;
	else
		t->mm->mmap = vmp;
	vmp->vm_next = next;
#endif

	inode = vmp->vm_inode;
	if (!inode)
		return;

	/* insert vmp into inode's circular share list */
	if ((share = inode->i_mmap)) {
		vmp->vm_next_share = share->vm_next_share;
		vmp->vm_next_share->vm_prev_share = vmp;
		share->vm_next_share = vmp;
		vmp->vm_prev_share = share;
	} else
		inode->i_mmap = vmp->vm_next_share = vmp->vm_prev_share = vmp;
}

/*
 * Remove one vm structure from the inode's i_mmap ring.
 */
void remove_shared_vm_struct(struct vm_area_struct *mpnt)
{
	struct inode * inode = mpnt->vm_inode;

	if (!inode)
		return;

	if (mpnt->vm_next_share == mpnt) {
		if (inode->i_mmap != mpnt)
			printk("Inode i_mmap ring corrupted\n");
		inode->i_mmap = NULL;
		return;
	}

	if (inode->i_mmap == mpnt)
		inode->i_mmap = mpnt->vm_next_share;

	mpnt->vm_prev_share->vm_next_share = mpnt->vm_next_share;
	mpnt->vm_next_share->vm_prev_share = mpnt->vm_prev_share;
}

/*
 * Merge the list of memory segments if possible.
 * Redundant vm_area_structs are freed.
 * This assumes that the list is ordered by address.
 * We don't need to traverse the entire list, only those segments
 * which intersect or are adjacent to a given interval.
 */
void merge_segments (struct task_struct * task, unsigned long start_addr, unsigned long end_addr)
{
	struct vm_area_struct *prev, *mpnt, *next;

	mpnt = find_vma(task, start_addr);
	if (!mpnt)
		return;
	avl_neighbours(mpnt, task->mm->mmap_avl, &prev, &next);
	/* we have  prev->vm_next == mpnt && mpnt->vm_next = next */

	if (!prev) {
		prev = mpnt;
		mpnt = next;
	}

	/* prev and mpnt cycle through the list, as long as
	 * start_addr < mpnt->vm_end && prev->vm_start < end_addr
	 */
	for ( ; mpnt && prev->vm_start < end_addr ; prev = mpnt, mpnt = next) {
#if 0
		printk("looping in merge_segments, mpnt=0x%lX\n", (unsigned long) mpnt);
#endif

		next = mpnt->vm_next;

		/*
		 * To share, we must have the same inode, operations.. 
		 */
		if (mpnt->vm_inode != prev->vm_inode)
			continue;
		if (mpnt->vm_pte != prev->vm_pte)
			continue;
		if (mpnt->vm_ops != prev->vm_ops)
			continue;
		if (mpnt->vm_flags != prev->vm_flags)
			continue;
		if (prev->vm_end != mpnt->vm_start)
			continue;
		/*
		 * and if we have an inode, the offsets must be contiguous..
		 */
		if ((mpnt->vm_inode != NULL) || (mpnt->vm_flags & VM_SHM)) {
			if (prev->vm_offset + prev->vm_end - prev->vm_start != mpnt->vm_offset)
				continue;
		}

		/*
		 * merge prev with mpnt and set up pointers so the new
		 * big segment can possibly merge with the next one.
		 * The old unused mpnt is freed.
		 */
		avl_remove(mpnt, &task->mm->mmap_avl);
		prev->vm_end = mpnt->vm_end;
		prev->vm_next = mpnt->vm_next;
		if (mpnt->vm_ops && mpnt->vm_ops->close) {
			mpnt->vm_offset += mpnt->vm_end - mpnt->vm_start;
			mpnt->vm_start = mpnt->vm_end;
			mpnt->vm_ops->close(mpnt);
		}
		remove_shared_vm_struct(mpnt);
		if (mpnt->vm_inode)
			mpnt->vm_inode->i_count--;
		kfree_s(mpnt, sizeof(*mpnt));
		mpnt = prev;
	}
}
