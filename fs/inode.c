/*
 * linux/fs/inode.c
 *
 * (C) 1997 Linus Torvalds
 */

#include <linux/fs.h>
#include <linux/string.h>
#include <linux/mm.h>

/*
 * New inode.c implementation.
 *
 * This implementation has the basic premise of trying
 * to be extremely low-overhead and SMP-safe, yet be
 * simple enough to be "obviously correct".
 *
 * Famous last words.
 */

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
 *  "in_use" - valid inode, hashed
 *  "dirty" - valid inode, hashed, dirty.
 *  "unused" - ready to be re-used. Not hashed.
 *
 * The two first versions also have a dirty list, allowing
 * for low-overhead inode sync() operations.
 */

static LIST_HEAD(inode_in_use);
static LIST_HEAD(inode_dirty);
static LIST_HEAD(inode_unused);
static struct list_head inode_hashtable[HASH_SIZE];

/*
 * A simple spinlock to protect the list manipulations
 */
spinlock_t inode_lock = SPIN_LOCK_UNLOCKED;

/*
 * Statistics gathering.. Not actually done yet.
 */
struct {
	int nr_inodes;
	int nr_free_inodes;
	int dummy[10];
} inodes_stat;

int max_inodes = NR_INODE;

void __mark_inode_dirty(struct inode *inode)
{
	spin_lock(&inode_lock);
	list_del(&inode->i_list);
	list_add(&inode->i_list, &inode_dirty);
	spin_unlock(&inode_lock);
}

static inline void unlock_inode(struct inode *inode)
{
	clear_bit(I_LOCK, &inode->i_state);
	wake_up(&inode->i_wait);
}

static void __wait_on_inode(struct inode * inode)
{
	struct wait_queue wait = { current, NULL };

	add_wait_queue(&inode->i_wait, &wait);
repeat:
	current->state = TASK_UNINTERRUPTIBLE;
	if (test_bit(I_LOCK, &inode->i_state)) {
		schedule();
		goto repeat;
	}
	remove_wait_queue(&inode->i_wait, &wait);
	current->state = TASK_RUNNING;
}

static inline void wait_on_inode(struct inode *inode)
{
	if (test_bit(I_LOCK, &inode->i_state))
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
	init_waitqueue(&inode->i_wait);
	INIT_LIST_HEAD(&inode->i_dentry);
	INIT_LIST_HEAD(&inode->i_hash);
	sema_init(&inode->i_sem, 1);
}


/*
 * Look out! This returns with the inode lock held if
 * it got an inode..
 */
static struct inode * grow_inodes(void)
{
	struct inode * inode = (struct inode *)__get_free_page(GFP_KERNEL);

	if (inode) {
		int size;
		struct inode * tmp;

		spin_lock(&inode_lock);
		size = PAGE_SIZE - 2*sizeof(struct inode);
		tmp = inode;
		do {
			tmp++;
			init_once(tmp);
			list_add(&tmp->i_list, &inode_unused);
			size -= sizeof(struct inode);
		} while (size >= 0);
		init_once(inode);
	}
	return inode;
}

static inline void write_inode(struct inode *inode)
{
	if (inode->i_sb && inode->i_sb->s_op && inode->i_sb->s_op->write_inode)
		inode->i_sb->s_op->write_inode(inode);
}

static inline void sync_list(struct list_head *head, struct list_head *clean)
{
	struct list_head * tmp;

	while ((tmp = head->prev) != head) {
		struct inode *inode = list_entry(tmp, struct inode, i_list);
		list_del(tmp);

		/*
		 * If the inode is locked, it's already being written out.
		 * We have to wait for it, though.
		 */
		if (test_bit(I_LOCK, &inode->i_state)) {
			list_add(tmp, head);
			spin_unlock(&inode_lock);
			__wait_on_inode(inode);
		} else {
			list_add(tmp, clean);
			clear_bit(I_DIRTY, &inode->i_state);
			set_bit(I_LOCK, &inode->i_state);
			spin_unlock(&inode_lock);
			write_inode(inode);
			unlock_inode(inode);
		}
		spin_lock(&inode_lock);
	}	
}

/*
 * "sync_inodes()" goes through the dirty list 
 * and writes them out and puts them back on
 * the normal list.
 */
void sync_inodes(kdev_t dev)
{
	spin_lock(&inode_lock);
	sync_list(&inode_dirty, &inode_in_use);
	spin_unlock(&inode_lock);
}

/*
 * This is called by the filesystem to tell us
 * that the inode is no longer useful. We just
 * terminate it with extreme predjudice.
 */
void clear_inode(struct inode *inode)
{
	truncate_inode_pages(inode, 0);
	wait_on_inode(inode);
	if (IS_WRITABLE(inode) && inode->i_sb && inode->i_sb->dq_op)
		inode->i_sb->dq_op->drop(inode);

	inode->i_state = 0;
}

#define CAN_UNUSE(inode) \
	(((inode)->i_count == 0) && \
	 ((inode)->i_nrpages == 0) && \
	 (!(inode)->i_state))

static void invalidate_list(struct list_head *head, kdev_t dev)
{
	struct list_head *next;

	next = head->next;
	for (;;) {
		struct list_head * tmp = next;
		struct inode * inode;

		next = next->next;
		if (tmp == head)
			break;
		inode = list_entry(tmp, struct inode, i_list);
		if (inode->i_dev != dev)
			continue;		
		if (!CAN_UNUSE(inode))
			continue;
		list_del(&inode->i_hash);
		INIT_LIST_HEAD(&inode->i_hash);
		list_del(&inode->i_list);
		list_add(&inode->i_list, &inode_unused);
	}
}

