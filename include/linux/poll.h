#ifndef _LINUX_POLL_H

#include <asm/poll.h>

#ifdef __KERNEL__

#include <linux/wait.h>
#include <linux/string.h>
#include <asm/uaccess.h>


struct poll_table_entry {
	struct wait_queue wait;
	struct wait_queue ** wait_address;
};

typedef struct poll_table_struct {
	unsigned int nr;
	struct poll_table_entry * entry;
} poll_table;

#define __MAX_POLL_TABLE_ENTRIES (PAGE_SIZE / sizeof (struct poll_table_entry))

extern inline void poll_wait(struct wait_queue ** wait_address, poll_table *p)
{
	struct poll_table_entry * entry;

	if (!p || !wait_address)
		return;
	if (p->nr >= __MAX_POLL_TABLE_ENTRIES)
		return;
 	entry = p->entry + p->nr;
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

typedef struct {
	kernel_fd_set in, out, ex;
	kernel_fd_set res_in, res_out, res_ex;
} fd_set_buffer;


/*
 * We do a VERIFY_WRITE here even though we are only reading this time:
 * we'll write to it eventually..
 *
 * Use "unsigned long" accesses to let user-mode fd_set's be long-aligned.
 */
static inline
int get_fd_set(unsigned long nr, void *ufdset, unsigned long *fdset)
{
	/* round up nr to nearest "unsigned long" */
	nr = (nr + 8*sizeof(long) - 1) / (8*sizeof(long)) * sizeof(long);
	if (ufdset) {
		int error;
		error = verify_area(VERIFY_WRITE, ufdset, nr);
		if (!error)
			error = __copy_from_user(fdset, ufdset, nr);
		return error;
	}
	memset(fdset, 0, nr);
	return 0;
}

static inline
void set_fd_set(unsigned long nr, void *ufdset, unsigned long *fdset)
{
	if (ufdset) {
		nr = (nr + 8*sizeof(long) - 1) / (8*sizeof(long))*sizeof(long);
		__copy_to_user(ufdset, fdset, nr);
	}
}

static inline
void zero_fd_set(unsigned long nr, unsigned long *fdset)
{
	nr = (nr + 8*sizeof(long) - 1) / (8*sizeof(long)) * sizeof(long);
	memset(fdset, 0, nr);
}

extern int do_select(int n, fd_set_buffer *fds, unsigned long timeout);

#endif /* KERNEL */

#endif /* _LINUX_POLL_H */
