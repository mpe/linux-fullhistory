/*
 *  linux/fs/dcache.c
 *
 *  (C) Copyright 1994 Linus Torvalds
 */

/* Speeded up searches a bit and threaded the mess. -DaveM */

/*
 * The directory cache is a "two-level" cache, each level doing LRU on
 * its entries.  Adding new entries puts them at the end of the LRU
 * queue on the first-level cache, while the second-level cache is
 * fed by any cache hits.
 *
 * The idea is that new additions (from readdir(), for example) will not
 * flush the cache of entries that have really been used.
 *
 * There is a global hash-table over both caches that hashes the entries
 * based on the directory inode number and device as well as on a
 * string-hash computed over the name. 
 */

#include <linux/fs.h>
#include <linux/string.h>

#include <asm/unaligned.h>
#include <asm/spinlock.h>

spinlock_t dcache_lock = SPIN_LOCK_UNLOCKED;

/*
 * Don't bother caching long names.. They just take up space in the cache, and
 * for a name cache you just want to cache the "normal" names anyway which tend
 * to be short.
 */
#define DCACHE_NAME_LEN	15
#define DCACHE_SIZE 1024
#define DCACHE_HASH_QUEUES 256	/* keep this a pow2 */

/*
 * The dir_cache_entry must be in this order: we do ugly things with the pointers
 */
struct dir_cache_entry {
	struct dir_cache_entry *next;
	struct dir_cache_entry **pprev;
	kdev_t dc_dev;
	unsigned long dir;
	unsigned long version;
	unsigned long ino;
	unsigned char name_len;
	char name[DCACHE_NAME_LEN];
	struct dir_cache_entry ** lru_head;
	struct dir_cache_entry * next_lru,  * prev_lru;
};

#define dcache_offset(x) ((unsigned long)&((struct dir_cache_entry*)0)->x)
#define dcache_datalen (dcache_offset(lru_head) - dcache_offset(dc_dev))

#define COPYDATA(de, newde) \
memcpy((void *) &newde->dc_dev, (void *) &de->dc_dev, dcache_datalen)

static struct dir_cache_entry level1_cache[DCACHE_SIZE];
static struct dir_cache_entry level2_cache[DCACHE_SIZE];

/*
 * The LRU-lists are doubly-linked circular lists, and do not change in size
 * so these pointers always have something to point to (after _init)
 */
static struct dir_cache_entry * level1_head;
static struct dir_cache_entry * level2_head;

/* The hash queues are layed out in a slightly different manner. */
static struct dir_cache_entry *hash_table[DCACHE_HASH_QUEUES];

#define hash_fn(dev,dir,namehash) \
	((HASHDEV(dev) ^ (dir) ^ (namehash)) & (DCACHE_HASH_QUEUES - 1))

/*
 * Stupid name"hash" algorithm. Write something better if you want to,
 * but I doubt it matters that much.
 */
static unsigned long namehash(const char * name, int len)
{
	unsigned long hash = 0;

	while ((len -= sizeof(unsigned long)) > 0) {
		hash += get_unaligned((unsigned long *)name);
		name += sizeof(unsigned long);
	}
	return hash +
		(get_unaligned((unsigned long *)name) &
		 ~(~0UL << ((len + sizeof(unsigned long)) << 3)));
}

static inline struct dir_cache_entry **get_hlist(struct inode *dir,
						 const char *name, int len)
{
	return hash_table + hash_fn(dir->i_dev, dir->i_ino, namehash(name, len));
}

static inline void remove_lru(struct dir_cache_entry * de)
{
	struct dir_cache_entry * next = de->next_lru;
	struct dir_cache_entry * prev = de->prev_lru;

	next->prev_lru = prev;
	prev->next_lru = next;
}

static inline void add_lru(struct dir_cache_entry * de, struct dir_cache_entry *head)
{
	struct dir_cache_entry * prev = head->prev_lru;

	de->next_lru = head;
	de->prev_lru = prev;
	prev->next_lru = de;
	head->prev_lru = de;
}

static inline void update_lru(struct dir_cache_entry * de)
{
	if (de == *de->lru_head)
		*de->lru_head = de->next_lru;
	else {
		remove_lru(de);
		add_lru(de,*de->lru_head);
	}
}

/*
 * Hash queue manipulation. Look out for the casts..
 *
 * What casts? 8-) -DaveM
 */
