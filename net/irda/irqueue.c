/*********************************************************************
 *                
 * Filename:      irqueue.c
 * Version:       0.3
 * Description:   General queue implementation
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Tue Jun  9 13:29:31 1998
 * Modified at:   Wed Jan 13 21:21:22 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (C) 1998, Aage Kvalnes <aage@cs.uit.no>
 *     Copyright (C) 1998, Dag Brattli, 
 *     All Rights Reserved.
 *
 *     This code is taken from the Vortex Operating System written by Aage
 *     Kvalnes. Aage has agreed that this code can use the GPL licence,
 *     although he does not use that licence in his own code.
 *     
 *     This copyright does however _not_ include the ELF hash() function
 *     which I currently don't know which licence or copyright it
 *     has. Please inform me if you know.
 *      
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 *  
 *     Neither Dag Brattli nor University of Tromsø admit liability nor
 *     provide warranty for any of this software. This material is 
 *     provided "AS-IS" and at no charge.
 *     
 ********************************************************************/

#include <net/irda/irda.h>
#include <net/irda/irqueue.h>
#include <net/irda/irmod.h>

static QUEUE *dequeue_general( QUEUE **queue, QUEUE* element);
static __u32 hash( char* name);

/*
 * Function hashbin_create ( type, name )
 *
 *    Create hashbin!
 *
 */
hashbin_t *hashbin_new( int type)
{
	hashbin_t* hashbin;
	
	DEBUG( 4, __FUNCTION__ "()\n");
	
	/*
	 * Allocate new hashbin
	 */
	hashbin = kmalloc( sizeof(hashbin_t), GFP_ATOMIC);

	/*
	 * Initialize structure
	 */
	memset( hashbin, 0, sizeof(hashbin_t));
	hashbin->hb_type = type;
	hashbin->magic = HB_MAGIC;
	
	return hashbin;
}

/*
 * Function hashbin_clear (hashbin, free_func)
 *
 *    Remove all entries from the hashbin, see also the comments in 
 *    hashbin_delete() below
 */
int hashbin_clear( hashbin_t* hashbin, FREE_FUNC free_func)
{
	QUEUE* queue;
	int i;
	
	ASSERT( hashbin != NULL, return -1;);
	ASSERT( hashbin->magic == HB_MAGIC, return -1;);

	/*
	 * Free the entries in the hashbin
	 */
	for ( i = 0; i < HASHBIN_SIZE; i ++ ) {
		queue = dequeue_first( (QUEUE**) &hashbin->hb_queue[ i]);
		while( queue ) {
			if ( free_func)
				(*free_func)( queue );
			queue = dequeue_first( 
				(QUEUE**) &hashbin->hb_queue[ i]);
		}
	}
	hashbin->hb_size = 0;

	return 0;
}


/*
 * Function hashbin_delete (hashbin, free_func)
 *
 *    Destroy hashbin, the free_func can be a user supplied special routine 
 *    for deallocating this structure if it's complex. If not the user can 
 *    just supply kfree, which should take care of the job.
 */
int hashbin_delete( hashbin_t* hashbin, FREE_FUNC free_func)
{
	int i;
	QUEUE* queue;

	DEBUG( 4, "hashbin_destroy()\n");
	
	/*
	 *  Free the entries in the hashbin, TODO: use hashbin_clear when
	 *  it has been shown to work
	 */
	for ( i = 0; i < HASHBIN_SIZE; i ++ ) {
		queue = dequeue_first( (QUEUE**) &hashbin->hb_queue[ i ] );
		while( queue ) {
			if ( free_func)
				(*free_func)( queue );
			queue = dequeue_first( 
				(QUEUE**) &hashbin->hb_queue[ i ]);
		}
	}
	
	/*
	 *  Free the hashbin structure
	 */
	hashbin->magic = ~HB_MAGIC;
	kfree( hashbin );

	return 0;
}

