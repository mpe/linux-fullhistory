/*
 * linux/fs/inode.c
 *
 * (C) 1997 Linus Torvalds
 */

#include <linux/fs.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/dcache.h>
#include <linux/init.h>
#include <linux/quotaops.h>

/*
 * New inode.c implementation.
 *
 * This implementation has the basic premise of trying
 * to be extremely low-overhead and SMP-safe, yet be
 * simple enough to be "obviously correct".
 *
 * Famous last words.
 */

#define INODE_PARANOIA 1
/* #define INODE_DEBUG 1 */

/*
 * Inode lookup is no longer as critical as it used to be:
 * most of the lookups are going to be through the dcache.
 */
#define HASH_BITS	8
#define HASH_SIZE	(1UL << HASH_BITS)
#define HASH_MASK	(HASH_SIZE-1)

/*
 * Each inode can be on two separate lists. One is
 * the hash list of the inode, used for lookups. The
 * other linked list is the "type" list:
 *  "in_use" - valid inode, hashed if i_nlink > 0
 *  "dirty"  - valid inode, hashed if i_nlink > 0, dirty.
 *  "unused" - ready to be re-used. Not hashed.
 *
 * A "dirty" list is maintained for each super block,
 * allowing for low-overhead inode sync() operations.
 */

static LIST_HEAD(inode_in_use);
static LIST_HEAD(inode_unused);
static struct list_head inode_hashtable[HASH_SIZE];

/*
 * A simple spinlock to protect the list manipulations.
 *
 * NOTE! You also have to own the lock if you change
 * the i_state of an inode while it is in use..
 */
spinlock_t inode_lock = SPIN_LOCK_UNLOCKED;

/*
 * Statistics gathering..
 */
struct {
	int nr_inodes;
	int nr_free_inodes;
	int dummy[5];
} inodes_stat = {0, 0,};

int max_inodes;

/*
 * Put the inode on the super block's dirty list.
 *
 * CAREFUL! We mark it dirty unconditionally, but
 * move it onto the dirty list only if it is hashed.
 * If it was not hashed, it will never be added to
 * the dirty list even if it is later hashed, as it
 * will have been marked dirty already.
 *
 * In short, make sure you hash any inodes _before_
 * you start marking them dirty..
 */
void __mark_inode_dirty(struct inode *inode)
{
	struct super_block * sb = inode->i_sb;

	if (sb) {
		spin_lock(&inode_lock);
		if (!(inode->i_state & I_DIRTY)) {
			inode->i_state |= I_DIRTY;
			/* Only add valid (ie hashed) inodes to the dirty list */
			if (!list_empty(&inode->i_hash)) {
				list_del(&inode->i_list);
				list_add(&inode->i_list, &sb->s_dirty);
			}
		}
		spin_unlock(&inode_lock);
	}
}

static void __wait_on_inode(struct inode * inode)
{
	DECLARE_WAITQUEUE(wait, current);

	add_wait_queue(&inode->i_wait, &wait);
repeat:
	current->state = TASK_UNINTERRUPTIBLE;
	if (inode->i_state & I_LOCK) {
		schedule();
		goto repeat;
	}
	remove_wait_queue(&inode->i_wait, &wait);
	current->state = TASK_RUNNING;
}

static inline void wait_on_inode(struct inode *inode)
{
	if (inode->i_state & I_LOCK)
		__wait_on_inode(inode);
}

/*
 * These are initializations that only need to be done
 * once, because the fields are idempotent across use
 * of the inode..
 */
static inline void init_once(struct inode * inode)
{
	memset(inode, 0, sizeof(*inode));
	init_waitqueue_head(&inode->i_wait);
	INIT_LIST_HEAD(&inode->i_hash);
	INIT_LIST_HEAD(&inode->i_dentry);
	sema_init(&inode->i_sem, 1);
	sema_init(&inode->i_atomic_write, 1);
}

static inline void write_inode(struct inode *inode)
{
	if (inode->i_sb && inode->i_sb->s_op && inode->i_sb->s_op->write_inode)
		inode->i_sb->s_op->write_inode(inode);
}