static inline void remove_hash(struct dir_cache_entry * de)
{
	if(de->pprev) {
		if(de->next)
			de->next->pprev = de->pprev;
		*de->pprev = de->next;
		de->pprev = NULL;
	}
}

static inline void add_hash(struct dir_cache_entry * de, struct dir_cache_entry ** hash)
{
	if((de->next = *hash) != NULL)
		(*hash)->pprev = &de->next;
	*hash = de;
	de->pprev = hash;
}

/*
 * Find a directory cache entry given all the necessary info.
 */
static inline struct dir_cache_entry * find_entry(struct inode * dir, const char * name, unsigned char len, struct dir_cache_entry ** hash)
{
	struct dir_cache_entry *de;

	de = *hash;
	goto inside;
	for (;;) {
		de = de->next;
inside:
		if (!de)
			break;
		if((de->name_len == (unsigned char) len)	&&
		   (de->dc_dev == dir->i_dev)			&&
		   (de->dir == dir->i_ino)			&&
		   (de->version == dir->i_version)		&&
		   (!memcmp(de->name, name, len)))
			break;
	}
	return de;
}

/*
 * Move a successfully used entry to level2. If already at level2,
 * move it to the end of the LRU queue..
 */
static inline void move_to_level2(struct dir_cache_entry * old_de, struct dir_cache_entry ** hash)
{
	struct dir_cache_entry * de;

	if (old_de->lru_head == &level2_head) {
		update_lru(old_de);
		return;
	}	
	de = level2_head;
	level2_head = de->next_lru;
	remove_hash(de);
	COPYDATA(old_de, de);
	add_hash(de, hash);
}

int dcache_lookup(struct inode * dir, const char * name, int len, unsigned long * ino)
{
	int ret = 0;

	if(len <= DCACHE_NAME_LEN) {
		struct dir_cache_entry **hash = get_hlist(dir, name, len);
		struct dir_cache_entry *de;

		spin_lock(&dcache_lock);
		de = find_entry(dir, name, (unsigned char) len, hash);
		if(de) {
			*ino = de->ino;
			move_to_level2(de, hash);
			ret = 1;
		}
		spin_unlock(&dcache_lock);
	}
	return ret;
}

void dcache_add(struct inode * dir, const char * name, int len, unsigned long ino)
{
	if (len <= DCACHE_NAME_LEN) {
		struct dir_cache_entry **hash = get_hlist(dir, name, len);
		struct dir_cache_entry *de;

		spin_lock(&dcache_lock);
		de = find_entry(dir, name, (unsigned char) len, hash);
		if (de) {
			de->ino = ino;
			update_lru(de);
		} else {
			de = level1_head;
			level1_head = de->next_lru;
			remove_hash(de);
			de->dc_dev = dir->i_dev;
			de->dir = dir->i_ino;
			de->version = dir->i_version;
			de->ino = ino;
			de->name_len = len;
			memcpy(de->name, name, len);
			add_hash(de, hash);
		}
		spin_unlock(&dcache_lock);
	}
}

unsigned long name_cache_init(unsigned long mem_start, unsigned long mem_end)
{
	int i;
	struct dir_cache_entry * p;

	/*
	 * Init level1 LRU lists..
	 */
	p = level1_cache;
	do {
		p[1].prev_lru = p;
		p[0].next_lru = p+1;
		p[0].lru_head = &level1_head;
	} while (++p < level1_cache + DCACHE_SIZE-1);
	level1_cache[0].prev_lru = p;
	p[0].next_lru = &level1_cache[0];
	p[0].lru_head = &level1_head;
	level1_head = level1_cache;

	/*
	 * Init level2 LRU lists..
	 */
	p = level2_cache;
	do {
		p[1].prev_lru = p;
		p[0].next_lru = p+1;
		p[0].lru_head = &level2_head;
	} while (++p < level2_cache + DCACHE_SIZE-1);
	level2_cache[0].prev_lru = p;
	p[0].next_lru = &level2_cache[0];
	p[0].lru_head = &level2_head;
	level2_head = level2_cache;

	/*
	 * Empty hash queues..
	 */
	for (i = 0 ; i < DCACHE_HASH_QUEUES ; i++)
		hash_table[i] = NULL;

	return mem_start;
}