/*
 * Function hashbin_lock (hashbin, hashv, name)
 *
 *    Lock the hashbin
 *
 */
void hashbin_lock( hashbin_t* hashbin, __u32 hashv, char* name, 
		   unsigned long flags)
{
	int bin;
	
	DEBUG( 0, "hashbin_lock\n");

	ASSERT( hashbin != NULL, return;);
	ASSERT( hashbin->magic == HB_MAGIC, return;);

	/*
	 * Locate hashbin
	 */
	if ( name )
		hashv = hash( name );
	bin = GET_HASHBIN( hashv);
	
	/* Synchronize */
	if ( hashbin->hb_type & HB_GLOBAL ) {

		spin_lock_irqsave( &hashbin->hb_mutex[ bin], flags);
	} else {
		save_flags( flags);
		cli();
	}
}

/*
 * Function hashbin_unlock (hashbin, hashv, name)
 *
 *    Unlock the hashbin
 *
 */
void hashbin_unlock( hashbin_t* hashbin, __u32 hashv, char* name, 
		     unsigned long flags)
{
	int bin;

	DEBUG( 0, "hashbin_unlock()\n");

	ASSERT( hashbin != NULL, return;);
	ASSERT( hashbin->magic == HB_MAGIC, return;);
	
	/*
	 * Locate hashbin
	 */
	if ( name )
		hashv = hash( name );
	bin = GET_HASHBIN( hashv );
	
	/* Release lock */
	if ( hashbin->hb_type & HB_GLOBAL) {
		spin_unlock_irq( &hashbin->hb_mutex[ bin]);
	} else if ( hashbin->hb_type & HB_LOCAL) {
		restore_flags( flags);
	}
}

/*
 * Function hashbin_insert (hashbin, entry, name)
 *
 *    Insert an entry into the hashbin
 *
 */
void hashbin_insert( hashbin_t* hashbin, QUEUE* entry, __u32 hashv, 
		     char* name)
{
	unsigned long flags = 0;
	int bin;

	DEBUG( 4, "hashbin_insert()\n");

	ASSERT( hashbin != NULL, return;);
	ASSERT( hashbin->magic == HB_MAGIC, return;);

	/*
	 * Locate hashbin
	 */
	if ( name )
		hashv = hash( name );
	bin = GET_HASHBIN( hashv );

	/* Synchronize */
	if ( hashbin->hb_type & HB_GLOBAL ) {
		spin_lock_irqsave( &hashbin->hb_mutex[ bin ], flags);

	} else if ( hashbin->hb_type & HB_LOCAL ) {
		save_flags(flags);
		cli();
	} /* Default is no-lock  */
	
	/*
	 * Store name and key
	 */
	entry->q_hash = hashv;
	if ( name )
		strncpy( entry->q_name, name, 32);
	
	/*
	 * Insert new entry first
	 * TODO: Perhaps allow sorted lists?
	 *       -> Merge sort if a sorted list should be created
	 */
	if ( hashbin->hb_type & HB_SORTED) {
	} else {
		enqueue_first( (QUEUE**) &hashbin->hb_queue[ bin ],
			       entry);
	}
	hashbin->hb_size++;

	/* Release lock */
	if ( hashbin->hb_type & HB_GLOBAL) {

		spin_unlock_irq( &hashbin->hb_mutex[ bin]);

	} else if ( hashbin->hb_type & HB_LOCAL) {
		restore_flags( flags);
	}
}

/*
 * Function hashbin_find (hashbin, hashv, name)
 *
 *    Find item with the given hashv or name
 *
 */
