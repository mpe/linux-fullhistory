/*
 * "inohash" is a misnomer.  Inodes are just stored in a single list,
 * since this code is only ever asked for the most recently inserted
 * inode.
 *
 *   Copyright 1999 Jeremy Fitzhardinge <jeremy@goop.org>
 */

#include "autofs_i.h"

void autofs4_init_ihash(struct autofs_inohash *ih)
{
	INIT_LIST_HEAD(&ih->head);
}

void autofs4_ihash_insert(struct autofs_inohash *ih, 
			  struct autofs_info *ino)
{
	DPRINTK(("autofs_ihash_insert: adding ino %ld\n", ino->ino));
	
	list_add(&ino->ino_hash, &ih->head);
}

void autofs4_ihash_delete(struct autofs_info *inf)
{
	DPRINTK(("autofs_ihash_delete: deleting ino %ld\n", inf->ino));

	if (!list_empty(&inf->ino_hash))
		list_del(&inf->ino_hash);
}

struct autofs_info *autofs4_ihash_find(struct autofs_inohash *ih, 
				       ino_t inum)
{
	struct list_head *tmp;

	for(tmp = ih->head.next;
	    tmp != &ih->head;
	    tmp = tmp->next) {
		struct autofs_info *ino = list_entry(tmp, struct autofs_info, ino_hash);
		if (ino->ino == inum) {
			DPRINTK(("autofs_ihash_find: found %ld -> %p\n",
				 inum, ino));
			return ino;
		}
	}
	DPRINTK(("autofs_ihash_find: didn't find %ld\n", inum));
	return NULL;
}

void autofs4_ihash_nuke(struct autofs_inohash *ih)
{
	struct list_head *tmp = ih->head.next;
	struct list_head *next;
		
	for(; tmp != &ih->head; tmp = next) {
		struct autofs_info *ino;

		next = tmp->next;

		ino = list_entry(tmp, struct autofs_info, ino_hash);

		DPRINTK(("autofs_ihash_nuke: nuking %ld\n", ino->ino));
		autofs4_free_ino(ino);
	}
	INIT_LIST_HEAD(&ih->head);
}

