/*
 * The "user cache".
 *
 * (C) Copyright 1991-2000 Linus Torvalds
 *
 * We have a per-user structure to keep track of how many
 * processes, files etc the user has claimed, in order to be
 * able to have per-user limits for system resources. 
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/slab.h>

/*
 * UID task count cache, to get fast user lookup in "alloc_uid"
 * when changing user ID's (ie setuid() and friends).
 */
#define UIDHASH_SZ	(256)

static struct user_struct *uidhash[UIDHASH_SZ];

spinlock_t uidhash_lock = SPIN_LOCK_UNLOCKED;

struct user_struct root_user = {
	__count:	ATOMIC_INIT(1),
	processes:	ATOMIC_INIT(1),
	files:		ATOMIC_INIT(0)
};

static kmem_cache_t *uid_cachep;

#define uidhashfn(uid)	(((uid >> 8) ^ uid) & (UIDHASH_SZ - 1))

/*
 * These routines must be called with the uidhash spinlock held!
 */
static inline void uid_hash_insert(struct user_struct *up, unsigned int hashent)
{
	if((up->next = uidhash[hashent]) != NULL)
		uidhash[hashent]->pprev = &up->next;
	up->pprev = &uidhash[hashent];
	uidhash[hashent] = up;
}

static inline void uid_hash_remove(struct user_struct *up)
{
	if(up->next)
		up->next->pprev = up->pprev;
	*up->pprev = up->next;
}

static inline struct user_struct *uid_hash_find(unsigned short uid, unsigned int hashent)
{
	struct user_struct *up, *next;

	next = uidhash[hashent];
	for (;;) {
		up = next;
		if (next) {
			next = up->next;
			if (up->uid != uid)
				continue;
			atomic_inc(&up->__count);
		}
		break;
	}
	return up;
}

/*
 * For SMP, we need to re-test the user struct counter
 * after having aquired the spinlock. This allows us to do
 * the common case (not freeing anything) without having
 * any locking.
 */
#ifdef CONFIG_SMP
  #define uid_hash_free(up)	(!atomic_read(&(up)->__count))
#else
  #define uid_hash_free(up)	(1)
#endif

void free_uid(struct user_struct *up)
{
	if (up) {
		if (atomic_dec_and_test(&up->__count)) {
			spin_lock(&uidhash_lock);
			if (uid_hash_free(up)) {
				uid_hash_remove(up);
				kmem_cache_free(uid_cachep, up);
			}
			spin_unlock(&uidhash_lock);
		}
	}
}

struct user_struct * alloc_uid(uid_t uid)
{
	unsigned int hashent = uidhashfn(uid);
	struct user_struct *up;

	spin_lock(&uidhash_lock);
	up = uid_hash_find(uid, hashent);
	spin_unlock(&uidhash_lock);

	if (!up) {
		struct user_struct *new;

		new = kmem_cache_alloc(uid_cachep, SLAB_KERNEL);
		if (!new)
			return NULL;
		new->uid = uid;
		atomic_set(&new->__count, 1);
		atomic_set(&new->processes, 0);
		atomic_set(&new->files, 0);

		/*
		 * Before adding this, check whether we raced
		 * on adding the same user already..
		 */
		spin_lock(&uidhash_lock);
		up = uid_hash_find(uid, hashent);
		if (up) {
			kmem_cache_free(uid_cachep, new);
		} else {
			uid_hash_insert(new, hashent);
			up = new;
		}
		spin_unlock(&uidhash_lock);

	}
	return up;
}


static int __init uid_cache_init(void)
{
	int i;

	uid_cachep = kmem_cache_create("uid_cache", sizeof(struct user_struct),
				       0,
				       SLAB_HWCACHE_ALIGN, NULL, NULL);
	if(!uid_cachep)
		panic("Cannot create uid taskcount SLAB cache\n");

	for(i = 0; i < UIDHASH_SZ; i++)
		uidhash[i] = 0;

	/* Insert the root user immediately - init already runs with this */
	uid_hash_insert(&root_user, uidhashfn(0));
	return 0;
}

module_init(uid_cache_init);