void* hashbin_find( hashbin_t* hashbin, __u32 hashv, char* name )
{
	int bin, found = FALSE;
	unsigned long flags = 0;
	QUEUE* entry;

	DEBUG( 4, "hashbin_find()\n");

	ASSERT( hashbin != NULL, return NULL;);
	ASSERT( hashbin->magic == HB_MAGIC, return NULL;);

	/*
	 * Locate hashbin
	 */
	if ( name )
		hashv = hash( name );
	bin = GET_HASHBIN( hashv );
	
	/* Synchronize */
	if ( hashbin->hb_type & HB_GLOBAL ) {
		spin_lock_irqsave( &hashbin->hb_mutex[ bin ], flags);

	} else if ( hashbin->hb_type & HB_LOCAL ) {
		save_flags(flags);
		cli();
	} /* Default is no-lock  */
	
	/*
	 * Search for entry
	 */
	entry = hashbin->hb_queue[ bin];
	if ( entry ) {
		do {
			/*
			 * Check for key
			 */
			if ( entry->q_hash == hashv ) {
				/*
				 * Name compare too?
				 */
				if ( name ) {
					if ( strcmp( entry->q_name, name ) == 0 ) {
						found = TRUE;
						break;
					}
				} else {
					found = TRUE;
					break;
				}
			}
			entry = entry->q_next;
		} while ( entry != hashbin->hb_queue[ bin ] );
	}
	
	/* Release lock */
	if ( hashbin->hb_type & HB_GLOBAL) {
		spin_unlock_irq( &hashbin->hb_mutex[ bin]);

	} else if ( hashbin->hb_type & HB_LOCAL) {
		restore_flags( flags);
	}
	
	if ( found ) 
		return entry;
	else
		return NULL;
}

void *hashbin_remove_first( hashbin_t *hashbin)
{
	unsigned long flags;
	QUEUE *entry = NULL;

	save_flags(flags);
	cli();

	entry = hashbin_get_first( hashbin);
	if ( entry != NULL)
		hashbin_remove( hashbin, entry->q_hash, NULL);

	restore_flags( flags);

	return entry;
}


/* 
 *  Function hashbin_remove (hashbin, hashv, name)
 *
 *    Remove entry with the given name
 *
 */
void* hashbin_remove( hashbin_t* hashbin, __u32 hashv, char* name)
{
	int bin, found = FALSE;
	unsigned long flags = 0;
	QUEUE* entry;

	DEBUG( 4, __FUNCTION__ "()\n");

	ASSERT( hashbin != NULL, return NULL;);
	ASSERT( hashbin->magic == HB_MAGIC, return NULL;);
	
	/*
	 * Locate hashbin
	 */
	if ( name )
		hashv = hash( name );
	bin = GET_HASHBIN( hashv );

	if ( hashbin->hb_type & HB_GLOBAL ) {
		spin_lock_irqsave( &hashbin->hb_mutex[ bin ], flags);

	} else if ( hashbin->hb_type & HB_LOCAL ) {
		save_flags(flags);
		cli();
	} /* Default is no-lock  */

	/*
	 * Search for entry
	 */
	entry = hashbin->hb_queue[ bin ];
	if ( entry ) {
		do {
			/*
			 * Check for key
			 */
			if ( entry->q_hash == hashv ) {
				/*
				 * Name compare too?
				 */
				if ( name ) {
					if ( strcmp( entry->q_name, name) == 0)
					{
						found = TRUE;
						break;
					}
				} else {
					found = TRUE;
					break;
				}
			}
			entry = entry->q_next;
		} while ( entry != hashbin->hb_queue[ bin ] );
	}
	
	/*
	 * If entry was found, dequeue it
	 */
	if ( found ) {
		dequeue_general( (QUEUE**) &hashbin->hb_queue[ bin ],
				 (QUEUE*) entry );
		hashbin->hb_size--;

		/*
		 *  Check if this item is the currently selected item, and in
		 *  that case we must reset hb_current
		 */
		if ( entry == hashbin->hb_current)
			hashbin->hb_current = NULL;
	}

	/* Release lock */
	if ( hashbin->hb_type & HB_GLOBAL) {
		spin_unlock_irq( &hashbin->hb_mutex[ bin]);

	} else if ( hashbin->hb_type & HB_LOCAL) {
		restore_flags( flags);
	}
       
	
	/* Return */
	if ( found ) 
		return entry;
	else
		return NULL;
	
}

