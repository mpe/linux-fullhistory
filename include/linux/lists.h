/*
 * lists.h:  Simple list macros for Linux
 */

#define DLNODE(ptype)			       	       	       	\
	struct {			 			\
		ptype * dl_prev;	 			\
		ptype * dl_next;	 			\
	}

#define DNODE_SINGLE(node) {(node),(node)}
#define DNODE_NULL {0,0}

#define DLIST_INIT(listnam)	                                \
	(listnam).dl_prev = &(listnam);				\
	(listnam).dl_last = &(listnam);

#define DLIST_NEXT(listnam)	listnam.dl_next
#define DLIST_PREV(listnam)	listnam.dl_prev

#define DLIST_INSERT_AFTER(node, new, listnam)	do {		\
	(new)->listnam.dl_prev = (node);			\
	(new)->listnam.dl_next = (node)->listnam.dl_next;	\
	(node)->listnam.dl_next->listnam.dl_prev = (new);	\
	(node)->listnam.dl_next = (new);			\
	} while (0)

#define DLIST_INSERT_BEFORE(node, new, listnam)	do {		\
	(new)->listnam.dl_next = (node);			\
	(new)->listnam.dl_prev = (node)->listnam.dl_prev;	\
	(node)->listnam.dl_prev->listnam.dl_next = (new);	\
	(node)->listnam.dl_prev = (new);			\
	} while (0)

#define DLIST_DELETE(node, listnam)	do {		\
	node->listnam.dl_prev->listnam.dl_next =		\
		node->listnam.dl_next;				\
	node->listnam.dl_next->listnam.dl_prev =		\
		node->listnam.dl_prev;				\
	} while (0)