static inline void sync_one(struct inode *inode)
{
	if (inode->i_state & I_LOCK) {
		spin_unlock(&inode_lock);
		__wait_on_inode(inode);
		spin_lock(&inode_lock);
	} else {
		list_del(&inode->i_list);
		list_add(&inode->i_list, &inode_in_use);
		/* Set I_LOCK, reset I_DIRTY */
		inode->i_state ^= I_DIRTY | I_LOCK;
		spin_unlock(&inode_lock);

		write_inode(inode);

		spin_lock(&inode_lock);
		inode->i_state &= ~I_LOCK;
		wake_up(&inode->i_wait);
	}
}

static inline void sync_list(struct list_head *head)
{
	struct list_head * tmp;

	while ((tmp = head->prev) != head)
		sync_one(list_entry(tmp, struct inode, i_list));
}

/*
 * "sync_inodes()" goes through the super block's dirty list, 
 * writes them out, and puts them back on the normal list.
 */
void sync_inodes(kdev_t dev)
{
	struct super_block * sb = sb_entry(super_blocks.next);

	/*
	 * Search the super_blocks array for the device(s) to sync.
	 */
	spin_lock(&inode_lock);
	for (; sb != sb_entry(&super_blocks); sb = sb_entry(sb->s_list.next)) {
		if (!sb->s_dev)
			continue;
		if (dev && sb->s_dev != dev)
			continue;

		sync_list(&sb->s_dirty);

		if (dev)
			break;
	}
	spin_unlock(&inode_lock);
}

/*
 * Called with the spinlock already held..
 */
static void sync_all_inodes(void)
{
	struct super_block * sb = sb_entry(super_blocks.next);
	for (; sb != sb_entry(&super_blocks); sb = sb_entry(sb->s_list.next)) {
		if (!sb->s_dev)
			continue;
		sync_list(&sb->s_dirty);
	}
}

/*
 * Needed by knfsd
 */
void write_inode_now(struct inode *inode)
{
	struct super_block * sb = inode->i_sb;

	if (sb) {
		spin_lock(&inode_lock);
		while (inode->i_state & I_DIRTY)
			sync_one(inode);
		spin_unlock(&inode_lock);
	}
	else
		printk("write_inode_now: no super block\n");
}

/*
 * This is called by the filesystem to tell us
 * that the inode is no longer useful. We just
 * terminate it with extreme prejudice.
 */
void clear_inode(struct inode *inode)
{
	if (inode->i_nrpages)
		truncate_inode_pages(inode, 0);
	wait_on_inode(inode);
	if (IS_QUOTAINIT(inode))
		DQUOT_DROP(inode);
	if (inode->i_sb && inode->i_sb->s_op && inode->i_sb->s_op->clear_inode)
		inode->i_sb->s_op->clear_inode(inode);

	inode->i_state = 0;
}

/*
 * Dispose-list gets a local list, so it doesn't need to
 * worry about list corruption. It releases the inode lock
 * while clearing the inodes.
 */
static void dispose_list(struct list_head * head)
{
	struct list_head *next;
	int count = 0;

	spin_unlock(&inode_lock);
	next = head->next;
	for (;;) {
		struct list_head * tmp = next;
		struct inode * inode;

		next = next->next;
		if (tmp == head)
			break;
		inode = list_entry(tmp, struct inode, i_list);
		clear_inode(inode);
		count++;
	}

	/* Add them all to the unused list in one fell swoop */
	spin_lock(&inode_lock);
	list_splice(head, &inode_unused);
	inodes_stat.nr_free_inodes += count;
}

/*
 * Invalidate all inodes for a device.
 */
static int invalidate_list(struct list_head *head, struct super_block * sb, struct list_head * dispose)
{
	struct list_head *next;
	int busy = 0;

	next = head->next;
	for (;;) {
		struct list_head * tmp = next;
		struct inode * inode;

		next = next->next;
		if (tmp == head)
			break;
		inode = list_entry(tmp, struct inode, i_list);
		if (inode->i_sb != sb)
			continue;
		if (!inode->i_count) {
			list_del(&inode->i_hash);
			INIT_LIST_HEAD(&inode->i_hash);
			list_del(&inode->i_list);
			list_add(&inode->i_list, dispose);
			inode->i_state |= I_FREEING;
			continue;
		}
		busy = 1;
	}
	return busy;
}