/*
 * Function hashbin_get_first (hashbin)
 *
 *    Get a pointer to first element in hashbin, this function must be
 *    called before any calls to hashbin_get_next()!
 *
 */
QUEUE *hashbin_get_first( hashbin_t* hashbin) 
{
	QUEUE *entry;
	int i;

	ASSERT( hashbin != NULL, return NULL;);
	ASSERT( hashbin->magic == HB_MAGIC, return NULL;);

	if ( hashbin == NULL)
		return NULL;

	for ( i = 0; i < HASHBIN_SIZE; i ++ ) {
		entry = hashbin->hb_queue[ i];
		if ( entry) {
			hashbin->hb_current = entry;
			return entry;
		}
	}
	/*
	 *  Did not find any item in hashbin
	 */
	return NULL;
}

/*
 * Function hashbin_get_next (hashbin)
 *
 *    Get next item in hashbin. A series of hashbin_get_next() calls must
 *    be started by a call to hashbin_get_first(). The function returns
 *    NULL when all items have been traversed
 * 
 */
QUEUE *hashbin_get_next( hashbin_t *hashbin)
{
	QUEUE* entry;
	int bin;
	int i;

	ASSERT( hashbin != NULL, return NULL;);
	ASSERT( hashbin->magic == HB_MAGIC, return NULL;);

	if ( hashbin->hb_current == NULL) {
		ASSERT( hashbin->hb_current != NULL, return NULL;);
		return NULL;
	}	
	entry = hashbin->hb_current->q_next;
	bin = GET_HASHBIN( entry->q_hash);

	/*  
	 *  Make sure that we are not back at the beginning of the queue
	 *  again 
	 */
	if ( entry != hashbin->hb_queue[ bin ]) {
		hashbin->hb_current = entry;

		return entry;
	}

	/*
	 *  Check that this is not the last queue in hashbin
	 */
	if ( bin >= HASHBIN_SIZE)
		return NULL;
	
	/*
	 *  Move to next queue in hashbin
	 */
	bin++;
	for ( i = bin; i < HASHBIN_SIZE; i++ ) {
		entry = hashbin->hb_queue[ i];
		if ( entry) {
			hashbin->hb_current = entry;
			
			return entry;
		}
	}
	return NULL;
}

/*
 * Function enqueue_last (queue, proc)
 *
 *    Insert item into end of queue.
 *
 */
static void __enqueue_last( QUEUE **queue, QUEUE* element)
{
	DEBUG( 4, __FUNCTION__ "()\n");

	/*
	 * Check if queue is empty.
	 */
	if ( *queue == NULL ) {
		/*
		 * Queue is empty.  Insert one element into the queue.
		 */
		element->q_next = element->q_prev = *queue = element;
		
	} else {
		/*
		 * Queue is not empty.  Insert element into end of queue.
		 */
		element->q_prev         = (*queue)->q_prev;
		element->q_prev->q_next = element;
		(*queue)->q_prev        = element;
		element->q_next         = *queue;
	}	
}

inline void enqueue_last( QUEUE **queue, QUEUE* element)
{
	unsigned long flags;
	
        save_flags(flags);
        cli();

        __enqueue_last( queue, element);

        restore_flags(flags);
}

/*
 * Function enqueue_first (queue, proc)
 *
 *    Insert item first in queue.
 *
 */
void enqueue_first(QUEUE **queue, QUEUE* element)
{
	
	DEBUG( 4, "enqueue_first()\n");

	/*
	 * Check if queue is empty.
	 */
	if ( *queue == NULL ) {
		/*
		 * Queue is empty.  Insert one element into the queue.
		 */
		element->q_next = element->q_prev = *queue = element;
		
	} else {
		/*
		 * Queue is not empty.  Insert element into front of queue.
		 */
		element->q_next          = (*queue);
		(*queue)->q_prev->q_next = element;
		element->q_prev          = (*queue)->q_prev;
		(*queue)->q_prev         = element;
		(*queue)                 = element;
	}
}

