/*
 *	linux/kernel/resource.c
 *
 * Copyright (C) 1995	Linus Torvalds
 *			David Hinds
 *
 * Kernel io-region resource management
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/ioport.h>

#define IOTABLE_SIZE 64

typedef struct resource_entry_t {
	u_long from, num;
	const char *name;
	struct resource_entry_t *next;
} resource_entry_t;

static resource_entry_t iolist = { 0, 0, "", NULL };

static resource_entry_t iotable[IOTABLE_SIZE];

/*
 * This generates the report for /proc/ioports
 */
int get_ioport_list(char *buf)
{
	resource_entry_t *p;
	int len = 0;

	for (p = iolist.next; (p) && (len < 4000); p = p->next)
		len += sprintf(buf+len, "%04lx-%04lx : %s\n",
			   p->from, p->from+p->num-1, p->name);
	if (p)
		len += sprintf(buf+len, "4K limit reached!\n");
	return len;
}

/*
 * The workhorse function: find where to put a new entry
 */
static resource_entry_t *find_gap(resource_entry_t *root,
				  u_long from, u_long num)
{
	unsigned long flags;
	resource_entry_t *p;
	
	if (from > from+num-1)
		return NULL;
	save_flags(flags);
	cli();
	for (p = root; ; p = p->next) {
		if ((p != root) && (p->from+p->num-1 >= from)) {
			p = NULL;
			break;
		}
		if ((p->next == NULL) || (p->next->from > from+num-1))
			break;
	}
	restore_flags(flags);
	return p;
}

/*
 * Call this from the device driver to register the ioport region.
 */
void request_region(unsigned int from, unsigned int num, const char *name)
{
	resource_entry_t *p;
	int i;

	for (i = 0; i < IOTABLE_SIZE; i++)
		if (iotable[i].num == 0)
			break;
	if (i == IOTABLE_SIZE)
		printk("warning: ioport table is full\n");
	else {
		p = find_gap(&iolist, from, num);
		if (p == NULL)
			return;
		iotable[i].name = name;
		iotable[i].from = from;
		iotable[i].num = num;
		iotable[i].next = p->next;
		p->next = &iotable[i];
		return;
	}
}

/*
 * This is for compatibility with older drivers.
 * It can be removed when all drivers call the new function.
 */
void snarf_region(unsigned int from, unsigned int num)
{
	request_region(from,num,"No name given.");
}

/* 
 * Call this when the device driver is unloaded
 */
void release_region(unsigned int from, unsigned int num)
{
	resource_entry_t *p, *q;

	for (p = &iolist; ; p = q) {
		q = p->next;
		if (q == NULL)
			break;
		if ((q->from == from) && (q->num == num)) {
			q->num = 0;
			p->next = q->next;
			return;
		}
	}
}

/*
 * Call this to check the ioport region before probing
 */
int check_region(unsigned int from, unsigned int num)
{
	return (find_gap(&iolist, from, num) == NULL) ? -EBUSY : 0;
}

/* Called from init/main.c to reserve IO ports. */
void reserve_setup(char *str, int *ints)
{
	int i;

	for (i = 1; i < ints[0]; i += 2)
		request_region(ints[i], ints[i+1], "reserved");
}
