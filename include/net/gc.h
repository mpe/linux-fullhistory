/*
 *	Interface routines assumed by gc()
 *
 *	Copyright (C) Barak A. Pearlmutter.
 *	Released under the GPL version 2 or later.
 *
 */

typedef struct object *pobj;	/* pointer to a guy of the type we gc */

/*
 *	How to mark and unmark objects
 */

extern void gc_mark(pobj);
extern void gc_unmark(pobj);
extern int gc_marked(pobj);

/* 
 *	How to count and access an object's children
 */

extern int n_children(pobj);	/* how many children */
extern pobj child_n(pobj, int);	/* child i, numbered 0..n-1 */

/*
 *	How to access the root set
 */

extern int root_size(void);	/* number of things in root set */
extern pobj root_elt(int);	/* element i of root set, numbered 0..n-1 */

/*
 *	How to access the free list
 */

extern void clear_freelist(void);
extern void add_to_free_list(pobj);

/*
 *	How to iterate through all objects in memory
 */

extern int N_OBJS;
extern pobj obj_number(int);

