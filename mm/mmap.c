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

#include <asm/segment.h>
#include <asm/system.h>
#include <asm/pgtable.h>

static int anon_map(struct inode *, struct file *, struct vm_area_struct *);

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

unsigned long do_mmap(struct file * file, unsigned long addr, unsigned long len,
	unsigned long prot, unsigned long flags, unsigned long off)
{
	int error;
	struct vm_area_struct * vma;

	if ((len = PAGE_ALIGN(len)) == 0)
		return addr;

	if (addr > TASK_SIZE || len > TASK_SIZE || addr > TASK_SIZE-len)
		return -EINVAL;

	/* offset overflow? */
	if (off + len < off)
		return -EINVAL;

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
			/* fall through */
		case MAP_PRIVATE:
			if (!(file->f_mode & 1))
				return -EACCES;
			break;

		default:
			return -EINVAL;
		}
		if ((flags & MAP_DENYWRITE) && (file->f_inode->i_wcount > 0))
			return -ETXTBSY;
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
		addr = get_unmapped_area(len);
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

	vma->vm_task = current;
	vma->vm_start = addr;
	vma->vm_end = addr + len;
	vma->vm_flags = prot & (VM_READ | VM_WRITE | VM_EXEC);
	vma->vm_flags |= flags & (VM_GROWSDOWN | VM_DENYWRITE | VM_EXECUTABLE);

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

	if (file)
		error = file->f_op->mmap(file->f_inode, file, vma);
	else
		error = anon_map(NULL, NULL, vma);
	
	if (error) {
		kfree(vma);
		return error;
	}
	insert_vm_struct(current, vma);
	merge_segments(current, vma->vm_start, vma->vm_end);
	return addr;
}

/*
 * Get an address range which is currently unmapped.
 * For mmap() without MAP_FIXED and shmat() with addr=0.
 * Return value 0 means ENOMEM.
 */
unsigned long get_unmapped_area(unsigned long len)
{
	struct vm_area_struct * vmm;
	unsigned long gap_start = 0, gap_end;

	for (vmm = current->mm->mmap; ; vmm = vmm->vm_next) {
		if (gap_start < SHM_RANGE_START)
			gap_start = SHM_RANGE_START;
		if (!vmm || ((gap_end = vmm->vm_start) > SHM_RANGE_END))
			gap_end = SHM_RANGE_END;
		gap_start = PAGE_ALIGN(gap_start);
		gap_end &= PAGE_MASK;
		if ((gap_start <= gap_end) && (gap_end - gap_start >= len))
			return gap_start;
		if (!vmm)
			return 0;
		gap_start = vmm->vm_end;
	}
}

asmlinkage int sys_mmap(unsigned long *buffer)
{
	int error;
	unsigned long flags;
	struct file * file = NULL;

	error = verify_area(VERIFY_READ, buffer, 6*sizeof(long));
	if (error)
		return error;
	flags = get_fs_long(buffer+3);
	if (!(flags & MAP_ANONYMOUS)) {
		unsigned long fd = get_fs_long(buffer+4);
		if (fd >= NR_OPEN || !(file = current->files->fd[fd]))
			return -EBADF;
	}
	return do_mmap(file, get_fs_long(buffer), get_fs_long(buffer+1),
		get_fs_long(buffer+2), flags, get_fs_long(buffer+5));
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
#define avl_empty	(struct vm_area_struct *) NULL

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

/* Look up the first VMA which satisfies  addr < vm_end,  NULL if none. */
struct vm_area_struct * find_vma (struct task_struct * task, unsigned long addr)
{
#if 0 /* equivalent, but slow */
	struct vm_area_struct * vma;

	for (vma = task->mm->mmap ; ; vma = vma->vm_next) {
		if (!vma)
			return NULL;
		if (vma->vm_end > addr)
			return vma;
	}
#else
	struct vm_area_struct * result = NULL;
	struct vm_area_struct * tree;

	for (tree = task->mm->mmap_avl ; ; ) {
		if (tree == avl_empty)
			return result;
		if (tree->vm_end > addr) {
			if (tree->vm_start <= addr)
				return tree;
			result = tree;
			tree = tree->vm_avl_left;
		} else
			tree = tree->vm_avl_right;
	}
#endif
}

/* Look up the first VMA which intersects the interval start_addr..end_addr-1,
   NULL if none.  Assume start_addr < end_addr. */
struct vm_area_struct * find_vma_intersection (struct task_struct * task, unsigned long start_addr, unsigned long end_addr)
{
	struct vm_area_struct * vma;

#if 0 /* equivalent, but slow */
	for (vma = task->mm->mmap; vma; vma = vma->vm_next) {
		if (end_addr <= vma->vm_start)
			break;
		if (start_addr < vma->vm_end)
			return vma;
	}
	return NULL;
#else
	vma = find_vma(task,start_addr);
	if (!vma || end_addr <= vma->vm_start)
		return NULL;
	return vma;
#endif
}

/* Look up the nodes at the left and at the right of a given node. */
static void avl_neighbours (struct vm_area_struct * node, struct vm_area_struct * tree, struct vm_area_struct ** to_the_left, struct vm_area_struct ** to_the_right)
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
static void avl_rebalance (struct vm_area_struct *** nodeplaces_ptr, int count)
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
static void avl_insert (struct vm_area_struct * new_node, struct vm_area_struct ** ptree)
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
static void avl_insert_neighbours (struct vm_area_struct * new_node, struct vm_area_struct ** ptree,
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
static void avl_remove (struct vm_area_struct * node_to_delete, struct vm_area_struct ** ptree)
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
void unmap_fixup(struct vm_area_struct *area,
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

		unmap_fixup(mpnt, st, end-st);
		kfree(mpnt);
	}

	unmap_page_range(addr, len);
	return 0;
}

/* Build the AVL tree corresponding to the VMA list. */
void build_mmap_avl(struct task_struct * task)
{
	struct vm_area_struct * vma;

	task->mm->mmap_avl = NULL;
	for (vma = task->mm->mmap; vma; vma = vma->vm_next)
		avl_insert(vma, &task->mm->mmap_avl);
}

/* Release all mmaps. */
void exit_mmap(struct task_struct * task)
{
	struct vm_area_struct * mpnt;

	mpnt = task->mm->mmap;
	task->mm->mmap = NULL;
	task->mm->mmap_avl = NULL;
	while (mpnt) {
		struct vm_area_struct * next = mpnt->vm_next;
		if (mpnt->vm_ops && mpnt->vm_ops->close)
			mpnt->vm_ops->close(mpnt);
		remove_shared_vm_struct(mpnt);
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

/*
 * Map memory not associated with any file into a process
 * address space.  Adjacent memory is merged.
 */
static int anon_map(struct inode *ino, struct file * file, struct vm_area_struct * vma)
{
	if (zeromap_page_range(vma->vm_start, vma->vm_end - vma->vm_start, vma->vm_page_prot))
		return -ENOMEM;
	return 0;
}