/*
 * This is a two-stage process. First we collect all
 * offending inodes onto the throw-away list, and in
 * the second stage we actually dispose of them. This
 * is because we don't want to sleep while messing
 * with the global lists..
 */
int invalidate_inodes(struct super_block * sb)
{
	int busy;
	LIST_HEAD(throw_away);

	spin_lock(&inode_lock);
	busy = invalidate_list(&inode_in_use, sb, &throw_away);
	busy |= invalidate_list(&sb->s_dirty, sb, &throw_away);
	dispose_list(&throw_away);
	spin_unlock(&inode_lock);

	return busy;
}

/*
 * This is called with the inode lock held. It searches
 * the in-use for freeable inodes, which are moved to a
 * temporary list and then placed on the unused list by
 * dispose_list. 
 *
 * We don't expect to have to call this very often.
 *
 * N.B. The spinlock is released during the call to
 *      dispose_list.
 */
#define CAN_UNUSE(inode) \
	(((inode)->i_count | (inode)->i_state) == 0)
#define INODE(entry)	(list_entry(entry, struct inode, i_list))

static int free_inodes(void)
{
	struct list_head list, *entry, *freeable = &list;
	int found = 0;

	INIT_LIST_HEAD(freeable);
	entry = inode_in_use.next;
	while (entry != &inode_in_use) {
		struct list_head *tmp = entry;

		entry = entry->next;
		if (!CAN_UNUSE(INODE(tmp)))
			continue;
		list_del(tmp);
		list_del(&INODE(tmp)->i_hash);
		INIT_LIST_HEAD(&INODE(tmp)->i_hash);
		list_add(tmp, freeable);
		list_entry(tmp, struct inode, i_list)->i_state = I_FREEING;
		found = 1;
	}

	if (found)
		dispose_list(freeable);

	return found;
}

/*
 * Searches the inodes list for freeable inodes,
 * shrinking the dcache before (and possible after,
 * if we're low)
 */
static void try_to_free_inodes(int goal)
{
	/*
	 * First stry to just get rid of unused inodes.
	 *
	 * If we can't reach our goal that way, we'll have
	 * to try to shrink the dcache and sync existing
	 * inodes..
	 */
	free_inodes();
	goal -= inodes_stat.nr_free_inodes;
	if (goal > 0) {
		spin_unlock(&inode_lock);
		select_dcache(goal, 0);
		prune_dcache(goal);
		spin_lock(&inode_lock);
		sync_all_inodes();
		free_inodes();
	}
}

/*
 * This is the externally visible routine for
 * inode memory management.
 */
void free_inode_memory(int goal)
{
	spin_lock(&inode_lock);
	free_inodes();
	spin_unlock(&inode_lock);
}


/*
 * This is called with the spinlock held, but releases
 * the lock when freeing or allocating inodes.
 * Look out! This returns with the inode lock held if
 * it got an inode..
 *
 * We do inode allocations two pages at a time to reduce
 * fragmentation.
 */
#define INODE_PAGE_ORDER	1
#define INODE_ALLOCATION_SIZE	(PAGE_SIZE << INODE_PAGE_ORDER)
#define INODES_PER_ALLOCATION	(INODE_ALLOCATION_SIZE/sizeof(struct inode))

static struct inode * grow_inodes(void)
{
	struct inode * inode;

	/*
	 * Check whether to restock the unused list.
	 */
	if (inodes_stat.nr_inodes > max_inodes) {
		struct list_head *tmp;
		try_to_free_inodes(inodes_stat.nr_inodes >> 2);
		tmp = inode_unused.next;
		if (tmp != &inode_unused) {
			inodes_stat.nr_free_inodes--;
			list_del(tmp);
			inode = list_entry(tmp, struct inode, i_list);
			return inode;
		}
	}
		
