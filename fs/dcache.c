/*
 *  linux/fs/dcache.c
 *
 *  (C) Copyright 1994 Linus Torvalds
 */

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

#include <stddef.h>

#include <linux/fs.h>
#include <linux/string.h>

/*
 * Don't bother caching long names.. They just take up space in the cache, and
 * for a name cache you just want to cache the "normal" names anyway which tend
 * to be short.
 */
#define DCACHE_NAME_LEN	15
#define DCACHE_SIZE 128

struct hash_list {
	struct dir_cache_entry * next;
	struct dir_cache_entry * prev;
};

/*
 * The dir_cache_entry must be in this order: we do ugly things with the pointers
 */
struct dir_cache_entry {
	struct hash_list h;
	unsigned long dev;
	unsigned long dir;
	unsigned long version;
	unsigned long ino;
	unsigned char name_len;
	char name[DCACHE_NAME_LEN];
	struct dir_cache_entry ** lru_head;
	struct dir_cache_entry * next_lru,  * prev_lru;
};

#define COPYDATA(de, newde) \
memcpy((void *) &newde->dev, (void *) &de->dev, \
4*sizeof(unsigned long) + 1 + DCACHE_NAME_LEN)

static struct dir_cache_entry level1_cache[DCACHE_SIZE];
static struct dir_cache_entry level2_cache[DCACHE_SIZE];

/*
 * The LRU-lists are doubly-linked circular lists, and do not change in size
 * so these pointers always have something to point to (after _init)
 */
static struct dir_cache_entry * level1_head;
static struct dir_cache_entry * level2_head;

/*
 * The hash-queues are also doubly-linked circular lists, but the head is
 * itself on the doubly-linked list, not just a pointer to the first entry.
 */
#define DCACHE_HASH_QUEUES 19
#define hash_fn(dev,dir,namehash) (((dev) ^ (dir) ^ (namehash)) % DCACHE_HASH_QUEUES)

static struct hash_list hash_table[DCACHE_HASH_QUEUES];

static inline void remove_lru(struct dir_cache_entry * de)
{
	de->next_lru->prev_lru = de->prev_lru;
	de->prev_lru->next_lru = de->next_lru;
}

static inline void add_lru(struct dir_cache_entry * de, struct dir_cache_entry *head)
{
	de->next_lru = head;
	de->prev_lru = head->prev_lru;
	de->prev_lru->next_lru = de;
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
 * Stupid name"hash" algorithm. Write something better if you want to,
 * but I doubt it matters that much
 */
static inline unsigned long namehash(const char * name, int len)
{
	return len * *(unsigned char *) name;
}

/*
 * Hash queue manipulation. Look out for the casts..
 */
static inline void remove_hash(struct dir_cache_entry * de)
{
	if (de->h.next) {
		de->h.next->h.prev = de->h.prev;
		de->h.prev->h.next = de->h.next;
		de->h.next = NULL;
	}
}

static inline void add_hash(struct dir_cache_entry * de, struct hash_list * hash)
{
	de->h.next = hash->next;
	de->h.prev = (struct dir_cache_entry *) hash;
	hash->next->h.prev = de;
	hash->next = de;
}

/*
 * Find a directory cache entry given all the necessary info.
 */
static struct dir_cache_entry * find_entry(struct inode * dir, const char * name, int len, struct hash_list * hash)
{
	struct dir_cache_entry * de = hash->next;

	for (de = hash->next ; de != (struct dir_cache_entry *) hash ; de = de->h.next) {
		if (de->dev != dir->i_dev)
			continue;
		if (de->dir != dir->i_ino)
			continue;
		if (de->version != dir->i_version)
			continue;
		if (de->name_len != len)
			continue;
		if (memcmp(de->name, name, len))
			continue;
		return de;
	}
	return NULL;
}

/*
 * Move a successfully used entry to level2. If already at level2,
 * move it to the end of the LRU queue..
 */
static inline void move_to_level2(struct dir_cache_entry * old_de, struct hash_list * hash)
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
	struct hash_list * hash;
	struct dir_cache_entry *de;

	if (len > DCACHE_NAME_LEN)
		return 0;
	hash = hash_table + hash_fn(dir->i_dev, dir->i_ino, namehash(name,len));
	de = find_entry(dir, name, len, hash);
	if (!de)
		return 0;
	*ino = de->ino;
	move_to_level2(de, hash);
	return 1;
}

void dcache_add(struct inode * dir, const char * name, int len, unsigned long ino)
{
	struct hash_list * hash;
	struct dir_cache_entry *de;

	if (len > DCACHE_NAME_LEN)
		return;
	hash = hash_table + hash_fn(dir->i_dev, dir->i_ino, namehash(name,len));
	if ((de = find_entry(dir, name, len, hash)) != NULL) {
		de->ino = ino;
		update_lru(de);
		return;
	}
	de = level1_head;
	level1_head = de->next_lru;
	remove_hash(de);
	de->dev = dir->i_dev;
	de->dir = dir->i_ino;
	de->version = dir->i_version;
	de->ino = ino;
	de->name_len = len;
	memcpy(de->name, name, len);
	add_hash(de, hash);
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
		hash_table[i].next = hash_table[i].next =
			(struct dir_cache_entry *) &hash_table[i];
	return mem_start;
}
