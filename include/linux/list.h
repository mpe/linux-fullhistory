#ifndef _LINUX_LIST_H
#define _LINUX_LIST_H

/*
 * Simple doubly linked list implementation.
 */
struct list_head {
	struct list_head *next, *prev;
};

#define LIST_HEAD(name) \
	struct list_head name = { &name, &name }

#define INIT_LIST_HEAD(ptr) do { \
	(ptr)->next = (ptr); (ptr)->prev = (ptr); \
} while (0)

static inline void list_add(struct list_head *new, struct list_head *head)
{
	struct list_head *next = head->next;
	next->prev = new;
	new->next = next;
	new->prev = head;
	head->next = new;
}

static inline void list_del(struct list_head *entry)
{
	struct list_head *next, *prev;
	next = entry->next;
	prev = entry->prev;
	next->prev = prev;
	prev->next = next;
}

#define list_entry(ptr, type, member) \
	((type *)((char *)(ptr)-(unsigned long)(&((type *)0)->member)))

#endif