	spin_unlock(&inode_lock);
	inode = (struct inode *)__get_free_pages(GFP_KERNEL,INODE_PAGE_ORDER);
	if (inode) {
		int size;
		struct inode * tmp;

		size = INODE_ALLOCATION_SIZE - 2*sizeof(struct inode);
		tmp = inode;
		spin_lock(&inode_lock);
		do {
			tmp++;
			init_once(tmp);
			list_add(&tmp->i_list, &inode_unused);
			size -= sizeof(struct inode);
		} while (size >= 0);
		init_once(inode);
		/*
		 * Update the inode statistics
		 */
		inodes_stat.nr_inodes += INODES_PER_ALLOCATION;
		inodes_stat.nr_free_inodes += INODES_PER_ALLOCATION - 1;
		return inode;
	}

	/*
	 * If the allocation failed, do an extensive pruning of 
	 * the dcache and then try again to free some inodes.
	 */
	prune_dcache(inodes_stat.nr_inodes >> 2);

	spin_lock(&inode_lock);
	free_inodes();
	{
		struct list_head *tmp = inode_unused.next;
		if (tmp != &inode_unused) {
			inodes_stat.nr_free_inodes--;
			list_del(tmp);
			inode = list_entry(tmp, struct inode, i_list);
			return inode;
		}
	}
	spin_unlock(&inode_lock);

	printk("grow_inodes: allocation failed\n");
	return NULL;
}

/*
 * Called with the inode lock held.
 */
static struct inode * find_inode(struct super_block * sb, unsigned long ino, struct list_head *head)
{
	struct list_head *tmp;
	struct inode * inode;

	tmp = head;
	for (;;) {
		tmp = tmp->next;
		inode = NULL;
		if (tmp == head)
			break;
		inode = list_entry(tmp, struct inode, i_hash);
		if (inode->i_sb != sb)
			continue;
		if (inode->i_ino != ino)
			continue;
		inode->i_count++;
		break;
	}
	return inode;
}

/*
 * This just initializes the inode fields
 * to known values before returning the inode..
 *
 * i_sb, i_ino, i_count, i_state and the lists have
 * been initialized elsewhere..
 */
void clean_inode(struct inode *inode)
{
	memset(&inode->u, 0, sizeof(inode->u));
	inode->i_sock = 0;
	inode->i_op = NULL;
	inode->i_nlink = 1;
	inode->i_writecount = 0;
	inode->i_size = 0;
	inode->i_generation = 0;
	memset(&inode->i_dquot, 0, sizeof(inode->i_dquot));
	sema_init(&inode->i_sem, 1);
	inode->i_pipe = NULL;
}

/*
 * This is called by things like the networking layer
 * etc that want to get an inode without any inode
 * number, or filesystems that allocate new inodes with
 * no pre-existing information.
 */
struct inode * get_empty_inode(void)
{
	static unsigned long last_ino = 0;
	struct inode * inode;
	struct list_head * tmp;

	spin_lock(&inode_lock);
	tmp = inode_unused.next;
	if (tmp != &inode_unused) {
		list_del(tmp);
		inodes_stat.nr_free_inodes--;
		inode = list_entry(tmp, struct inode, i_list);
add_new_inode:
		list_add(&inode->i_list, &inode_in_use);
		inode->i_sb = NULL;
		inode->i_dev = 0;
		inode->i_ino = ++last_ino;
		inode->i_flags = 0;
		inode->i_count = 1;
		inode->i_state = 0;
		spin_unlock(&inode_lock);
		clean_inode(inode);
		return inode;
	}

	/*
	 * Warning: if this succeeded, we will now
	 * return with the inode lock.
	 */
	inode = grow_inodes();
	if (inode)
		goto add_new_inode;

	return inode;
}

/*
 * This is called with the inode lock held.. Be careful.
 *
 * We no longer cache the sb_flags in i_flags - see fs.h
 *	-- rmk@arm.uk.linux.org
 */
static struct inode * get_new_inode(struct super_block *sb, unsigned long ino, struct list_head *head)
{
	struct inode * inode;
	struct list_head * tmp = inode_unused.next;

