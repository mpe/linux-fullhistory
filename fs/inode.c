/*
 * linux/fs/inode.c
 *
 * (C) 1997 Linus Torvalds
 */

#include <linux/config.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/dcache.h>
#include <linux/init.h>
#include <linux/quotaops.h>
#include <linux/slab.h>

/*
 * New inode.c implementation.
 *
 * This implementation has the basic premise of trying
 * to be extremely low-overhead and SMP-safe, yet be
 * simple enough to be "obviously correct".
 *
 * Famous last words.
 */

/* inode dynamic allocation 1999, Andrea Arcangeli <andrea@suse.de> */

#define INODE_PARANOIA 1
/* #define INODE_DEBUG 1 */

/*
 * Inode lookup is no longer as critical as it used to be:
 * most of the lookups are going to be through the dcache.
 */
#define HASH_BITS	14
#define HASH_SIZE	(1UL << HASH_BITS)
#define HASH_MASK	(HASH_SIZE-1)

/*
 * Each inode can be on two separate lists. One is
 * the hash list of the inode, used for lookups. The
 * other linked list is the "type" list:
 *  "in_use" - valid inode, i_count > 0, i_nlink > 0
 *  "dirty"  - as "in_use" but also dirty
 *  "unused" - valid inode, i_count = 0
 *
 * A "dirty" list is maintained for each super block,
 * allowing for low-overhead inode sync() operations.
 */

static LIST_HEAD(inode_in_use);
static LIST_HEAD(inode_unused);
static struct list_head inode_hashtable[HASH_SIZE];
static LIST_HEAD(anon_hash_chain); /* for inodes with NULL i_sb */

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
	int nr_unused;
	int dummy[5];
} inodes_stat = {0, 0,};

static kmem_cache_t * inode_cachep;

#define alloc_inode() \
	 ((struct inode *) kmem_cache_alloc(inode_cachep, SLAB_KERNEL))
#define destroy_inode(inode) kmem_cache_free(inode_cachep, (inode))

/*
 * These are initializations that only need to be done
 * once, because the fields are idempotent across use
 * of the inode, so let the slab aware of that.
 */
static void init_once(void * foo, kmem_cache_t * cachep, unsigned long flags)
{
	struct inode * inode = (struct inode *) foo;

	if ((flags & (SLAB_CTOR_VERIFY|SLAB_CTOR_CONSTRUCTOR)) ==
	    SLAB_CTOR_CONSTRUCTOR)
	{
		memset(inode, 0, sizeof(*inode));
		init_waitqueue_head(&inode->i_wait);
		INIT_LIST_HEAD(&inode->i_hash);
		INIT_LIST_HEAD(&inode->i_data.pages);
		INIT_LIST_HEAD(&inode->i_dentry);
		sema_init(&inode->i_sem, 1);
		spin_lock_init(&inode->i_shared_lock);
	}
}

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
	set_current_state(TASK_UNINTERRUPTIBLE);
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
		list_add(&inode->i_list,
			 inode->i_count ? &inode_in_use : &inode_unused);
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
	if (inode->i_data.nrpages)
		BUG();
	if (!(inode->i_state & I_FREEING))
		BUG();
	wait_on_inode(inode);
	if (IS_QUOTAINIT(inode))
		DQUOT_DROP(inode);
	if (inode->i_sb && inode->i_sb->s_op && inode->i_sb->s_op->clear_inode)
		inode->i_sb->s_op->clear_inode(inode);
	if (inode->i_bdev) {
		bdput(inode->i_bdev);
		inode->i_bdev = NULL;
	}
	inode->i_state = 0;
}

/*
 * Dispose-list gets a local list with local inodes in it, so it doesn't
 * need to worry about list corruption and SMP locks.
 */
static void dispose_list(struct list_head * head)
{
	struct list_head * inode_entry;
	struct inode * inode;

	while ((inode_entry = head->next) != head)
	{
		list_del(inode_entry);

		inode = list_entry(inode_entry, struct inode, i_list);
		if (inode->i_data.nrpages)
			truncate_inode_pages(inode, 0);
		clear_inode(inode);
		destroy_inode(inode);
	}
}

/*
 * Invalidate all inodes for a device.
 */
static int invalidate_list(struct list_head *head, struct super_block * sb, struct list_head * dispose)
{
	struct list_head *next;
	int busy = 0, count = 0;

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
			count++;
			continue;
		}
		busy = 1;
	}
	/* only unused inodes may be cached with i_count zero */
	inodes_stat.nr_unused -= count;
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
	busy |= invalidate_list(&inode_unused, sb, &throw_away);
	busy |= invalidate_list(&sb->s_dirty, sb, &throw_away);
	spin_unlock(&inode_lock);

	dispose_list(&throw_away);

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
	(((inode)->i_state | (inode)->i_data.nrpages) == 0)
