#ifndef _LINUX_POLL_H
#define _LINUX_POLL_H

#include <asm/poll.h>

#ifdef __KERNEL__

#include <linux/wait.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <asm/uaccess.h>


struct poll_table_entry {
	struct file * filp;
	struct wait_queue wait;
	struct wait_queue ** wait_address;
};

typedef struct poll_table_struct {
	struct poll_table_struct * next;
	unsigned int nr;
	struct poll_table_entry * entry;
} poll_table;

#define __MAX_POLL_TABLE_ENTRIES ((PAGE_SIZE - sizeof (poll_table)) / sizeof (struct poll_table_entry))

extern inline void poll_wait(struct file * filp, struct wait_queue ** wait_address, poll_table *p)
{
	struct poll_table_entry * entry;

	if (!p || !wait_address)
		return;
	while (p->nr >= __MAX_POLL_TABLE_ENTRIES && p->next != NULL)
		p = p->next;
	if (p->nr >= __MAX_POLL_TABLE_ENTRIES) {
		poll_table *tmp = (poll_table *) __get_free_page(GFP_KERNEL);
		if (!tmp)
			return;
		tmp->nr = 0;
		tmp->entry = (struct poll_table_entry *)(tmp + 1);
		tmp->next = NULL;
		p->next = tmp;
		p = tmp;
	}
 	entry = p->entry + p->nr;
 	entry->filp = filp;
 	filp->f_count++;
	entry->wait_address = wait_address;
	entry->wait.task = current;
	entry->wait.next = NULL;
	add_wait_queue(wait_address,&entry->wait);
	p->nr++;
}


/*
 * For the kernel fd_set we use a fixed set-size for allocation purposes.
 * This set-size doesn't necessarily bear any relation to the size the user
 * uses, but should preferably obviously be larger than any possible user
 * size (NR_OPEN bits).
 *
 * We need 6 bitmaps (in/out/ex for both incoming and outgoing), and we
 * allocate one page for all the bitmaps. Thus we have 8*PAGE_SIZE bits,
 * to be divided by 6. And we'd better make sure we round to a full
 * long-word (in fact, we'll round to 64 bytes).
 */


#define KFDS_64BLOCK ((PAGE_SIZE/(6*64))*64)
#define KFDS_NR (KFDS_64BLOCK*8 > NR_OPEN ? NR_OPEN : KFDS_64BLOCK*8)
typedef unsigned long kernel_fd_set[KFDS_NR/__NFDBITS];

/*
 * XXX - still used by alpha osf and sparc32 compatiblity.
 */

typedef struct {
	kernel_fd_set in, out, ex;
	kernel_fd_set res_in, res_out, res_ex;
} fd_set_buffer;

/*
 * Scaleable version of the fd_set.
 */

typedef struct {
	unsigned long *in, *out, *ex;
	unsigned long *res_in, *res_out, *res_ex;
} fd_set_bits;

/*
 * How many longwords for "nr" bits?
 */
#define FDS_BITPERLONG	(8*sizeof(long))
#define FDS_LONGS(nr)	(((nr)+FDS_BITPERLONG-1)/FDS_BITPERLONG)
#define FDS_BYTES(nr)	(FDS_LONGS(nr)*sizeof(long))

/*
 * We do a VERIFY_WRITE here even though we are only reading this time:
 * we'll write to it eventually..
 *
 * Use "unsigned long" accesses to let user-mode fd_set's be long-aligned.
 */
static inline
int get_fd_set(unsigned long nr, void *ufdset, unsigned long *fdset)
{
	nr = FDS_BYTES(nr);
	if (ufdset) {
		int error;
		error = verify_area(VERIFY_WRITE, ufdset, nr);
		if (!error && __copy_from_user(fdset, ufdset, nr))
			error = -EFAULT;
		return error;
	}
	memset(fdset, 0, nr);
	return 0;
}

static inline
void set_fd_set(unsigned long nr, void *ufdset, unsigned long *fdset)
{
	if (ufdset)
		__copy_to_user(ufdset, fdset, FDS_BYTES(nr));
}

static inline
void zero_fd_set(unsigned long nr, unsigned long *fdset)
{
	memset(fdset, 0, FDS_BYTES(nr));
}

extern int do_select(int n, fd_set_bits *fds, long *timeout);

#endif /* KERNEL */

#endif /* _LINUX_POLL_H */