	if (tmp != &inode_unused) {
		list_del(tmp);
		inodes_stat.nr_free_inodes--;
		inode = list_entry(tmp, struct inode, i_list);
add_new_inode:
		list_add(&inode->i_list, &inode_in_use);
		list_add(&inode->i_hash, head);
		inode->i_sb = sb;
		inode->i_dev = sb->s_dev;
		inode->i_ino = ino;
		inode->i_flags = 0;
		inode->i_count = 1;
		inode->i_state = I_LOCK;
		spin_unlock(&inode_lock);

		clean_inode(inode);
		sb->s_op->read_inode(inode);

		/*
		 * This is special!  We do not need the spinlock
		 * when clearing I_LOCK, because we're guaranteed
		 * that nobody else tries to do anything about the
		 * state of the inode when it is locked, as we
		 * just created it (so there can be no old holders
		 * that haven't tested I_LOCK).
		 */
		inode->i_state &= ~I_LOCK;
		wake_up(&inode->i_wait);

		return inode;
	}

	/*
	 * We need to expand. Note that "grow_inodes()" will
	 * release the spinlock, but will return with the lock 
	 * held again if the allocation succeeded.
	 */
	inode = grow_inodes();
	if (inode) {
		/* We released the lock, so.. */
		struct inode * old = find_inode(sb, ino, head);
		if (!old)
			goto add_new_inode;
		list_add(&inode->i_list, &inode_unused);
		inodes_stat.nr_free_inodes++;
		spin_unlock(&inode_lock);
		wait_on_inode(old);
		return old;
	}
	return inode;
}

static inline unsigned long hash(struct super_block *sb, unsigned long i_ino)
{
	unsigned long tmp = i_ino | (unsigned long) sb;
	tmp = tmp + (tmp >> HASH_BITS) + (tmp >> HASH_BITS*2);
	return tmp & HASH_MASK;
}

/* Yeah, I know about quadratic hash. Maybe, later. */
ino_t iunique(struct super_block *sb, ino_t max_reserved)
{
	static ino_t counter = 0;
	struct inode *inode;
	struct list_head * head;
	ino_t res;
	spin_lock(&inode_lock);
retry:
	if (counter > max_reserved) {
		head = inode_hashtable + hash(sb,counter);
		inode = find_inode(sb, res = counter++, head);
		if (!inode) {
			spin_unlock(&inode_lock);
			return res;
		}
		inode->i_count--; /* compensate find_inode() */
	} else {
		counter = max_reserved + 1;
	}
	goto retry;
	
}

struct inode *igrab(struct inode *inode)
{
	spin_lock(&inode_lock);
	if (inode->i_state & I_FREEING)
		inode = NULL;
	else
		inode->i_count++;
	spin_unlock(&inode_lock);
	if (inode)
		wait_on_inode(inode);
	return inode;
}

struct inode *iget(struct super_block *sb, unsigned long ino)
{
	struct list_head * head = inode_hashtable + hash(sb,ino);
	struct inode * inode;

	spin_lock(&inode_lock);
	inode = find_inode(sb, ino, head);
	if (inode) {
		spin_unlock(&inode_lock);
		wait_on_inode(inode);
		return inode;
	}
	/*
	 * get_new_inode() will do the right thing, releasing
	 * the inode lock and re-trying the search in case it
	 * had to block at any point.
	 */
	return get_new_inode(sb, ino, head);
}

void insert_inode_hash(struct inode *inode)
{
	struct list_head *head = inode_hashtable + hash(inode->i_sb, inode->i_ino);
	spin_lock(&inode_lock);
	list_add(&inode->i_hash, head);
	spin_unlock(&inode_lock);
}

void remove_inode_hash(struct inode *inode)
{
	spin_lock(&inode_lock);
	list_del(&inode->i_hash);
	INIT_LIST_HEAD(&inode->i_hash);
	spin_unlock(&inode_lock);
}

