/*
 *	linux/kernel/resource.c
 *
 * Copyright (C) 1999	Linus Torvalds
 *
 * Arbitrary resource management.
 */

#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/malloc.h>

struct resource ioport_resource = { "PCI IO", 0x0000, 0xFFFF };
struct resource iomem_resource = { "PCI mem", 0x00000000, 0xFFFFFFFF };

/*
 * This generates reports for /proc/ioports and /proc/memory
 */
static char * do_resource_list(struct resource *entry, const char *fmt, int offset, char *buf, char *end)
{
	if (offset < 0)
		offset = 0;

	while (entry) {
		const char *name = entry->name;
		unsigned long from, to;

		if ((int) (end-buf) < 80)
			return buf;

		from = entry->start;
		to = entry->end;
		if (!name)
			name = "<BAD>";

		buf += sprintf(buf, fmt + offset, from, to, name);
		if (entry->child)
			buf = do_resource_list(entry->child, fmt, offset-2, buf, end);
		entry = entry->sibling;
	}

	return buf;
}

int get_resource_list(struct resource *root, char *buf, int size)
{
	char *fmt;

	fmt = "        %08lx-%08lx : %s\n";
	if (root == &ioport_resource)
		fmt = "        %04lx-%04lx : %s\n";
	return do_resource_list(root->child, fmt, 8, buf, buf + size) - buf;
}	

int request_resource(struct resource *root, struct resource *new)
{
	unsigned long start = new->start;
	unsigned long end = new->end;
	struct resource *tmp, **p;

	if (end < start)
		return -EINVAL;
	if (start < root->start)
		return -EINVAL;
	if (end > root->end)
		return -EINVAL;
	p = &root->child;
	for (;;) {
		tmp = *p;
		if (!tmp || tmp->start > end) {
			new->sibling = tmp;
			*p = new;
			new->parent = root;
			return 0;
		}
		p = &tmp->sibling;
		if (tmp->end < start)
			continue;
		return -EBUSY;
	}
}

int release_resource(struct resource *old)
{
	struct resource *tmp, **p;

	p = &old->parent->child;
	for (;;) {
		tmp = *p;
		if (!tmp)
			break;
		if (tmp == old) {
			*p = tmp->sibling;
			old->parent = NULL;
			return 0;
		}
		p = &tmp->sibling;
	}
	return -EINVAL;
}

struct resource * __request_region(struct resource *parent, unsigned long start, unsigned long n, const char *name)
{
	struct resource *res = kmalloc(sizeof(*res), GFP_KERNEL);

	if (res) {
		memset(res, 0, sizeof(*res));
		res->name = name;
		res->start = start;
		res->end = start + n - 1;
		if (request_resource(parent, res) != 0) {
			kfree(res);
			res = NULL;
		}
	}
	return res;
}

/*
 * Compatibility cruft.
 *
 * Check-region returns non-zero if something already exists.
 *
 * Release-region releases an anonymous region that matches
 * the IO port range.
 */
int __check_region(struct resource *parent, unsigned long start, unsigned long n)
{
	struct resource * res;

	res = __request_region(parent, start, n, "check-region");
	if (!res)
		return -EBUSY;

	release_resource(res);
	kfree(res);
	return 0;
}

void __release_region(struct resource *parent, unsigned long start, unsigned long n)
{
	struct resource **p;
	unsigned long end;

	p = &parent->child;
	end = start + n - 1;

	for (;;) {
		struct resource *res = *p;

		if (!res)
			break;
		if (res->start == start && res->end == end) {
			*p = res->sibling;
			kfree(res);
			break;
		}
		p = &res->sibling;
	}
}

/*
 * Called from init/main.c to reserve IO ports.
 */
#define MAXRESERVE 4
void __init reserve_setup(char *str, int *ints)
{
	int i;
	static int reserved = 0;
	static struct resource reserve[MAXRESERVE];

	for (i = 1; i < ints[0]; i += 2) {
		int x = reserved;
		if (x < MAXRESERVE) {
			struct resource *res = reserve + x;
			res->name = "reserved";
			res->start = ints[i];
			res->end = res->start + ints[i] - 1;
			res->child = NULL;
			if (request_resource(&ioport_resource, res) == 0)
				reserved = x+1;
		}
	}
}