void invalidate_inodes(kdev_t dev)
{
	spin_lock(&inode_lock);
	invalidate_list(&inode_in_use, dev);
	invalidate_list(&inode_dirty, dev);
	spin_unlock(&inode_lock);
}

/*
 * This is called with the inode lock held. It just looks at the last
 * inode on the in-use list, and if the inode is trivially freeable
 * we just move it to the unused list.
 *
 * Otherwise we just move the inode to be the first inode and expect to
 * get back to the problem later..
 */
static void try_to_free_inodes(void)
{
	struct list_head * tmp;
	struct list_head *head = &inode_in_use;

	tmp = head->prev;
	if (tmp != head) {
		struct inode * inode;

		list_del(tmp);
		inode = list_entry(tmp, struct inode, i_list);
		if (CAN_UNUSE(inode)) {
			list_del(&inode->i_hash);
			INIT_LIST_HEAD(&inode->i_hash);
			head = &inode_unused;
		}
		list_add(tmp, head);
	}
}
		

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
	memset(&inode->i_dquot, 0, sizeof(inode->i_dquot));
	sema_init(&inode->i_sem, 1);
}

/*
 * This gets called with I_LOCK held: it needs
 * to read the inode and then unlock it
 */
static inline void read_inode(struct inode *inode, struct super_block *sb)
{
	sb->s_op->read_inode(inode);
	unlock_inode(inode);
}

struct inode * get_empty_inode(void)
{
	static unsigned long last_ino = 0;
	struct inode * inode;
	struct list_head * tmp;

	spin_lock(&inode_lock);
	try_to_free_inodes();
	tmp = inode_unused.next;
	if (tmp != &inode_unused) {
		list_del(tmp);
		inode = list_entry(tmp, struct inode, i_list);
add_new_inode:
		inode->i_sb = NULL;
		inode->i_ino = ++last_ino;
		inode->i_count = 1;
		list_add(&inode->i_list, &inode_in_use);
		inode->i_state = 0;
		spin_unlock(&inode_lock);
		clean_inode(inode);
		return inode;
	}

	/*
	 * Warning: if this succeeded, we will now
	 * return with the inode lock.
	 */
	spin_unlock(&inode_lock);
	inode = grow_inodes();
	if (inode)
		goto add_new_inode;

	return inode;
}

/*
 * This is called with the inode lock held.. Be careful.
 */
static struct inode * get_new_inode(struct super_block *sb, unsigned long ino, struct list_head *head)
{
	struct inode * inode;
	struct list_head * tmp = inode_unused.next;

	if (tmp != &inode_unused) {
		list_del(tmp);
		inode = list_entry(tmp, struct inode, i_list);
add_new_inode:
		list_add(&inode->i_list, &inode_in_use);
		list_add(&inode->i_hash, head);
		inode->i_sb = sb;
		inode->i_dev = sb->s_dev;
		inode->i_ino = ino;
		inode->i_flags = sb->s_flags;
		inode->i_count = 1;
		inode->i_state = 1 << I_LOCK;
		spin_unlock(&inode_lock);
		clean_inode(inode);
		read_inode(inode, sb);
		return inode;
	}

	/*
	 * Uhhuh.. We need to expand.  Unlock for the allocation,
	 * but note that "grow_inodes()" will return with the
	 * lock held again if the allocation succeeded.
	 */
	spin_unlock(&inode_lock);
	inode = grow_inodes();
	if (inode) {
		/* We released the lock, so.. */
		struct inode * old = find_inode(sb, ino, head);
		if (!old)
			goto add_new_inode;
		list_add(&inode->i_list, &inode_unused);
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

struct inode *iget(struct super_block *sb, unsigned long ino)
{
	struct list_head * head = inode_hashtable + hash(sb,ino);
	struct inode * inode;

	spin_lock(&inode_lock);
	inode = find_inode(sb, ino, head);
	if (!inode) {
		try_to_free_inodes();
		return get_new_inode(sb, ino, head);
	}
	spin_unlock(&inode_lock);
	wait_on_inode(inode);
	return inode;
}

void insert_inode_hash(struct inode *inode)
{
	struct list_head *head = inode_hashtable + hash(inode->i_sb, inode->i_ino);
	list_add(&inode->i_hash, head);
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
				if (op && op->delete_inode) {
					void (*delete)(struct inode *) = op->delete_inode;
					spin_unlock(&inode_lock);
					delete(inode);
					spin_lock(&inode_lock);
				}
			}
			if (list_empty(&inode->i_hash)) {
				list_del(&inode->i_list);
				list_add(&inode->i_list, &inode_unused);
			}
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
 * Initialize the hash tables
 */
void inode_init(void)
{
	int i;
	struct list_head *head = inode_hashtable;

	i = HASH_SIZE;
	do {
		INIT_LIST_HEAD(head);
		head++;
		i--;
	} while (i);
}

/*
 * FIXME! These need to go through the in-use inodes to
 * check whether we can mount/umount/remount.
 */
int fs_may_mount(kdev_t dev)
{
	return 1;
}

int fs_may_umount(struct super_block *sb, struct dentry * root)
{
	shrink_dcache();
	return root->d_count == 1;
}

int fs_may_remount_ro(struct super_block *sb)
{
	return 1;
}