#define INODE(entry)	(list_entry(entry, struct inode, i_list))

void prune_icache(int goal)
{
	LIST_HEAD(list);
	struct list_head *entry, *freeable = &list;
	int count = 0;
	struct inode * inode;

	spin_lock(&inode_lock);
	/* go simple and safe syncing everything before starting */
	sync_all_inodes();

	entry = inode_unused.prev;
	while (entry != &inode_unused)
	{
		struct list_head *tmp = entry;

		entry = entry->prev;
		inode = INODE(tmp);
		if (!CAN_UNUSE(inode))
			continue;
		if (inode->i_count)
			BUG();
		list_del(tmp);
		list_del(&inode->i_hash);
		INIT_LIST_HEAD(&inode->i_hash);
		list_add(tmp, freeable);
		inode->i_state |= I_FREEING;
		count++;
		if (!--goal)
			break;
	}
	inodes_stat.nr_unused -= count;
	spin_unlock(&inode_lock);

	dispose_list(freeable);
}

int shrink_icache_memory(int priority, int gfp_mask, zone_t *zone)
{
	int count = 0;
		
	if (priority)
		count = inodes_stat.nr_unused / priority;
	prune_icache(count);
	/* FIXME: kmem_cache_shrink here should tell us
	   the number of pages freed, and it should
	   work in a __GFP_DMA/__GFP_HIGHMEM behaviour
	   to free only the interesting pages in
	   function of the needs of the current allocation. */
	kmem_cache_shrink(inode_cachep);

	return 0;
}

static inline void __iget(struct inode * inode)
{
	if (!inode->i_count++)
	{
		if (!(inode->i_state & I_DIRTY))
		{
			list_del(&inode->i_list);
			list_add(&inode->i_list, &inode_in_use);
		}
		inodes_stat.nr_unused--;
	}
}

/*
 * Called with the inode lock held.
 * NOTE: we are not increasing the inode-refcount, you must call __iget()
 * by hand after calling find_inode now! This simplify iunique and won't
 * add any additional branch in the common code.
 */
static struct inode * find_inode(struct super_block * sb, unsigned long ino, struct list_head *head, find_inode_t find_actor, void *opaque)
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
		if (find_actor && !find_actor(inode, ino, opaque))
			continue;
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
static void clean_inode(struct inode *inode)
{
	static struct address_space_operations empty_aops = {};
	memset(&inode->u, 0, sizeof(inode->u));
	inode->i_sock = 0;
	inode->i_op = NULL;
	inode->i_nlink = 1;
	atomic_set(&inode->i_writecount, 0);
	inode->i_size = 0;
	inode->i_generation = 0;
	memset(&inode->i_dquot, 0, sizeof(inode->i_dquot));
	inode->i_pipe = NULL;
	inode->i_bdev = NULL;
	inode->i_data.a_ops = &empty_aops;
	inode->i_data.host = (void*)inode;
	inode->i_mapping = &inode->i_data;
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

	inode = alloc_inode();
	if (inode)
	{
		spin_lock(&inode_lock);
		list_add(&inode->i_list, &inode_in_use);
		inode->i_sb = NULL;
		inode->i_dev = 0;
		inode->i_ino = ++last_ino;
		inode->i_flags = 0;
		inode->i_count = 1;
		inode->i_state = 0;
		spin_unlock(&inode_lock);
		clean_inode(inode);
	}
	return inode;
}

/*
 * This is called without the inode lock held.. Be careful.
 *
 * We no longer cache the sb_flags in i_flags - see fs.h
 *	-- rmk@arm.uk.linux.org
 */
static struct inode * get_new_inode(struct super_block *sb, unsigned long ino, struct list_head *head, find_inode_t find_actor, void *opaque)
{
	struct inode * inode;

	inode = alloc_inode();
	if (inode) {
		struct inode * old;

		spin_lock(&inode_lock);
		/* We released the lock, so.. */
		old = find_inode(sb, ino, head, find_actor, opaque);
		if (!old) {
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
		 * Uhhuh, somebody else created the same inode under
		 * us. Use the old inode instead of the one we just
		 * allocated.
		 */
		__iget(old);
		spin_unlock(&inode_lock);
		destroy_inode(inode);
		inode = old;
		wait_on_inode(inode);
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
		inode = find_inode(sb, res = counter++, head, NULL, NULL);
		if (!inode) {
			spin_unlock(&inode_lock);
			return res;
		}
	} else {
		counter = max_reserved + 1;
	}
	goto retry;
	
}

struct inode *igrab(struct inode *inode)
{
	spin_lock(&inode_lock);
	if (!(inode->i_state & I_FREEING))
		__iget(inode);
	else
		inode = NULL;
	spin_unlock(&inode_lock);
	if (inode)
		wait_on_inode(inode);
	return inode;
}

struct inode *iget4(struct super_block *sb, unsigned long ino, find_inode_t find_actor, void *opaque)
{
	struct list_head * head = inode_hashtable + hash(sb,ino);
	struct inode * inode;

