/* -*- linux-c -*- --------------------------------------------------------- *
 *
 * linux/fs/autofs/dirhash.c
 *
 *  Copyright 1997-1998 Transmeta Corporation -- All Rights Reserved
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * ------------------------------------------------------------------------- */

#include "autofs_i.h"

/* Functions for maintenance of expiry queue */

static void autofs_init_usage(struct autofs_dirhash *dh,
			      struct autofs_dir_ent *ent)
{
	ent->exp_next = &dh->expiry_head;
	ent->exp_prev = dh->expiry_head.exp_prev;
	dh->expiry_head.exp_prev->exp_next = ent;
	dh->expiry_head.exp_prev = ent;
	ent->last_usage = jiffies;
}

static void autofs_delete_usage(struct autofs_dir_ent *ent)
{
	ent->exp_prev->exp_next = ent->exp_next;
	ent->exp_next->exp_prev = ent->exp_prev;
}

void autofs_update_usage(struct autofs_dirhash *dh,
			 struct autofs_dir_ent *ent)
{
	autofs_delete_usage(ent);   /* Unlink from current position */
	autofs_init_usage(dh,ent);  /* Relink at queue tail */
}

struct autofs_dir_ent *autofs_expire(struct autofs_dirhash *dh,
				     unsigned long timeout)
{
	struct autofs_dir_ent *ent;

	ent = dh->expiry_head.exp_next;

	if ( ent == &(dh->expiry_head) ) return NULL;
	return (jiffies - ent->last_usage >= timeout) ? ent : NULL;
}

void autofs_initialize_hash(struct autofs_dirhash *dh) {
	memset(&dh->h, 0, AUTOFS_HASH_SIZE*sizeof(struct autofs_dir_ent *));
	dh->expiry_head.exp_next = dh->expiry_head.exp_prev =
		&dh->expiry_head;
}

struct autofs_dir_ent *autofs_hash_lookup(const struct autofs_dirhash *dh, struct qstr *name)
{
	struct autofs_dir_ent *dhn;

	DPRINTK(("autofs_hash_lookup: hash = 0x%08x, name = ", name->hash));
	autofs_say(name->name,name->len);

	for ( dhn = dh->h[(unsigned) name->hash % AUTOFS_HASH_SIZE] ; dhn ; dhn = dhn->next ) {
		if ( name->hash == dhn->hash &&
		     name->len == dhn->len &&
		     !memcmp(name->name, dhn->name, name->len) )
			break;
	}

	return dhn;
}

void autofs_hash_insert(struct autofs_dirhash *dh, struct autofs_dir_ent *ent)
{
	struct autofs_dir_ent **dhnp;

	DPRINTK(("autofs_hash_insert: hash = 0x%08x, name = ", ent->hash));
	autofs_say(ent->name,ent->len);

	autofs_init_usage(dh,ent);

	dhnp = &dh->h[(unsigned) ent->hash % AUTOFS_HASH_SIZE];
	ent->next = *dhnp;
	ent->back = dhnp;
	*dhnp = ent;
	if ( ent->next )
		ent->next->back = &(ent->next);
}

void autofs_hash_delete(struct autofs_dir_ent *ent)
{
	*(ent->back) = ent->next;
	if ( ent->next )
		ent->next->back = ent->back;

	autofs_delete_usage(ent);

	kfree(ent->name);
	kfree(ent);
}

/*
 * Used by readdir().  We must validate "ptr", so we can't simply make it
 * a pointer.  Values below 0xffff are reserved; calling with any value
 * <= 0x10000 will return the first entry found.
 */
struct autofs_dir_ent *autofs_hash_enum(const struct autofs_dirhash *dh, off_t *ptr)
{
	int bucket, ecount, i;
	struct autofs_dir_ent *ent;

	bucket = (*ptr >> 16) - 1;
	ecount = *ptr & 0xffff;

	if ( bucket < 0 ) {
		bucket = ecount = 0;
	} 

	DPRINTK(("autofs_hash_enum: bucket %d, entry %d\n", bucket, ecount));

	ent = NULL;

	while  ( bucket < AUTOFS_HASH_SIZE ) {
		ent = dh->h[bucket];
		for ( i = ecount ; ent && i ; i-- )
			ent = ent->next;

		if (ent) {
			ecount++; /* Point to *next* entry */
			break;
		}

		bucket++; ecount = 0;
	}

#ifdef DEBUG
	if ( !ent )
		printk("autofs_hash_enum: nothing found\n");
	else {
		printk("autofs_hash_enum: found hash %08x, name", ent->hash);
		autofs_say(ent->name,ent->len);
	}
#endif

	*ptr = ((bucket+1) << 16) + ecount;
	return ent;
}

/* Delete everything.  This is used on filesystem destruction, so we
   make no attempt to keep the pointers valid */
void autofs_hash_nuke(struct autofs_dirhash *dh)
{
	int i;
	struct autofs_dir_ent *ent, *nent;

	for ( i = 0 ; i < AUTOFS_HASH_SIZE ; i++ ) {
		for ( ent = dh->h[i] ; ent ; ent = nent ) {
			nent = ent->next;
			kfree(ent->name);
			kfree(ent);
		}
	}
}
