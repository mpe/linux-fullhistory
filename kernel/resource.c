/*
 *	linux/kernel/resource.c
 *
 * Copyright (C) 1995, 1999	Linus Torvalds
 *				David Hinds
 *
 * Kernel resource management
 *
 * We now distinguish between claiming space for devices (using the
 * 'occupy' and 'vacate' calls), and associating a resource with a
 * device driver (with the 'request', 'release', and 'check' calls).
 * A resource can be claimed even if there is no associated driver
 * (by occupying with name=NULL).  Vacating a resource makes it
 * available for other dynamically configured devices.
 */

#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/init.h>

#define RSRC_TABLE_SIZE 128

struct resource_entry {
	u_long from, num;
	const char *name;
	struct resource_entry *next;
};

struct resource_entry res_list[] = {
    { 0, 0, NULL, NULL }, /* IO */
    { 0, 0, NULL, NULL }  /* mem */
};

static struct resource_entry rsrc_table[RSRC_TABLE_SIZE];

/*
 * This generates reports for /proc/ioports and /proc/memory
 */
int get_resource_list(int class, char *buf)
{
	struct resource_entry *root = &res_list[class];
	struct resource_entry *p;
	int len = 0;
	char *fmt = (class == RES_IO) ?
		"%04lx-%04lx : %s\n" : "%08lx-%08lx : %s\n";
	
	for (p = root->next; (p) && (len < 4000); p = p->next)
		len += sprintf(buf+len, fmt, p->from, p->from+p->num-1,
			       (p->name ? p->name : "occupied"));
	if (p)
		len += sprintf(buf+len, "4K limit reached!\n");
	return len;
}

/*
 * Basics: find a matching resource entry, or find an insertion point
 */
static struct resource_entry *
find_match(struct resource_entry *root, u_long from, u_long num)
{
	struct resource_entry *p;
	for (p = root; p; p = p->next)
		if ((p->from == from) && (p->num == num))
			return p;
	return NULL;
}

static struct resource_entry *
find_gap(struct resource_entry *root, u_long from, u_long num)
{
	struct resource_entry *p;
	if (from > from+num-1)
		return NULL;
	for (p = root; ; p = p->next) {
		if ((p != root) && (p->from+p->num-1 >= from)) {
			p = NULL;
			break;
		}
		if ((p->next == NULL) || (p->next->from > from+num-1))
			break;
	}
	return p;
}

/*
 * Call this from a driver to assert ownership of a resource
 */
void request_resource(int class, unsigned long from,
		      unsigned long num, const char *name)
{
	struct resource_entry *root = &res_list[class];
	struct resource_entry *p;
	long flags;
	int i;

	p = find_match(root, from, num);
	if (p) {
		p->name = name;
		return;
	}

	save_flags(flags);
	cli();
	for (i = 0; i < RSRC_TABLE_SIZE; i++)
		if (rsrc_table[i].num == 0)
			break;
	if (i == RSRC_TABLE_SIZE)
		printk("warning: resource table is full\n");
	else {
		p = find_gap(root, from, num);
		if (p == NULL) {
			restore_flags(flags);
			return;
		}
		rsrc_table[i].name = name;
		rsrc_table[i].from = from;
		rsrc_table[i].num = num;
		rsrc_table[i].next = p->next;
		p->next = &rsrc_table[i];
	}
	restore_flags(flags);
}

/* 
 * Call these when a driver is unloaded but the device remains
 */
void release_resource(int class, unsigned long from, unsigned long num)
{
	struct resource_entry *root = &res_list[class];
	struct resource_entry *p;
	p = find_match(root, from, num);
	if (p) p->name = NULL;
}

/*
 * Call these to check a region for conflicts before probing
 */
int check_resource(int class, unsigned long from, unsigned long num)
{
	struct resource_entry *root = &res_list[class];
	struct resource_entry *p;
	p = find_match(root, from, num);
	if (p != NULL)
		return (p->name != NULL) ? -EBUSY : 0;
	return (find_gap(root, from, num) == NULL) ? -EBUSY : 0;
}

/*
 * Call this to claim a resource for a piece of hardware
 */
unsigned long occupy_resource(int class, unsigned long base,
			      unsigned long end, unsigned long num,
			      unsigned long align, const char *name)
{
	struct resource_entry *root = &res_list[class];
	unsigned long from = 0, till;
	unsigned long flags;
	int i;
	struct resource_entry *p, *q;

	if ((base > end-1) || (num > end - base))
		return 0;

	for (i = 0; i < RSRC_TABLE_SIZE; i++)
		if (rsrc_table[i].num == 0)
			break;
	if (i == RSRC_TABLE_SIZE)
		return 0;

	save_flags(flags);
	cli();
	/* printk("occupy: search in %08lx[%08lx] ", base, end - base); */
	for (p = root; p != NULL; p = q) {
		q = p->next;
		/* Find window in list */
		from = (p->from+p->num + align-1) & ~(align-1);
		till = (q == NULL) ? (0 - align) : q->from;
		/* printk(" %08lx:%08lx", from, till); */
		/* Clip window with base and end */
		if (from < base) from = base;
		if (till > end) till = end;
		/* See if result is large enougth */
		if ((from < till) && (from + num < till))
			break;
	}
	/* printk("\r\n"); */
	restore_flags(flags);

	if (p == NULL)
		return 0;

	rsrc_table[i].name = name;
	rsrc_table[i].from = from;
	rsrc_table[i].num = num;
	rsrc_table[i].next = p->next;
	p->next = &rsrc_table[i];
	return from;
}

/*
 * Call this when a resource becomes available for other hardware
 */
void vacate_resource(int class, unsigned long from, unsigned long num)
{
	struct resource_entry *root = &res_list[class];
	struct resource_entry *p, *q;
	long flags;

	save_flags(flags);
	cli();
	for (p = root; ; p = q) {
		q = p->next;
		if (q == NULL)
			break;
		if ((q->from == from) && (q->num == num)) {
			q->num = 0;
			p->next = q->next;
			break;
		}
	}
	restore_flags(flags);
}

/* Called from init/main.c to reserve IO ports. */
void __init reserve_setup(char *str, int *ints)
{
	int i;

	for (i = 1; i < ints[0]; i += 2)
		request_region(ints[i], ints[i+1], "reserved");
}