	spin_lock(&inode_lock);
	inode = find_inode(sb, ino, head, find_actor, opaque);
	if (inode) {
		__iget(inode);
		spin_unlock(&inode_lock);
		wait_on_inode(inode);
		return inode;
	}
	spin_unlock(&inode_lock);

	/*
	 * get_new_inode() will do the right thing, re-trying the search
	 * in case it had to block at any point.
	 */
	return get_new_inode(sb, ino, head, find_actor, opaque);
}

void insert_inode_hash(struct inode *inode)
{
	struct list_head *head = &anon_hash_chain;
	if (inode->i_sb)
		head = inode_hashtable + hash(inode->i_sb, inode->i_ino);
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
		int destroy = 0;

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
					if (inode->i_data.nrpages)
						truncate_inode_pages(inode, 0);
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
				destroy = 1;
				spin_lock(&inode_lock);
			}
			else
			{
				if (!(inode->i_state & I_DIRTY)) {
					list_del(&inode->i_list);
					list_add(&inode->i_list,
						 &inode_unused);
				}
				inodes_stat.nr_unused++;
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
#endif
		}
		if (inode->i_count > (1<<31)) {
			printk(KERN_ERR "iput: inode %s/%ld count wrapped\n",
				kdevname(inode->i_dev), inode->i_ino);
		}
		spin_unlock(&inode_lock);
		if (destroy)
			destroy_inode(inode);
	}
}

int bmap(struct inode * inode, int block)
{
	int res = 0;
	if (inode->i_mapping->a_ops->bmap)
		res = inode->i_mapping->a_ops->bmap(inode->i_mapping, block);
	return res;
}

/*
 * Initialize the hash tables.
 */
void __init inode_init(void)
{
	int i;
	struct list_head *head = inode_hashtable;

	i = HASH_SIZE;
	do {
		INIT_LIST_HEAD(head);
		head++;
		i--;
	} while (i);

	/* inode slab cache */
	inode_cachep = kmem_cache_create("inode_cache", sizeof(struct inode),
					 0, SLAB_HWCACHE_ALIGN, init_once,
					 NULL);
	if (!inode_cachep)
		panic("cannot create inode slab cache");
}

void update_atime (struct inode *inode)
{
    if ( IS_NOATIME (inode) ) return;
    if ( IS_NODIRATIME (inode) && S_ISDIR (inode->i_mode) ) return;
    if ( IS_RDONLY (inode) ) return;
    inode->i_atime = CURRENT_TIME;
    mark_inode_dirty (inode);
}   /*  End Function update_atime  */


/*
 *	Quota functions that want to walk the inode lists..
 */
#ifdef CONFIG_QUOTA

/* Functions back in dquot.c */
void put_dquot_list(struct list_head *);
int remove_inode_dquot_ref(struct inode *, short, struct list_head *);

void remove_dquot_ref(kdev_t dev, short type)
{
	struct super_block *sb = get_super(dev);
	struct inode *inode;
	struct list_head *act_head;
	LIST_HEAD(tofree_head);

	if (!sb || !sb->dq_op)
		return;	/* nothing to do */

	/* We have to be protected against other CPUs */
	spin_lock(&inode_lock);
 
	for (act_head = inode_in_use.next; act_head != &inode_in_use; act_head = act_head->next) {
		inode = list_entry(act_head, struct inode, i_list);
		if (inode->i_sb != sb || !IS_QUOTAINIT(inode))
			continue;
		remove_inode_dquot_ref(inode, type, &tofree_head);
	}
	for (act_head = inode_unused.next; act_head != &inode_unused; act_head = act_head->next) {
		inode = list_entry(act_head, struct inode, i_list);
		if (inode->i_sb != sb || !IS_QUOTAINIT(inode))
			continue;
		remove_inode_dquot_ref(inode, type, &tofree_head);
	}
	for (act_head = sb->s_dirty.next; act_head != &sb->s_dirty; act_head = act_head->next) {
		inode = list_entry(act_head, struct inode, i_list);
		if (!IS_QUOTAINIT(inode))
			continue;
  		remove_inode_dquot_ref(inode, type, &tofree_head);
	}
	spin_unlock(&inode_lock);

	put_dquot_list(&tofree_head);
}

#endif