/*
 * Function enqueue_queue (queue, list)
 *
 *    Insert a queue (list) into the start of the first queue
 *
 */
void enqueue_queue( QUEUE** queue, QUEUE** list )
{
	QUEUE* tmp;
	
	/*
	 * Check if queue is empty
	 */ 
	if ( *queue ) {
		(*list)->q_prev->q_next  = (*queue);
		(*queue)->q_prev->q_next = (*list); 
		tmp                      = (*list)->q_prev;
		(*list)->q_prev          = (*queue)->q_prev;
		(*queue)->q_prev         = tmp;
	} else {
		*queue                   = (*list); 
	}
	
	(*list) = NULL;
}

/*
 * Function enqueue_second (queue, proc)
 *
 *    Insert item behind head of queue.
 *
 */
#if 0
static void enqueue_second(QUEUE **queue, QUEUE* element)
{
	DEBUG( 0, "enqueue_second()\n");

	/*
	 * Check if queue is empty.
	 */
	if ( *queue == NULL ) {
		/*
		 * Queue is empty.  Insert one element into the queue.
		 */
		element->q_next = element->q_prev = *queue = element;
		
	} else {
		/*
		 * Queue is not empty.  Insert element into ..
		 */
		element->q_prev = (*queue);
		(*queue)->q_next->q_prev = element;
		element->q_next = (*queue)->q_next;
		(*queue)->q_next = element;
	}
}
#endif

/*
 * Function dequeue (queue)
 *
 *    Remove first entry in queue
 *
 */
QUEUE *dequeue_first(QUEUE **queue)
{
	QUEUE *ret;

	DEBUG( 4, "dequeue_first()\n");
	
	/*
	 * Set return value
	 */
	ret =  *queue;
	
	if ( *queue == NULL ) {
		/*
		 * Queue was empty.
		 */
	} else if ( (*queue)->q_next == *queue ) {
		/* 
		 *  Queue only contained a single element. It will now be
		 *  empty.  
		 */
		*queue = NULL;
	} else {
		/*
		 * Queue contained several element.  Remove the first one.
		 */
		(*queue)->q_prev->q_next = (*queue)->q_next;
		(*queue)->q_next->q_prev = (*queue)->q_prev;
		*queue = (*queue)->q_next;
	}
	
	/*
	 * Return the removed entry (or NULL of queue was empty).
	 */
	return ret;
}

/*
 * Function dequeue_general (queue, element)
 *
 *
 */
static QUEUE *dequeue_general(QUEUE **queue, QUEUE* element)
{
	QUEUE *ret;
	
	DEBUG( 4, "dequeue_general()\n");
	
	/*
	 * Set return value
	 */
	ret =  *queue;
		
	if ( *queue == NULL ) {
		/*
		 * Queue was empty.
		 */
	} if ( (*queue)->q_next == *queue ) {
		/* 
		 *  Queue only contained a single element. It will now be
		 *  empty.  
		 */
		*queue = NULL;
		
	} else {
		/*
		 *  Remove specific element.
		 */
		element->q_prev->q_next = element->q_next;
		element->q_next->q_prev = element->q_prev;
		if ( (*queue) == element)
			(*queue) = element->q_next;
	}
	
	/*
	 * Return the removed entry (or NULL of queue was empty).
	 */
	return ret;
}

/*
 * Function hash (name)
 *
 *    This function hash the input string 'name' using the ELF hash
 *    function for strings.
 */
static __u32 hash( char* name)
{
	__u32 h = 0;
	__u32 g;
	
	while(*name) {
		h = (h<<4) + *name++;
		if ((g = (h & 0xf0000000)))
			h ^=g>>24;
		h &=~g;
	}
	return h;
}
