/* -*- linux-c -*- --------------------------------------------------------- *
 *
 * linux/fs/autofs/dirhash.c
 *
 *  Copyright 1997 Transmeta Corporation -- All Rights Reserved
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * ------------------------------------------------------------------------- */

#include <linux/modversions.h>
#include <linux/string.h>
#include <linux/malloc.h>
#include <linux/auto_fs.h>

/* Adapted from the Dragon Book, page 436 */
/* This particular hashing algorithm requires autofs_hash_t == u32 */
autofs_hash_t autofs_hash(const char *name, int len)
{
	autofs_hash_t h = 0;
	while ( len-- ) {
		h = (h << 4) + (unsigned char) (*name++);
		h ^= ((h & 0xf0000000) >> 24);
	}
	return h;
}

void autofs_initialize_hash(struct autofs_dirhash *dh) {
	memset(&dh->h, 0, AUTOFS_HASH_SIZE*sizeof(struct autofs_dir_ent *));
}

struct autofs_dir_ent *autofs_hash_lookup(const struct autofs_dirhash *dh, autofs_hash_t hash, const char *name, int len)
{
	struct autofs_dir_ent *dhn;

	DPRINTK(("autofs_hash_lookup: hash = 0x%08x, name = ", hash));
	autofs_say(name,len);

	for ( dhn = dh->h[hash % AUTOFS_HASH_SIZE] ; dhn ; dhn = dhn->next ) {
		if ( hash == dhn->hash &&
		     len == dhn->len &&
		     !memcmp(name, dhn->name, len) )
			break;
	}

	return dhn;
}

void autofs_hash_insert(struct autofs_dirhash *dh, struct autofs_dir_ent *ent)
{
	struct autofs_dir_ent **dhnp;

	DPRINTK(("autofs_hash_insert: hash = 0x%08x, name = ", ent->hash));
	autofs_say(ent->name,ent->len);

	dhnp = &dh->h[ent->hash % AUTOFS_HASH_SIZE];
	ent->next = *dhnp;
	ent->back = dhnp;
	*dhnp = ent;
}

void autofs_hash_delete(struct autofs_dir_ent *ent)
{
	*(ent->back) = ent->next;
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
