/*
 * include/linux/writeback.h.
 */
#ifndef WRITEBACK_H
#define WRITEBACK_H

struct backing_dev_info;

extern spinlock_t inode_lock;
extern struct list_head inode_in_use;
extern struct list_head inode_unused;

/*
 * Yes, writeback.h requires sched.h
 * No, sched.h is not included from here.
 */
static inline int current_is_pdflush(void)
{
	return current->flags & PF_FLUSHER;
}

/*
 * fs/fs-writeback.c
 */
enum writeback_sync_modes {
	WB_SYNC_NONE,	/* Don't wait on anything */
	WB_SYNC_ALL,	/* Wait on every mapping */
	WB_SYNC_HOLD,	/* Hold the inode on sb_dirty for sys_sync() */
};

/*
 * A control structure which tells the writeback code what to do.  These are
 * always on the stack, and hence need no locking.  They are always initialised
 * in a manner such that unspecified fields are set to zero.
 */
struct writeback_control {
	struct backing_dev_info *bdi;	/* If !NULL, only write back this
					   queue */
	enum writeback_sync_modes sync_mode;
	unsigned long *older_than_this;	/* If !NULL, only write back inodes
					   older than this */
	long nr_to_write;		/* Write this many pages, and decrement
					   this for each page written */
	long pages_skipped;		/* Pages which were not written */

	/*
	 * For a_ops->writepages(): is start or end are non-zero then this is
	 * a hint that the filesystem need only write out the pages inside that
	 * byterange.  The byte at `end' is included in the writeout request.
	 */
	loff_t start;
	loff_t end;

	unsigned nonblocking:1;			/* Don't get stuck on request queues */
	unsigned encountered_congestion:1;	/* An output: a queue is full */
	unsigned for_kupdate:1;			/* A kupdate writeback */
	unsigned for_reclaim:1;			/* Invoked from the page allocator */
};

/*
 * ->writepage() return values (make these much larger than a pagesize, in
 * case some fs is returning number-of-bytes-written from writepage)
 */
#define WRITEPAGE_ACTIVATE	0x80000	/* IO was not started: activate page */

/*
 * fs/fs-writeback.c
 */	
void writeback_inodes(struct writeback_control *wbc);
void wake_up_inode(struct inode *inode);
void __wait_on_inode(struct inode * inode);
void sync_inodes_sb(struct super_block *, int wait);
void sync_inodes(int wait);

/* writeback.h requires fs.h; it, too, is not included from here. */
static inline void wait_on_inode(struct inode *inode)
{
	might_sleep();
	if (inode->i_state & I_LOCK)
		__wait_on_inode(inode);
}

/*
 * mm/page-writeback.c
 */
int wakeup_bdflush(long nr_pages);
void laptop_io_completion(void);
void laptop_sync_completion(void);

/* These are exported to sysctl. */
extern int dirty_background_ratio;
extern int vm_dirty_ratio;
extern int dirty_writeback_centisecs;
extern int dirty_expire_centisecs;
extern int block_dump;
extern int laptop_mode;

struct ctl_table;
struct file;
int dirty_writeback_centisecs_handler(struct ctl_table *, int, struct file *,
				      void __user *, size_t *, loff_t *);

void page_writeback_init(void);
void balance_dirty_pages_ratelimited(struct address_space *mapping);
int pdflush_operation(void (*fn)(unsigned long), unsigned long arg0);
int do_writepages(struct address_space *mapping, struct writeback_control *wbc);
int sync_page_range(struct inode *inode, struct address_space *mapping,
			loff_t pos, size_t count);

/* pdflush.c */
extern int nr_pdflush_threads;	/* Global so it can be exported to sysctl
				   read-only. */


#endif		/* WRITEBACK_H */
