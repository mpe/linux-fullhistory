/*
 * Generic pidhash and scalable, time-bounded PID allocator
 *
 * (C) 2002 William Irwin, IBM
 * (C) 2002 Ingo Molnar, Red Hat
 *
 * pid-structures are backing objects for tasks sharing a given ID to chain
 * against. There is very little to them aside from hashing them and
 * parking tasks using given ID's on a list.
 *
 * The hash is always changed with the tasklist_lock write-acquired,
 * and the hash is only accessed with the tasklist_lock at least
 * read-acquired, so there's no additional SMP locking needed here.
 *
 * We have a list of bitmap pages, which bitmaps represent the PID space.
 * Allocating and freeing PIDs is completely lockless. The worst-case
 * allocation scenario when all but one out of 1 million PIDs possible are
 * allocated already: the scanning of 32 list entries and at most PAGE_SIZE
 * bytes. The typical fastpath is a single successful setbit. Freeing is O(1).
 */

#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/bootmem.h>

#define PIDHASH_SIZE 4096
#define pid_hashfn(nr) ((nr >> 8) ^ nr) & (PIDHASH_SIZE - 1)
static struct list_head pid_hash[PIDTYPE_MAX][PIDHASH_SIZE];

int pid_max = PID_MAX_DEFAULT;
int last_pid;

#define RESERVED_PIDS		300

#define PIDMAP_ENTRIES		(PID_MAX_LIMIT/PAGE_SIZE/8)
#define BITS_PER_PAGE		(PAGE_SIZE*8)
#define BITS_PER_PAGE_MASK	(BITS_PER_PAGE-1)

/*
 * PID-map pages start out as NULL, they get allocated upon
 * first use and are never deallocated. This way a low pid_max
 * value does not cause lots of bitmaps to be allocated, but
 * the scheme scales to up to 4 million PIDs, runtime.
 */
typedef struct pidmap {
	atomic_t nr_free;
	void *page;
} pidmap_t;

static pidmap_t pidmap_array[PIDMAP_ENTRIES] =
	 { [ 0 ... PIDMAP_ENTRIES-1 ] = { ATOMIC_INIT(BITS_PER_PAGE), NULL } };

static pidmap_t *map_limit = pidmap_array + PIDMAP_ENTRIES;

inline void free_pidmap(int pid)
{
	pidmap_t *map = pidmap_array + pid / BITS_PER_PAGE;
	int offset = pid & BITS_PER_PAGE_MASK;

	clear_bit(offset, map->page);
	atomic_inc(&map->nr_free);
}

/*
 * Here we search for the next map that has free bits left.
 * Normally the next map has free PIDs.
 */
static inline pidmap_t *next_free_map(pidmap_t *map, int *max_steps)
{
	while (--*max_steps) {
		if (++map == map_limit)
			map = pidmap_array;
		if (unlikely(!map->page)) {
			unsigned long page = get_zeroed_page(GFP_KERNEL);
			/*
			 * Free the page if someone raced with us
			 * installing it:
			 */
			if (cmpxchg(&map->page, NULL, (void *) page))
				free_page(page);
			if (!map->page)
				break;
		}
		if (atomic_read(&map->nr_free))
			return map;
	}
	return NULL;
}

int alloc_pidmap(void)
{
	int pid, offset, max_steps = PIDMAP_ENTRIES + 1;
	pidmap_t *map;

	pid = last_pid + 1;
	if (pid >= pid_max)
		pid = RESERVED_PIDS;

	offset = pid & BITS_PER_PAGE_MASK;
	map = pidmap_array + pid / BITS_PER_PAGE;

	if (likely(map->page && !test_and_set_bit(offset, map->page))) {
		/*
		 * There is a small window for last_pid updates to race,
		 * but in that case the next allocation will go into the
		 * slowpath and that fixes things up.
		 */
return_pid:
		atomic_dec(&map->nr_free);
		last_pid = pid;
		return pid;
	}
	
	if (!offset || !atomic_read(&map->nr_free)) {
next_map:
		map = next_free_map(map, &max_steps);
		if (!map)
			goto failure;
		offset = 0;
	}
	/*
	 * Find the next zero bit:
	 */
scan_more:
	offset = find_next_zero_bit(map->page, BITS_PER_PAGE, offset);
	if (offset == BITS_PER_PAGE)
		goto next_map;
	if (test_and_set_bit(offset, map->page))
		goto scan_more;

	/* we got the PID: */
	pid = (map - pidmap_array) * BITS_PER_PAGE + offset;
	goto return_pid;

failure:
	return -1;
}

inline struct pid *find_pid(enum pid_type type, int nr)
{
	struct list_head *elem, *bucket = &pid_hash[type][pid_hashfn(nr)];
	struct pid *pid;

	list_for_each_noprefetch(elem, bucket) {
		pid = list_entry(elem, struct pid, hash_chain);
		if (pid->nr == nr)
			return pid;
	}
	return NULL;
}

int attach_pid(task_t *task, enum pid_type type, int nr)
{
	struct pid *pid = find_pid(type, nr);

	if (pid)
		atomic_inc(&pid->count);
	else {
		pid = &task->pids[type].pid;
		pid->nr = nr;
		atomic_set(&pid->count, 1);
		INIT_LIST_HEAD(&pid->task_list);
		pid->task = current;
		get_task_struct(current);
		list_add(&pid->hash_chain, &pid_hash[type][pid_hashfn(nr)]);
	}
	list_add(&task->pids[type].pid_chain, &pid->task_list);
	task->pids[type].pidptr = pid;

	return 0;
}

void detach_pid(task_t *task, enum pid_type type)
{
	struct pid_link *link = task->pids + type;
	struct pid *pid = link->pidptr;
	int nr;

	list_del(&link->pid_chain);
	if (!atomic_dec_and_test(&pid->count))
		return;

	nr = pid->nr;
	list_del(&pid->hash_chain);
	put_task_struct(pid->task);

	for (type = 0; type < PIDTYPE_MAX; ++type)
		if (find_pid(type, nr))
			return;
	free_pidmap(nr);
}

extern task_t *find_task_by_pid(int nr)
{
	struct pid *pid = find_pid(PIDTYPE_PID, nr);

	if (!pid)
		return NULL;
	return pid_task(pid->task_list.next, PIDTYPE_PID);
}

void __init pidhash_init(void)
{
	int i, j;

	/*
	 * Allocate PID 0, and hash it via all PID types:
	 */
	pidmap_array->page = (void *)get_zeroed_page(GFP_KERNEL);
	set_bit(0, pidmap_array->page);
	atomic_dec(&pidmap_array->nr_free);

	for (i = 0; i < PIDTYPE_MAX; i++) {
		for (j = 0; j < PIDHASH_SIZE; j++)
			INIT_LIST_HEAD(&pid_hash[i][j]);
		attach_pid(current, i, 0);
	}
}