void iput(struct inode *inode)
{
	if (inode) {
		struct super_operations *op = NULL;

		if (inode->i_sb && inode->i_sb->s_op)
			op = inode->i_sb->s_op;
		if (op && op->put_inode)
			op->put_inode(inode);

		spin_lock(&inode_lock);
		if (!--inode->i_count) {
			if (!inode->i_nlink) {
				list_del(&inode->i_hash);
				INIT_LIST_HEAD(&inode->i_hash);
				list_del(&inode->i_list);
				INIT_LIST_HEAD(&inode->i_list);
				inode->i_state|=I_FREEING;
				if (op && op->delete_inode) {
					void (*delete)(struct inode *) = op->delete_inode;
					spin_unlock(&inode_lock);
					delete(inode);
					spin_lock(&inode_lock);
				}
			}
			if (list_empty(&inode->i_hash)) {
				list_del(&inode->i_list);
				INIT_LIST_HEAD(&inode->i_list);
				inode->i_state|=I_FREEING;
				spin_unlock(&inode_lock);
				clear_inode(inode);
				spin_lock(&inode_lock);
				list_add(&inode->i_list, &inode_unused);
				inodes_stat.nr_free_inodes++;
			}
			else if (!(inode->i_state & I_DIRTY)) {
				list_del(&inode->i_list);
				list_add(&inode->i_list, &inode_in_use);
			}
#ifdef INODE_PARANOIA
if (inode->i_flock)
printk(KERN_ERR "iput: inode %s/%ld still has locks!\n",
kdevname(inode->i_dev), inode->i_ino);
if (!list_empty(&inode->i_dentry))
printk(KERN_ERR "iput: device %s inode %ld still has aliases!\n",
kdevname(inode->i_dev), inode->i_ino);
if (inode->i_count)
printk(KERN_ERR "iput: device %s inode %ld count changed, count=%d\n",
kdevname(inode->i_dev), inode->i_ino, inode->i_count);
if (atomic_read(&inode->i_sem.count) != 1)
printk(KERN_ERR "iput: Aieee, semaphore in use inode %s/%ld, count=%d\n",
kdevname(inode->i_dev), inode->i_ino, atomic_read(&inode->i_sem.count));
if (atomic_read(&inode->i_atomic_write.count) != 1)
printk(KERN_ERR "iput: Aieee, atomic write semaphore in use inode %s/%ld, count=%d\n",
kdevname(inode->i_dev), inode->i_ino, atomic_read(&inode->i_sem.count));
#endif
		}
		if (inode->i_count > (1<<31)) {
			printk(KERN_ERR "iput: inode %s/%ld count wrapped\n",
				kdevname(inode->i_dev), inode->i_ino);
		}
		spin_unlock(&inode_lock);
	}
}

int bmap(struct inode * inode, int block)
{
	if (inode->i_op && inode->i_op->bmap)
		return inode->i_op->bmap(inode, block);
	return 0;
}

/*
 * Initialize the hash tables and default
 * value for max inodes
 */
#define MAX_INODE (16384)

void __init inode_init(void)
{
	int i, max;
	struct list_head *head = inode_hashtable;

	i = HASH_SIZE;
	do {
		INIT_LIST_HEAD(head);
		head++;
		i--;
	} while (i);

	/* Initial guess at reasonable inode number */
	max = num_physpages >> 1;
	if (max > MAX_INODE)
		max = MAX_INODE;
	max_inodes = max;
}

/* This belongs in file_table.c, not here... */
int fs_may_remount_ro(struct super_block *sb)
{
	struct file *file;

	/* Check that no files are currently opened for writing. */
	for (file = inuse_filps; file; file = file->f_next) {
		struct inode *inode;
		if (!file->f_dentry)
			continue;
		inode = file->f_dentry->d_inode;
		if (!inode || inode->i_sb != sb)
			continue;

		/* File with pending delete? */
		if (inode->i_nlink == 0)
			return 0;

		/* Writable file? */
		if (S_ISREG(inode->i_mode) && (file->f_mode & FMODE_WRITE))
			return 0;
	}
	return 1; /* Tis' cool bro. */
}

void update_atime (struct inode *inode)
{
    if ( IS_NOATIME (inode) ) return;
    if ( IS_NODIRATIME (inode) && S_ISDIR (inode->i_mode) ) return;
    if ( IS_RDONLY (inode) ) return;
    inode->i_atime = CURRENT_TIME;
    mark_inode_dirty (inode);
}   /*  End Function update_atime  */
